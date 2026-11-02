/* The track's AI tuning run from the exe (game/track_table.c): every track the code knows, both ways,
   every car class it tests, runs to its return; the zones are node ranges. Skips (exit 0) without data. */
#include "exe.h"
#include "game/race_car.h"
#include "game/track.h"
#include "game/track_table.h"
#include "vfs.h"
#include "vfs_host.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("track_table_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    static const char *tracks[] = {"monaco1", "monaco2", "monaco3", "monaco4", "monaco5", "alps", "autobahn", "canyon",
                                   "castle", "coastal", "farmland", "foothills", "forest", "industrial", "skidpad"};
    static const char *cars[] = {"356", "356A", "356B", "550A", "914", "928", "930T", "935", "944", "959", "964",
                                 "965T", "993", "993T", "996", "996T", "BOXSTER", "GT1", "GT2", "GT3", "other"};
    int zoned = 0;
    for (size_t t = 0; t < sizeof tracks / sizeof *tracks; t++)
        for (int rev = 0; rev < 2; rev++) {
            snprintf(g_track_name, sizeof g_track_name, "%s", tracks[t]);
            g_race_reverse = rev;
            for (size_t k = 0; k < sizeof cars / sizeof *cars; k++) {
                float f = Track_Table(cars[k]);
                CHECK(Track_TableOk() && isfinite(f) && 0.2f < f && f < 5.0f);
                float g = Track_CarFactor(cars[k]);
                CHECK(Track_TableOk() && isfinite(g) && 0.2f < g && g < 5.0f);
            }
            CHECK(g_track_road_width > 1.0f && g_track_road_width < 100.0f);
            int nz = 0;
            for (int z = 0; z < TRACK_NZONES; z++) {
                /* (foothills backwards has a zone 620 .. 600 that never holds: the original's) */
                CHECK(0 <= g_track_zone[z][0] && g_track_zone[z][1] < 0x2000);
                CHECK(isfinite(g_track_zone_f[z]) && g_track_zone_f[z] > 0);
                if (g_track_zone[z][1]) nz++;
            }
            if (nz) zoned++;
            printf("  %-10s %d: width %.1f, %d zones, 964 %.3f / %.3f, GT1 %.3f\n", tracks[t], rev, g_track_road_width, nz,
                   Track_Table("964"), Track_CarFactor("964"), Track_Table("GT1"));
        }
    CHECK(zoned > 4);
    printf("track_table_test: %d checks: %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
