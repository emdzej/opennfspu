/* The race's cars (game/car.c): one human car per model of the car database, its specs (.sim plus the
   engine model), the task lists. Skips (exit 0) without data. */
#include "eagl_fake_thrash.h"
#include "exe.h"
#include "game/car.h"
#include "game/race_setup.h"
#include "vfs.h"
#include "vfs_host.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("car_specs_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    fake_install_graphics();   /* the cars' renderers load their models */
    static const char *models[] = {"964", "356_1", "959", "boxster", "914", "993"};
    for (size_t m = 0; m < sizeof models / sizeof *models; m++) {
        memset(&g_tasks_tick, 0, sizeof g_tasks_tick);
        memset(&g_tasks_even, 0, sizeof g_tasks_even);
        CHECK(RaceSetup_SingleCar(models[m]));
        Car_Construct();
        CHECK(g_ncars == 1 && g_cars[0]);
        if (!g_ncars) continue;
        Car *c = g_cars[0];
        CHECK((*CAR_I(c, CAR_FLAGS) & 5) == 5);
        float mass = *SPEC_F(c, 0x40), gears = (float)*SPEC_I(c, 0x50);
        CHECK(mass > 500 && mass < 2500);
        CHECK(gears >= 4 && gears <= 9);
        for (int o = 0x40; o < CARSPECS_SIZE; o += 4) CHECK(isfinite(*SPEC_F(c, o)) || (*SPEC_I(c, o) == *SPEC_I(c, o)));
        CHECK(g_tasks_even.count == 6 && g_tasks_tick.count == 3);
        for (int i = 1; i < g_tasks_even.count; i++) CHECK(g_tasks_even.e[i - 1].prio <= g_tasks_even.e[i].prio);
        printf("  %-8s %-14s mass %.0f gears %d final %.2f top %.3f\n", models[m], (const char *)CAR_DESCB(c) + 0x268, mass,
               *SPEC_I(c, 0x50), *SPEC_F(c, 0x80), *SPEC_F(c, 0x1b0));
    }
    printf("car_specs_test: %d checks: %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
