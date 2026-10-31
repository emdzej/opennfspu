/* The car against the world's objects (game/car_collide.c, 0x498d50): a car put overlapping a roadside
   cylinder on canyon is pushed out to its box's extent plus the cylinder's radius, one nosed into a
   box on castle is pushed back out, and the impacts are recorded (+0x438, +0x440). Skips (exit 0) without data. */
#include "eagl_fake_thrash.h"
#include "exe.h"
#include "game/car.h"
#include "game/car_collide.h"
#include "game/car_physics.h"
#include "game/race_car.h"
#include "game/race_setup.h"
#include "game/road.h"
#include "game/track.h"
#include "game/wtree.h"
#include "vfs.h"
#include "vfs_host.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("car_objects_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    fake_install_graphics();
    snprintf(g_track_name, sizeof g_track_name, "canyon");
    Track_Load();
    CHECK(RaceSetup_SingleCar("964"));
    Car_Construct();
    if (g_ncars != 1 || !g_world || !g_world->tree) return 1;
    Car *c = g_cars[0];
    g_sim_tick = 0;
    Car_RaceInit(c, 0);
    /* A solid cylinder (not drive-through) of the tree. */
    WTreeObject *cyl = NULL;
    WTreeElems *all = g_world->tree->all;
    for (uint32_t i = 0; i < all->n && !cyl; i++) {
        WTreeObject *o = (WTreeObject *)all->e[i];
        if (o->e.kind == WTREE_OBJECT && o->shape == WOBJ_CYLINDER && !o->drivethru) cyl = o;
    }
    CHECK(cyl != NULL);
    if (!cyl) return 1;
    float ctr[3];
    cyl->e.vt->centre(&cyl->e, ctr);
    /* The car beside it, its right side 0.5 into the cylinder's radius. */
    const float *right = CAR_F(c, 0x364);
    float hx = *CAR_F(c, 0x3a8), want = hx + cyl->radius, start = want - 0.5f;
    float *pos = CAR_F(c, 0x330);
    pos[0] = ctr[0] - right[0] * start;
    pos[2] = ctr[2] - right[2] * start;
    memset(CAR_F(c, 0x33c), 0, 12);
    *CAR_I(c, 0x43c) = 0;
    *CAR_I(c, 0x438) = 0;
    CarSim_Objects(c);
    float dx = pos[0] - ctr[0], dz = pos[2] - ctr[2], d = sqrtf(dx * dx + dz * dz);
    printf("  cylinder at %.1f %.1f: the car from %.3f to %.3f (its half width %.3f + 0.25), impact flags %#x\n", ctr[0], ctr[2],
           start, d, hx, (unsigned)*CAR_I(c, 0x438));
    CHECK(fabsf(d - want) < 0.01f);
    CHECK((*CAR_I(c, 0x438) & 0xffff0000) == 0x50000);
    /* Far from it nothing happens. */
    pos[0] = ctr[0] + 50;
    *CAR_I(c, 0x438) = 0;
    CarSim_Objects(c);
    CHECK(*CAR_I(c, 0x438) == 0 && pos[0] == ctr[0] + 50);
    /* A box on castle: the car nosed 5 cm into the middle of one of its sides, still, is pushed back out. */
    BWorld_DeInit();
    snprintf(g_track_name, sizeof g_track_name, "castle");
    g_road = NULL;
    Track_Load();
    memset(&g_tasks_tick, 0, sizeof g_tasks_tick);
    memset(&g_tasks_even, 0, sizeof g_tasks_even);
    CHECK(RaceSetup_SingleCar("964"));
    Car_Construct();
    if (g_ncars != 1 || !g_world || !g_world->tree) return 1;
    c = g_cars[0];
    Car_RaceInit(c, 0);
    pos = CAR_F(c, 0x330);
    all = g_world->tree->all;
    WTreeObject *box = NULL;
    for (uint32_t i = 0; i < all->n && !box; i++) {
        WTreeObject *o = (WTreeObject *)all->e[i];
        if (o->e.kind == WTREE_OBJECT && o->shape == WOBJ_BOX && !o->drivethru) {
            float sx = o->corner[1][0] - o->corner[0][0], sz = o->corner[1][2] - o->corner[0][2];
            float cx = (o->bmin[0] + o->bmax[0]) * 0.5f, cz = (o->bmin[2] + o->bmax[2]) * 0.5f;
            /* a side over 2 m, away from the prototypes at the origin and from other objects */
            if (sx * sx + sz * sz > 4.0f && cx * cx + cz * cz > 2500.0f) {
                float q[3] = {cx, 0, cz};
                WTreeElems *nb = WTree_ObjectsNear(q, 8.0f);
                int others = 0;
                for (uint32_t k = 0; nb && k < nb->n; k++) others += nb->e[k] != &o->e;
                if (nb) WTreeElems_free(nb);
                if (!others) box = o;
            }
        }
    }
    CHECK(box != NULL);
    if (box) {
        const float *a = box->corner[0], *b = box->corner[1];
        float sx = b[0] - a[0], sz = b[2] - a[2], sl = sqrtf(sx * sx + sz * sz);
        float n[3] = {-sz / sl, 0, sx / sl};   /* the side's normal, as the box's test makes it */
        float bc[3];
        box->e.vt->centre(&box->e, bc);
        float mid[3] = {(a[0] + b[0]) * 0.5f, 0, (a[2] + b[2]) * 0.5f};
        if ((mid[0] - bc[0]) * n[0] + (mid[2] - bc[2]) * n[2] < 0) n[0] = -n[0], n[2] = -n[2];   /* outwards */
        /* Facing the box: forward = -n, right = up x forward. */
        float m[9] = {-n[2], 0, n[0], 0, 1, 0, -n[0], 0, -n[2]};
        m[0] = 1 * m[8] - 0 * m[7], m[2] = 0 * m[7] - 1 * m[6];
        memcpy(CAR_F(c, 0x364), m, sizeof m);
        float hz = *CAR_F(c, 0x3b0), out = hz - 0.05f;
        pos[0] = mid[0] + n[0] * out;
        pos[2] = mid[2] + n[2] * out;
        pos[1] = (box->bmin[1] + box->bmax[1]) * 0.5f;
        memset(CAR_F(c, 0x33c), 0, 12);
        memset(CAR_F(c, 0x388), 0, 12);
        CarSim_BodyPoints(c);
        *CAR_I(c, 0x438) = 0;
        CarSim_Objects(c);
        float after = (pos[0] - mid[0]) * n[0] + (pos[2] - mid[2]) * n[2];
        printf("  box side at %.1f %.1f: the car's centre from %.3f to %.3f out (half length %.3f), flags %#x\n", mid[0], mid[2],
               out, after, hz, (unsigned)*CAR_I(c, 0x438));
        CHECK(after > out + 0.02f && after < hz + 0.05f);
        CHECK((*CAR_I(c, 0x438) & 0xffff0000) == 0x50000);
    }
    printf("car_objects_test: %d checks: %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
