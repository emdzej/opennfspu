/* The cars' damage (game/car_damage.c): an impact of 50 pushing the car straight back dents the front
   zone by half of it and its neighbours to the mean of their sides; the roof takes a hit from above; a
   crash over 80 throws the car up. Skips (exit 0) without data. */
#include "eagl_fake_thrash.h"
#include "exe.h"
#include "game/car.h"
#include "game/car_damage.h"
#include "game/race_car.h"
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

static float z(Car *c, int i) { return *CAR_F(c, 0x4e4 + 4 * i); }

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("car_damage_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    fake_install_graphics();
    snprintf(g_track_name, sizeof g_track_name, "autobahn");
    Track_Load();
    CHECK(RaceSetup_SingleCar("964"));
    Car_Construct();
    if (g_ncars != 1) return 1;
    Car *c = g_cars[0];
    g_sim_tick = 0;
    Car_RaceInit(c, 0);
    CHECK(g_race_damage == 1);
    for (int i = 0; i < 9; i++) CHECK(z(c, i) == 0);
    const float *f = CAR_F(c, 0x37c), *up = CAR_F(c, 0x370);
    float back[3] = {-f[0], -f[1], -f[2]};
    *CAR_F(c, 0x430) = 50.0f;
    memset(CAR_F(c, 0x388), 0, 12);
    CarSim_ImpactDamage(c, back, 1);
    printf("  front hit of 50: zones");
    for (int i = 0; i < 9; i++) printf(" %.2f", z(c, i));
    printf("; spin %.3f %.3f %.3f\n", CAR_F(c, 0x388)[0], CAR_F(c, 0x388)[1], CAR_F(c, 0x388)[2]);
    CHECK(fabsf(z(c, 1) - 25.0f) < 1e-4f);
    CHECK(fabsf(z(c, 0) - 12.5f) < 1e-4f && fabsf(z(c, 2) - 12.5f) < 1e-4f);
    CHECK(z(c, 5) == 0 && z(c, 8) == 0);
    CHECK(fabsf(CAR_F(c, 0x388)[0]) + fabsf(CAR_F(c, 0x388)[1]) + fabsf(CAR_F(c, 0x388)[2]) > 0.01f);
    /* A weaker hit keeps the deeper dent; one at 10 or less does nothing. */
    *CAR_F(c, 0x430) = 20.0f;
    CarSim_ImpactDamage(c, back, 0);
    CHECK(fabsf(z(c, 1) - 25.0f) < 1e-4f);
    /* From above: the roof. */
    float down[3] = {-up[0], -up[1], -up[2]};
    *CAR_F(c, 0x430) = 40.0f;
    CarSim_ImpactDamage(c, down, 0);
    CHECK(fabsf(z(c, 8) - 20.0f) < 1e-4f);
    /* A crash: 200 at the rear throws the car up. */
    float vy = CAR_F(c, 0x33c)[1];
    *CAR_F(c, 0x430) = 200.0f;
    CarSim_ImpactDamage(c, f, 1);
    printf("  rear crash of 200: rear zone %.1f, up speed %.2f -> %.2f, out of control %d\n", z(c, 5), vy, CAR_F(c, 0x33c)[1],
           *CAR_I(c, 0x42c));
    CHECK(z(c, 5) == 100.0f);
    CHECK(CAR_F(c, 0x33c)[1] > vy && *CAR_I(c, 0x42c) == 1);
    printf("car_damage_test: %d checks: %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
