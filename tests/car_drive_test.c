/* The cars' drive (game/car_drive.c): a 964 on the autobahn's start straight, held through the start
   countdown, then on full throttle for five seconds: it pulls away in gear, stays upright and on the
   road's height. Skips (exit 0) without data. */
#include "eagl_fake_thrash.h"
#include "exe.h"
#include "game/car.h"
#include "game/car_drive.h"
#include "game/car_physics.h"
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

static void run(int ticks)
{
    for (int t = 0; t < ticks; t++) {
        g_sim_tick++;
        TaskList_Run(&g_tasks_tick);
        if (!(g_sim_tick & 1)) TaskList_Run(&g_tasks_even);
    }
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("car_drive_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    fake_install_graphics();
    snprintf(g_track_name, sizeof g_track_name, "autobahn");
    Track_Load();
    CHECK(RaceSetup_SingleCar("964"));
    Car_Construct();
    CHECK(g_ncars == 1 && g_road && g_road->nnodes > 20);
    if (g_ncars != 1 || !g_road) return 1;
    Car *c = g_cars[0];
    /* Placed at a node facing the next one (CarSim_PlaceOnGround stands in for the grid placement). */
    const float *a = Road_NodePos(g_road, 20), *n = Road_NodePos(g_road, 21);
    float f[3] = {n[0] - a[0], 0, n[2] - a[2]};
    CarSim_Init(c, 0, *SPEC_F(c, 0x40) * 0.0078125f, *CAR_F(c, 0x3a8), *CAR_F(c, 0x3ac), *CAR_F(c, 0x3b0));
    CarSim_PlaceOnGround(c, a, f);
    float *pos = CAR_F(c, 0x330);
    CarSim_ResetDrive(c);
    g_sim_tick = 0;
    run(0x1c8);                         /* the countdown */
    float speed0 = *CAR_F(c, 0xd60);
    c->b[0xd7c] = 0xff;                 /* full throttle */
    for (int s = 1; s <= 5; s++) {
        run(64);
        printf("  t+%ds: forward %.2f m/s, gear %d, rpm %.0f, up.y %.3f, height over ground %.2f\n", s, *CAR_F(c, 0xd60),
               (int8_t)c->b[0xd82] - 1, *CAR_F(c, 0xdb4), *CAR_F(c, 0x374), pos[1] - WSurface_HeightAt(pos));
    }
    float v = *CAR_F(c, 0xd60);
    CHECK(fabsf(speed0) < 0.5f);
    CHECK(v > 10.0f && v < 80.0f);
    CHECK((int8_t)c->b[0xd82] >= 2);
    CHECK(*CAR_F(c, 0x374) > 0.95f);
    float h = pos[1] - WSurface_HeightAt(pos);
    CHECK(h > -0.5f && h < 2.0f);
    printf("car_drive_test: %d checks: %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
