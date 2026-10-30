/* The render system's draw lists (see rlists.h). */
#include "render/rlists.h"
#include "real/set3d.h"
#include <stdlib.h>
#include <string.h>

typedef struct { RListEntry *e; uint32_t n, cap; } RList;

static RList lists[RLIST_COUNT];   /* the object's eight maps (+0x00 .. +0x1c) */

void RLists_Init(void)
{
    for (int i = 0; i < RLIST_COUNT; i++) lists[i].n = 0;
}

/* The map's comparison: +0x04 orders by ascending key, the others by descending key (0x466880,
   0x466950 and 0x466a20 go left while the node's key is below the new one). */
static bool before(int list, uint32_t a, uint32_t b) { return list == RLIST_PRIORITY ? a < b : a > b; }

void RLists_Insert(int list, const RListEntry *e)
{
    RList *l = &lists[list];
    if (l->n == l->cap) {
        uint32_t c = l->cap ? l->cap * 2 : 256;
        RListEntry *ne = realloc(l->e, c * sizeof *ne);
        if (!ne) return;   /* the original's node allocator aborts when it runs out (0x59ec60) */
        l->e = ne;
        l->cap = c;
    }
    /* After every entry that doesn't order after the new one: equal keys keep their insertion order. */
    uint32_t lo = 0, hi = l->n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (before(list, e->key, l->e[mid].key)) hi = mid;
        else lo = mid + 1;
    }
    memmove(l->e + lo + 1, l->e + lo, (l->n - lo) * sizeof *l->e);
    l->e[lo] = *e;
    l->n++;
}

uint32_t RLists_Count(int list) { return lists[list].n; }

void RLists_Flush(void)
{
    /* The priority list (the 0x20000 'pr' lists: ordered by priority, then distance), alpha reference 10. */
    THRASH.fn.setstate(0x24, 10);
    for (uint32_t i = 0; i < lists[RLIST_PRIORITY].n; i++) {
        const RListEntry *e = &lists[RLIST_PRIORITY].e[i];
        e->obj->vt->draw(e->obj, e->index, e);   /* 0x466790 */
    }
    /* Translucent, farthest first, alpha reference 0xa0. */
    THRASH.fn.setstate(0x24, 0xa0);
    for (uint32_t i = 0; i < lists[RLIST_TRANSLUCENT].n; i++) {
        const RListEntry *e = &lists[RLIST_TRANSLUCENT].e[i];
        e->obj->vt->draw(e->obj, e->index, e);
    }
    THRASH.fn.setstate(0x24, 10);
    for (uint32_t i = 0; i < lists[RLIST_OPAQUE].n; i++) {
        const RListEntry *e = &lists[RLIST_OPAQUE].e[i];
        e->obj->vt->draw(e->obj, e->index, e);
    }
    for (uint32_t i = 0; i < lists[RLIST_EFFECTS].n; i++) {
        const RListEntry *e = &lists[RLIST_EFFECTS].e[i];
        e->obj->vt->special(e->obj, e->index, e);
    }
    /* Emptied: the four above and +0x18, +0x1c; +0x00 and +0x14 keep theirs. */
    static const int cleared[] = {RLIST_PRIORITY, RLIST_TRANSLUCENT, RLIST_OPAQUE, RLIST_EFFECTS, 6, 7};
    for (size_t i = 0; i < sizeof cleared / sizeof *cleared; i++) lists[cleared[i]].n = 0;
}
