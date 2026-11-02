/* EA real library packers, see pack.h. */
#include "real/pack.h"
#include "pending.h"
#include "real/locatbig.h"

/* The codecs below the RefPack one, which no shipped file uses: 0x5941b0 (type 0x18), 0x5935b0
   (0x30/0x32/0x34), 0x5933f0 (0x46). */

static uint32_t le16(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8); }   /* 0x56e058 */

/* 0x594240: a 4 x 4 block of 565 pixels from 8 bytes: two colours (little-endian), the two between them
   (thirds, by the 0x5556/0xaaaa products) when the first is the greater, else their mean and 0; then
   four rows of four 2-bit indices, the low bits first. */
static void block565(uint8_t *dst, int stride, const uint8_t *src)
{
    uint32_t c[4];
    c[0] = le16(src);
    c[1] = le16(src + 2);
    if (c[1] < c[0]) {
        uint32_t a = c[0], b = c[1];
        c[2] = (((b & 0xf800) * 0x5556 + (a & 0xf800) * 0xaaaa & 0xf800ffffu) |
                ((b & 0x7e0) * 0x5556 + (a & 0x7e0) * 0xaaaa & 0x7e00000) |
                ((b & 0x1f) * 0x5556 + (a & 0x1f) * 0xaaaa & 0x1f0000)) >> 16;
        c[3] = (((a & 0xf800) * 0x5556 + (b & 0xf800) * 0xaaaa & 0xf800ffffu) |
                ((a & 0x7e0) * 0x5556 + (b & 0x7e0) * 0xaaaa & 0x7e00000) |
                ((a & 0x1f) * 0x5556 + (b & 0x1f) * 0xaaaa & 0x1f0000)) >> 16;
    } else {
        c[3] = 0;
        c[2] = (((c[1] & 0xf800) + (c[0] & 0xf800) & 0x1f001) | ((c[1] & 0x7e0) + (c[0] & 0x7e0) & 0xfc0) |
                ((c[1] & 0x1f) + (c[0] & 0x1f) & 0x3e)) >> 1;
    }
    for (int r = 0; r < 4; r++, dst += stride) {
        uint8_t b = src[4 + r];
        for (int k = 0; k < 4; k++) {
            uint32_t v = c[b >> 2 * k & 3];
            dst[2 * k] = (uint8_t)v;
            dst[2 * k + 1] = (uint8_t)(v >> 8);
        }
    }
}

/* Type 0x18: the unpacked size (3 bytes, +2) and the width (2 bytes, +6), both big-endian, then the 4 x
   4 blocks row by row. A row of blocks moves the output by 8 bytes a block and then by three more pixel
   rows (width * 6 bytes): the same as four pixel rows only for widths a multiple of 4. */
static uint32_t unpack_type18(const uint8_t *src, uint8_t *dst)
{
    int size = (int)BIG_getnum(src + 2, 3), w = (int)BIG_getnum(src + 6, 2);
    const uint8_t *p = src + 8;
    if (w <= 0) return (uint32_t)size;   /* (the original loops for ever on such a header) */
    for (int done = 0; done < size; done += w * 8) {
        for (int n = (w + 3) >> 2; n > 0; n--, dst += 8, p += 8) block565(dst, w * 2, p);
        dst += w * 6;
    }
    return (uint32_t)size;
}

static uint32_t unpack_type30(const uint8_t *src, uint8_t *dst) { PENDING_NOTE(0x5935b0, "UNPACK_type30"); return 0; }

/* Type 0x46 (BTREE): the size (3 bytes big-endian, after the 5-byte packed size of 0x47), an escape byte
   and the pairs: n nodes {byte, left, right}. A node byte stands for its two bytes, each expanded again
   (0x593540, the right ones in a loop); the escape byte is followed by a literal, or 0 for the end. */
typedef struct { uint8_t kind[256], left[256], right[256]; uint8_t *out; } Btree;   /* 0x6af08c .. 0x6af098 */

static void btree_expand(Btree *t, uint8_t b)   /* 0x593540 */
{
    if (!t->kind[b]) {
        *t->out++ = b;
        return;
    }
    do {
        btree_expand(t, t->left[b]);
        b = t->right[b];
    } while (t->kind[b]);
    *t->out++ = b;
}

static uint32_t unpack_type46(const uint8_t *src, uint8_t *dst)
{
    if (!src) return 0;
    const uint8_t *p = (src[0] << 8 | src[1]) == 0x47fb ? src + 5 : src + 2;
    uint32_t size = BIG_getnum(p, 3);
    Btree t = {.out = dst};
    t.kind[p[3]] = 1;
    int n = p[4];
    p += 5;
    for (; n > 0; n--, p += 3) {
        t.left[p[0]] = p[1];
        t.kind[p[0]] = 0xff;
        t.right[p[0]] = p[2];
    }
    for (;;) {
        uint8_t b = *p;
        if (!t.kind[b]) {
            *t.out++ = b;
            p++;
        } else if (t.kind[b] == 0xff) {
            p++;
            btree_expand(&t, t.left[b]);
            btree_expand(&t, t.right[b]);
        } else {
            uint8_t lit = p[1];
            p += 2;
            if (!lit) break;
            *t.out++ = lit;
        }
    }
    return size;
}

uint32_t UNPACK_size(const uint8_t *src)
{
    if (src[1] != 0xfb && src[1] != 0x32) return 0;
    switch (src[0] & 0xfe) {
    case 0x10: case 0x18: case 0x30: case 0x32: case 0x34: case 0x46:
        return BIG_getnum(src + 2, 3);   /* the size after the magic, even for types with a packed size */
    default:
        return 0;
    }
}

uint32_t UNPACK_dispatch(const uint8_t *src, uint8_t *dst)
{
    if (src[1] != 0xfb && src[1] != 0x32) return 0;
    switch (src[0] & 0xfe) {
    case 0x10: return REFPACK_decode(src, dst, 1);
    case 0x18: return unpack_type18(src, dst);
    case 0x30: case 0x32: case 0x34: return unpack_type30(src, dst);
    case 0x46: return unpack_type46(src, dst);
    default: return 0;
    }
}

/* Back-reference copy, byte by byte so overlapping runs repeat (0x5946a0); returns the new end. */
static uint8_t *refpack_copy(uint8_t *dst, uint32_t dist, uint32_t len)
{
    const uint8_t *s = dst - dist;
    while (len--) *dst++ = *s++;
    return dst;
}

/* Four command forms by the first byte, each a few literal bytes then a back-reference:
     0xxxxxxx: 2 bytes, literals b0&3, length ((b0>>2)&7)+3, distance ((b0&0x60)<<3)+b1+1
     10xxxxxx: 3 bytes, literals b1>>6, length (b0&0x3f)+4, distance ((b1&0x3f)<<8)+b2+1
     110xxxxx: 4 bytes, literals b0&3, length ((b0&0x0c)<<6)+b3+5, distance ((b0&0x10)<<12)+(b1<<8)+b2+1
     111xxxxx: 1 byte, (b0&0x1f)*4+4 literals; from 0xfc on: b0&3 literals and the end.
   The original copies literals a whole word at a time (the bytes past the count are overwritten by what
   follows), so the output is the same as this byte copy. */
uint32_t REFPACK_decode(const uint8_t *src, uint8_t *dst, int decode)
{
    if (!src) return 0;
    const uint8_t *p = src + ((src[0] & 1) ? 5 : 2);
    uint32_t size = BIG_getnum(p, 3);
    p += 3;
    if (!decode) return size;
    uint8_t *o = dst;
    for (;;) {
        uint32_t b0 = p[0], n, len, dist;
        if (!(b0 & 0x80)) {
            uint32_t b1 = p[1];
            n = b0 & 3;
            len = ((b0 >> 2) & 7) + 3;
            dist = ((b0 & 0x60) << 3) + b1 + 1;
            p += 2;
        } else if (!(b0 & 0x40)) {
            uint32_t b1 = p[1], b2 = p[2];
            n = b1 >> 6;
            len = (b0 & 0x3f) + 4;
            dist = ((b1 & 0x3f) << 8) + b2 + 1;
            p += 3;
        } else if (!(b0 & 0x20)) {
            uint32_t b1 = p[1], b2 = p[2], b3 = p[3];
            n = b0 & 3;
            len = ((b0 & 0x0c) << 6) + b3 + 5;
            dist = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1;
            p += 4;
        } else {
            p++;
            if (b0 >= 0xfc) {
                for (n = b0 & 3; n; n--) *o++ = *p++;
                return size;
            }
            for (n = (b0 & 0x1f) * 4 + 4; n; n--) *o++ = *p++;
            continue;
        }
        while (n--) *o++ = *p++;
        o = refpack_copy(o, dist, len);
    }
}
