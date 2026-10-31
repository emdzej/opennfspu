/* The race's progress (game/race_progress.c): a car taken node by node along monaco1's circuit counts
   its laps from the progress at its node, finishes after the race's laps (+0xb50 2) with its lap times,
   and one reversing over the line owes a lap first. Skips (exit 0) without data. */
#include "eagl_fake_thrash.h"
#include "exe.h"
#include "game/car.h"
#include "game/race_car.h"
#include "game/race_progress.h"
#include "game/race_setup.h"
#include "game/road.h"
#include "game/track.h"
#include "vfs.h"
#include "vfs_host.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static void at_node(Car *c, int n)
{
    int16_t v = (int16_t)n;
    memcpy(c->b + 8, &v, 2);
    c->body.node = v;
    g_sim_tick += 2;
    CarSim_RaceTask(c);
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("race_laps_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    fake_install_graphics();
    snprintf(g_track_name, sizeof g_track_name, "monaco1");
    Track_Load();
    CHECK(RaceSetup_SingleCar("964"));
    Car_Construct();
    if (g_ncars != 1 || !g_road) return 1;
    Car *c = g_cars[0];
    g_sim_tick = 0;
    Car_RaceInit(c, 0);
    CHECK(g_race_laps == 2 && g_racers.n == 1 && g_humans.n == 1);
    int n = *(int16_t *)(void *)(c->b + 8);
    float start = *CAR_F(c, 0xaf8);
    /* Forward round the circuit, two laps and a bit. */
    int steps = 0, lapsat[3] = {0};
    for (; steps < 2 * g_road->length + 40 && *CAR_I(c, 0xb50) == 0; steps++) {
        n = Road_Step(g_road, n, 1, -1);
        at_node(c, n);
        int l = *CAR_I(c, 0x530);
        if (l < 3 && !lapsat[l]) lapsat[l] = steps;
    }
    printf("  monaco1 (%d nodes): from %.3f, lap 1 after %d nodes, lap 2 after %d; finished %d after %d, distance %.3f, lap times %d %d\n",
           g_road->length, start, lapsat[1], lapsat[2], *CAR_I(c, 0xb50), steps, *CAR_F(c, 0xaf8), *CAR_I(c, 0xb28), *CAR_I(c, 0xb2c));
    CHECK(*CAR_I(c, 0x530) == 2 && *CAR_I(c, 0xb50) == 2);
    CHECK(lapsat[1] > 0 && lapsat[2] - lapsat[1] == g_road->length);
    CHECK(*CAR_I(c, 0xb2c) == 2 * g_road->length);   /* the second lap's time: two ticks a node */
    /* A second race: backwards over the line first. */
    Car_RaceInit(c, 0);
    n = *(int16_t *)(void *)(c->b + 8);
    for (int k = 0; k < 20; k++) {
        n = Road_Step(g_road, n, -1, -1);
        at_node(c, n);
    }
    printf("  backwards 20 nodes: laps %d, owed %d, distance %.3f\n", *CAR_I(c, 0x530), *CAR_I(c, 0x534), *CAR_F(c, 0xaf8));
    CHECK(*CAR_I(c, 0x534) == 1 && *CAR_I(c, 0x530) == 0);
    for (int k = 0; k < 40; k++) {
        n = Road_Step(g_road, n, 1, -1);
        at_node(c, n);
    }
    printf("  forward 40 nodes: laps %d, owed %d\n", *CAR_I(c, 0x530), *CAR_I(c, 0x534));
    CHECK(*CAR_I(c, 0x534) == 0 && *CAR_I(c, 0x530) == 0);
    printf("race_laps_test: %d checks: %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
