/* The cars' ground contact (game/car_ground.c): a car dropped just above a track's road through the
   simulation's task lists comes to rest on it, on its wheels, with its suspension in range. Skips
   (exit 0) without data. */
#include "eagl_fake_thrash.h"
#include "exe.h"
#include "game/car.h"
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

/* The car at a road node, level and facing +z, still (the grid placement isn't ported yet). */
static void place(Car *c, const float at[3], float lift)
{
    float *pos = CAR_F(c, 0x330), *m = CAR_F(c, 0x364);
    pos[0] = at[0];
    pos[1] = at[1] + lift;
    pos[2] = at[2];
    memset(CAR_F(c, 0x33c), 0, 12);
    memset(CAR_F(c, 0x388), 0, 12);
    static const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    memcpy(m, id, sizeof id);
    c->b[0x7f] = 1;
    c->b[0x7c] = 1;
    *CAR_F(c, 0x84) = 1.0f;   /* the gravity scale (set by the race code, not ported yet) */
    CarSim_LoadBodyPoints(c); /* (0x49b480's) */
    CarSim_BodyPoints(c);
}

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
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("car_ground_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    fake_install_graphics();
    static const char *tracks[] = {"alps", "autobahn", "monaco1"};
    for (size_t k = 0; k < sizeof tracks / sizeof *tracks; k++) {
        snprintf(g_track_name, sizeof g_track_name, "%s", tracks[k]);
        g_road = NULL;
        Track_Load();
        memset(&g_tasks_tick, 0, sizeof g_tasks_tick);
        memset(&g_tasks_even, 0, sizeof g_tasks_even);
        CHECK(RaceSetup_SingleCar("964"));
        Car_Construct();
        CHECK(g_ncars == 1 && g_road && g_road->nnodes > 20);
        if (g_ncars != 1 || !g_road) continue;
        Car *c = g_cars[0];
        g_sim_tick = 0;
        const float *node = Road_NodePos(g_road, 10);
        float ground = WSurface_HeightAt(node);
        place(c, node, 0.6f);
        run(256);   /* four seconds */
        const float *pos = CAR_F(c, 0x330), *vel = CAR_F(c, 0x33c);
        float above = pos[1] - ground, speed = sqrtf(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
        CHECK(isfinite(pos[0]) && isfinite(pos[1]) && isfinite(pos[2]));
        CHECK(above > -0.5f && above < 1.5f);
        CHECK(speed < 1.0f);
        CHECK(*CAR_F(c, 0x374) > 0.95f);   /* upright */
        int down = 0;
        for (int w = 0; w < 4; w++) down += *CAR_I(c, 0x83c + 0xc4 * w) == 0;
        CHECK(down >= 3);
        printf("  %-9s ground %.3f: car at %.3f above it, speed %.3f, up.y %.3f, %d wheels down\n", tracks[k], ground, above,
               speed, *CAR_F(c, 0x374), down);
        BWorld_DeInit();
    }
    printf("car_ground_test: %d checks: %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
