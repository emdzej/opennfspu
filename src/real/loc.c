/* EA real library: localised strings (see loc.h). */
#include "real/loc.h"
#include <string.h>

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

void LOC_setlanguage(uint8_t *loc, int set, int language)
{
    if (set == 0) {
        uint16_t l = (uint16_t)language;
        memcpy(loc + 0xe, &l, 2);
    }
}

/* The string index of an id through the index block (a binary search on the ids), -1 if absent. 0x536b60 */
static int lookup(int id, const uint8_t *index)
{
    int lo = 0, hi = (int)rd32(index + 8) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int d = (int)rd16(index + 0x10 + 4 * mid) - (int)(uint16_t)id;
        if (d == 0) return rd16(index + 0x10 + 4 * mid + 2);
        if (d < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

const char *LOC_string(const uint8_t *loc, int id)
{
    if (rd32(loc + 8) & 1) id = lookup(id, loc + rd32(loc + 4));
    const uint8_t *t = loc + rd32(loc + 0x10 + 4u * rd16(loc + 0xe));
    if (id < 0 || id >= (int)rd32(t + 0xc)) return NULL;
    return (const char *)(t + rd32(t + 0x10 + 4u * (uint32_t)id));
}
