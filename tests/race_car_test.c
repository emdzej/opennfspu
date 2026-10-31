/* The race car's set-up (game/race_car.c): on each track the car is put on its grid slot behind the start
   line (0x410790 -> 0x4105e0 -> 0x49b000), on the road, upright and facing along it, and comes to rest
   there. Skips (exit 0) without data. */
#include "eagl_fake_thrash.h"
#include "exe.h"
#include "game/car.h"
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
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("race_car_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    fake_install_graphics();
    static const char *tracks[] = {"alps", "autobahn", "monaco1", "coastal", "castle", "canyon", "industrial", "skidpad"};
    for (size_t k = 0; k < sizeof tracks / sizeof *tracks; k++) {
        snprintf(g_track_name, sizeof g_track_name, "%s", tracks[k]);
        g_road = NULL;
        Track_Load();
        memset(&g_tasks_tick, 0, sizeof g_tasks_tick);
        memset(&g_tasks_even, 0, sizeof g_tasks_even);
        CHECK(RaceSetup_SingleCar("964"));
        Car_Construct();
        if (g_ncars != 1 || !g_road) { CHECK(0); continue; }
        Car *c = g_cars[0];
        g_sim_tick = 0;
        Car_RaceInit(c, 0);
        const float *pos = CAR_F(c, 0x330), *m = CAR_F(c, 0x364);
        int node = *(int16_t *)(void *)(c->b + 8);
        const float *np = Road_NodePos(g_road, node), *nf = (const float *)(const void *)(g_road->nodes + node * ROAD_NODE_SIZE + 0x1c);
        float ground = WSurface_HeightAt(pos), along = m[6] * nf[0] + m[7] * nf[1] + m[8] * nf[2];
        float dx = pos[0] - np[0], dz = pos[2] - np[2];
        printf("  %-9s node %d at %.2f %.2f %.2f (%.2f from the node), %.3f above the ground, up.y %.3f, along %.3f\n", tracks[k], node,
               pos[0], pos[1], pos[2], sqrtf(dx * dx + dz * dz), pos[1] - ground, m[4], along);
        CHECK(isfinite(pos[0]) && isfinite(pos[1]) && isfinite(pos[2]));
        CHECK(m[4] > 0.95f);
        CHECK(along > 0.95f);
        CHECK(sqrtf(dx * dx + dz * dz) < 6.0f);
        CHECK(pos[1] - ground > -0.5f && pos[1] - ground < 1.5f);
        for (int t = 0; t < 256; t++) {
            g_sim_tick++;
            TaskList_Run(&g_tasks_tick);
            if (!(g_sim_tick & 1)) TaskList_Run(&g_tasks_even);
        }
        const float *vel = CAR_F(c, 0x33c);
        float speed = sqrtf(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
        printf("            after 4 s: %.3f above the ground, speed %.3f, up.y %.3f\n", pos[1] - WSurface_HeightAt(pos), speed, m[4]);
        CHECK(speed < 1.0f && m[4] > 0.95f);
        BWorld_DeInit();
    }
    printf("race_car_test: %d checks: %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
