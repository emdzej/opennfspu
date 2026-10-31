/* The car against the world's objects (game/car_collide.c, 0x498d50): a car put overlapping a roadside
   cylinder on canyon is pushed out to its box's extent plus the cylinder's radius, and the impact is
   recorded (+0x438, +0x440). Skips (exit 0) without data. */
#include "eagl_fake_thrash.h"
#include "exe.h"
#include "game/car.h"
#include "game/car_collide.h"
#include "game/race_car.h"
#include "game/race_setup.h"
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
    printf("car_objects_test: %d checks: %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
