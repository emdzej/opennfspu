/* \real\cmn\locatbig.c: see locatbig.h. Header layouts as the original reads them:
   - BIGF: "BIGF", archive size, then (big-endian) the entry count at +8 and the directory size at +12;
     records from +16: offset (4), size (4), NUL-terminated name.
   - 0xC0FB: the 16-bit magic, the directory size minus 4 at +2 (2 bytes), the count at +4 (2 bytes);
     records from +6: offset (3), size (3), NUL-terminated name.
   The directory size bounds the scan: a record is only looked at while it starts inside it. */
#include "real/locatbig.h"

uint32_t BIG_getnum(const uint8_t *p, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v = v << 8 | p[i];
    return v;
}

/* MSVC _stricmp in the C locale (0x5ae170 is the CRT's): ASCII letters fold to lower case. */
static int big_stricmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca < cb ? -1 : 1;
        if (!ca) return 0;
    }
}

int BIG_kind(const uint8_t *big)
{
    if (BIG_getnum(big, 2) == 0xc0fb) return BIG_C0FB;
    return BIG_getnum(big, 4) == 0x42494746 ? BIG_BIGF : BIG_NONE;   /* "BIGF" */
}

uint32_t BIG_headersize(const uint8_t *big)
{
    switch (BIG_kind(big)) {
    case BIG_C0FB: return BIG_getnum(big + 2, 2) + 4;
    case BIG_BIGF: return BIG_getnum(big + 12, 4);
    default: return 0;
    }
}

uint32_t BIG_count(const uint8_t *big)
{
    switch (BIG_kind(big)) {
    case BIG_C0FB: return BIG_getnum(big + 4, 2);
    case BIG_BIGF: return BIG_getnum(big + 8, 4);
    default: return 0;
    }
}

const char *BIG_findentry(const uint8_t *big, const char *name, int index, uint32_t *offset, uint32_t *size)
{
    int kind = BIG_kind(big);
    uint32_t end = BIG_headersize(big);
    /* Record layout per kind: where the records start and how wide the two numbers are. */
    uint32_t first = kind == BIG_C0FB ? 6 : 16;
    int w = kind == BIG_C0FB ? 3 : 4;
    if (kind != BIG_NONE) {
        int i = 0;
        for (uint32_t r = first; r < end;) {
            const char *n = (const char *)big + r + 2 * w;
            if (name ? big_stricmp(n, name) == 0 : i == index) {
                if (offset) *offset = BIG_getnum(big + r, w);
                if (size) *size = BIG_getnum(big + r + w, w);
                return n;
            }
            size_t l = 0;
            while (n[l]) l++;
            r += 2 * w + (uint32_t)l + 1;
            i++;
        }
    }
    if (offset) *offset = 0;
    if (size) *size = 0;
    return NULL;
}

/* The original reports a miss only when its diagnostics flag (0x5deb88) is set, which the release build
   never does: silent here too. */
const char *BIG_locateentry(const uint8_t *big, const char *name, int index, uint32_t *offset, uint32_t *size)
{
    return BIG_findentry(big, name, index, offset, size);
}
