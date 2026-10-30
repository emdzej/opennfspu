/* The world's collision tree (game/wtree.c) for every track: the road edges and the flagged polygons are
   in it, every leaf's elements touch the leaf, and the ground under the road's nodes is found at about
   the nodes' height. Skips (exit 0) without data. */
#include "eagl_fake_thrash.h"
#include "exe.h"
#include "game/road.h"
#include "game/track.h"
#include "game/wtree.h"
#include "vfs.h"
#include "vfs_host.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails, checks, tracks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

typedef struct { int leaves, entries, bad, maxdepth; } Walk;

static void walk(const WTreeCell *c, int depth, Walk *w)
{
    if (depth > w->maxdepth) w->maxdepth = depth;
    if (c->list) {
        w->leaves++;
        float x0, z0, s;
        WTree_CellBounds(c, &x0, &z0, &s);
        for (uint32_t i = 0; i < c->list->n; i++) {
            const WTreeElem *e = c->list->e[i].elem;
            float mn[3], mx[3];
            e->vt->bounds(e, mn, mx);
            w->entries++;
            if (mn[0] > x0 + s || mn[2] > z0 + s || mx[0] < x0 || mx[2] < z0) w->bad++;
        }
    }
    for (int k = 0; k < 4; k++)
        if (c->child[k]) walk(c->child[k], depth + 1, w);
}

static void one(const char *name, void *user)
{
    (void)user;
    char base[64];
    snprintf(base, sizeof base, "%s", name);
    char *dot = strrchr(base, '.');
    if (dot) *dot = 0;
    snprintf(g_track_name, sizeof g_track_name, "%s", base);
    g_road = NULL;
    Track_Load();
    WTree *t = g_world ? g_world->tree : NULL;
    CHECK(t && t->all && t->all->n > 100);
    if (!t || !t->all || !g_road) return;
    int polys = 0, edges = 0;
    for (uint32_t i = 0; i < t->all->n; i++) {
        polys += t->all->e[i]->kind == WTREE_POLY;
        edges += t->all->e[i]->kind == WTREE_EDGE;
    }
    CHECK(polys > 0 && edges > 0);
    Walk w = {0};
    walk(t->root, 0, &w);
    CHECK(w.bad == 0);
    CHECK(w.maxdepth <= 12);
    /* The ground under the road's nodes. */
    int tried = 0, found = 0, near = 0;
    WSurface s = {0};
    for (int n = 0; n < g_road->nnodes; n += 5) {
        const float *p = Road_NodePos(g_road, n);
        float q[3] = {p[0], p[1], p[2]};
        tried++;
        WSurface_Update(&s, q, true);
        if (s.none) continue;
        found++;
        float h = WSurface_HeightAt(q);
        if (fabsf(h - q[1]) < 2.0f) near++;
        const float *nrm = WSurface_Normal(&s);
        CHECK(nrm[1] > 0.0f && fabsf(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2] - 1.0f) < 1e-3f);
    }
    CHECK(found * 10 >= tried * 9);
    CHECK(near * 10 >= found * 9);
    printf("  %-10s %6d polygons %5d edges, %5d leaves (depth %2d), ground under %d of %d nodes, %d within 2\n", base,
           polys, edges, w.leaves, w.maxdepth, found, tried, near);
    tracks++;
    BWorld_DeInit();
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("wtree_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    fake_install_graphics();
    Track_FindCrp(one, NULL);
    printf("wtree_test: %d tracks, %d checks: %s\n", tracks, checks, fails ? "FAILED" : "ok");
    return fails != 0 || tracks == 0;
}
