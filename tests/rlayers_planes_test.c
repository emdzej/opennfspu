/* The back-face planes the coordinate layers cull with (rlayers.c, RCustomLayers::LayerTransformIndexCoords
   0x4492b0): for every triangle list of every part of every car, the '$n' stream found the way the stream
   binding finds it (R3DMaterial_Bind 0x44a190: a record of the part's own article) must hold one plane per
   triangle, and each plane must lie along its triangle's normal. A plane taken from anywhere else (the
   object's root, as the port once did) culls faces at random. Skips without data. */
#include "exe.h"
#include "render/r3dcar.h"
#include "support/udata.h"
#include "vfs.h"
#include "vfs_host.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static float f32(const uint8_t *p) { uint32_t u = u32(p); float f; memcpy(&f, &u, 4); return f; }

static char names[128][64];
static int nnames;
static long lists, tris, aligned, degenerate;

static void collect(const char *name, bool is_dir, void *user)
{
    size_t l = strlen(name);
    if (is_dir || l < 5 || strcasecmp(name + l - 4, ".crp") || nnames >= 128) return;
    snprintf(names[nnames], sizeof names[nnames], "%.*s", (int)(l - 4), name);
    nnames++;
}

enum { KIND_PLANES = 0x246e };   /* '$n' */

static void check_part(R3DPart *p)
{
    UData *end = UData_itemsend(p->article);
    for (UData *it = UData_finditem(p->article, 0x70722020u, 0, 0xffffffffu); it != end && UData_key(it) == 0x70722020u; it++) {
        const uint8_t *b = UData_data(it);
        if (!b || (u32(b) & 0xff) != 3) continue;            /* triangle lists only */
        uint32_t ns = u32(b + 0x28), nl = u32(b + 0x2c), n = it->count;
        const uint8_t *st = b + 0x30, *lsts = st + 16 * ns, *area = lsts + 8 * nl;
        const uint8_t *planes = NULL, *verts = NULL, *ix = NULL;
        uint32_t nplanes = 0, nv = 0;
        for (uint32_t s = 0; s < ns; s++) {
            const uint8_t *e = st + 16 * s;
            uint16_t kind = u16(e + 10), index = u16(e + 12), list = u16(e + 14);
            if (kind == KIND_PLANES) {
                UData *r = UData_find(p->article, (uint32_t)kind << 16 | index);
                CHECK(r && r != end);
                if (!r || r == end) continue;
                planes = (const uint8_t *)UData_data(r) + u32(e + 4);
                nplanes = u16(e + 8) / 16u;
            } else if (kind == 0 && list != 0xffff && list < nl) {   /* 'vt' through an index list */
                UData *r = UData_find(p->article, (exe_u32(0x5e5084) & 0xffff0000u) | index);
                if (!r || r == end) continue;
                verts = (const uint8_t *)UData_data(r) + u32(e + 4);
                nv = r->count;
                ix = area + u32(lsts + 8 * list + 4);
            }
        }
        if (!planes || !verts) continue;
        lists++;
        CHECK(nplanes == n / 3);
        for (uint32_t t = 0; t < n / 3 && t < nplanes; t++) {
            uint8_t i0 = ix[3 * t], i1 = ix[3 * t + 1], i2 = ix[3 * t + 2];
            if (i0 >= nv || i1 >= nv || i2 >= nv) { CHECK(!"index out of range"); break; }
            float a[3], bb[3], c[3];
            for (int k = 0; k < 3; k++) {
                a[k] = f32(verts + 16 * i0 + 4 * k);
                bb[k] = f32(verts + 16 * i1 + 4 * k);
                c[k] = f32(verts + 16 * i2 + 4 * k);
            }
            float u[3] = {bb[0] - a[0], bb[1] - a[1], bb[2] - a[2]}, v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            float nn[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
            const uint8_t *q = planes + 16 * t;
            float pl[3] = {f32(q), f32(q + 4), f32(q + 8)};
            float ln = sqrtf(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
            float lp = sqrtf(pl[0] * pl[0] + pl[1] * pl[1] + pl[2] * pl[2]);
            tris++;
            if (ln < 1e-6f || lp < 1e-6f) { degenerate++; continue; }
            if (fabsf(nn[0] * pl[0] + nn[1] * pl[1] + nn[2] * pl[2]) / (ln * lp) > 0.99f) aligned++;
        }
    }
}

static void check_car(const char *base)
{
    char name[96];
    snprintf(name, sizeof name, "GameData\\CarModel\\%s", base);
    R3DCarState *st = calloc(1, sizeof *st);
    R3DCar *car = calloc(1, sizeof *car);
    R3DCar_Init(car, st);
    st->rot[0] = st->rot[4] = st->rot[8] = 1;
    if (R3DCar_LoadCrp(car, name) && car->loaded)
        for (uint32_t l = 0; l < car->nlods; l++)
            if (car->lods[l])
                for (uint32_t k = 0; k < car->lods[l]->nparts; k++) check_part(&car->lods[l]->parts[k]);
    R3DCar_Free(car);
    free(car);
    free(st);
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) {
        printf("rlayers_planes_test: no game data, skipped\n");
        return 0;
    }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("rlayers_planes_test: %s\n", err); return 1; }
    vfs_list("GameData/CarModel", collect, NULL);
    for (int i = 0; i < nnames; i++) check_car(names[i]);
    long usable = tris - degenerate;
    printf("rlayers_planes_test: %d cars, %ld triangle lists with planes, %ld triangles (%ld degenerate), %ld aligned\n",
           nnames, lists, tris, degenerate, aligned);
    CHECK(lists > 0);
    /* Every non-degenerate triangle's plane along its normal (a little slack for near-degenerate ones). */
    CHECK(usable > 0 && aligned >= usable - usable / 1000);
    printf("rlayers_planes_test: %d checks: %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
