/* EA real library packers, see pack.h. */
#include "real/pack.h"
#include "pending.h"
#include "real/locatbig.h"

/* The codecs the data doesn't use. 0x5941b0 (type 0x18), 0x5935b0 (0x30/0x32/0x34), 0x5933f0 (0x46). */
static uint32_t unpack_type18(const uint8_t *src, uint8_t *dst) { PENDING_NOTE(0x5941b0, "UNPACK_type18"); return 0; }
static uint32_t unpack_type30(const uint8_t *src, uint8_t *dst) { PENDING_NOTE(0x5935b0, "UNPACK_type30"); return 0; }
static uint32_t unpack_type46(const uint8_t *src, uint8_t *dst) { PENDING_NOTE(0x5933f0, "UNPACK_type46"); return 0; }

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
