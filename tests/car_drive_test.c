/* The cars' drive (game/car_drive.c): a 964 on the autobahn's start straight, held through the start
   countdown, then on full throttle for five seconds: it pulls away in gear, stays upright and on the
   road's height. Skips (exit 0) without data. */
#include "eagl_fake_thrash.h"
#include "exe.h"
#include "game/car.h"
#include "game/car_drive.h"
#include "game/car_physics.h"
#include "game/car_respawn.h"
#include "game/input.h"
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

static uint8_t pkt[4];   /* the player's packet: flags, steering, throttle, brake (game/input.h) */

static void run(int ticks)
{
    for (int t = 0; t < ticks; t++) {
        /* As the race loop and Sim_Step: the packet queued and taken on even ticks, the even list, the
           every-tick list, then the tick counted. */
        if (!(g_sim_tick & 1)) {
            Input_Enqueue(0, pkt);
            Sim_HalfStep(g_sim_tick >> 1);
            TaskList_Run(&g_tasks_even);
        }
        TaskList_Run(&g_tasks_tick);
        g_sim_tick++;
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
    /* On its grid slot behind the start line (the race's set-up 0x410790). */
    Car_RaceInit(c, 0);
    float *pos = CAR_F(c, 0x330);
    g_sim_tick = 0;
    Input_Reset(false);
    run(0x1c8);                         /* the countdown */
    float speed0 = *CAR_F(c, 0xd60);
    pkt[2] = 0x7f;                      /* full throttle (0xfe) */
    for (int s = 1; s <= 5; s++) {
        run(64);
        printf("  t+%ds: forward %.2f m/s, gear %d, rpm %.0f, up.y %.3f, height over ground %.2f\n", s, *CAR_F(c, 0xd60),
               (int8_t)c->b[0xd82] - 1, *CAR_F(c, 0xdb4), *CAR_F(c, 0x374), pos[1] - WSurface_HeightAt(pos));
    }
    float v = *CAR_F(c, 0xd60);
    /* Steering: positive turns towards the car's first row (right = up x forward), as the AI's steering
       (0x4067c0: the target's direction dotted with that row, times 256) takes it. */
    float right[3], from[3];
    memcpy(right, CAR_F(c, 0x364), sizeof right);
    memcpy(from, pos, sizeof from);
    pkt[1] = 0x3f;                      /* steering 0x7e */
    run(64);
    float side = (pos[0] - from[0]) * right[0] + (pos[2] - from[2]) * right[2];
    printf("  steering 0x7f for 1 s: %.2f m towards the first row\n", side);
    CHECK(side > 1.0f);
    pkt[1] = 0;
    CHECK(fabsf(speed0) < 0.5f);
    CHECK(v > 10.0f && v < 80.0f);
    CHECK((int8_t)c->b[0xd82] >= 2);
    CHECK(*CAR_F(c, 0x374) > 0.95f);
    float h = pos[1] - WSurface_HeightAt(pos);
    CHECK(h > -0.5f && h < 2.0f);
    /* The reset key (+0xd89) after the start: the car back on its road node, still. */
    g_sim_tick = 0x400;
    *CAR_I(c, 0xb4c) = 0;
    *CAR_F(c, 0x35c) = 0;
    c->b[0xd89] = 1;
    CarSim_HitTask(c);
    float vel = fabsf(CAR_F(c, 0x33c)[0]) + fabsf(CAR_F(c, 0x33c)[1]) + fabsf(CAR_F(c, 0x33c)[2]);
    printf("  reset: key %d, velocity %.3f, timer %d\n", c->b[0xd89], vel, *CAR_I(c, 0xb4c));
    CHECK(c->b[0xd89] == 0 && vel == 0 && *CAR_I(c, 0xb4c) == 0x9f);
    printf("car_drive_test: %d checks: %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
