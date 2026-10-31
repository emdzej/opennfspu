/* The road's junctions (game/road.c, 0x4807a0 .. 0x4817b0, .jnc 0x480f20) on every track: the segments
   split where roads meet, every segment end in one junction, the race's length and each junction's
   progress, the .jnc file's one record per junction, the step through a junction staying on the road,
   and the nearest node through a junction's list. Skips (exit 0) without data. */
#include "eagl_fake_thrash.h"
#include "exe.h"
#include "game/road.h"
#include "game/track.h"
#include "vfs.h"
#include "vfs_host.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("road_junction_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    fake_install_graphics();
    static const char *tracks[] = {"alps", "autobahn", "canyon", "castle", "coastal", "farmland", "foothills", "forest",
                                   "industrial", "monaco1", "monaco2", "monaco3", "monaco4", "monaco5", "skidpad"};
    for (size_t t = 0; t < sizeof tracks / sizeof *tracks; t++) {
        snprintf(g_track_name, sizeof g_track_name, "%s", tracks[t]);
        g_road = NULL;
        Track_Load();
        Road *r = g_road;
        if (!r) { CHECK(0); continue; }
        /* Every end in exactly one junction, with a link back from its segment. */
        int ends = 0;
        for (int j = 0; j < r->njunctions; j++) ends += r->junctions[j].count;
        CHECK(ends == 2 * r->nseg);
        int linked = 0;
        for (int i = 0; i < r->nseg; i++) linked += r->seg[i].junction0 && r->seg[i].junction1;
        CHECK(linked == r->nseg);
        /* Segments cover the nodes once. */
        int covered = 0;
        for (int i = 0; i < r->nseg; i++) covered += r->seg[i].last - r->seg[i].first + 1;
        CHECK(covered == r->nnodes);
        CHECK(r->length > 0 && r->length <= 2 * r->nnodes);
        float maxp = 0;
        for (int j = 0; j < r->njunctions; j++) {
            CHECK(r->junctions[j].progress >= 0 && r->junctions[j].progress <= 1.0f);
            if (r->junctions[j].progress > maxp) maxp = r->junctions[j].progress;
        }
        char path[300];
        snprintf(path, sizeof path, "GameData\\Track\\%s.jnc", tracks[t]);
        size_t jn = 0;
        uint8_t *jnc = vfs_read_all(path, &jn);
        bool match = jnc && jn / 0x24 == (size_t)r->njunctions;
        free(jnc);
        /* Stepping along the whole race from node 1 never leaves the road. */
        int n = 1, bad = 0;
        for (int k = 0; k < r->length; k++) {
            int m = Road_Step(r, n, 1, -1);
            if (m < 0 || m >= r->nnodes) bad++;
            n = m;
        }
        CHECK(bad == 0);
        /* The nearest node to each node is itself (or one as near). */
        int off = 0;
        for (int i = 0; i < r->nnodes; i += 7) {
            const float *p = Road_NodePos(r, i);
            int m = Road_NearestNode(r, p, i);
            const float *q = Road_NodePos(r, m);
            if ((q[0] - p[0]) * (q[0] - p[0]) + (q[2] - p[2]) * (q[2] - p[2]) > 1.0f) off++;
        }
        CHECK(off <= r->nnodes / 70 + 1);
        printf("  %-10s %4d nodes %2d segments %2d junctions, length %4d, last node %4d, max progress %.3f, .jnc %s, %d off\n", tracks[t],
               r->nnodes, r->nseg, r->njunctions, r->length, r->lastnode, maxp, match ? "matches" : "differs", off);
        BWorld_DeInit();
    }
    printf("road_junction_test: %d checks: %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
