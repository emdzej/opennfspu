/* The track's road (game/road.c) for every track: nodes and segments from 'SimD'/'SimT', every node's
   own position found as that node. Skips (exit 0) without data. */
#include "eagl_fake_thrash.h"
#include "exe.h"
#include "game/road.h"
#include "game/track.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <string.h>

static int fails, checks, tracks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static void one(const char *name, void *user)
{
    char base[64];
    snprintf(base, sizeof base, "%s", name);
    char *dot = strrchr(base, '.');
    if (dot) *dot = 0;
    snprintf(g_track_name, sizeof g_track_name, "%s", base);
    g_road = NULL;
    Track_Load();
    CHECK(g_road && g_road->nnodes > 100);
    if (!g_road || !g_road->nnodes) return;
    const Road *r = g_road;
    CHECK(r->nseg >= 1 && r->seg[0].first == 0);
    int covered = 0;
    for (int i = 0; i < r->nseg; i++) covered += r->seg[i].last - r->seg[i].first + 1;
    CHECK(covered == r->nnodes);
    int found = 0;
    for (int n = 0; n < r->nnodes; n += 7) {
        int k = Road_NearestNode(r, Road_NodePos(r, n), n > 0 ? n - 1 : 0);
        found += k == n;
    }
    CHECK(found > 0);
    printf("  %-10s %5d nodes %2d segments, %d of %d nodes found from their neighbour\n", base, r->nnodes, r->nseg, found,
           (r->nnodes + 6) / 7);
    tracks++;
    BWorld_DeInit();
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("road_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    fake_install_graphics();
    Track_FindCrp(one, NULL);
    printf("road_test: %d tracks, %d checks: %s\n", tracks, checks, fails ? "FAILED" : "ok");
    return fails != 0 || tracks == 0;
}
