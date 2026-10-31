/* The track's dynamic lights (dynlight.h). The original's map is a red-black tree keyed by the record's
   address; the port keeps a sorted array, the same order. */
#include "render/dynlight.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    const DynLightRec *rec;
    Texture *tex;
} DynLight;

static DynLight *s_lights;
static int s_n, s_cap;
static int s_live;   /* 0x627dfc made */

void DynLights_Init(void)
{
    free(s_lights);
    s_lights = NULL;
    s_n = s_cap = 0;
    s_live = 1;
}

void DynLights_Free(void)
{
    free(s_lights);
    s_lights = NULL;
    s_n = s_cap = 0;
    s_live = 0;
}

void DynLights_Add(const DynLightRec *rec, Texture *tex)
{
    if (!s_live) return;
    int lo = 0, hi = s_n;
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if ((uintptr_t)s_lights[m].rec < (uintptr_t)rec) lo = m + 1;
        else hi = m;
    }
    if (lo < s_n && s_lights[lo].rec == rec) return;
    if (s_n == s_cap) {
        int cap = s_cap ? s_cap * 2 : 64;
        DynLight *a = realloc(s_lights, (size_t)cap * sizeof *a);
        if (!a) return;
        s_lights = a;
        s_cap = cap;
    }
    memmove(&s_lights[lo + 1], &s_lights[lo], (size_t)(s_n - lo) * sizeof *s_lights);
    s_lights[lo] = (DynLight){rec, tex};
    s_n++;
}

const DynLightRec *DynLights_Nearest(const float *p, Texture **tex)
{
    const DynLightRec *best = NULL;
    Texture *bt = NULL;
    float d = 400.0f;
    for (int i = 0; i < s_n; i++) {
        const DynLightRec *r = s_lights[i].rec;
        float dx = p[0] - r->pos[0], dz = p[2] - r->pos[2];
        if (dz < 0) dz = -dz;
        if (dx < 0) dx = -dx;
        float k = dx <= dz ? dx * 0.25f + dz : dz * 0.25f + dx;   /* 0x5b2468 */
        if (k < d && k < r->radius + 2.5f) {                        /* 0x5b2464 */
            best = r;
            bt = s_lights[i].tex;
            d = k;
        }
    }
    *tex = bt;
    return best;
}
