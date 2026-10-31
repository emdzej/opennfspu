/* Track loading (see track.h). */
#include "game/track.h"
#include "game/scene.h"
#include "game/track_pending.h"
#include "game/wtree.h"
#include "real/real.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

BWorld *g_world;                       /* 0x628960 */
char g_track_name[32];                 /* 0x657444 */
/* The original reads the tracks' directory through the pointer at 0x65b2b8, set at startup (the setter
   is not located yet; the exe holds no such string, so it is built from the installation). The installed
   game keeps the tracks here. */
const char *g_track_dir = "GameData\\Track\\";

/* ------------------------------------------------------------------ camera animations */

/* 0x471660 makes the camera-animation holder (the global 0x628938, with the scale 1/64 = 0x3c800000);
   0x4717b0 appends one record per 'CmAn' item of the root. */
static void load_cams(BWorld *w)
{
    const uint32_t CMAN = 0x436d416e;
    UData *r = w->root;
    int n = UData_countitems(r, CMAN);
    UData *it = UData_finditem(r, CMAN, 0xffffffffu, 0xffffffffu);
    if (it == UData_itemsend(r)) return;
    w->cams = calloc((size_t)(n > 0 ? n : 1), sizeof(WCamAnim));
    for (int i = 0; i < n; i++, it++) {
        const uint8_t *d = UData_data(it);
        WCamAnim *c = &w->cams[w->ncams++];
        c->one = 1;
        memcpy(&c->a, d + 0x24, 4);
        memcpy(&c->b, d + 0x28, 4);
        c->keys = d + 0x2c;
        c->pos[0] = c->pos[1] = c->pos[2] = 0;   /* the zero vector 0x5bb2b0 */
    }
}

/* ------------------------------------------------------------------ visibility walls */

void BWorld_LoadVis(BWorld *w)
{
    const uint32_t VIS = 0x56697320;   /* 'Vis ' */
    UData *it = UData_find(w->root, VIS);
    if (it == UData_itemsend(w->root)) return;
    w->nvis = it->count;
    const float *p = UData_data(it);
    w->vis = calloc(w->nvis ? w->nvis : 1, sizeof(WVisWall));
    w->vis_off = calloc(w->nvis ? w->nvis : 1, 1);
    for (uint32_t i = 0; i < w->nvis; i++, p += 6) WVisWall_set(&w->vis[i], p, p + 3);
}

/* ------------------------------------------------------------------ bundles and objects */

static void add_objects(BWorld *w, W3DObjectBundle *b)
{
    if (w->nbundles < (int)(sizeof w->bundles / sizeof *w->bundles)) w->bundles[w->nbundles++] = b;
    WObjectRef *o = realloc(w->objects, (w->nobjects + b->count + 1) * sizeof *o);
    if (!o) return;
    w->objects = o;
    for (uint32_t i = 0; i < b->count; i++) {
        WObjectRef *r = &w->objects[w->nobjects++];   /* 0x489760, appended by 0x488d20 */
        r->bundle = b;
        r->flag = 0;
        r->index = (uint16_t)i;
    }
}

/* The first part of 0x487940: every 'Arti' article of the root, given to the bundles in the original's
   order; each bundle goes into the bundle list (0x488e80) and its entries into the object list. */
void BWorld_BuildBundles(BWorld *w)
{
    const uint32_t ARTI = 0x41727469;
    UData *r = w->root;
    int n = UData_countarticles(r, ARTI);
    UData *a = UData_findarticle(r, ARTI, 0xffffffffu, 0xffffffffu);
    UData **arts = calloc((size_t)(n > 0 ? n : 1), sizeof *arts);
    for (int i = 0; i < n; i++) arts[i] = a + i;

    W3DObjectBundle *b = WSceneBundle_new(w->r3d);                  /* 0x482550 */
    w->scene = b;
    ((WSceneBundle *)b)->unlimited = Scene_CountGeom() == 0;       /* 0x46d910 */
    b->vt->load(b, arts, n);
    add_objects(w, b);

    b = WInstanceBundle_new(w->r3d);                                /* 0x477fd0 */
    w->instances = b;
    b->vt->load(b, arts, n);
    add_objects(w, b);

    b = WAnimBundle_new(w->r3d);                                    /* 0x4776a0 */
    w->anim = b;
    b->vt->load(b, arts, n);
    add_objects(w, b);

    b = WMidGroundBundle_new(w->r3d);                               /* 0x47cb40 */
    b->vt->load(b, arts, n);
    add_objects(w, b);

    b = WStaticBundle_new(w->r3d);                                  /* 0x483350 */
    b->vt->load(b, arts, n);
    add_objects(w, b);

    free(arts);
    /* Then the line bundle (0x479890 with the 0x10 object), the links, Track_LoadEdges 0x488000, the
       sync tasks, the visibility walls and the two 0x488820 passes (the edges here, the walls below). */
    World_BuildBundles(w);
    PENDING_NOTE(0x482e80, "0x487940's holder 0x628bd4");
    Scene_PlaceGeometry();   /* 0x46d120 (0x487940's) */
    Track_LoadEdges(w);                      /* 0x488000 */
    BWorld_LoadVis(w);
}

/* ------------------------------------------------------------------ the world */

BWorld *BWorld_new(void)
{
    BWorld *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    w->file = calloc(1, sizeof *w->file);
    if (w->file) UDataFile_ctor(w->file);    /* 0x59f630 */
    return w;
}

void BWorld_free(BWorld *w)
{
    if (!w) return;
    for (int i = 0; i < w->nbundles; i++) W3DObjectBundle_delete(w->bundles[i]);
    WTree_delete(w->tree);                   /* 0x487850: 0x483780 */
    free(w->objects);
    free(w->vis);
    free(w->vis_off);
    free(w->cams);
    if (w->file) {
        UDataFile_free(w->file);
        UDataFile_dtor(w->file);
        free(w->file);
    }
    free(w->name);
    free(w);
}

bool Track_LoadWorld(BWorld *w, const char *path)
{
    if (w->file && w->file->data) UDataFile_free(w->file);   /* 0x487850 */
    free(w->name);
    w->name = malloc(strlen(path) + 1);
    if (!w->name) return false;
    strcpy(w->name, path);
    char crp[260];
    snprintf(crp, sizeof crp, "%s.crp", w->name);
    if (!w->file || !UDataFile_load(w->file, crp, false)) return false;
    w->root = UDataFile_root(w->file);
    w->f38 = 0;
    load_cams(w);
    w->r3d = R3DTrack_new(NULL);             /* with *(0x628130 + 0x74) */
    R3DTrack_load(w->r3d, w->root);
    const char *base = strrchr(path, '\\');
    base = base ? base + 1 : path;
    Track_LoadScenes(base, 0, 0);            /* Scene_LoadStarts, then the skill test or start scenes */
    char defs[256];
    snprintf(defs, sizeof defs, "%s%s", g_track_dir, "animdefs.txt");
    WMidGround_LoadObjectDefs(defs);
    w->tree = WTree_new(8192.0f, 15);        /* 0x4835e0 (0x46000000) */
    g_wtree = w->tree;
    BWorld_BuildBundles(w);
    Road_new();                              /* 0x47fbd0 */
    World_LoadSim(w->root);                  /* 0x47ff20 */
    snprintf(crp, sizeof crp, "%s.jnc", w->name);
    World_LoadJunctions(crp);
    return true;
}

void Track_Load(void)
{
    g_world = BWorld_new();
    char path[260];
    snprintf(path, sizeof path, "%s%s%s", g_track_dir, g_track_name, "");   /* the third is "" (0x5e8e50) */
    if (!Track_LoadWorld(g_world, path))
        REAL_abort(__FILE__, __LINE__, "ERROR - Couldn't load track: '%s'\n", g_track_name);   /* 0x59fb40 */
}

void BWorld_DeInit(void)
{
    BWorld_free(g_world);
    g_world = NULL;
}

/* ------------------------------------------------------------------ track files */

typedef struct { char (*names)[64]; int n, cap; } NameList;

static void collect_crp(const char *name, bool is_dir, void *user)
{
    NameList *l = user;
    size_t len = strlen(name);
    if (is_dir || len < 4 || len >= 64 || strcasecmp(name + len - 4, ".crp")) return;
    if (l->n == l->cap) {
        int c = l->cap ? l->cap * 2 : 32;
        char (*nn)[64] = realloc(l->names, (size_t)c * sizeof *nn);
        if (!nn) return;
        l->names = nn;
        l->cap = c;
    }
    memcpy(l->names[l->n++], name, len + 1);
}

static int cmp_names(const void *a, const void *b) { return strcasecmp(a, b); }

/* The original lists "%s*.crp" with the utility's FindFirstFile wrappers (0x59e020 / 0x59fd90), which
   on the game's NTFS or CD file systems come in name order; vfs_list doesn't promise an order, so the
   names are sorted case-insensitively. No .crp at all is the original's fatal error. */
int Track_FindCrp(void (*fn)(const char *name, void *user), void *user)
{
    NameList l = {0};
    char dir[260];
    snprintf(dir, sizeof dir, "%s", g_track_dir);
    size_t dl = strlen(dir);
    if (dl && (dir[dl - 1] == '\\' || dir[dl - 1] == '/')) dir[dl - 1] = 0;
    vfs_list(dir, collect_crp, &l);
    if (!l.n) REAL_abort(__FILE__, __LINE__, "No *.crp files in %s", g_track_dir);
    qsort(l.names, (size_t)l.n, sizeof *l.names, cmp_names);
    for (int i = 0; i < l.n; i++) fn(l.names[i], user);
    free(l.names);
    return l.n;
}
