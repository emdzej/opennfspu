/* Track loading (game/track.c, game/world.c) against every track of the game: the world loads, every
   article ends up in exactly one bundle (scene prototypes are copies), the Base items are in bounds and
   finite, the object list covers the bundles, the camera animations and visibility walls are read, and
   every material's texture name is an entry of the track's shape file. Line of sight through the walls is
   checked on a synthetic wall, and from each track's first object sphere to the others. Skips (exit 0)
   without data. */
#include "exe.h"
#include "game/track.h"
#include "real/mem.h"
#include "real/shape.h"
#include "support/udata.h"
#include "vfs.h"
#include "vfs_host.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static bool finite3(const float *v) { return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]); }

static void check_texnames(BWorld *w, const char *track)
{
    char fsh[128];
    snprintf(fsh, sizeof fsh, "%s%s.fsh", g_track_dir, track);
    void *shp = SHAPE_loadfile(fsh, 0, 0);
    CHECK(shp != NULL);
    if (!shp) return;
    uint32_t n = SHAPE_count(shp), mats = 0, missing = 0;
    UData *r = w->root;
    for (UData *it = UData_items(r); it < UData_itemsend(r); it++) {
        if (UData_key(it) != 0x6d742020) continue;   /* 'mt  ' */
        mats++;
        uint32_t tex;
        memcpy(&tex, (const uint8_t *)UData_data(it) + 0x28, 4);
        bool found = tex == 0;
        for (uint32_t i = 0; i < n && !found; i++) found = SHAPE_entryname(shp, (int)i) == tex;
        if (!found) missing++;
    }
    printf("  %u materials, %u texture names not in %s\n", mats, missing, fsh);
    CHECK(mats > 0);
    /* One material per track names a texture that isn't there (alps: 1 of 204); allow a few. */
    CHECK(missing * 50 <= mats);
    MEM_purge(shp);
}

/* The first trigger of <track>_fstart.scn (the forward start grid): its position. Test code, not the port:
   the scene parser (Scene_ParseElements 0x46c170) is pending. */
static bool start_position(const char *track, float pos[3])
{
    char path[128];
    snprintf(path, sizeof path, "%s%s_fstart.scn", g_track_dir, track);
    size_t n = 0;
    uint8_t *raw = vfs_read_all(path, &n);
    if (!raw) return false;
    char *t = malloc(n + 1);   /* vfs_read_all's data isn't NUL-terminated */
    if (t) { memcpy(t, raw, n); t[n] = 0; }
    free(raw);
    if (!t) return false;
    char *e = strstr(t, "TRIGGER_ELEMENT");
    bool ok = e && sscanf(strchr(e, '\n') ? strchr(e, '\n') : e, "%f %f %f", &pos[0], &pos[1], &pos[2]) == 3;
    free(t);
    return ok;
}

/* Objects whose bounding sphere comes within `dist` of the start and that the visibility walls don't hide. */
static void check_start_view(BWorld *w, const char *track)
{
    float start[3];
    if (!start_position(track, start)) { printf("  no start grid\n"); return; }
    const float dist = 300.0f;
    int near = 0, seen = 0;
    for (uint32_t i = 0; i < w->nobjects; i++) {
        WObjectRef *o = &w->objects[i];
        if (o->bundle == w->scene) continue;   /* scene slots are placed later */
        float s[4];
        WArticleBundle_sphere((WArticleBundle *)o->bundle, o->index, s);
        float dx = s[0] - start[0], dz = s[2] - start[2];
        if (sqrtf(dx * dx + dz * dz) - s[3] > dist) continue;
        near++;
        if (!w->nvis || !WVis_blocked(w->vis, w->vis_off, w->nvis, start, s, false)) seen++;
    }
    printf("  start (%.1f, %.1f, %.1f): %d objects within %.0f m, %d not behind a visibility wall\n",
           start[0], start[1], start[2], near, dist, seen);
    CHECK(near > 0 && seen > 0 && seen <= near);
    CHECK(near < (int)w->nobjects);   /* the start sees a part of the track, not all of it */
}

static void check_track(const char *track)
{
    char path[128];
    snprintf(path, sizeof path, "%s%s", g_track_dir, track);
    BWorld *w = BWorld_new();
    bool ok = Track_LoadWorld(w, path);
    CHECK(ok);
    if (!ok) { BWorld_free(w); return; }
    UData *r = w->root;
    int narts = UData_countarticles(r, 0x41727469);
    CHECK(narts > 0);
    UData *first = UData_children(r);
    const uint8_t *lo = w->file->data, *hi = lo + w->file->size;

    /* each article in exactly one of instance/anim/midground/static; prototypes only in the scene's and
       instance bundles' prototype lists (and the static bundle, which takes everything left) */
    int *owner = calloc((size_t)narts, sizeof *owner);
    uint32_t total = 0;
    for (int bi = 1; bi < w->nbundles; bi++) {
        W3DObjectBundle *b = w->bundles[bi];
        for (uint32_t i = 0; i < b->count; i++) {
            long k = b->entries[i].article - first;
            CHECK(k >= 0 && k < narts);
            if (k < 0 || k >= narts) continue;
            CHECK(owner[k] == 0);
            owner[k] = bi;
            const WBase *bs = WArticleBundle_base((WArticleBundle *)b, (int)i);
            CHECK(bs != NULL);
            if (!bs) continue;
            CHECK((const uint8_t *)bs >= lo && (const uint8_t *)(bs + 1) <= hi);
            CHECK(finite3(bs->centre) && finite3(bs->bmin) && finite3(bs->bmax) && isfinite(bs->radius) && bs->radius >= 0);
            for (int c = 0; c < 3; c++) CHECK(bs->bmin[c] <= bs->bmax[c]);
            float s[4];
            WArticleBundle_sphere((WArticleBundle *)b, (int)i, s);
            CHECK(s[3] == bs->radius);
        }
        total += b->count;
    }
    int unowned = 0;
    for (int k = 0; k < narts; k++) unowned += owner[k] == 0;
    /* the vertices of every article, through the bundles (cross-checked with out/tools/vtcount.py:
       alps 173195) */
    unsigned long verts = 0;
    for (int bi = 1; bi < w->nbundles; bi++)
        for (uint32_t i = 0; i < w->bundles[bi]->count; i++) {
            UData *a = w->bundles[bi]->entries[i].article;
            for (UData *it = UData_items(a); it < UData_itemsend(a); it++)
                if (UData_key(it) == 0x76742020) verts += it->count;   /* 'vt  ' */
        }
    printf("  %lu vertices\n", verts);
    if (!strcmp(track, "alps")) CHECK(verts == 173195);
    CHECK(unowned == 0);
    WSceneBundle *sc = (WSceneBundle *)w->scene;
    WInstanceBundle *in = (WInstanceBundle *)w->instances;
    CHECK(sc->nproto == in->nproto);
    for (int p = 0; p < sc->nproto; p++) CHECK(sc->protos[p].article == in->protos[p].article);
    /* the object list: scene slots + every other bundle's entries */
    CHECK(w->nobjects == total + w->scene->count);
    for (uint32_t i = 0; i < in->a.base.count; i++) CHECK(finite3(in->placed[i].pos));

    printf("%-10s %4d articles: scene %u (%d prototypes), instance %u, anim %u, midground %u, static %u; "
           "%u objects, %u cameras, %u vis walls\n", track, narts, w->scene->count, sc->nproto,
           w->bundles[1]->count, w->bundles[2]->count, w->bundles[3]->count, w->bundles[4]->count,
           w->nobjects, w->ncams, w->nvis);
    CHECK(w->bundles[4]->count > 0);
    for (uint32_t i = 0; i < w->nvis; i++) CHECK(isfinite(w->vis[i].inv_len2) && w->vis[i].len2 > 0);
    for (uint32_t i = 0; i < w->ncams; i++) CHECK((const uint8_t *)w->cams[i].keys > lo && (const uint8_t *)w->cams[i].keys < hi);

    /* line of sight between object centres: walls block some lines on tracks that have walls */
    if (w->nvis && w->nobjects > 1) {
        WArticleBundle *st = (WArticleBundle *)w->bundles[4];
        float a[4], b[4];
        int blocked = 0, lines = 0;
        WArticleBundle_sphere(st, 0, a);
        for (uint32_t i = 1; i < st->base.count; i += 7, lines++) {
            WArticleBundle_sphere(st, (int)i, b);
            blocked += WVis_blocked(w->vis, w->vis_off, w->nvis, a, b, false);
        }
        printf("  line of sight from static object 0: %d of %d lines blocked\n", blocked, lines);
        CHECK(blocked < lines);
    }
    check_texnames(w, track);
    check_start_view(w, track);
    free(owner);
    BWorld_free(w);
}

static void check_segments(void)
{
    float p[6] = {0, 0, 0, 2, 0, 2}, q[6] = {0, 0, 2, 2, 0, 0}, at[3];
    CHECK(World_SegmentsCross(p, q, at) && fabsf(at[0] - 1) < 1e-6f && fabsf(at[2] - 1) < 1e-6f);
    float far[6] = {3, 0, 3, 5, 0, 5};
    CHECK(!World_SegmentsCross(p, far, NULL));
    float par[6] = {0, 0, 1, 2, 0, 3};
    CHECK(!World_SegmentsCross(p, par, NULL));   /* parallel: no crossing */
    WVisWall wall;
    float w0[3] = {-1, 0, 5}, w1[3] = {1, 0, 5};
    WVisWall_set(&wall, w0, w1);
    CHECK(!wall.high && wall.dx == 2 && wall.dz == 0 && wall.len2 == 4 && wall.inv_len2 == 0.25f);
    uint8_t off = 0;
    float a[3] = {0, 0, 0}, b[3] = {0, 0, 10}, c[3] = {3, 0, 10};
    CHECK(WVis_blocked(&wall, &off, 1, a, b, false));
    CHECK(!WVis_blocked(&wall, &off, 1, a, c, false));
    CHECK(!WVis_blocked(&wall, &off, 1, a, b, true));      /* only high walls count */
    off = 1;
    CHECK(!WVis_blocked(&wall, &off, 1, a, b, false));     /* a disabled wall */
}

static void list_track(const char *name, void *user)
{
    char t[64];
    snprintf(t, sizeof t, "%s", name);
    t[strlen(t) - 4] = 0;
    check_track(t);
    ++*(int *)user;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("track_world_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    check_segments();
    int n = 0;
    CHECK(Track_FindCrp(list_track, &n) == 15);
    CHECK(n == 15);
    printf("track_world_test: %d checks, %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
