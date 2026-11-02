/* The race's cars (see car.h). */
#include "game/car.h"
#include "game/track_table.h"
#include "game/traffic.h"
#include "game/skid.h"
#include "render/carparticle.h"
#include "game/race_car.h"
#include "exe.h"
#include "pending.h"
#include "real/mem.h"
#include "render/carpages.h"
#include "render/r3dcar.h"
#include "support/ufile.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

Car *g_cars[CAR_MAX];
int g_ncars;
Car *g_traffic[TRAFFIC_MAX];
int g_ntraffic;
uint8_t g_cardesc[CAR_MAX][CARDESC_SIZE];
int g_nracecars, g_ntrafficcars;
TaskList g_tasks_tick, g_tasks_even;

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
#define K(va) exe_f32(va)   /* the float constant the original reads at va */

CarList g_racers, g_humans, g_opponents, g_viewed, g_rivals, g_roadcars, g_chasers, g_ranked, g_ordered;
int32_t g_race_cops;
static int g_created;   /* 0x5e8bd0 */

static void list_add(CarList *l, Car *c)
{
    if (l->n < CAR_MAX * 3) l->c[l->n++] = c;
}

/* ---- task lists ---- */

void TaskList_Add(TaskList *l, TaskFn fn, void *arg, int prio)
{
    int n = l->count, at = 0;
    if (n + 1 > (int)(sizeof l->e / sizeof *l->e)) return;   /* the original has no bound */
    l->count = n + 1;
    for (; at < n + 1; at++)
        if (prio < l->e[at].prio) {
            memmove(&l->e[at + 1], &l->e[at], (size_t)(n - at) * sizeof *l->e);
            break;
        }
    if (at > n) at = n;
    l->e[at].prio = prio;
    l->e[at].fn = fn;
    l->e[at].arg = arg;
}

void TaskList_Remove(TaskList *l, TaskFn fn, void *arg)
{
    int n = l->count;
    if (!n) return;
    for (int i = 0; i < n; i++)
        if (l->e[i].fn == fn && l->e[i].arg == arg) {
            memmove(&l->e[i], &l->e[i + 1], (size_t)(n - 1 - i) * sizeof *l->e);
            break;
        }
    l->e[n - 1].prio = 0x7ffe;   /* the vacated last slot */
    l->count = n - 1;
}

void TaskList_Run(TaskList *l)
{
    for (int i = 0; i < l->count; i++)
        if (l->e[i].fn) l->e[i].fn(l->e[i].arg);
}

/* ---- parts ---- */

static uint8_t *g_parts;   /* 0x628f84 (0x628f88 the allocation) */
static int g_nparts;       /* 0x628f80 */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* The front end's data directory (0x65b348): FEData\Data\ in the installed game. */
static const char *fedata_dir(void) { return "FEData\\Data\\"; }

int Parts_Load(void)
{
    char path[80];
    snprintf(path, sizeof path, "%s%s", fedata_dir(), exe_str(0x5d0c4c));   /* "nfs5.prt" */
    int64_t size = vfs_size(path);
    if (size <= 0) return 0;
    size_t n = 0;
    uint8_t *d = vfs_read_all(path, &n);
    if (!d) return 0;
    free(g_parts);
    g_parts = d;
    g_nparts = (int)(n / CARPART_SIZE);
    return g_nparts;
}

void Parts_Find(Car *c)
{
    uint8_t *desc = CAR_DESCB(c);
    for (int off = 0x32c; off < 0x50c; off += 4) memset(desc + off, 0xff, 4);   /* GEOMCONFIG: -1 */
    for (int i = 0; i < CAR_NPARTS; i++) {
        uint8_t *p = c->p[CARP_PARTS + i];
        memset(p + 0x26, 0, 2);
        memset(p + 0x48, 0, 9);
        memset(p + 0x164, 0, 4);
        memset(p + 0x54, 0, 4);
        float one = 1.0f;
        memcpy(p + 0xc0, &one, 4);
        memcpy(p + 0xc4, &one, 4);
        memset(p + 0xc8, 0, 4);
        memset(p + 0xea, 0, 4);
        memset(p + 0xf0, 0, 0x164 - 0xf0);
        int32_t id;
        memcpy(&id, desc + 0x28 + 4 * i, 4);   /* PARTINDEX */
        if (!id) continue;
        const uint8_t *src = NULL;
        for (int k = 0; k < g_nparts; k++)
            if ((int16_t)rd16(g_parts + k * CARPART_SIZE) == id) {
                src = g_parts + k * CARPART_SIZE;
                break;
            }
        if (!src) {
            PENDING_NOTE(0x4db520, "the \"Part %d not found.\" message (the original then copies from address 0)");
            continue;
        }
        memcpy(p, src, CARPART_SIZE);
        *CAR_I(c, 0xbf8 + 4 * i) = (int16_t)rd16(p);   /* +0xbf8: the part's id */
        int16_t n = (int16_t)rd16(p + 0x8c);
        for (int k = 0; k < n; k++) {
            int16_t a = (int16_t)rd16(p + 0x8e + 6 * k), b = (int16_t)rd16(p + 0x90 + 6 * k);
            int32_t v = (int16_t)rd16(p + 0x92 + 6 * k);
            memcpy(desc + 0x32c + 4 * (b + a * 0x3c), &v, 4);
        }
    }
}

/* ---- specifications ---- */

/* The simulation's directories (0x65b2ac, 0x65b2b0). */
static const char *cardata_dir(void) { return "GameData\\Simulation\\CarData\\"; }
static const char *aicardata_dir(void) { return "GameData\\Simulation\\AICarData\\"; }

void Sim_LoadCarSim(Car *c)
{
    char path[100];
    snprintf(path, sizeof path, exe_str(0x5d0cc0), cardata_dir(), (const char *)CAR_DESCB(c) + 0x268);   /* "%s%s.sim" */
    uint8_t *d = UTIL_loadfile(path, 0);
    if (!d) {
        snprintf(path, sizeof path, exe_str(0x5d0cb0), cardata_dir());                                 /* "%sdefault.sim" */
        d = UTIL_loadfileabort(path, 0);
    }
    /* The original copies 0x1c4 bytes from a 328-byte file, reading on into the heap; those bytes are
       fields Sim_BuildEngine computes: the port copies what the file has and zeroes the rest. */
    if (d) {
        int n = MEM_size(d);
        memset(CAR_SPECSB(c), 0, CARSPECS_SIZE);
        MEM_copy(CAR_SPECSB(c), d, n < CARSPECS_SIZE ? n : CARSPECS_SIZE);
    }
    *SPEC_I(c, 0x50) += 2;
    *SPEC_F(c, 0x90) *= K(0x5b4a28);
    static const int pct[] = {0x58, 0x5c, 0x98, 0xf0, 0xf8, 0x108, 0x114, 0x118};
    for (size_t i = 0; i < sizeof pct / sizeof *pct; i++) *SPEC_F(c, pct[i]) *= K(0x5b3ca0);
    static const int k2[] = {0x44, 0x10c, 0x110, 0x120, 0x124, 0x128, 0x12c};
    for (size_t i = 0; i < sizeof k2 / sizeof *k2; i++) *SPEC_F(c, k2[i]) *= K(0x5b3060);
    if (DESC_I(c, 0x518)) {   /* TINYCAR */
        *SPEC_F(c, 0x44) *= K(0x5b24e8);
        *SPEC_F(c, 0x114) *= K(0x5b4a24);
        *SPEC_F(c, 0x118) *= K(0x5b4a24);
        *SPEC_F(c, 0xf4) *= K(0x5b3f64);
    }
    MEM_purge(d);
    Sim_BuildEngine(c);
}

void Sim_LoadCarAis(Car *c)
{
    char path[100];
    snprintf(path, sizeof path, exe_str(0x5d0cdc), aicardata_dir(), (const char *)CAR_DESCB(c) + 0x2a8);   /* "%s%s.ais" */
    uint8_t *d = UTIL_loadfile(path, 0);
    if (!d) {
        snprintf(path, sizeof path, exe_str(0x5d0ccc), aicardata_dir());
        d = UTIL_loadfileabort(path, 0);
    }
    if (!d) return;
    /* The 40 speeds from +0x80: their minimum (scanned from +0x11c down, starting at 100), the spread
       above it scaled by the car's +0x10d8, times the track's factor for the car (Track_Table 0x4a2ba0). */
    float lo = 100.0f;
    for (int i = 0; i < 0x28; i++) {
        float v;
        memcpy(&v, d + 0x11c - 4 * i, 4);
        if (v < lo) lo = v;
    }
    for (int i = 0; i < 0x28; i++) {
        float v;
        memcpy(&v, d + 0x80 + 4 * i, 4);
        v = (v - lo) * *CAR_F(c, CAR_10D8) + lo;
        v = Track_Table((const char *)CAR_DESCB(c) + 0x2a8) * v;
        memcpy(d + 0x80 + 4 * i, &v, 4);
    }
    *SPEC_F(c, 0x140) = Track_CarFactor((const char *)CAR_DESCB(c) + 0x2a8) * *SPEC_F(c, 0x140);
    {
        int an = MEM_size(d);
        memset(c->p[CARP_AISPECS], 0, AISPECS_SIZE);
        MEM_copy(c->p[CARP_AISPECS], d, an < AISPECS_SIZE ? an : AISPECS_SIZE);
    }
    MEM_purge(d);
}

/* MSVC _stricmp (0x5ae170 in the CRT). */
static int name_is(const char *a, const char *b) { return strcasecmp(a, b) == 0; }

/* x87 __ftol: truncation. */
static int32_t ftol(float v) { return (int32_t)v; }

void Sim_BuildEngine(Car *c)
{
    uint8_t *desc = CAR_DESCB(c);
    const char *ai = (const char *)desc + 0x2a8;
    /* The racing classes (GT1, GT2, GT3, 935) get the type 2 at +0x1c0. */
    *SPEC_I(c, 0x1c0) = name_is(ai, exe_str(0x5d0d30)) || name_is(ai, exe_str(0x5d0d2c)) || name_is(ai, exe_str(0x5d0d28)) ||
                        name_is(ai, exe_str(0x5d0d24)) ? 2 : 0;
    *SPEC_F(c, 0x14c) = K(0x5b24a8) / *SPEC_F(c, 0x94);
    *SPEC_F(c, 0x148) = (*SPEC_F(c, 0x8c) * K(0x5b4a40) + *SPEC_F(c, 0x88) * K(0x5b3714) * *SPEC_F(c, 0x84)) * K(0x5b4a3c);
    float peak0 = 0;   /* the torque curve's peak before the parts (21 values from +0x9c) */
    for (int i = 0; i < 0x15; i++)
        if (peak0 <= *SPEC_F(c, 0x9c + 4 * i)) peak0 = *SPEC_F(c, 0x9c + 4 * i);
    *CAR_F(c, CAR_10D8) = *SPEC_F(c, 0xf4);
    for (int i = 0; i < CAR_NPARTS; i++) {
        uint8_t *p = c->p[CARP_PARTS + i];
        int16_t kind = (int16_t)rd16(p + 0x26);
        if (!kind) continue;
        float pf54, pf58, pc0, pc4;
        memcpy(&pf54, p + 0x54, 4);
        memcpy(&pf58, p + 0x58, 4);
        memcpy(&pc0, p + 0xc0, 4);
        memcpy(&pc4, p + 0xc4, 4);
        *SPEC_F(c, 0x40) = pf54 + *SPEC_F(c, 0x40);                              /* mass */
        *SPEC_F(c, 0x90) = *SPEC_F(c, 0x90) / (pf58 * K(0x5b3ca0) + K(0x5b24a8));
        const char *name = (const char *)p + 0x28;
        if (kind == 1) {
            if (!strcmp(name, exe_str(0x5d0d1c))) {   /* "Engine": its torque curve */
                memcpy(SPEC_F(c, 0x9c), p + 0x9c + 0x74, 0xf0 - 0x9c);
            } else {
                if (!strcmp(name, exe_str(0x5d0d14))) *SPEC_I(c, 0x4c) = 1;        /* "Turbo" */
                else if (!strcmp(name, exe_str(0x5d0d04))) *SPEC_I(c, 0x4c) = 2;   /* "Supercharged" */
                for (int o = 0x9c; o < 0xf0; o += 4) {
                    float add;
                    memcpy(&add, p + o + 0x74, 4);
                    *SPEC_F(c, o) = add * K(0x5b4a38) + *SPEC_F(c, o);
                }
            }
        } else if (kind == 2) {
            *SPEC_F(c, 0x104) = (pc0 + K(0x5b2600)) * K(0x5b2468) * *SPEC_F(c, 0x104);
        } else if (kind == 3) {
            if (!strcmp(name, exe_str(0x5d0cf8))) {   /* "Gear Box": its gears */
                *SPEC_I(c, 0x54) = (int16_t)rd16(p + 0xec);
                *SPEC_I(c, 0x50) = (int16_t)rd16(p + 0xea) + 2;
                memcpy(SPEC_F(c, 0x80), &pc0, 4);
                memcpy(SPEC_F(c, 0x60), p + 0x60 + 0x90, 0x80 - 0x60);
            } else
                *SPEC_I(c, 0x54) += (int16_t)rd16(p + 0xec);
        } else if (kind == 4) {
            *SPEC_F(c, 0x11c) = (pc0 + K(0x5b2404)) * *SPEC_F(c, 0x11c) * K(0x5b2694);
            *SPEC_F(c, 0x130) = (pc0 + K(0x5b2404)) * *SPEC_F(c, 0x130) * K(0x5b2694);
            *SPEC_F(c, 0xf4) = (pc0 + K(0x5b243c)) * *SPEC_F(c, 0xf4) * K(0x5b26a4);
            *SPEC_F(c, 0x12c) = *SPEC_F(c, 0x12c) / ((pc0 + K(0x5b2600)) * K(0x5b2468));
            *SPEC_F(c, 0x120) = pc4 * K(0x5b3060);
        } else if (kind == 5) {
            *SPEC_F(c, 0x114) = *SPEC_F(c, 0x114) / ((pc0 + K(0x5b2404)) * K(0x5b2694));
            *SPEC_F(c, 0x118) = *SPEC_F(c, 0x118) / ((pc0 + K(0x5b2404)) * K(0x5b2694));
            *SPEC_F(c, 0xf4) = (pc0 + K(0x5b263c)) * *SPEC_F(c, 0xf4) * K(0x5b49b0);
        } else if (kind == 8) {
            *SPEC_F(c, 0x11c) = (pc0 + K(0x5b2404)) * *SPEC_F(c, 0x11c) * K(0x5b2694);
            *SPEC_F(c, 0xf4) = (pc0 + K(0x5b263c)) * *SPEC_F(c, 0xf4) * K(0x5b49b0);
            *SPEC_F(c, 0x12c) = *SPEC_F(c, 0x12c) / ((pc0 + K(0x5b2600)) * K(0x5b2468));
        } else if (kind == 6) {
            float f5 = pc4 * K(0x5b3284) + K(0x5b24a8);
            *SPEC_F(c, 0x10c) = (pc0 * K(0x5b3284) + K(0x5b24a8)) * *SPEC_F(c, 0x10c);
            *SPEC_F(c, 0x110) = f5 * *SPEC_F(c, 0x110);
        } else if (kind == 9) {
            float f5 = *SPEC_F(c, 0xf4) * K(0x5b2600);
            float f6 = (f5 + pc0) / (f5 + K(0x5b24a8));
            f5 = (f5 + pc4) / (f5 + K(0x5b24a8)) + f6;
            *SPEC_F(c, 0xf4) = (f5 + K(0x5b24a8)) * *SPEC_F(c, 0xf4) * K(0x5b2680);
            *SPEC_F(c, 0xf0) = ((f6 + f6) / f5 + K(0x5b24a8)) * K(0x5b23f0) * *SPEC_F(c, 0xf0);
            memcpy(SPEC_I(c, 0x1c0), p + 200, 4);
        }
    }
    float peak = K(0x5b23f8);
    for (int i = 0; i < 0x15; i++)
        if (peak <= *SPEC_F(c, 0x9c + 4 * i)) peak = *SPEC_F(c, 0x9c + 4 * i);
    *SPEC_F(c, 0x90) = (peak / peak0 + K(0x5b2600)) * K(0x5b2468) * *SPEC_F(c, 0x90);
    float r = *SPEC_F(c, 0xf4) / *CAR_F(c, CAR_10D8);
    *CAR_F(c, CAR_10D8) = r;
    *SPEC_F(c, 0x140) = *SPEC_F(c, 0x140) / ((r + K(0x5b2580)) * K(0x5b2680));
    /* The front and rear downforce settings (FRONTDOWNFORCE +0x114, REARDOWNFORCE +0x118 of the
       description). */
    float f4 = ((float)DESC_I(c, 0x114) * K(0x5b253c) * K(0x5b3714) + K(0x5b24a8)) * K(0x5b23f0);
    float f5 = ((float)DESC_I(c, 0x118) * K(0x5b253c) * K(0x5b3714) + K(0x5b24a8)) * K(0x5b23f0);
    *SPEC_F(c, 0x90) = *SPEC_F(c, 0x90) / ((f5 + f4 + K(0x5b2600)) * K(0x5b2694));
    *SPEC_F(c, 0x10c) = f4 * *SPEC_F(c, 0x10c);
    *SPEC_F(c, 0x110) = f5 * *SPEC_F(c, 0x110);
    /* The final drive and the gear ratios (FINALRATIO +0xf0, FIRSTRATIO.. from +0xf4: the reverse slot,
       +0x64 of the specs, is skipped). */
    *SPEC_F(c, 0x80) = (float)DESC_I(c, 0xf0) * K(0x5b253c) + *SPEC_F(c, 0x80);
    for (int o = 0x60; o < 0x80; o += 4)
        if (o != 100) *SPEC_F(c, o) = (float)DESC_I(c, o + 0x94) * K(0x5b253c) + *SPEC_F(c, o);
    *SPEC_F(c, 0x108) = ((float)(DESC_I(c, 0x11c) / 100) * K(0x5b253c) - K(0x5b23f0)) * K(0x5b23f0) + *SPEC_F(c, 0x108);
    *SPEC_F(c, 0x100) = (float)DESC_I(c, 0x128) * K(0x5b253c) + *SPEC_F(c, 0x100);
    *SPEC_F(c, 0x11c) = ((float)DESC_I(c, 0x134) * K(0x5b253c) + K(0x5b2580)) * *SPEC_F(c, 0x11c) * K(0x5b2680);
    *SPEC_F(c, 0xf4) = (*SPEC_F(c, 0x11c) + K(0x5b368c)) * K(0x5b49b4) * *SPEC_F(c, 0xf4);
    *SPEC_F(c, 0x114) = (K(0x5b24a8) - (float)DESC_I(c, 0x134) * K(0x5b253c)) * K(0x5b2694) + *SPEC_F(c, 0x114);
    *SPEC_F(c, 0x118) = (K(0x5b24a8) - (float)DESC_I(c, 0x134) * K(0x5b253c)) * K(0x5b2694) + *SPEC_F(c, 0x118);
    *SPEC_F(c, 0x12c) = *SPEC_F(c, 0x12c) / ((*SPEC_F(c, 0x11c) + K(0x5b24a8)) * K(0x5b23f0));
    float travel = (float)DESC_I(c, 0x124) * K(0x5b253c) * K(0x5b3060);
    *SPEC_F(c, 0x124) = travel + *SPEC_F(c, 0x124);
    *SPEC_F(c, 0xf4) = (travel + K(0x5b24a8)) * *SPEC_F(c, 0xf4);
    *SPEC_F(c, 0x12c) = *SPEC_F(c, 0x12c) / (travel + K(0x5b24a8));
    *SPEC_F(c, 0x120) = (float)DESC_I(c, 0x120) * K(0x5b253c) * K(0x5b3060);
    *SPEC_F(c, 0x12c) = *SPEC_F(c, 0x12c) / (*SPEC_F(c, 0x120) * K(0x5b2460) + K(0x5b24a8));
    /* The tyre pressures (+0x13c front, +0x140 rear). */
    float tf = (float)DESC_I(c, 0x13c) * K(0x5b253c) * K(0x5b3f30);
    float tr = (float)DESC_I(c, 0x140) * K(0x5b253c) * K(0x5b3f30);
    if (tf < K(0x5b24a8)) tf *= K(0x5b3354);
    if (tr < K(0x5b24a8)) tr *= K(0x5b3354);
    *SPEC_F(c, 0xf4) = (tr + tf + K(0x5b2608)) * *SPEC_F(c, 0xf4) * K(0x5b49b4);
    *SPEC_F(c, 0xf0) = ((tf + tf) / (tr + tf) + K(0x5b24a8)) * K(0x5b23f0) * *SPEC_F(c, 0xf0);
    for (int o = 0x60; o < 0x80; o += 4) *SPEC_F(c, o) = (*SPEC_F(c, 0x80) * *SPEC_F(c, o) * K(0x5b26f0)) / *SPEC_F(c, 0x148);
    /* Per gear (from +0x60, the count at +0x50): +0x170 + 4g = 1 / ratio (0.1 for a zero ratio), +0x190 + 4g
       the ratio over the mass times 0x5b49b4 times +0x58, more for the SILSPD driver (or less without the
       transmission +0x0c), +0x14c + 4g = 1 / (+0x16c + 4g times +0x94). */
    const char *driver = (const char *)desc + 0x17c;
    for (int g = 0; g < *SPEC_I(c, 0x50); g++) {
        int o = 400 + 4 * g;
        float ratio = *SPEC_F(c, o - 0x130);
        *SPEC_F(c, o - 0x20) = ratio == K(0x5b23f8) ? 0.1f : K(0x5b24a8) / ratio;   /* 0x3dcccccd */
        *SPEC_F(c, o) = ratio / *SPEC_F(c, 0x40);
        *SPEC_F(c, o) = *SPEC_F(c, o) * K(0x5b49b4);
        *SPEC_F(c, o) = *SPEC_F(c, 0x58) * *SPEC_F(c, o);
        if (!strcmp(driver, exe_str(0x5d0cf0))) *SPEC_F(c, o) = *SPEC_F(c, o) * K(0x5b2480);   /* "SILSPD" */
        else if (DESC_I(c, 0xc) == 0) *SPEC_F(c, o) = *SPEC_F(c, o) * K(0x5b3f90);
        *SPEC_F(c, o + 4 - 0x44) = K(0x5b24a8) / (*SPEC_F(c, o + 4 - 0x24) * *SPEC_F(c, 0x94));
    }
    if (!strcmp(driver, exe_str(0x5d0cf0))) {
        *SPEC_F(c, 0xf4) = *SPEC_F(c, 0xf4) * K(0x5b2480);
        *SPEC_F(c, 0x12c) = *SPEC_F(c, 0x12c) * K(0x5b3f00);
        *SPEC_F(c, 0x118) = *SPEC_F(c, 0x118) * K(0x5b3354);
    } else if (!strcmp(driver, exe_str(0x5d0ce8))) {   /* "ROLLEY" */
        *SPEC_F(c, 0xf4) = *SPEC_F(c, 0xf4) * K(0x5b246c);
        *SPEC_F(c, 0x12c) = *SPEC_F(c, 0x12c) * K(0x5b4a34);
    }
    /* The torque at the top gear's top speed (0x49d66f..): the speed (top gear times +0x90, truncated, at
       most +0x94, at least 0) between the curve's 500-rpm samples, times +0x18c of the top gear, over +0x90
       cubed. */
    int n = *SPEC_I(c, 0x50);
    float rpm = (float)ftol(*SPEC_F(c, 0x5c + 4 * n) * *SPEC_F(c, 0x90));
    float s94 = *SPEC_F(c, 0x94);
    if (s94 <= rpm) rpm = s94;
    if (!(K(0x5b23f8) <= rpm)) rpm = K(0x5b23f8);
    int i = ftol(rpm * K(0x5b4a30));
    float a = *SPEC_F(c, 0x9c + 4 * i), b = *SPEC_F(c, 0x90);
    float tq = ((*SPEC_F(c, 0xa0 + 4 * i) - a) * ((rpm - (float)(i * 500)) * K(0x5b4a30)) + a) * *SPEC_F(c, 0x18c + 4 * n);
    *SPEC_F(c, 0x1b0) = tq / (b * b * *SPEC_F(c, 0x90));
    *SPEC_F(c, 0x1b4) = *SPEC_F(c, 0x44) * K(0x5b23f0);
    *SPEC_F(c, 0x1b4) = *SPEC_F(c, 0x1b4) * K(0x5b4a2c);
    *SPEC_F(c, 0x1b4) = *SPEC_F(c, 0x1b4) * K(0x5b26b0);
    *SPEC_F(c, 0x1b8) = K(0x5b24a8) / *SPEC_F(c, 0x1b4);
    *SPEC_F(c, 0x1bc) = K(0x5b24a8) / *SPEC_F(c, 0xf4);
}

/* ---- the cars ---- */

/* The car's place in the race's lists by its description's CLASS (+0x08). 0x40f1c0 */
static void Car_AddToLists(Car *c)
{
    uint32_t cls = (uint32_t)DESC_I(c, 8);
    if (!(cls & 0x80)) {
        g_cars[g_ncars] = c;
        list_add(&g_ranked, c);
        list_add(&g_ordered, c);
        g_ncars++;
        const char *name = exe_str(0x5d61e8 + 7u * (uint32_t)DESC_I(c, 0));   /* the TYPE's name */
        snprintf((char *)c->b + CAR_NAME, 9, "%s", name);
    }
    *CAR_I(c, CAR_INDEX) = g_created++;
    *CAR_I(c, CAR_FLAGS) = 0;
    *CAR_I(c, CAR_PLAYER) = -1;
    c->b[CAR_NAME2] = 0;
    int32_t *f = CAR_I(c, CAR_FLAGS);
    if (cls & 0x80) {
        g_traffic[g_ntraffic++] = c;
        *f |= 0x80;
    }
    if (cls & 1) {
        *CAR_I(c, CAR_PLAYER) = g_humans.n;
        list_add(&g_humans, c);
        *f |= 4;
        list_add(&g_racers, c);
        *f |= 1;
        list_add(&g_viewed, c);
        uint32_t was = (uint32_t)*f;
        *f = (int32_t)(was | 0x100);
        if (cls & 0x40) *f = (int32_t)(was | 0x300);
    }
    if (cls & 2) {
        list_add(&g_opponents, c);
        *f |= 2;
        list_add(&g_rivals, c);
        *f |= 8;
        list_add(&g_racers, c);
        *f |= 1;
        if (g_race_cops) {
            list_add(&g_viewed, c);
            *f |= 0x100;
        }
    }
    if (cls & 4) {
        list_add(&g_opponents, c);
        *f |= 2;
        list_add(&g_roadcars, c);
        *f |= 0x10;
    }
    if (cls & 0x18) {
        list_add(&g_opponents, c);
        *f |= 2;
        list_add(&g_chasers, c);
        uint32_t was = (uint32_t)*f;
        *f = (int32_t)(was | 0x20);
        if (cls & 0x10) *f = (int32_t)(was | 0x60);
    }
}

/* The car's specs, AI specs, task functions, renderer and wheels. 0x410dc0 (CarSpecs_Load) */
/* The car's renderer (R3DCar_ctor 0x437850 through 0x443ce0, then its load through the vtable): the
   model <FILENAME>.crp of GameData\CarModel. The renderer reads the car through its state, which the port
   keeps beside the car (the original's state is the car object itself). */
typedef struct {
    R3DCar car;
    R3DCarState state;
    R3DCarSpec spec;
} CarRenderer;

static void Car_MakeRenderer(Car *c)
{
    static bool common;
    if (!common) {
        common = true;
        R3DCar_LoadCommon();   /* carcmn.fsh, the shared pages and the colour packages (0x443690), once */
    }
    CarRenderer *r = calloc(1, sizeof *r);
    if (!r) return;
    r->state.rot[0] = r->state.rot[4] = r->state.rot[8] = 1;
    /* The renderer's view of the description (the original's spec is the description itself). */
    const uint8_t *d = CAR_DESCB(c);
    memcpy(r->spec.licence, d + 0x1c0, sizeof r->spec.licence);
    memcpy(r->spec.colours, d + 0x218, sizeof r->spec.colours);
    memcpy(r->spec.options, d + 0x32c, sizeof r->spec.options);
    r->spec.slot1end = DESC_I(c, 0x50c);
    r->spec.racenumber = DESC_I(c, 0x510);
    r->spec.scheme = (uint32_t)DESC_I(c, 0x514);
    r->spec.stretch = DESC_I(c, 0x518);
    r->spec.customtpage = DESC_I(c, 0x51c);
    r->state.spec = &r->spec;
    r->state.style = DESC_I(c, 0x328);   /* +0x568: BODYSTYLE */
    r->state.index = *CAR_I(c, CAR_INDEX);
    r->state.damage = CAR_F(c, 0x4e4);   /* (the state is the car itself in the original) */
    R3DCar_Init(&r->car, &r->state);
    R3DCar_SetColours(&r->car, r->spec.scheme);   /* 0x443ce0 */
    char path[128];
    snprintf(path, sizeof path, "GameData\\CarModel\\%s", (const char *)CAR_DESCB(c) + 0x228);
    R3DCar_LoadCrp(&r->car, path);
    c->p[CARP_R3D] = &r->car;
    r->car.particles = CarParticles_New(c);   /* (R3DCar_ctor 0x43794e: 0x43ed40) */
    /* The sizes the load writes into the state are the car's own fields in the original (+0x3a8 the half
       box, +0x3b4 the radius): the simulation's collisions read them there. */
    memcpy(CAR_F(c, 0x3a8), r->state.halfsize, 12);
    *CAR_F(c, 0x3b4) = r->state.radius;
    /* the wheels' trails (+0x7b8 + 0xc4 w, 0x478e30), by the tyres' width (the specs' +0x84) */
    for (int w = 0; w < 4; w++) c->p[CARP_WHEELS + w] = SkidTrail_new(CAR_SPECSB(c) ? *(const float *)(const void *)(CAR_SPECSB(c) + 0x84) : 0);
}

static void Car_LoadSpecs(Car *c, int i)
{
    if (i < g_nracecars) {
        c->p[CARP_SPECS] = MEM_alloc(exe_str(0x5cc598), CARSPECS_SIZE, 0);    /* "carSpecs" */
        c->p[CARP_AISPECS] = MEM_alloc(exe_str(0x5cc590), AISPECS_SIZE, 0);   /* "AISpecs" */
        Sim_LoadCarSim(c);
        Sim_LoadCarAis(c);
        AI_Setup(c);   /* 0x406f50 */
        AI_Reset(c);   /* 0x406db0 */
    } else {
        c->p[CARP_SPECS] = NULL;
        c->p[CARP_AISPECS] = NULL;
    }
    Car_SetTasks(c);
    if ((*CAR_I(c, CAR_FLAGS) & 0x80) && DESC_I(c, 4) == 1) {
        /* a knockable object: no renderer (its bundle draws it), no wheels' trails */
        for (int w = 0; w < 4; w++) c->p[CARP_WHEELS + w] = NULL;
        return;
    }
    Car_MakeRenderer(c);
}

void Car_Construct(void)
{
    Parts_Load();
    g_ncars = g_ntraffic = 0;   /* 0x5e8b58, 0x5e8b5c and the lists' counts */
    g_created = 0;
    CarList *lists[] = {&g_racers, &g_humans, &g_opponents, &g_viewed, &g_rivals, &g_roadcars, &g_chasers, &g_ranked, &g_ordered};
    for (size_t k = 0; k < sizeof lists / sizeof *lists; k++) lists[k]->n = 0;
    for (int i = 0; i < g_nracecars + g_ntrafficcars && i < CAR_MAX + TRAFFIC_MAX; i++) {
        Car *c = MEM_alloc(exe_str(0x5cc5b4), (int)sizeof(Car), 0);   /* "Car_tObj" (0x1254) */
        memset(c, 0, sizeof *c);
        WSurface_Init(&c->body);
        for (int k = 0; k < 4; k++) WSurface_Init(&c->surf[k]);
        for (int k = 0; k < 0x14; k++) memset(c->b + 0xbf8 + 4 * k, 0xff, 4);
        if (i < g_nracecars) {
            c->p[CARP_DESC] = g_cardesc[i];
            for (int k = 0; k < CAR_NPARTS; k++)
                c->p[CARP_PARTS + k] = MEM_alloc(exe_str(0x5cc5a4), CARPART_SIZE, 0);   /* "Parts_tCarParts" */
            Parts_Find(c);
        } else {
            c->p[CARP_DESC] = g_trafficdesc[i - g_nracecars];   /* 0x628fc0 */
        }
        Car_AddToLists(c);
    }
    for (int i = 0; i < g_ncars; i++) {
        Car *c = g_cars[i];
        Car_LoadSpecs(c, i);
        Car_RegisterTasks(c);
    }
    for (int i = 0; i < g_ntraffic; i++) {
        Car *c = g_traffic[i];
        Car_LoadSpecs(c, g_nracecars + i);
        Car_RegisterTasks(c);
    }
    free(g_parts);   /* 0x49c3d0 */
    g_parts = NULL;
}
