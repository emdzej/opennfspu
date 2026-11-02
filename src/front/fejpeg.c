/* Baseline JPEG for a player's own picture (gimme.dll's reader, see feuserpic.c): Huffman-coded
   sequential DCT, 8-bit samples, one (grey) or three (YCbCr) components with any sampling factors up to
   2 x 2, restart intervals. Progressive and arithmetic-coded files are refused. */
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

/* one block's coefficients, dequantised, its inverse DCT into out (stride) */
static bool block(Jpeg *j, Comp *c, uint8_t *out, int stride)
{
    float coef[64] = {0};
    int t = decode(j, &j->hd[c->td]);
    if (t < 0 || t > 16) return false;
    int diff = t ? extend(getbits(j, t), t) : 0;
    c->pred += diff;
    coef[0] = (float)(c->pred * j->q[c->tq][0]);
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
        coef[zigzag[k]] = (float)(extend(getbits(j, s), s) * j->q[c->tq][k]);
        k++;
    }
    static float cosv[8][8];
    static bool init;
    if (!init) {
        for (int x = 0; x < 8; x++)
            for (int u = 0; u < 8; u++) cosv[x][u] = (u ? 1.0f : 0.70710678f) * cosf((float)((2 * x + 1) * u) * 3.14159265f / 16.0f);
        init = true;
    }
    float tmp[64];
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
    return true;
}

static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }

static bool scan(Jpeg *j)
{
    int mcux = (j->width + 8 * j->hmax - 1) / (8 * j->hmax), mcuy = (j->height + 8 * j->vmax - 1) / (8 * j->vmax);
    for (int i = 0; i < j->nc; i++) {
        Comp *c = &j->c[i];
        c->bw = mcux * c->h;
        c->bh = mcuy * c->v;
        c->data = malloc((size_t)c->bw * 8 * (size_t)c->bh * 8);
        if (!c->data) return false;
        c->pred = 0;
    }
    int left = j->restart;
    for (int my = 0; my < mcuy; my++)
        for (int mx = 0; mx < mcux; mx++) {
            if (j->restart && left == 0) {
                /* the RSTn marker: realign and reset the predictions */
                j->bitcnt = 0;
                j->marker = false;
                while (j->p + 1 < j->n && !(j->d[j->p] == 0xff && j->d[j->p + 1] >= 0xd0 && j->d[j->p + 1] <= 0xd7)) j->p++;
                j->p += 2;
                for (int i = 0; i < j->nc; i++) j->c[i].pred = 0;
                left = j->restart;
            }
            for (int i = 0; i < j->nc; i++) {
                Comp *c = &j->c[i];
                int stride = c->bw * 8;
                for (int by = 0; by < c->v; by++)
                    for (int bx = 0; bx < c->h; bx++) {
                        uint8_t *o = c->data + (size_t)((my * c->v + by) * 8) * (size_t)stride + (size_t)((mx * c->h + bx) * 8);
                        if (!block(j, c, o, stride)) return false;
                    }
            }
            if (j->restart) left--;
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
    bool ok = false, frame = false;
    while (j->p + 4 <= n) {
        if (d[j->p] != 0xff) {
            j->p++;
            continue;
        }
        uint8_t m = d[j->p + 1];
        if (m == 0xff) {
            j->p++;
            continue;
        }
        j->p += 2;
        if (m == 0xd8 || (m >= 0xd0 && m <= 0xd7) || m == 0x01) continue;
        if (m == 0xd9) break;
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
        } else if (m == 0xc0 || m == 0xc1) {   /* baseline / extended sequential, Huffman */
            if (d[s] != 8) goto done;
            j->height = (int)be16(d + s + 1), j->width = (int)be16(d + s + 3), j->nc = d[s + 5];
            if (j->nc != 1 && j->nc != 3) goto done;
            for (int i = 0; i < j->nc; i++) {
                const uint8_t *q = d + s + 6 + 3 * i;
                j->c[i] = (Comp){.id = q[0], .h = q[1] >> 4, .v = q[1] & 15, .tq = q[2] & 3};
                if (j->c[i].h < 1 || j->c[i].h > 2 || j->c[i].v < 1 || j->c[i].v > 2) goto done;
                if (j->c[i].h > j->hmax) j->hmax = j->c[i].h;
                if (j->c[i].v > j->vmax) j->vmax = j->c[i].v;
            }
            frame = true;
        } else if ((m >= 0xc2 && m <= 0xcf) && m != 0xc4 && m != 0xc8 && m != 0xcc) {
            goto done;   /* progressive, lossless, arithmetic */
        } else if (m == 0xda) {   /* the scan (one, interleaved) */
            if (!frame || d[s] != j->nc) goto done;
            for (int i = 0; i < j->nc; i++) {
                int id = d[s + 1 + 2 * i], t = d[s + 2 + 2 * i];
                for (int k = 0; k < j->nc; k++)
                    if (j->c[k].id == id) j->c[k].td = t >> 4 & 3, j->c[k].ta = t & 3;
            }
            j->p = e;
            if (!scan(j)) goto done;
            ok = true;
            break;
        }
        j->p = e;
    }
    if (ok) {
        int W = j->width, H = j->height;
        *rgb = malloc((size_t)W * (size_t)H * 3);
        if (!*rgb) ok = false;
        for (int y = 0; ok && y < H; y++)
            for (int x = 0; x < W; x++) {
                float v[3];
                for (int i = 0; i < j->nc; i++) {
                    const Comp *c = &j->c[i];
                    int sx = x * c->h / j->hmax, sy = y * c->v / j->vmax;
                    v[i] = c->data[(size_t)sy * (size_t)(c->bw * 8) + (size_t)sx];
                }
                uint8_t *o = *rgb + ((size_t)y * (size_t)W + (size_t)x) * 3;
                if (j->nc == 1) o[0] = o[1] = o[2] = (uint8_t)v[0];
                else {
                    float Y = v[0], cb = v[1] - 128.0f, cr = v[2] - 128.0f;
                    float r = Y + 1.402f * cr, g = Y - 0.344136f * cb - 0.714136f * cr, b = Y + 1.772f * cb;
                    o[0] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : lrintf(r));
                    o[1] = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : lrintf(g));
                    o[2] = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : lrintf(b));
                }
            }
        *w = W, *h = H;
    }
done:
    for (int i = 0; i < 3; i++) free(j->c[i].data);
    free(j);
    if (!ok) {
        free(*rgb);
        *rgb = NULL;
    }
    return ok;
}
