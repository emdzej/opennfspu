/* The cars against each other (game/car_cars.c, 0x498040): on autobahn, a car nosed into the back of a
   standing one at 20 m/s is pushed out of it, the standing car gains speed, the hitter loses some, and
   both record the hit (+0x438 0x50003, +0x434 the other). Two cars apart don't touch. Skips (exit 0)
   without data. */
#include "eagl_fake_thrash.h"
#include "exe.h"
#include "game/car.h"
#include "game/car_cars.h"
#include "game/race_car.h"
#include "game/race_setup.h"
#include "game/track.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("car_cars_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    fake_install_graphics();
    snprintf(g_track_name, sizeof g_track_name, "autobahn");
    Track_Load();
    CHECK(RaceSetup_SingleCar("964"));
    CHECK(RaceSetup_Opponents(1));
    Car_Construct();
    CHECK(g_ncars == 2);
    if (g_ncars != 2) return 1;
    Car *a = g_cars[0], *b = g_cars[1];
    g_sim_tick = 0;
    Car_RaceInit(a, 0);
    Car_RaceInit(b, 1);
    /* b in a's lane, its back 0.3 into a's nose; a at 20 m/s along its forward row, b standing. */
    const float *fw = CAR_F(a, 0x37c);
    float len = *CAR_F(a, 0x3b0) + *CAR_F(b, 0x3b0) - 0.3f;
    memcpy(CAR_F(b, 0x364), CAR_F(a, 0x364), 36);
    for (int k = 0; k < 3; k++) {
        CAR_F(b, 0x330)[k] = CAR_F(a, 0x330)[k] + fw[k] * len;
        CAR_F(a, 0x33c)[k] = fw[k] * 20.0f;
        CAR_F(b, 0x33c)[k] = 0;
    }
    memset(CAR_F(a, 0x388), 0, 12);
    memset(CAR_F(b, 0x388), 0, 12);
    *CAR_I(a, 0x434) = *CAR_I(b, 0x434) = 0;
    bool hit = CarSim_Cars(a, b);
    float d[3];
    for (int k = 0; k < 3; k++) d[k] = CAR_F(b, 0x330)[k] - CAR_F(a, 0x330)[k];
    float gap = dot3(d, fw), va = dot3(CAR_F(a, 0x33c), fw), vb = dot3(CAR_F(b, 0x33c), fw);
    printf("  nosed in 0.3: hit %d, centres %.3f apart along (boxes %.3f), speeds a %.2f b %.2f, kinds %#x %#x, others %d %d\n", hit, gap,
           *CAR_F(a, 0x3b0) + *CAR_F(b, 0x3b0), va, vb, (unsigned)*CAR_I(a, 0x438), (unsigned)*CAR_I(b, 0x438), *CAR_I(a, 0x434),
           *CAR_I(b, 0x434));
    CHECK(hit);
    CHECK(len < gap);
    CHECK(vb > 1.0f && va < 20.0f);
    CHECK(*CAR_I(a, 0x438) == 0x50003 && *CAR_I(b, 0x438) == 0x50003);
    CHECK(*CAR_I(a, 0x434) == *CAR_I(b, 0x520) + 1 && *CAR_I(b, 0x434) == *CAR_I(a, 0x520) + 1);
    /* Apart: no contact. */
    for (int k = 0; k < 3; k++) CAR_F(b, 0x330)[k] = CAR_F(a, 0x330)[k] + fw[k] * 20.0f;
    CHECK(!CarSim_Cars(a, b));
    printf("car_cars_test: %d checks: %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
