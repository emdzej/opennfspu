/* TIFF for a player's own picture (gimme.dll's reader, libtiff, see feuserpic.c): the first image of a
   file in either byte order, in strips or tiles, the samples together or in planes, uncompressed,
   PackBits, LZW or Deflate (with the horizontal predictor); bilevel, grey (1, 4 or 8 bits), palette (1, 4
   or 8 bits) and RGB or RGBA (8 bits). CCITT fax and JPEG-in-TIFF are refused. */
#include "front/fetiff.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *d;
    size_t n;
    bool be;
} Tiff;

static uint32_t rd16(const Tiff *t, size_t o)
{
    if (o + 2 > t->n) return 0;
    return t->be ? (uint32_t)t->d[o] << 8 | t->d[o + 1] : (uint32_t)t->d[o + 1] << 8 | t->d[o];
}

static uint32_t rd32(const Tiff *t, size_t o)
{
    if (o + 4 > t->n) return 0;
    return t->be ? rd16(t, o) << 16 | rd16(t, o + 2) : rd16(t, o + 2) << 16 | rd16(t, o);
}

/* the i-th value of an entry (type 3 SHORT or 4 LONG; inline when it fits in 4 bytes) */
static uint32_t value(const Tiff *t, size_t e, uint32_t i)
{
    uint32_t type = rd16(t, e + 2), count = rd32(t, e + 4);
    uint32_t size = type == 3 ? 2 : type == 4 ? 4 : type == 1 ? 1 : 0;
    if (!size || i >= count) return 0;
    size_t o = (size_t)size * count <= 4 ? e + 8 : rd32(t, e + 8);
    o += (size_t)i * size;
    return size == 2 ? rd16(t, o) : size == 4 ? rd32(t, o) : o < t->n ? t->d[o] : 0;
}

static size_t packbits(const uint8_t *s, size_t n, uint8_t *o, size_t cap)
{
    size_t i = 0, k = 0;
    while (i < n && k < cap) {
        int c = (int8_t)s[i++];
        if (c >= 0) {
            for (int j = 0; j <= c && i < n && k < cap; j++) o[k++] = s[i++];
        } else if (c != -128 && i < n) {
            for (int j = 0; j <= -c && k < cap; j++) o[k++] = s[i];
            i++;
        }
    }
    return k;
}

/* TIFF's LZW: MSB-first codes of 9 to 12 bits, 256 clear, 257 end, the width growing one code early */
static size_t lzw(const uint8_t *s, size_t n, uint8_t *o, size_t cap)
{
    static uint16_t prefix[4096];
    static uint8_t suffix[4096], first[4096];
    uint8_t stack[4096];
    size_t k = 0, bit = 0;
    int width = 9, next = 258, old = -1;
    for (int i = 0; i < 256; i++) suffix[i] = first[i] = (uint8_t)i;
    while (k < cap && (bit + (size_t)width) <= n * 8) {
        int code = 0;
        for (int b = 0; b < width; b++, bit++) code = code << 1 | (s[bit >> 3] >> (7 - (bit & 7)) & 1);
        if (code == 257) break;
        if (code == 256) {
            width = 9, next = 258, old = -1;
            continue;
        }
        int c = code;
        if (old < 0) {
            if (c > 255) break;
            o[k++] = (uint8_t)c;
            old = c;
            continue;
        }
        int sp = 0;
        uint8_t f;
        if (c < next) {
            f = first[c];
        } else if (c == next) {   /* KwKwK */
            f = first[old];
            stack[sp++] = f;
            c = old;
        } else break;
        while (c > 255 && sp < 4095) stack[sp++] = suffix[c], c = prefix[c];
        stack[sp++] = (uint8_t)c;
        while (sp && k < cap) o[k++] = stack[--sp];
        if (next < 4096) {
            prefix[next] = (uint16_t)old, suffix[next] = f, first[next] = first[old];
            next++;
        }
        if (next + 1 >= (1 << width) && width < 12) width++;
        old = code;
    }
    return k;
}

/* Deflate (RFC 1951) after the zlib header: stored, fixed and dynamic Huffman blocks */
typedef struct {
    const uint8_t *s;
    size_t n, bit;
} Bits;

static int bits_get(Bits *b, int k)
{
    int v = 0;
    for (int i = 0; i < k; i++, b->bit++) {
        if ((b->bit >> 3) >= b->n) return -1;
        v |= (b->s[b->bit >> 3] >> (b->bit & 7) & 1) << i;
    }
    return v;
}

typedef struct {
    uint16_t count[16], sym[320];
} Inf;

static void inf_build(Inf *t, const uint8_t *len, int n)
{
    uint16_t offs[16];
    memset(t->count, 0, sizeof t->count);
    for (int i = 0; i < n; i++) t->count[len[i]]++;
    t->count[0] = 0;
    offs[1] = 0;
    for (int l = 1; l < 15; l++) offs[l + 1] = (uint16_t)(offs[l] + t->count[l]);
    for (int i = 0; i < n; i++)
        if (len[i]) t->sym[offs[len[i]]++] = (uint16_t)i;
}

static int inf_decode(Bits *b, const Inf *t)
{
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; l++) {
        int v = bits_get(b, 1);
        if (v < 0) return -1;
        code |= v;
        int c = t->count[l];
        if (code - first < c) return t->sym[index + code - first];
        index += c, first += c;
        first <<= 1, code <<= 1;
    }
    return -1;
}

static size_t inflate(const uint8_t *s, size_t n, uint8_t *o, size_t cap)
{
    static const uint16_t lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115,
                                       131, 163, 195, 227, 258};
    static const uint8_t lextra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
                                       2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const uint8_t dextra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    if (n < 2 || (s[0] & 15) != 8) return 0;
    Bits b = {s + 2, n - 2, 0};
    size_t k = 0;
    Inf lt, dt;
    int last;
    do {
        last = bits_get(&b, 1);
        int type = bits_get(&b, 2);
        if (last < 0 || type < 0 || type == 3) break;
        if (type == 0) {
            b.bit = (b.bit + 7) & ~(size_t)7;
            size_t p = b.bit >> 3;
            if (p + 4 > b.n) break;
            size_t len = (size_t)b.s[p] | (size_t)b.s[p + 1] << 8;
            p += 4;
            if (p + len > b.n) len = b.n - p;
            for (size_t i = 0; i < len && k < cap; i++) o[k++] = b.s[p + i];
            b.bit = (p + len) * 8;
            continue;
        }
        uint8_t len[320];
        if (type == 1) {
            int i = 0;
            for (; i < 144; i++) len[i] = 8;
            for (; i < 256; i++) len[i] = 9;
            for (; i < 280; i++) len[i] = 7;
            for (; i < 288; i++) len[i] = 8;
            inf_build(&lt, len, 288);
            for (i = 0; i < 30; i++) len[i] = 5;
            inf_build(&dt, len, 30);
        } else {
            int hlit = bits_get(&b, 5) + 257, hdist = bits_get(&b, 5) + 1, hclen = bits_get(&b, 4) + 4;
            if (hclen < 4) return k;
            uint8_t cl[19] = {0};
            for (int i = 0; i < hclen; i++) cl[order[i]] = (uint8_t)bits_get(&b, 3);
            Inf ct;
            inf_build(&ct, cl, 19);
            int i = 0;
            while (i < hlit + hdist) {
                int sym = inf_decode(&b, &ct), rep = 0, val = 0;
                if (sym < 0) return k;
                if (sym < 16) {
                    len[i++] = (uint8_t)sym;
                    continue;
                }
                if (sym == 16) {
                    if (!i) return k;
                    val = len[i - 1], rep = 3 + bits_get(&b, 2);
                } else if (sym == 17) rep = 3 + bits_get(&b, 3);
                else rep = 11 + bits_get(&b, 7);
                if (i + rep > hlit + hdist) return k;
                while (rep--) len[i++] = (uint8_t)val;
            }
            inf_build(&lt, len, hlit);
            inf_build(&dt, len + hlit, hdist);
        }
        for (;;) {
            int sym = inf_decode(&b, &lt);
            if (sym < 0 || sym == 256) break;
            if (sym < 256) {
                if (k < cap) o[k++] = (uint8_t)sym;
                continue;
            }
            sym -= 257;
            if (sym >= 29) return k;
            int l = lbase[sym] + bits_get(&b, lextra[sym]);
            int ds = inf_decode(&b, &dt);
            if (ds < 0 || ds >= 30) return k;
            size_t dist = dbase[ds] + (size_t)bits_get(&b, dextra[ds]);
            if (dist > k) return k;
            while (l-- && k < cap) o[k] = o[k - dist], k++;
        }
    } while (!last && k < cap);
    return k;
}

bool FETiff_Decode(const uint8_t *d, size_t n, int *w, int *h, uint8_t **rgb)
{
    *rgb = NULL;
    if (n < 8) return false;
    Tiff t = {d, n, d[0] == 'M'};
    if (!((d[0] == 'I' && d[1] == 'I') || (d[0] == 'M' && d[1] == 'M')) || rd16(&t, 2) != 42) return false;
    size_t ifd = rd32(&t, 4);
    uint32_t cnt = rd16(&t, ifd);
    if (!cnt || ifd + 2 + 12 * (size_t)cnt > n) return false;
    uint32_t W = 0, H = 0, bps = 1, comp = 1, photo = 1, spp = 1, rps = 0xffffffffu, planar = 1, pred = 1;
    uint32_t tw = 0, tl = 0, nchunks = 0;
    size_t offs = 0, counts = 0, cmap = 0;
    for (uint32_t i = 0; i < cnt; i++) {
        size_t e = ifd + 2 + 12 * (size_t)i;
        uint32_t tag = rd16(&t, e);
        if (tag == 256) W = value(&t, e, 0);
        else if (tag == 257) H = value(&t, e, 0);
        else if (tag == 258) bps = value(&t, e, 0);
        else if (tag == 259) comp = value(&t, e, 0);
        else if (tag == 262) photo = value(&t, e, 0);
        else if (tag == 273 || tag == 324) offs = e, nchunks = rd32(&t, e + 4);
        else if (tag == 277) spp = value(&t, e, 0);
        else if (tag == 278) rps = value(&t, e, 0);
        else if (tag == 279 || tag == 325) counts = e;
        else if (tag == 284) planar = value(&t, e, 0);
        else if (tag == 317) pred = value(&t, e, 0);
        else if (tag == 320) cmap = e;
        else if (tag == 322) tw = value(&t, e, 0);
        else if (tag == 323) tl = value(&t, e, 0);
    }
    if (!W || !H || W > 4096 || H > 4096 || !offs || !counts || (planar != 1 && planar != 2)) return false;
    if (comp != 1 && comp != 5 && comp != 8 && comp != 32946 && comp != 32773) return false;
    if (photo == 2 ? !(bps == 8 && spp >= 3) : !(spp == 1 && (bps == 1 || bps == 4 || bps == 8))) return false;
    if (photo > 3 || (photo == 3 && !cmap)) return false;
    if (rps == 0 || rps > H) rps = H;
    bool tiled = tw && tl;
    if (tiled && (tw > 4096 || tl > 4096 || (tw * bps) % 8)) return false;
    if (!tiled) tw = W, tl = rps;
    /* every chunk (a strip or a tile, of one plane when they're apart) into the full image, its samples
       together: rows of W * spp samples */
    uint32_t planes = planar == 2 ? spp : 1, cspp = spp / planes;
    uint32_t across = (W + tw - 1) / tw, down = (H + tl - 1) / tl;
    size_t row = ((size_t)W * spp * bps + 7) / 8, crow = ((size_t)tw * cspp * bps + 7) / 8;
    uint8_t *raw = calloc(1, row * H), *chunk = malloc(crow * tl);
    uint8_t *out = malloc((size_t)W * H * 3);
    bool ok = raw && chunk && out;
    for (uint32_t i = 0; ok && i < nchunks && i < planes * across * down; i++) {
        uint32_t plane = i / (across * down), at = i % (across * down), x0 = at % across * tw, y0 = at / across * tl;
        size_t o = value(&t, offs, i), c = value(&t, counts, i), want = crow * tl;
        if (o >= n) continue;
        if (c > n - o) c = n - o;
        memset(chunk, 0, want);
        if (comp == 1) memcpy(chunk, d + o, c < want ? c : want);
        else if (comp == 5) lzw(d + o, c, chunk, want);
        else if (comp == 32773) packbits(d + o, c, chunk, want);
        else inflate(d + o, c, chunk, want);
        if (pred == 2 && bps == 8)
            for (uint32_t y = 0; y < tl; y++)
                for (size_t x = cspp; x < crow; x++) chunk[y * crow + x] = (uint8_t)(chunk[y * crow + x] + chunk[y * crow + x - cspp]);
        for (uint32_t y = 0; y < tl && y0 + y < H; y++) {
            uint32_t cw = x0 + tw > W ? W - x0 : tw;
            if (planes == 1) {
                memcpy(raw + (y0 + y) * row + (size_t)x0 * spp * bps / 8, chunk + y * crow, ((size_t)cw * spp * bps + 7) / 8);
            } else {
                for (uint32_t x = 0; x < cw; x++) raw[(y0 + y) * row + (size_t)(x0 + x) * spp + plane] = chunk[y * crow + x];
            }
        }
    }
    uint32_t levels = (1u << bps) - 1;
    for (uint32_t y = 0; ok && y < H; y++)
        for (uint32_t x = 0; x < W; x++) {
            const uint8_t *r = raw + y * row;
            uint8_t *p = out + ((size_t)y * W + x) * 3;
            if (photo == 2) {
                memcpy(p, r + (size_t)x * spp, 3);
                continue;
            }
            uint32_t v = bps == 8 ? r[x] : bps == 4 ? (uint32_t)(r[x >> 1] >> (x & 1 ? 0 : 4) & 15) : (uint32_t)(r[x >> 3] >> (7 - (x & 7)) & 1);
            if (photo == 3) {   /* the colour map: all reds, all greens, all blues, 16 bits each */
                for (int k = 0; k < 3; k++) p[k] = (uint8_t)(value(&t, cmap, (uint32_t)k * (levels + 1) + v) >> 8);
            } else {
                uint32_t g = v * 255 / levels;
                p[0] = p[1] = p[2] = (uint8_t)(photo == 0 ? 255 - g : g);
            }
        }
    free(raw);
    free(chunk);
    if (!ok) {
        free(out);
        return false;
    }
    *w = (int)W, *h = (int)H, *rgb = out;
    return true;
}
