/* JPEG for a player's own picture (gimme.dll's reader, IJG's libjpeg, see feuserpic.c): Huffman-coded
   sequential or progressive DCT, 8-bit samples, one (grey) or three (YCbCr) components with sampling
   factors up to 2 x 2, any number of scans, restart intervals. Arithmetic-coded and lossless files are
   refused. A progressive file whose first AC bands never all arrive (a truncated one, or bits never
   refined) has its blocks smoothed as libjpeg does: their five lowest AC coefficients, where unknown
   and zero, predicted from the DC values of the blocks around (the JPEG standard's K.8). */
#include "front/fejpeg.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t bits[17];
    uint8_t vals[256];
    int lookup_code[17], lookup_index[17], maxcode[18];
} Huff;

typedef struct {
    int id, h, v, tq, td, ta, pred;
    int bw, bh;            /* blocks across and down (whole MCUs) */
    int cw, ch;            /* the blocks its own size covers (a one-component scan's) */
    int16_t *coef;         /* bw * bh blocks of 64 coefficients, zigzag order */
    int bits[64];          /* each coefficient's bits still missing (the last scan's Al), -1 none yet */
    uint8_t *data;         /* bw * 8 by bh * 8 samples */
} Comp;

typedef struct {
    const uint8_t *d;
    size_t n, p;
    uint32_t bitbuf;
    int bitcnt;
    bool marker;
    uint16_t q[4][64];
    Huff hd[4], ha[4];
    Comp c[3];
    int nc, width, height, hmax, vmax, restart;
    int bw, bh;            /* the MCUs' blocks at the largest sampling */
    bool progressive;
    int ss, se, ah, al, eobrun;   /* the scan's band, its bits, the end-of-band run */
} Jpeg;

static const uint8_t zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21,
    28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61,
    54, 47, 55, 62, 63};

static void huff_build(Huff *h)
{
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
        h->lookup_index[l] = k;
        h->lookup_code[l] = code;
        code += h->bits[l];
        k += h->bits[l];
        h->maxcode[l] = h->bits[l] ? code - 1 : -1;
        code <<= 1;
    }
    h->maxcode[17] = 0x7fffffff;
}

static int getbit(Jpeg *j)
{
    if (j->bitcnt == 0) {
        uint8_t b = 0;
        if (!j->marker && j->p < j->n) {
            b = j->d[j->p++];
            if (b == 0xff) {
                uint8_t nx = j->p < j->n ? j->d[j->p] : 0;
                if (nx == 0) j->p++;
                else {
                    j->marker = true;   /* a marker: zeros from here */
                    j->p--;
                    b = 0;
                }
            }
        }
        j->bitbuf = b;
        j->bitcnt = 8;
    }
    j->bitcnt--;
    return (int)(j->bitbuf >> j->bitcnt) & 1;
}

static int getbits(Jpeg *j, int n)
{
    int v = 0;
    while (n--) v = v << 1 | getbit(j);
    return v;
}

static int decode(Jpeg *j, const Huff *h)
{
    int code = 0;
    for (int l = 1; l <= 16; l++) {
        code = code << 1 | getbit(j);
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l] && code >= h->lookup_code[l])
            return h->vals[h->lookup_index[l] + code - h->lookup_code[l]];
    }
    return -1;
}

static int extend(int v, int t) { return v < (1 << (t - 1)) ? v - (1 << t) + 1 : v; }

/* One block's part of a scan into its coefficients (in zigzag order): a sequential block, or a progressive
   scan's DC first / DC refinement / AC first / AC refinement pass (the spectral band Ss..Se, bit Al). */
static bool block(Jpeg *j, Comp *c, int16_t *z)
{
    if (!j->progressive) {
        int t = decode(j, &j->hd[c->td]);
        if (t < 0 || t > 16) return false;
        c->pred += t ? extend(getbits(j, t), t) : 0;
        z[0] = (int16_t)c->pred;
        for (int k = 1; k < 64;) {
            int rs = decode(j, &j->ha[c->ta]);
            if (rs < 0) return false;
            int r = rs >> 4, s = rs & 15;
            if (s == 0) {
                if (r != 15) break;
                k += 16;
                continue;
            }
            k += r;
            if (k > 63) return false;
            z[k++] = (int16_t)extend(getbits(j, s), s);
        }
        return true;
    }
    if (j->ss == 0) {   /* DC */
        if (j->ah == 0) {
            int t = decode(j, &j->hd[c->td]);
            if (t < 0 || t > 16) return false;
            c->pred += t ? extend(getbits(j, t), t) : 0;
            z[0] = (int16_t)(c->pred * (1 << j->al));
        } else if (getbit(j)) {
            z[0] = (int16_t)(z[0] | 1 << j->al);
        }
        return true;
    }
    if (j->ah == 0) {   /* AC first */
        if (j->eobrun > 0) {
            j->eobrun--;
            return true;
        }
        for (int k = j->ss; k <= j->se;) {
            int rs = decode(j, &j->ha[c->ta]);
            if (rs < 0) return false;
            int r = rs >> 4, s = rs & 15;
            if (s == 0) {
                if (r < 15) {
                    j->eobrun = (1 << r) - 1 + (r ? getbits(j, r) : 0);
                    break;
                }
                k += 16;
                continue;
            }
            k += r;
            if (k > 63) return false;
            z[k++] = (int16_t)(extend(getbits(j, s), s) * (1 << j->al));
        }
        return true;
    }
    /* AC refinement: a bit for each coefficient already nonzero, new ones of +-1 << Al among the zeros */
    int bit = 1 << j->al, k = j->ss;
    if (j->eobrun <= 0) {
        do {
            int rs = decode(j, &j->ha[c->ta]);
            if (rs < 0) return false;
            int r = rs >> 4, s = rs & 15;
            if (s == 0) {
                if (r < 15) {
                    j->eobrun = (1 << r) - 1 + (r ? getbits(j, r) : 0);
                    r = 64;   /* the rest of the block only refined */
                }
            } else {
                s = getbit(j) ? bit : -bit;
            }
            while (k <= j->se) {
                int16_t *p = &z[k++];
                if (*p) {
                    if (getbit(j) && !(*p & bit)) *p = (int16_t)(*p + (*p > 0 ? bit : -bit));
                } else {
                    if (r == 0) {
                        *p = (int16_t)s;
                        break;
                    }
                    r--;
                }
            }
        } while (k <= j->se);
    } else {
        j->eobrun--;
        for (; k <= j->se; k++)
            if (z[k] && getbit(j) && !(z[k] & bit)) z[k] = (int16_t)(z[k] + (z[k] > 0 ? bit : -bit));
    }
    return true;
}

/* The prediction of one AC coefficient from num, its quantiser q and its missing bits al (clamped below
   what those bits could have held). */
static int predict(int num, int q, int al)
{
    int neg = num < 0, p;
    if (neg) num = -num;
    p = ((q << 7) + num) / (q << 8);
    if (al > 0 && p >= 1 << al) p = (1 << al) - 1;
    return neg ? -p : p;
}

/* The smoothing (progressive files only): for each of the component's blocks inside the picture, its
   neighbours' DC values (the edge blocks repeated past the picture's edge) give AC01, AC10, AC20, AC11
   and AC02 where they are still zero and not all their bits arrived. Useful only with the DC known, the
   six quantisers non-zero and one of the five incomplete. */
static void smooth(const Jpeg *j, Comp *c)
{
    const uint16_t *q = j->q[c->tq];
    if (c->bits[0] < 0) return;
    bool useful = false;
    for (int k = 0; k < 6; k++)
        if (!q[k]) return;
    for (int k = 1; k < 6; k++)
        if (c->bits[k] != 0) useful = true;
    if (!useful) return;
    int16_t *dc = malloc((size_t)c->cw * (size_t)c->ch * sizeof *dc);
    if (!dc) return;
    for (int by = 0; by < c->ch; by++)
        for (int bx = 0; bx < c->cw; bx++) dc[by * c->cw + bx] = c->coef[((size_t)by * (size_t)c->bw + (size_t)bx) * 64];
    for (int by = 0; by < c->ch; by++)
        for (int bx = 0; bx < c->cw; bx++) {
            int u = by ? by - 1 : by, d = by + 1 < c->ch ? by + 1 : by;
            int l = bx ? bx - 1 : bx, r = bx + 1 < c->cw ? bx + 1 : bx;
            int d1 = dc[u * c->cw + l], d2 = dc[u * c->cw + bx], d3 = dc[u * c->cw + r];
            int d4 = dc[by * c->cw + l], d5 = dc[by * c->cw + bx], d6 = dc[by * c->cw + r];
            int d7 = dc[d * c->cw + l], d8 = dc[d * c->cw + bx], d9 = dc[d * c->cw + r];
            int16_t *z = c->coef + ((size_t)by * (size_t)c->bw + (size_t)bx) * 64;
            int q0 = q[0];
            /* (zigzag 1 .. 5: AC01, AC10, AC20, AC11, AC02) */
            const int num[6] = {0, 36 * q0 * (d4 - d6), 36 * q0 * (d2 - d8), 9 * q0 * (d2 + d8 - 2 * d5),
                                5 * q0 * (d1 - d3 - d7 + d9), 9 * q0 * (d4 + d6 - 2 * d5)};
            for (int k = 1; k < 6; k++)
                if (c->bits[k] != 0 && z[k] == 0) z[k] = (int16_t)predict(num[k], q[k], c->bits[k]);
        }
    free(dc);
}

/* a block's coefficients dequantised and through the inverse DCT into out (stride) */
static void idct(const Jpeg *j, const Comp *c, const int16_t *z, uint8_t *out, int stride)
{
    static float cosv[8][8];
    static bool init;
    if (!init) {
        for (int x = 0; x < 8; x++)
            for (int u = 0; u < 8; u++) cosv[x][u] = (u ? 1.0f : 0.70710678f) * cosf((float)((2 * x + 1) * u) * 3.14159265f / 16.0f);
        init = true;
    }
    float coef[64], tmp[64];
    for (int k = 0; k < 64; k++) coef[zigzag[k]] = (float)(z[k] * j->q[c->tq][k]);
    for (int y = 0; y < 8; y++)
        for (int u = 0; u < 8; u++) {
            float s = 0;
            for (int v = 0; v < 8; v++) s += cosv[y][v] * coef[v * 8 + u];
            tmp[y * 8 + u] = s;
        }
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            float s = 0;
            for (int u = 0; u < 8; u++) s += cosv[x][u] * tmp[y * 8 + u];
            int v = (int)lrintf(s / 4.0f + 128.0f);
            out[y * stride + x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
}

/* A component's samples at the picture's size into out (W x H). A half-width one (h2v1) or half in both
   (h2v2) goes through libjpeg's "fancy" upsampling, its default: each output sample 3/4 its nearer input
   and 1/4 the next farther, across and (h2v2) down, the edges repeated, with libjpeg's roundings
   (+1/+2 for the pairs across, +8/+7 in 16ths for both). Other ratios repeat the samples. */
static void upsample(const Jpeg *j, const Comp *c, uint8_t *out)
{
    int W = j->width, H = j->height, stride = c->bw * 8;
    int hx = j->hmax / c->h, vy = j->vmax / c->v;
    int n = (W * c->h + j->hmax - 1) / j->hmax, rows = (H * c->v + j->vmax - 1) / j->vmax;   /* (its own size) */
    const uint8_t *d = c->data;
    if (hx == 2 && (vy == 1 || vy == 2) && n >= 2) {
        int *sum = malloc((size_t)n * sizeof *sum);
        uint8_t *row = malloc((size_t)n * 2);
        if (!sum || !row) {
            free(sum), free(row);
            goto plain;
        }
        for (int y = 0; y < H; y++) {
            int r = y / vy, k = n - 1;
            const uint8_t *near = d + (size_t)r * (size_t)stride;
            if (vy == 1) {
                row[0] = near[0];
                row[1] = (uint8_t)((near[0] * 3 + near[1] + 2) >> 2);
                for (int i = 1; i < k; i++) {
                    row[2 * i] = (uint8_t)((near[i] * 3 + near[i - 1] + 1) >> 2);
                    row[2 * i + 1] = (uint8_t)((near[i] * 3 + near[i + 1] + 2) >> 2);
                }
                row[2 * k] = (uint8_t)((near[k] * 3 + near[k - 1] + 1) >> 2);
                row[2 * k + 1] = near[k];
            } else {
                int fr = y % 2 ? r + 1 : r - 1;   /* (the lower output row leans down, the upper up) */
                if (fr < 0) fr = 0;
                if (fr >= rows) fr = rows - 1;
                const uint8_t *far = d + (size_t)fr * (size_t)stride;
                for (int i = 0; i < n; i++) sum[i] = near[i] * 3 + far[i];
                row[0] = (uint8_t)((sum[0] * 4 + 8) >> 4);
                row[1] = (uint8_t)((sum[0] * 3 + sum[1] + 7) >> 4);
                for (int i = 1; i < k; i++) {
                    row[2 * i] = (uint8_t)((sum[i] * 3 + sum[i - 1] + 8) >> 4);
                    row[2 * i + 1] = (uint8_t)((sum[i] * 3 + sum[i + 1] + 7) >> 4);
                }
                row[2 * k] = (uint8_t)((sum[k] * 3 + sum[k - 1] + 8) >> 4);
                row[2 * k + 1] = (uint8_t)((sum[k] * 4 + 7) >> 4);
            }
            memcpy(out + (size_t)y * (size_t)W, row, (size_t)W);
        }
        free(sum), free(row);
        return;
    }
plain:
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) out[(size_t)y * (size_t)W + (size_t)x] = d[(size_t)(y / vy) * (size_t)stride + (size_t)(x / hx)];
}

static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }

static void restart(Jpeg *j, int *left)
{
    if (!j->restart) return;
    if (*left == 0) {
        /* the RSTn marker: realign and reset the predictions and the band's end-of-block run */
        j->bitcnt = 0;
        j->marker = false;
        while (j->p + 1 < j->n && !(j->d[j->p] == 0xff && j->d[j->p + 1] >= 0xd0 && j->d[j->p + 1] <= 0xd7)) j->p++;
        j->p += 2;
        for (int i = 0; i < j->nc; i++) j->c[i].pred = 0;
        j->eobrun = 0;
        *left = j->restart;
    }
    (*left)--;
}

/* A scan of ns components (sc: their indices): one component's blocks in order (its own size), or the
   interleaved MCUs */
static bool scan(Jpeg *j, const int *sc, int ns)
{
    int left = j->restart;
    if (ns == 1) {
        Comp *c = &j->c[sc[0]];
        for (int by = 0; by < c->ch; by++)
            for (int bx = 0; bx < c->cw; bx++) {
                restart(j, &left);
                if (!block(j, c, c->coef + ((size_t)by * (size_t)c->bw + (size_t)bx) * 64)) return false;
            }
        return true;
    }
    int mcux = j->bw / j->hmax, mcuy = j->bh / j->vmax;
    for (int my = 0; my < mcuy; my++)
        for (int mx = 0; mx < mcux; mx++) {
            restart(j, &left);
            for (int i = 0; i < ns; i++) {
                Comp *c = &j->c[sc[i]];
                for (int by = 0; by < c->v; by++)
                    for (int bx = 0; bx < c->h; bx++) {
                        size_t b = (size_t)(my * c->v + by) * (size_t)c->bw + (size_t)(mx * c->h + bx);
                        if (!block(j, c, c->coef + b * 64)) return false;
                    }
            }
        }
    return true;
}

bool FEJpeg_Decode(const uint8_t *d, size_t n, int *w, int *h, uint8_t **rgb)
{
    *rgb = NULL;
    if (n < 4 || d[0] != 0xff || d[1] != 0xd8) return false;
    Jpeg *j = calloc(1, sizeof *j);
    if (!j) return false;
    j->d = d, j->n = n, j->p = 2;
    bool ok = false, frame = false, scanned = false;
    while (j->p + 2 <= n) {
        if (d[j->p] != 0xff) {
            j->p++;
            continue;
        }
        uint8_t m = d[j->p + 1];
        if (m == 0xff || m == 0x00) {
            j->p++;
            continue;
        }
        j->p += 2;
        if (m == 0xd8 || (m >= 0xd0 && m <= 0xd7) || m == 0x01) continue;
        if (m == 0xd9) break;
        if (j->p + 2 > n) break;
        size_t len = be16(d + j->p), s = j->p + 2, e = j->p + len;
        if (len < 2 || e > n) break;
        if (m == 0xdb) {   /* quantisation tables */
            while (s < e) {
                int pq = d[s] >> 4, tq = d[s] & 3;
                s++;
                for (int k = 0; k < 64 && s < e; k++) j->q[tq][k] = pq ? (uint16_t)be16(d + s) : d[s], s += pq ? 2 : 1;
            }
        } else if (m == 0xc4) {   /* Huffman tables */
            while (s + 17 <= e) {
                int tc = d[s] >> 4, th = d[s] & 3;
                Huff *hf = tc ? &j->ha[th] : &j->hd[th];
                int total = 0;
                for (int l = 1; l <= 16; l++) total += hf->bits[l] = d[s + l];
                s += 17;
                if (total > 256 || s + (size_t)total > e) goto done;
                memcpy(hf->vals, d + s, (size_t)total);
                s += (size_t)total;
                huff_build(hf);
            }
        } else if (m == 0xdd) {
            j->restart = (int)be16(d + s);
        } else if (m == 0xc0 || m == 0xc1 || m == 0xc2) {   /* sequential or progressive, Huffman */
            if (frame || d[s] != 8) goto done;
            j->progressive = m == 0xc2;
            j->height = (int)be16(d + s + 1), j->width = (int)be16(d + s + 3), j->nc = d[s + 5];
            if ((j->nc != 1 && j->nc != 3) || !j->width || !j->height || j->width > 8192 || j->height > 8192) goto done;
            for (int i = 0; i < j->nc; i++) {
                const uint8_t *q = d + s + 6 + 3 * i;
                j->c[i] = (Comp){.id = q[0], .h = q[1] >> 4, .v = q[1] & 15, .tq = q[2] & 3};
                if (j->c[i].h < 1 || j->c[i].h > 2 || j->c[i].v < 1 || j->c[i].v > 2) goto done;
                if (j->c[i].h > j->hmax) j->hmax = j->c[i].h;
                if (j->c[i].v > j->vmax) j->vmax = j->c[i].v;
            }
            int mcux = (j->width + 8 * j->hmax - 1) / (8 * j->hmax), mcuy = (j->height + 8 * j->vmax - 1) / (8 * j->vmax);
            j->bw = mcux * j->hmax, j->bh = mcuy * j->vmax;
            for (int i = 0; i < j->nc; i++) {
                Comp *c = &j->c[i];
                c->bw = mcux * c->h, c->bh = mcuy * c->v;
                c->cw = ((j->width * c->h + j->hmax - 1) / j->hmax + 7) / 8;
                c->ch = ((j->height * c->v + j->vmax - 1) / j->vmax + 7) / 8;
                c->coef = calloc((size_t)c->bw * (size_t)c->bh, 64 * sizeof *c->coef);
                if (!c->coef) goto done;
                for (int k = 0; k < 64; k++) c->bits[k] = j->progressive ? -1 : 0;
            }
            frame = true;
        } else if (m >= 0xc3 && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc) {
            goto done;   /* lossless, hierarchical, arithmetic */
        } else if (m == 0xda) {   /* a scan */
            int ns = d[s], sc[3];
            if (!frame || ns < 1 || ns > j->nc || s + 4 + 2 * (size_t)ns > e) goto done;
            for (int i = 0; i < ns; i++) {
                int id = d[s + 1 + 2 * i], t = d[s + 2 + 2 * i];
                sc[i] = -1;
                for (int k = 0; k < j->nc; k++)
                    if (j->c[k].id == id) j->c[k].td = t >> 4 & 3, j->c[k].ta = t & 3, sc[i] = k;
                if (sc[i] < 0) goto done;
            }
            const uint8_t *q = d + s + 1 + 2 * ns;
            j->ss = q[0], j->se = q[1], j->ah = q[2] >> 4, j->al = q[2] & 15;
            if (j->progressive && (j->se > 63 || j->ss > j->se || (j->ss == 0) != (j->se == 0) || (j->ss && ns != 1)))
                goto done;
            j->p = e;
            j->bitcnt = 0, j->marker = false, j->eobrun = 0;
            for (int i = 0; i < j->nc; i++) j->c[i].pred = 0;
            if (!scan(j, sc, ns)) goto done;
            if (j->progressive)
                for (int i = 0; i < ns; i++)
                    for (int k = j->ss; k <= j->se; k++) j->c[sc[i]].bits[k] = j->al;
            scanned = true;
            continue;   /* (the bit reader stopped at the next marker) */
        }
        j->p = e;
    }
    if (scanned) {
        ok = true;
        for (int i = 0; ok && i < j->nc; i++) {   /* the samples, every block of the padded MCUs */
            Comp *c = &j->c[i];
            if (j->progressive) smooth(j, c);
            int stride = c->bw * 8;
            c->data = malloc((size_t)stride * (size_t)c->bh * 8);
            if (!c->data) ok = false;
            for (int by = 0; ok && by < c->bh; by++)
                for (int bx = 0; bx < c->bw; bx++)
                    idct(j, c, c->coef + ((size_t)by * (size_t)c->bw + (size_t)bx) * 64,
                         c->data + (size_t)by * 8 * (size_t)stride + (size_t)bx * 8, stride);
        }
    }
    uint8_t *plane[3] = {NULL, NULL, NULL};
    if (ok) {
        int W = j->width, H = j->height;
        for (int i = 0; ok && i < j->nc; i++) {
            plane[i] = malloc((size_t)W * (size_t)H);
            if (!plane[i]) ok = false;
            else upsample(j, &j->c[i], plane[i]);
        }
        *rgb = ok ? malloc((size_t)W * (size_t)H * 3) : NULL;
        if (!*rgb) ok = false;
        for (size_t p = 0; ok && p < (size_t)W * (size_t)H; p++) {
            uint8_t *o = *rgb + p * 3;
            if (j->nc == 1) {
                o[0] = o[1] = o[2] = plane[0][p];
                continue;
            }
            /* libjpeg's YCbCr to RGB in 16-bit fixed point (jdcolor.c's tables: 1.40200, 1.77200,
               -0.71414 and -0.34414, rounded) */
            int Y = plane[0][p], cb = plane[1][p] - 128, cr = plane[2][p] - 128;
            int r = Y + ((91881 * cr + 32768) >> 16);
            int g = Y + ((-22554 * cb - 46802 * cr + 32768) >> 16);
            int b = Y + ((116130 * cb + 32768) >> 16);
            o[0] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
            o[1] = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g);
            o[2] = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b);
        }
        *w = W, *h = H;
    }
    for (int i = 0; i < 3; i++) free(plane[i]);
done:
    for (int i = 0; i < 3; i++) free(j->c[i].data), free(j->c[i].coef);
    free(j);
    if (!ok) {
        free(*rgb);
        *rgb = NULL;
    }
    return ok;
}
