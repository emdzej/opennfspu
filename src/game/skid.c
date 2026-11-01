/* The skid marks: see skid.h. */
#include "game/skid.h"
#include "exe.h"
#include "game/track_pending.h"
#include "game/wgrid.h"
#include "game/world.h"
#include "render/render.h"
#include "render/rlists.h"
#include "render/rmesh.h"
#include "render/rtex.h"
#include "render/sky_levels.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static float f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

enum { NSTRIPS = 20 };

/* A strip (0x14 bytes): bits 0-3 its state (1 free, 4 fading, 8 kept), 4-13 its vertices. */
typedef struct {
    uint32_t word;        /* +0x00 */
    float (*v)[4];        /* +0x04 */
    uint32_t *col;        /* +0x08 */
    Texture *tex;         /* +0x0c */
    SkidTrail *owner;     /* +0x10 */
} Strip;

static unsigned strip_n(const Strip *s) { return s->word >> 4 & 0x3ff; }
static void strip_setstate(Strip *s, uint32_t st) { s->word = (s->word & 0xfffffff0u) ^ st; }
static void strip_add2(Strip *s) { s->word = (((s->word & 0xfffffff0u) + 0x20 ^ s->word) & 0x3ff0) ^ s->word; }

/* A cell (0x50 bytes, 0x478d20). */
typedef struct {
    float centre[3];      /* +0x00 */
    float min[3];         /* +0x0c */
    float max[3];         /* +0x18 */
    float sphere[4];      /* +0x24: the centre at y 0 until a strip sets it, the radius of the square */
    uint32_t key;         /* +0x34: its grid node (wgrid.c: their order) */
    Strip *strips;        /* +0x38: NSTRIPS, made with the first mark */
    int32_t nfree;        /* +0x3c */
    int32_t f40;
    int32_t nfade;        /* +0x44 */
    int32_t ring;         /* +0x48: the ring's start */
    int32_t cursor;       /* +0x4c */
} Cell;

/* WLineBundle (0x3c bytes). */
typedef struct {
    W3DObjectBundle base;
    Cell *cells;          /* +0x30 */
    int32_t level;        /* +0x34 */
    WGrid *grid;          /* +0x38 */
} WLineBundle;

static WLineBundle *s_line;      /* 0x628b70 (and 0x628b74) */
static uint32_t s_nverts;        /* 0x628b60: a strip's vertices */
static float (*s_uv)[2];         /* 0x628b64 */
static SkyBlend s_blend;         /* 0x628b6c */
static RMesh s_mesh;             /* 0x628b68 */
static bool s_mesh_made;

/* 0x4788c0: the strips' size, their texture corners (four vertices: (0, 0) (1, 0) (0, 1) (1, 1)), the
   blend and the mesh (type 9, strips, a stage). */
static void strips_init(uint32_t n)
{
    s_nverts = n;
    free(s_uv);
    s_uv = calloc(n ? n : 1, sizeof *s_uv);
    static const float uv[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    for (uint32_t i = 0; s_uv && i < n; i++) memcpy(s_uv[i], uv[i & 3], sizeof uv[0]);
    SkyBlend_ctor(&s_blend);
    if (s_mesh_made) RMesh_dtor(&s_mesh);
    RMesh_ctor(&s_mesh, 9, 1, 1, NULL, 0);
    RMesh_stage(&s_mesh, 0, NULL, &s_blend, NULL, NULL, 0);
    s_mesh_made = true;
}

static void cell_init(Cell *c, const float centre[3], const float mn[3], const float mx[3], uint32_t key)   /* 0x478d20 */
{
    c->key = key;
    memcpy(c->centre, centre, sizeof c->centre);
    memcpy(c->min, mn, sizeof c->min);
    memcpy(c->max, mx, sizeof c->max);
    float d = c->max[0] - c->centre[0];
    d = d * d;
    c->sphere[3] = sqrtf(d + d);   /* 0x532540 */
    c->sphere[0] = c->centre[0];
    c->sphere[1] = 0;
    c->sphere[2] = c->centre[2];
    c->strips = NULL;
    c->nfree = NSTRIPS;
    c->f40 = c->nfade = c->ring = c->cursor = 0;
}

static void strips_free(Cell *c)   /* 0x478de0 */
{
    if (!c->strips) return;
    for (int i = 0; i < NSTRIPS; i++) {
        free(c->strips[i].v);
        free(c->strips[i].col);
    }
    free(c->strips);
    c->strips = NULL;
}

/* 0x479710 (and 0x4790d0 for each): a cell's twenty strips, free and empty. */
static void strips_make(Cell *c)
{
    if (c->strips) return;
    c->strips = calloc(NSTRIPS, sizeof *c->strips);
    if (!c->strips) return;
    for (int i = 0; i < NSTRIPS; i++) {
        Strip *s = &c->strips[i];
        s->word = 1;
        s->v = calloc(s_nverts ? s_nverts : 1, sizeof *s->v);
        s->col = calloc(s_nverts ? s_nverts : 1, sizeof *s->col);
    }
}

/* The cell of a point: the grid's node down to the line level (0x47ba60), then the first cell whose key
   isn't below the node's (0x47a290); the original reads past the cells when there is none, the port takes
   the last. */
static int cell_of(const float p[3])
{
    WLineBundle *l = s_line;
    WGridNode *n = WGrid_Find(l->grid, p, l->level);
    uint32_t key = n ? n->seq : 0;
    int lo = 0, len = (int)l->base.count;
    while (0 < len) {
        int half = len >> 1;
        if (l->cells[lo + half].key < key) {
            lo += half + 1;
            len -= half + 1;
        } else {
            len = half;
        }
    }
    return lo < (int)l->base.count ? lo : (int)l->base.count - 1;
}

/* 0x478ba0: a strip of the cell for an owner: with none free the next one used after the cursor is freed;
   with fewer than two free the next two kept ones fade; then the first free from the cursor, its state
   set, emptied, its texture by name (from common.fsh, the name's bytes swapped). -1 if none. */
static int strip_get(Cell *c, uint32_t state, uint32_t name, SkidTrail *owner)
{
    Strip *s = c->strips;
    if (!s) return -1;
    if (c->nfree == 0) {
        int i = c->cursor + 1;
        if (NSTRIPS - 1 < i) i = c->ring;
        while ((s[i].word & 0xc) == 0) {
            i++;
            if (NSTRIPS - 1 < i) i = c->ring;
            if (i == c->cursor) goto keep;
        }
        strip_setstate(&s[i], 1);
        c->nfree++;
    }
keep:
    if (c->nfree < 2) {
        int n = 0, i = c->cursor + 1;
        if (NSTRIPS - 1 < i) i = c->ring;
        do {
            if (s[i].word & 8) {
                strip_setstate(&s[i], 4);
                n++;
                c->nfade++;
                if (n == 2) break;
            }
            i++;
            if (NSTRIPS - 1 < i) i = c->ring;
        } while (i != c->cursor);
    }
    int start = c->cursor;
    for (int i = start;;) {
        if ((s[i].word & 0xf) == 1) {
            if (state == 4) c->nfade++;
            Strip *t = &s[c->cursor];
            t->word = (t->word & 0xffffc000u) ^ (state & 0xf);
            t->owner = owner;
            t->tex = NULL;
            if (g_render_skidmarks && g_rtex_common[0]) {
                uint32_t sw = ((name & 0xff0000u) | name >> 16) >> 8 | ((name & 0xff00u) | name << 16) << 8;
                t->tex = RTexBank_find(g_rtex_common[0], 0, sw);
            }
            c->nfree--;
            return c->cursor;
        }
        c->cursor = i + 1;
        if (NSTRIPS - 1 < c->cursor) c->cursor = c->ring;
        i = c->cursor;
        if (i == start) break;
    }
    return -1;
}

SkidTrail *SkidTrail_new(float tyre)
{
    SkidTrail *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    float w = tyre * f32(0x5b4744);
    t->width = w * (g_render_skidmarks ? f32(0x5b246c) : f32(0x5b25e8));
    t->cell = t->strip = t->state = -1;
    t->mode = 2;
    t->f2c = 1.0f;
    return t;
}

void SkidTrail_Reset(SkidTrail *t)
{
    if (!t) return;
    t->cell = t->strip = -1;
    t->kind = 0;
    t->state = -1;
    t->mode = 2;
    t->f24 = t->f28 = 0;
    t->f2c = 1.0f;
    t->tex = 0;
    t->kind15 = 0;
    t->steps = 0;
}

static void set_kind(SkidTrail *t, int kind)
{
    t->kind = kind;
    t->state = kind == 6 ? 4 : 8;
    t->kind15 = kind & 0xf;
    t->tex = exe_u32(0x5d00e8 + 4u * (uint32_t)(kind & 0xf));
}

/* 0x479120: a pair across the way at the point, half the width each side, coloured by the ground (the
   quality's alpha by the strength) into the trail's strip: carried on from the last pair (the strip's
   last two copied) into a new strip when it fills, the trail leaves its cell (found again), the kind
   changed (the old strip gets a pair of the old colour first) or the strip went to another; every 8th
   step (the mask 7, 0x6197ec) the pair replaces the last; a strip's first pair is clear. */
static void trail_mark(SkidTrail *t, const float p[3], const float d[3], float k)
{
    WLineBundle *l = s_line;
    uint32_t every = (uint32_t)t->steps & g_render_skidmask;
    float y = p[1], w = t->width;
    float lx = p[0] - d[2] * w, lz = p[2] - -d[0] * w;
    float rx = d[2] * w + p[0], rz = -d[0] * w + p[2];
    uint32_t c;
    if (!g_render_skidmarks) {
        c = exe_u32(0x5d0128 + 4u * (uint32_t)t->kind15);
    } else {
        c = exe_u32(0x5d00a8 + 4u * (uint32_t)t->kind15);
        uint32_t a = (uint32_t)(int32_t)lrintf(k * f32(0x5b3710));
        c = ((((c >> 8) & 0xffff0000u) * a ^ c) & 0xffffffu) ^ ((c >> 8) & 0xff0000u) * a;
    }
    bool carried = false, renew = false;
    float keep[2][4];
    if ((t->mode == 0 || t->mode == 3) && 0 <= t->strip && l->cells[t->cell].strips) {
        Strip *s = &l->cells[t->cell].strips[t->strip];
        unsigned n = strip_n(s);
        if (s->owner == t && 2 <= n) {
            carried = true;
            memcpy(keep[0], s->v[n - 2], sizeof keep[0]);
            memcpy(keep[1], s->v[n - 1], sizeof keep[1]);
            if (t->mode == 3 && 1 < (int)(s_nverts - n)) {
                uint32_t oc = s->col[n - 2] & 0x7fffffffu;
                float a[4] = {lx, y, lz, 1.0f}, b[4] = {rx, y, rz, 1.0f};
                memcpy(s->v[n], a, sizeof a);
                memcpy(s->v[n + 1], b, sizeof b);
                s->col[n + 1] = s->col[n] = oc;
                strip_add2(s);
            }
        }
    }
    Cell *cell = &l->cells[t->cell];
    if (cell->max[0] < p[0] || p[0] < cell->min[0] || cell->max[2] < p[2] || p[2] < cell->min[2]) {
        int i = cell_of(p);
        strips_make(&l->cells[i]);
        t->cell = i;
        renew = true;
    }
    if (t->mode == 3) renew = true;
    cell = &l->cells[t->cell];
    if (!cell->strips || t->strip < 0) {
        renew = true;
    } else {
        const Strip *s = &cell->strips[t->strip];
        if ((s->word & 0xf) == 1 || s->owner != t) renew = true;
    }
    if (renew || !cell->strips || (int)(s_nverts - strip_n(&cell->strips[t->strip])) < 2) {
        t->strip = strip_get(cell, (uint32_t)t->state, t->tex, t);
        if (t->strip < 0) return;   /* (the original goes on with strip -1) */
        if (carried) {
            Strip *s = &cell->strips[t->strip];
            unsigned n = strip_n(s);
            memcpy(s->v[n], keep[0], sizeof keep[0]);
            memcpy(s->v[n + 1], keep[1], sizeof keep[1]);
            uint32_t cc = t->mode == 3 ? c & 0x7fffffffu : c;
            s->col[n + 1] = s->col[n] = cc;
            strip_add2(s);
        }
    }
    if (cell->sphere[1] < f32(0x5b27b4) && f32(0x5b3ee0) < cell->sphere[1]) cell->sphere[1] = y;
    Strip *s = &cell->strips[t->strip];
    if (every && 0x3f < (s->word & 0x3ff0)) s->word = (((s->word & 0xfffffff0u) - 0x11 ^ s->word) & 0x3ff0) ^ s->word;
    if ((s->word & 0x3ff0) == 0 && t->mode != 3) c &= 0xffffffu;
    unsigned n = strip_n(s);
    if (s_nverts < n + 2) return;
    float a[4] = {lx, y, lz, 1.0f}, b[4] = {rx, y, rz, 1.0f};   /* (their w is whatever the stack held) */
    memcpy(s->v[n], a, sizeof a);
    memcpy(s->v[n + 1], b, sizeof b);
    s->col[n + 1] = s->col[n] = c;
    strip_add2(s);
}

void SkidTrail_Step(SkidTrail *t, const float pos[3], float slip, int kind, const float dir[3], float threshold, float scale)
{
    if (!t || !s_line || !s_line->base.count || !g_render_skidmarks) return;
    if (slip <= threshold) {
        t->mode = 2;
        return;
    }
    float k = (slip - threshold) * scale;
    t->steps++;
    if (t->cell == -1) {
        set_kind(t, kind);
        int i = cell_of(pos);
        strips_make(&s_line->cells[i]);   /* (0x401b00 with 0x4790d0) */
        t->cell = i;
        t->strip = strip_get(&s_line->cells[i], (uint32_t)t->state, t->tex, t);
        t->mode = 1;
    } else if (t->strip == -1 || t->mode == 2) {
        set_kind(t, kind);
        t->strip = strip_get(&s_line->cells[t->cell], (uint32_t)t->state, t->tex, t);
        t->mode = 1;
    } else if (kind != t->kind) {
        set_kind(t, kind);
        t->mode = 3;
    }
    trail_mark(t, pos, dir, k);
    t->mode = 0;
}

/* ---- the line bundle ---- */

/* 0x4797a0: the grid's nodes down to the line level (or any without four children) in order, each one
   cell (counted only, or made: the node's centre and box, its key). */
static void cells_walk(WLineBundle *l, WGridNode *n, int *count, bool only_count)
{
    int k = 0;
    if (n->flags & 1)
        for (int q = 0; q < 4; q++) k += n->child[q] != NULL;
    if (WGrid_Level(n) < l->level && k == 4) {
        for (int q = 0; q < 4; q++) cells_walk(l, n->child[q], count, only_count);
        return;
    }
    if (!only_count) {
        float c[3], mn[3], mx[3];
        WGrid_Centre(l->grid, n, c);
        WGrid_Box(l->grid, n, mn, mx);
        cell_init(&l->cells[*count], c, mn, mx, n->seq);
    }
    (*count)++;
}

static int cell_cmp(const void *a, const void *b)
{
    uint32_t x = ((const Cell *)a)->key, y = ((const Cell *)b)->key;
    return x < y ? -1 : x > y;
}

static void line_dtor(W3DObjectBundle *b, int free_self)   /* WLineBundle_v0 0x479c80 */
{
    WLineBundle *l = (WLineBundle *)b;
    if (s_line == l) s_line = NULL;
    for (uint32_t i = 0; l->cells && i < b->count; i++) strips_free(&l->cells[i]);
    free(l->cells);
    free(b->entries);
    if (s_mesh_made) RMesh_dtor(&s_mesh);
    s_mesh_made = false;
    free(s_uv);
    s_uv = NULL;
    s_nverts = 0;
    if (b->draw) ((RDrawObject *)b->draw)->vt->dtor(b->draw, 1);
    if (free_self) free(b);
}

static const W3DObjectBundleVtbl vt_line = {line_dtor, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};   /* 0x5b4750 */

/* 0x479890: the strips' size (192 vertices on grids under 1200 wide, 0x5b4748, else 12), the line level (the
   last whose cells are 100 wide, 0x5b242c), a cell per node down to it (entries with the far flag
   0x400000), sorted by key; its draw object RDrawLineBundle (0x4512a0). */
W3DObjectBundle *WLineBundle_new(void *world, WGrid *g)
{
    WLineBundle *l = calloc(1, sizeof *l);
    if (!l || !g || !g->root) {
        free(l);
        return NULL;
    }
    W3DObjectBundle_init(&l->base, 0x3039, world);
    l->base.vt = &vt_line;
    s_line = l;
    strips_init(g->size[0] < f32(0x5b4748) ? 0xc0 : 0xc);
    l->grid = g;
    l->level = 0;
    bool found = false;
    if (0 < g->height) {
        do {
            if (found) break;
            if (f32(0x5b242c) <= g->size[l->level]) l->level++;
            else found = true;
        } while ((float)l->level < g->height && l->level < 13);
    }
    l->level--;
    if (l->level < 0) l->level = 0;
    int n = 0;
    cells_walk(l, g->root, &n, true);
    l->base.count = (uint32_t)n;
    l->base.draw = RDrawLineBundle_new(&l->base);
    l->base.entries = calloc(n ? (size_t)n : 1, sizeof *l->base.entries);
    for (int i = 0; l->base.entries && i < n; i++) l->base.entries[i].flags |= 0x400000;
    l->cells = calloc(n ? (size_t)n : 1, sizeof *l->cells);
    int k = 0;
    if (l->cells) cells_walk(l, g->root, &k, false);
    if (l->cells && 1 < n) qsort(l->cells, (size_t)n, sizeof *l->cells, cell_cmp);   /* (std::sort, keys distinct) */
    return &l->base;
}

void WLineBundle_Sphere(W3DObjectBundle *b, int cell, float out[4])
{
    memcpy(out, ((WLineBundle *)b)->cells[cell].sphere, 4 * sizeof(float));
}

/* Slot 4 (0x479f90): the cell's centre, 0.1 (0x5b2604) either way. */
void WLineBundle_Box(W3DObjectBundle *b, int cell, float mn[3], float mx[3])
{
    const Cell *c = &((WLineBundle *)b)->cells[cell];
    float e = f32(0x5b2604);
    for (int j = 0; j < 3; j++) {
        mn[j] = c->centre[j] - e;
        mx[j] = c->centre[j] + e;
    }
}

/* 0x478a30: a strip of more than one pair whose ends are both within 100 of the eye (x, z): its texture's
   detail bias by the nearer end (-80000, 0x5d00a4, by the render's depth factor over the distance squared,
   -10 .. -1), then the mesh. */
static void strip_draw(Strip *s)
{
    if ((s->word & 0x3ff0) <= 0x20 || !g_render || !g_render->camera) return;
    const float *eye = g_render->camera->frame.eye;
    unsigned n = strip_n(s);
    float dx = s->v[0][0] - eye[0], dz = s->v[0][2] - eye[2];
    float d1 = dx * dx + dz * dz;
    if (!(d1 <= f32(0x5b2764))) return;
    dx = s->v[n - 1][0] - eye[0];
    dz = s->v[n - 1][2] - eye[2];
    float d2 = dx * dx + dz * dz;
    if (!(d2 <= f32(0x5b2764))) return;
    float m = d2 < d1 ? d2 : d1;
    double num = (double)(int32_t)exe_u32(0x5d00a4) * (double)g_render->depthfactor;
    int32_t bias = (int32_t)lrintf((float)(num / (double)m));
    if (-1 < bias) bias = -1;
    else if (bias < -10) bias = -10;
    s_blend.v[4] = bias;   /* 0x5377d0 */
    RMesh_setPositions(&s_mesh, s->v, (int)n);
    RMesh_setUVs(&s_mesh, 0, s_uv);
    RMesh_setColours(&s_mesh, 0, s->col);
    RMesh_setTexture(&s_mesh, 0, s->tex, 0);
    RMesh_draw(&s_mesh, -1);
}

void WLineBundle_DrawCell(W3DObjectBundle *b, int cell)
{
    Cell *c = &((WLineBundle *)b)->cells[cell];
    if (!g_render || !g_render->device) return;
    RenderState *rs = g_render->device->rs;
    int zwrite = rs->state39;
    RenderState_SetZWrite(rs, 0);
    if (c->strips)
        for (int i = 0; i < NSTRIPS; i++)
            if ((c->strips[i].word & 0xf) != 1) strip_draw(&c->strips[i]);
    RenderState_SetZWrite(rs, zwrite);
}

void World_Tick(void)
{
    WLineBundle *l = s_line;
    if (!l) return;
    for (uint32_t ci = 0; ci < l->base.count; ci++) {
        Cell *c = &l->cells[ci];
        int fading = c->nfade;
        if (!c->strips || fading == 0) continue;
        int seen = 0;
        if (0 < fading) {
            for (int i = c->ring; seen < fading; i++) {
                if (NSTRIPS - 1 < i) break;
                Strip *s = &c->strips[i];
                if ((s->word & 0xf) != 4) continue;
                bool clear = true;
                unsigned n = strip_n(s);
                for (unsigned k = 0; k < n; k++) {
                    uint32_t v = s->col[k];
                    if (0xffffff < v) {
                        s->col[k] = v < 0x5000000u ? 0 : v + 0xfc000000u;
                        clear = false;
                    }
                }
                if (clear) {
                    strip_setstate(s, 1);
                    c->nfade--;
                    c->nfree++;
                }
                seen++;
            }
        }
    }
}

void WLineBundle_Clear(void)   /* 0x479d60 */
{
    WLineBundle *l = s_line;
    if (!l) return;
    for (uint32_t ci = 0; ci < l->base.count; ci++) {
        Cell *c = &l->cells[ci];
        strips_free(c);
        float centre[3], mn[3], mx[3];
        memcpy(centre, c->centre, sizeof centre);
        memcpy(mn, c->min, sizeof mn);
        memcpy(mx, c->max, sizeof mx);
        cell_init(c, centre, mn, mx, c->key);
    }
}
