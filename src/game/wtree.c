/* The world's collision tree, its elements and the surface queries (see wtree.h). */
#include "game/wtree.h"
#include "game/smack.h"
#include "exe.h"
#include "game/track.h"
#include "game/track_pending.h"
#include "game/world.h"
#include "pending.h"
#include "eagl/eagl.h"
#include "render/r3dcar.h"   /* EAGL_MatAxisAngle */
#include "render/rmaterial.h"
#include "vfs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

WTree *g_wtree;

/* The cells' widths per level (0x628be8) and the offset of the tree's origin (0x628c30). */
static float level_size[16], origin;

/* Constants read from the exe: 0.0 0x5b23f8, 0.25 0x5b2468, 1/3 0x5b2680, 0.5 0x5b23f0, 1.0 0x5b24a8,
   100.0 0x5b242c, 0.04 0x5b2418, 0.9999 0x5b3e0c (and 0x3f7ff972, the 0.9999 stored in its place),
   3.5 0x5b38ec, the double 0.01 at 0x5b2bc0, 1e38 (0x7e967699), the zero vector 0x5bb2b0. */
static float k_zero, k_quarter, k_third, k_half, k_one, k_edgemax, k_flat, k_ymax, k_ymax_set, k_above, k_far;
static double k_minlen;
static float vzero[3];
static bool consts_loaded;

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static void load_consts(void)
{
    if (consts_loaded) return;
    k_zero = exe_f32(0x5b23f8);
    k_quarter = exe_f32(0x5b2468);
    k_third = exe_f32(0x5b2680);
    k_half = exe_f32(0x5b23f0);
    k_one = exe_f32(0x5b24a8);
    k_edgemax = exe_f32(0x5b242c);
    k_flat = exe_f32(0x5b2418);
    k_ymax = exe_f32(0x5b3e0c);
    uint32_t y = 0x3f7ff972u;
    memcpy(&k_ymax_set, &y, 4);
    k_above = exe_f32(0x5b38ec);
    uint32_t far = 0x7e967699u;
    memcpy(&k_far, &far, 4);
    const uint8_t *d = exe_data(0x5b2bc0, 8), *z = exe_data(0x5bb2b0, sizeof vzero);
    if (d) memcpy(&k_minlen, d, 8);
    if (z) memcpy(vzero, z, sizeof vzero);
    consts_loaded = exe_loaded();
}

/* A float's bits as the int the polygons' side tests compare (DAT_006affa8). */
static int32_t fbits(float f)
{
    int32_t i;
    memcpy(&i, &f, 4);
    return i;
}

/* ---- vector helpers of the maths module ---- */

static void cross(const float a[3], const float b[3], float out[3])   /* 0x5325f0 (0x56e6a5) */
{
    float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

static double length(const float v[3])   /* 0x532890 */
{
    return sqrt((double)v[1] * v[1] + (double)v[0] * v[0] + (double)v[2] * v[2]);
}

static void normalize(const float v[3], float out[3])   /* 0x532640 */
{
    double r = (double)k_one / length(v);
    out[0] = (float)(r * v[0]);
    out[1] = (float)(v[1] * r);
    out[2] = (float)(v[2] * r);
}

/* The height of the plane through v with normal n at p's (x, z). 0x489f90 */
static double plane_height(const float n[3], const float v[3], const float p[3])
{
    return -((double)n[0] * p[0] + (double)p[2] * n[2] - ((double)n[0] * v[0] + (double)v[2] * n[2] + (double)v[1] * n[1])) / n[1];
}

/* How far the quad's fourth vertex is from the plane of the first three. 0x489fd0 (the normal 0x489ec0) */
static float quad_warp(const float *const v[4])
{
    float e1[3] = {v[1][0] - v[0][0], v[1][1] - v[0][1], v[1][2] - v[0][2]};
    float e2[3] = {v[0][0] - v[2][0], v[0][1] - v[2][1], v[0][2] - v[2][2]};
    float n[3];
    cross(e1, e2, n);
    double len = length(n);
    bool clamp = true;
    if (k_minlen <= len) {
        double r = (double)k_one / len;
        n[0] = (float)(n[0] * r);
        n[1] = (float)(r * n[1]);
        n[2] = (float)(n[2] * r);
        clamp = !(n[1] < k_ymax);
    }
    if (clamp) n[1] = 0.9999f;
    double d = (double)v[3][1] - plane_height(n, v[0], v[3]);
    if (d <= k_zero) d = -d;
    return (float)d;
}

/* The 2D side of p against the edge a -> b (> 0 left): the polygons' containment tests. */
static float side(const float *a, const float *b, const float p[3])
{
    return (p[2] - b[2]) * (a[0] - b[0]) - (p[0] - b[0]) * (a[2] - b[2]);
}

/* ---- the elements ---- */

static void elem_free(WTreeElem *e) { free(e); }   /* the slot 0s (0x4854e0 etc.: back to a pool) */

/* The polygon's vertices in order; its centre is on every edge's non-positive side (clockwise) or on
   every edge's non-negative side. Slot 3 of Quad 0x4856f0 and Tri 0x485fe0. */
static bool poly_contains(const WTreeElem *e, int n, const float p[3])
{
    const float *const *v = e->u.poly.v;
    for (int i = 0; i < n; i++) {
        int32_t s = fbits(side(v[i], v[(i + 1) % n], p));
        if ((uint8_t)e->w8 ? s >= 1 : s < 0) return false;
    }
    return true;
}

static void poly_bounds(const WTreeElem *e, int n, float mn[3], float mx[3])
{
    const float *const *v = e->u.poly.v;
    /* x: min of the first two, then the rest; the same for the maximum, then z. 0x4855a0 / 0x485df0 */
    float m = v[1][0] <= v[0][0] ? v[1][0] : v[0][0];
    for (int i = 2; i < n; i++)
        if (v[i][0] <= m) m = v[i][0];
    mn[0] = m;
    m = v[0][0] <= v[1][0] ? v[1][0] : v[0][0];
    for (int i = 2; i < n; i++)
        if (m <= v[i][0]) m = v[i][0];
    mx[0] = m;
    m = v[1][2] <= v[0][2] ? v[1][2] : v[0][2];
    for (int i = 2; i < n; i++)
        if (v[i][2] <= m) m = v[i][2];
    mn[2] = m;
    m = v[0][2] <= v[1][2] ? v[1][2] : v[0][2];
    for (int i = 2; i < n; i++)
        if (!(v[i][2] < m)) m = v[i][2];
    mx[2] = m;
}

/* The first edge the segment crosses (in the ground plane), -1 for none; the edges as the original
   pairs them: Tri (v0 v1) (v2 v1) (v2 v0), Quad (v0 v1) (v2 v1) (v2 v3) (v0 v3). Slot 9 (0x485960 /
   0x4861c0) */
static int poly_crossed(const WTreeElem *e, int n, const float seg[6])
{
    const float *const *v = e->u.poly.v;
    static const int tri[3][2] = {{0, 1}, {2, 1}, {2, 0}}, quad[4][2] = {{0, 1}, {2, 1}, {2, 3}, {0, 3}};
    for (int i = 0; i < n; i++) {
        const float *a = v[n == 3 ? tri[i][0] : quad[i][0]], *b = v[n == 3 ? tri[i][1] : quad[i][1]];
        float edge[6] = {a[0], a[1], a[2], b[0], b[1], b[2]};
        if (World_SegmentsCross(seg, edge, NULL)) return i;
    }
    return -1;
}

static bool poly_crosses(const WTreeElem *e, const float seg[6]) { return e->vt->crossed_side(e, seg) >= 0; }   /* 0x486290 */
static const float *const *poly_vertices(const WTreeElem *e) { return e->u.poly.v; }                         /* 0x485d20 */

static void quad_centre(const WTreeElem *e, float out[3])   /* 0x485530 */
{
    const float *const *v = e->u.poly.v;
    for (int k = 0; k < 3; k++) out[k] = (v[2][k] + v[1][k] + v[0][k] + v[3][k]) * k_quarter;
}
static void quad_bounds(const WTreeElem *e, float mn[3], float mx[3]) { poly_bounds(e, 4, mn, mx); }
static bool quad_contains(const WTreeElem *e, const float p[3]) { return poly_contains(e, 4, p); }
static int quad_crossed(const WTreeElem *e, const float seg[6]) { return poly_crossed(e, 4, seg); }
static int quad_count(const WTreeElem *e) { (void)e; return 4; }   /* 0x44a010 */
static const float *quad_vertex(const WTreeElem *e, int i) { return e->u.poly.v[i > 3 ? i % 4 : i]; }   /* 0x485a60 */
static uint16_t quad_colour(const WTreeElem *e, int i) { return e->u.poly.colour[i > 3 ? i % 4 : i]; }  /* 0x485a80 */

/* The normal of the quad's two mean edge directions (v1 - v0 with v2 - v3, v1 - v2 with v0 - v3), in the
   order of its winding; y held below 0.9999. 0x485aa0 */
static void quad_normal(const WTreeElem *e, float out[3])
{
    const float *a = e->u.poly.v[0], *b = e->u.poly.v[1], *c = e->u.poly.v[2], *d = e->u.poly.v[3];
    float u[3] = {((b[0] - a[0]) + (c[0] - d[0])) * k_half, ((b[1] - a[1]) + (c[1] - d[1])) * k_half,
                  ((b[2] - a[2]) + (c[2] - d[2])) * k_half};
    float w[3] = {((b[0] - c[0]) + (a[0] - d[0])) * k_half, ((a[1] - d[1]) + (b[1] - c[1])) * k_half,
                  ((a[2] - d[2]) + (b[2] - c[2])) * k_half};
    float n[3];
    if ((uint8_t)e->w8) cross(u, w, n);
    else cross(w, u, n);
    normalize(n, out);
    if (k_ymax <= out[1]) out[1] = k_ymax_set;
}

static void tri_centre(const WTreeElem *e, float out[3])   /* 0x485d90 */
{
    const float *const *v = e->u.poly.v;
    for (int k = 0; k < 3; k++) out[k] = (v[1][k] + v[0][k] + v[2][k]) * k_third;
}
static void tri_bounds(const WTreeElem *e, float mn[3], float mx[3]) { poly_bounds(e, 3, mn, mx); }
static bool tri_contains(const WTreeElem *e, const float p[3]) { return poly_contains(e, 3, p); }
static int tri_crossed(const WTreeElem *e, const float seg[6]) { return poly_crossed(e, 3, seg); }
static int tri_count(const WTreeElem *e) { (void)e; return 3; }   /* 0x485d30 */
static const float *tri_vertex(const WTreeElem *e, int i) { return e->u.poly.v[i > 2 ? i % 3 : i]; }   /* 0x485f80 */
static uint16_t tri_colour(const WTreeElem *e, int i) { return e->u.poly.colour[i > 2 ? i % 3 : i]; }  /* 0x485fb0 */

/* The normal of v1 - v0 and v0 - v2 in the order of its winding. 0x485ee0 */
static void tri_normal_slot(const WTreeElem *e, float out[3])
{
    const float *a = e->u.poly.v[0], *b = e->u.poly.v[1], *c = e->u.poly.v[2];
    float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, w[3] = {a[0] - c[0], a[1] - c[1], a[2] - c[2]};
    float n[3];
    if ((uint8_t)e->w8) cross(u, w, n);
    else cross(w, u, n);
    normalize(n, out);
    if (k_ymax <= out[1]) out[1] = k_ymax_set;
}

static const WTreeElemVtbl quad_vt = {elem_free, quad_centre, quad_bounds, quad_contains, poly_crosses, quad_normal,
                                      quad_vertex, poly_vertices, quad_count, quad_crossed, quad_colour};   /* 0x5b4880 */
static const WTreeElemVtbl tri_vt = {elem_free, tri_centre, tri_bounds, tri_contains, poly_crosses, tri_normal_slot,
                                     tri_vertex, poly_vertices, tri_count, tri_crossed, tri_colour};       /* 0x5b48b0 */

/* The winding: the centre on no edge's positive side (the float's bits as an int below 1). */
static uint8_t winding(const float *const *v, int n, const float c[3])
{
    for (int i = 0; i < n; i++)
        if (fbits(side(v[i], v[(i + 1) % n], c)) >= 1) return 0;
    return 1;
}

WTreeElem *WTreeQuad_new(const float *const pts[4], const uint16_t colours[4], uint16_t flags)
{
    load_consts();
    WTreeElem *e = calloc(1, sizeof *e);
    if (!e) return NULL;
    e->vt = &quad_vt;
    for (int i = 0; i < 4; i++) {
        e->u.poly.v[i] = pts[i];
        e->u.poly.colour[i] = colours[i];
    }
    e->kind = WTREE_POLY;
    e->flags = flags;
    float c[3];
    quad_centre(e, c);
    e->w8 = winding(e->u.poly.v, 4, c);
    return e;
}

WTreeElem *WTreeTri_new(const float *const pts[3], const uint16_t colours[3], uint16_t flags)
{
    load_consts();
    WTreeElem *e = calloc(1, sizeof *e);
    if (!e) return NULL;
    e->vt = &tri_vt;
    for (int i = 0; i < 3; i++) {
        e->u.poly.v[i] = pts[i];
        e->u.poly.colour[i] = colours[i];
    }
    e->kind = WTREE_POLY;
    e->flags = flags;
    /* The centre in x and z only (0x485ba0 computes it inline). */
    float c[3] = {(pts[1][0] + pts[2][0] + pts[0][0]) * k_third, 0,
                  (pts[0][2] + pts[1][2] + pts[2][2]) * k_third};
    e->w8 = winding(e->u.poly.v, 3, c);
    return e;
}

/* ---- the edges (0x5b484c) ---- */

static void edge_centre(const WTreeElem *e, float out[3])   /* 0x485220 */
{
    for (int k = 0; k < 3; k++) out[k] = (e->u.edge.p[1][k] + e->u.edge.p[0][k]) * k_half;
}

/* x and z only (the y of the bounds is left as it is). 0x4852b0 */
static void edge_bounds(const WTreeElem *e, float mn[3], float mx[3])
{
    const float *a = e->u.edge.p[0], *b = e->u.edge.p[1];
    mn[0] = b[0] <= a[0] ? b[0] : a[0];
    mx[0] = a[0] <= b[0] ? b[0] : a[0];
    mn[2] = b[2] <= a[2] ? b[2] : a[2];
    mx[2] = b[2] < a[2] ? a[2] : b[2];
}

static bool edge_contains(const WTreeElem *e, const float p[3]) { (void)e; (void)p; return false; }   /* 0x5076c0 */

static bool edge_crosses(const WTreeElem *e, const float seg[6])   /* 0x485260 */
{
    const float *a = e->u.edge.p[0], *b = e->u.edge.p[1];
    float edge[6] = {a[0], a[1], a[2], b[0], b[1], b[2]};
    return World_SegmentsCross(seg, edge, NULL);
}

static const WTreeElemVtbl edge_vt = {elem_free, edge_centre, edge_bounds, edge_contains, edge_crosses,
                                      NULL, NULL, NULL, NULL, NULL, NULL};   /* slot 5 is WTreeEdge_point */

const float *WTreeEdge_point(const WTreeElem *e, int i) { return e->u.edge.p[i]; }

WTreeElem *WTreeEdge_new(const float pts[8], uint16_t flags)
{
    load_consts();
    WTreeElem *e = calloc(1, sizeof *e);
    if (!e) return NULL;
    e->vt = &edge_vt;
    memcpy(e->u.edge.p[0], pts, 4 * sizeof(float));
    memcpy(e->u.edge.p[1], pts + 4, 4 * sizeof(float));
    float *a = e->u.edge.p[0], *b = e->u.edge.p[1];
    float x0 = b[0] <= a[0] ? b[0] : a[0], x1 = a[0] <= b[0] ? b[0] : a[0];
    float z0 = b[2] <= a[2] ? b[2] : a[2], z1 = a[2] <= b[2] ? b[2] : a[2];
    if (k_edgemax < x1 - x0 || k_edgemax < z1 - z0) {
        /* A bogus long edge: a unit edge along x at the second point. */
        float x = b[0] + k_one;
        memcpy(a, pts + 4, 4 * sizeof(float));
        b[0] = x;
    }
    e->kind = WTREE_EDGE;
    e->w8 = flags;
    return e;
}

/* ---- the tree ---- */

void WTreeElems_push(WTreeElems *v, WTreeElem *e)
{
    if (v->n == v->cap) {
        uint32_t c = v->cap ? v->cap * 2 : 64;
        WTreeElem **ne = realloc(v->e, c * sizeof *ne);
        if (!ne) return;
        v->e = ne;
        v->cap = c;
    }
    v->e[v->n++] = e;
}

void WTreeElems_free(WTreeElems *v)
{
    if (!v) return;   /* (the queries' NULL: no tree, or nothing found) */
    free(v->e);
    free(v);
}

static WTreeCell *cell_new(uint32_t id)
{
    WTreeCell *c = calloc(1, sizeof *c);
    if (c) c->id = id;
    return c;
}

/* The child id: one level down, x and z doubled, plus one in z for codes 0 and 1, in x for 1 and 3.
   0x4839b0 */
static WTreeCell *child_new(uint32_t id, int code)
{
    uint32_t level = (id >> 28) + 1, x = (id & 0x3fff) << 1 & 0x3fff, z = (id >> 14 & 0x3fff) << 1 & 0x3fff;
    if (code == 0 || code == 1) z = (z + 1) & 0x3fff;
    if (code == 1 || code == 3) x = (x + 1) & 0x3fff;
    return cell_new(level << 28 | z << 14 | x);
}

void WTree_CellBounds(const WTreeCell *c, float *x0, float *z0, float *size)
{
    float s = level_size[c->id >> 28];
    *x0 = (float)(c->id & 0x3fff) * s - origin;
    *z0 = (float)(c->id >> 14 & 0x3fff) * s - origin;
    *size = s;
}

static bool is_leaf(const WTreeCell *c) { return !c->child[0] && !c->child[1] && !c->child[2] && !c->child[3]; }

WTree *WTree_new(float half, int levels)
{
    load_consts();
    WTree *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    level_size[0] = half + half;
    origin = half;
    for (int i = 1; i < levels && i < 16; i++) level_size[i] = level_size[i - 1] * k_half;
    t->root = cell_new(0);
    t->all = calloc(1, sizeof *t->all);
    return t;
}

static void cell_free(WTreeCell *c)   /* 0x483a80 */
{
    for (int i = 0; i < 4; i++)
        if (c->child[i]) cell_free(c->child[i]);
    if (c->list) {
        free(c->list->e);
        free(c->list);
    }
    free(c);
}

void WTree_delete(WTree *t)
{
    if (!t) return;
    if (t->all) {
        for (uint32_t i = 0; i < t->all->n; i++)
            if (t->all->e[i]) t->all->e[i]->vt->dtor(t->all->e[i]);
        WTreeElems_free(t->all);
    }
    if (t->root) cell_free(t->root);
    if (g_wtree == t) g_wtree = NULL;
    free(t);
}

/* The element touches the square [x0, x0 + s] x [z0, z0 + s]: its centre in it, or its bounds over it
   (all comparisons closed). The children's tests of 0x4848c0 and 0x483bd0. */
static bool touches(const WTreeElem *e, float x0, float z0, float s)
{
    float c[3], mn[3], mx[3];
    e->vt->centre(e, c);
    if (x0 <= c[0] && c[0] <= x0 + s && z0 <= c[2] && c[2] <= z0 + s) return true;
    e->vt->bounds(e, mn, mx);
    return mn[0] <= x0 + s && mn[2] <= z0 + s && x0 <= mx[0] && z0 <= mx[2];
}

/* The cell's own test: the centre in its closed square, or bounds whose minimum is strictly inside the
   far sides. 0x484100 */
static bool cell_touches(const WTreeCell *cell, const WTreeElem *e)
{
    float x0, z0, s, c[3], mn[3], mx[3];
    WTree_CellBounds(cell, &x0, &z0, &s);
    e->vt->centre(e, c);
    if (x0 <= c[0] && c[0] <= x0 + s && z0 <= c[2] && c[2] <= z0 + s) return true;
    e->vt->bounds(e, mn, mx);
    return mn[0] < x0 + s && mn[2] < z0 + s && x0 <= mx[0] && z0 <= mx[2];
}

static void insert(WTreeCell *c, WTreeElem *e);

/* The four children's squares in the order the original tries them: [2] low/low, [0] low x/high z,
   [1] high/high, [3] high x/low z. */
static void child_square(int code, float x0, float z0, float h, float *cx, float *cz)
{
    *cx = code == 1 || code == 3 ? x0 + h : x0;
    *cz = code == 0 || code == 1 ? z0 + h : z0;
}

static const int child_order[4] = {2, 0, 1, 3};

/* Hands a leaf's elements to new children (cells at level 12 stay leaves). 0x483bd0 */
static void split(WTreeCell *c)
{
    if ((c->id & 0xf0000000u) == 0xc0000000u) return;
    float x0, z0, s;
    WTree_CellBounds(c, &x0, &z0, &s);
    float h = s * k_half;
    WTreeList *l = c->list;
    c->list = NULL;
    for (uint32_t i = 0; l && i < l->n; i++) {
        WTreeElem *e = l->e[i].elem;
        for (int k = 0; k < 4; k++) {
            int code = child_order[k];
            float cx, cz;
            child_square(code, x0, z0, h, &cx, &cz);
            if (!touches(e, cx, cz, h)) continue;
            if (!c->child[code]) c->child[code] = child_new(c->id, code);
            if (c->child[code]) insert(c->child[code], e);
        }
    }
    if (l) {
        free(l->e);
        free(l);
    }
}

/* 0x4848c0: into the children it touches (the last one by iteration), or the leaf's list. */
static void insert(WTreeCell *c, WTreeElem *e)
{
    while (cell_touches(c, e)) {
        if (is_leaf(c)) {
            if (!c->list) {
                c->list = calloc(1, sizeof *c->list);
                if (!c->list) return;
            }
            WTreeList *l = c->list;
            if (l->n == l->cap) {
                uint32_t cap = l->cap ? l->cap * 2 : 50;
                WTreeEntry *ne = realloc(l->e, cap * sizeof *ne);
                if (!ne) return;
                l->e = ne;
                l->cap = cap;
            }
            l->e[l->n++] = (WTreeEntry){e->kind, e};
            if (l->n > 48) split(c);
            return;
        }
        float x0, z0, s;
        WTree_CellBounds(c, &x0, &z0, &s);
        float h = s * k_half;
        for (int k = 0; k < 3; k++) {
            int code = child_order[k];
            float cx, cz;
            child_square(code, x0, z0, h, &cx, &cz);
            if (!touches(e, cx, cz, h)) continue;
            if (!c->child[code]) c->child[code] = child_new(c->id, code);
            if (c->child[code]) insert(c->child[code], e);
        }
        float cx, cz;
        child_square(3, x0, z0, h, &cx, &cz);
        if (!touches(e, cx, cz, h)) return;
        if (!c->child[3]) c->child[3] = child_new(c->id, 3);
        if (!c->child[3]) return;
        c = c->child[3];
    }
}

void WTree_Add(WTree *t, WTreeElems *v)
{
    load_consts();
    for (uint32_t i = 0; i < v->n; i++) WTreeElems_push(t->all, v->e[i]);
    for (uint32_t i = 0; i < v->n; i++) insert(t->root, v->e[i]);
}

bool WTree_CellContains(const WTreeCell *c, const float p[3])
{
    float x0, z0, s;
    WTree_CellBounds(c, &x0, &z0, &s);
    return x0 <= p[0] && p[0] <= x0 + s && z0 <= p[2] && p[2] <= z0 + s;
}

WTreeCell *WTree_LeafAt(WTreeCell *root, const float p[3])
{
    load_consts();
    if (is_leaf(root)) return root;
    float s = level_size[root->id >> 28];
    float cx = ((float)(root->id & 0x3fff) + k_half) * s - origin;
    float cz = ((float)(root->id >> 14 & 0x3fff) + k_half) * s - origin;
    for (WTreeCell *c = root; c; s *= k_half) {
        if (c->list) return c;
        float q = s * k_quarter;
        if (cx <= p[0]) {
            cx += q;
            if (cz <= p[2]) {
                c = c->child[1];
                cz += q;
            } else {
                c = c->child[3];
                cz -= q;
            }
        } else {
            cx -= q;
            if (cz <= p[2]) {
                c = c->child[0];
                cz += q;
            } else {
                c = c->child[2];
                cz -= q;
            }
        }
    }
    return root;
}

static void cells_push(WTreeCells *v, WTreeCell *c)
{
    if (v->n == v->cap) {
        uint32_t cap = v->cap ? v->cap * 2 : 16;
        WTreeCell **nc = realloc(v->c, cap * sizeof *nc);
        if (!nc) return;
        v->c = nc;
        v->cap = cap;
    }
    v->c[v->n++] = c;
}

void WTree_LeavesNear(WTreeCell *c, const float p[3], float r, WTreeCells *out)
{
    load_consts();
    while (!is_leaf(c)) {
        float s = level_size[c->id >> 28];
        float cx = ((float)(c->id & 0x3fff) + k_half) * s - origin;
        float cz = ((float)(c->id >> 14 & 0x3fff) + k_half) * s - origin;
        if (p[0] - r < cx) {
            if (p[2] - r < cz && c->child[2]) WTree_LeavesNear(c->child[2], p, r, out);
            if (!(r + p[2] < cz) && c->child[0]) WTree_LeavesNear(c->child[0], p, r, out);
        }
        if (r + p[0] < cx) return;
        if (p[2] - r < cz && c->child[3]) WTree_LeavesNear(c->child[3], p, r, out);
        if (r + p[2] < cz) return;
        c = c->child[1];
        if (!c) return;
    }
    cells_push(out, c);
}

/* ---- PCollideArticleBundle ---- */

void *PCollideArticleBundle_new(struct W3DObjectBundle *b)
{
    PCollideArticleBundle *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->bundle = b;
    c->bundle2 = b;
    return c;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* The item kinds' ids (0x5e5084: vt nm uv df Base pr ef). */
static uint32_t kind_id(unsigned kind) { return exe_u32(0x5e5084 + 4u * kind); }
enum { K_VT = 0, K_DF = 3, K_BASE = 4, K_PR = 5 };

/* The primitive types' vertex counts (0x5e5138), index steps (0x5e514c) and count shifts (0x5e5160). */
static int prim_nverts(int type) { return type >= 0 && type < 5 ? (int32_t)exe_u32(0x5e5138 + 4u * (unsigned)type) : 0; }
static int prim_step(int type) { return type >= 0 && type < 5 ? (int32_t)exe_u32(0x5e514c + 4u * (unsigned)type) : 0; }

/* The primitives of a list of n indices. 0x5a0930 */
static uint32_t prim_count(int type, uint32_t n)
{
    int32_t shift = type >= 0 && type < 5 ? (int32_t)exe_u32(0x5e5160 + 4u * (unsigned)type) : -1;
    uint32_t d = n - (uint32_t)prim_nverts(type);
    return shift >= 0 ? (d >> (shift & 0x1f)) + 1 : d / 3 + 1;
}

/* The nth stream of the list with this item id (by its kind: below 0x100 the table's id, else the
   kind as a two-character code), NULL if none. 0x5a0a00 */
static const uint8_t *prim_stream(const uint8_t *pr, uint32_t id, int nth)
{
    uint32_t ns = rd32(pr + 0x28);
    const uint8_t *s = pr + 0x30;
    for (; ns; ns--, s += 0x10) {
        uint16_t kind = rd16(s + 10);
        uint32_t sid = kind < 0x100 ? exe_u32(0x5e5084 + 4u * kind) : (uint32_t)kind << 16 | 0x2020;
        if (sid == id && nth-- == 0) return s;
    }
    return NULL;
}

/* The vertex indices of primitive k: from base, consecutive when the stream has no index list (0xffff),
   else the list's bytes. 0x5a0970 */
static void prim_indices(const uint8_t *pr, int type, uint32_t k, int *out, int base, uint16_t list)
{
    int nv = prim_nverts(type), step = prim_step(type);
    if (list == 0xffff) {
        for (int j = 0; j < nv; j++) out[j] = base + step * (int)k + j;
        return;
    }
    uint32_t ns = rd32(pr + 0x28), nl = rd32(pr + 0x2c);
    const uint8_t *area = pr + (nl + 6 + ns * 2) * 8;
    const uint8_t *b = area + rd32(pr + 0x30 + 0x10 * ns + 8u * list + 4) + (size_t)step * k;
    for (int j = 0; j < nv; j++) out[j] = b[j] + base;
}

static uint16_t to555(uint32_t c)
{
    return (uint16_t)((((c >> 16 & 0xff) >> 3) * 0x20 + ((c >> 8 & 0xff) >> 3)) * 0x20 + ((c & 0xff) >> 3));
}

static const void *item_exact(UData *article, uint32_t key, uint16_t index)
{
    UData *r = UData_find(article, (key & 0xffff0000u) | index);
    return r == UData_itemsend(article) ? NULL : UData_data(r);
}

/* The polygons of one 'pr' list. */
static void build_list(const uint8_t *pr, uint32_t count, uint32_t flags, const uint8_t *vt, const uint8_t *df,
                       WTreeElems *v)
{
    int type = (int16_t)rd32(pr);
    int nv = prim_nverts(type);
    if (nv < 3 || nv > 4) return;   /* (line lists would read stale vertices) */
    uint32_t n = prim_count(type, count);
    const uint8_t *svt = prim_stream(pr, kind_id(K_VT), 0), *sdf = prim_stream(pr, kind_id(K_DF), 0);
    int ivt[4] = {0}, idf[4] = {0};
    for (uint32_t k = 0; k < n; k++) {
        if (svt) prim_indices(pr, type, k, ivt, (int)(rd32(svt + 4) >> 4), rd16(svt + 0xe));
        if (sdf) prim_indices(pr, type, k, idf, (int)(rd32(sdf + 4) >> 2), rd16(sdf + 0xe));
        const float *p[4];
        uint16_t col[4];
        for (int j = 0; j < nv; j++) {
            p[j] = (const float *)(const void *)(vt + (size_t)ivt[j] * 16);
            col[j] = df ? to555(rd32(df + (size_t)idf[j] * 4)) : 0;
        }
        if (p[0][0] == p[1][0] && p[0][0] == p[2][0] && p[0][2] == p[1][2] && p[0][2] == p[2][2]) continue;
        if (nv == 4) {
            if (flags == 0 || quad_warp(p) <= k_flat) {
                WTreeElems_push(v, WTreeQuad_new(p, col, (uint16_t)flags));
                continue;
            }
            /* A warped quad: (v0, v1, v2) and (v0, v2, v3). */
            WTreeElems_push(v, WTreeTri_new(p, col, (uint16_t)flags));
            p[1] = p[0];
            col[1] = col[0];
            WTreeElems_push(v, WTreeTri_new(p + 1, col + 1, (uint16_t)flags));
            continue;
        }
        bool same01 = p[0][0] == p[1][0] && p[0][1] == p[1][1] && p[0][2] == p[1][2];
        bool same02 = p[0][0] == p[2][0] && p[0][1] == p[2][1] && p[0][2] == p[2][2];
        bool same12 = p[1][0] == p[2][0] && p[1][1] == p[2][1] && p[1][2] == p[2][2];
        if (same01 || same02 || same12) continue;
        WTreeElems_push(v, WTreeTri_new(p, col, (uint16_t)flags));
    }
}

void PCollideArticleBundle_Build(PCollideArticleBundle *c, WTreeElems *v, bool only_flagged)
{
    load_consts();
    W3DObjectBundle *b = c->bundle;
    R3DMaterialSet *ms = b->world;
    for (uint32_t i = 0; i < b->count; i++) {
        UData *article = b->entries[i].article;
        UData *end = UData_itemsend(article), *rb = UData_find(article, kind_id(K_BASE));
        const uint8_t *base = rb == end ? NULL : UData_data(rb);
        if (!base) continue;   /* (the original reads a NULL Base) */
        if (base[0] & 0x40) continue;
        const uint8_t *e = base + rd32(base + 4) * 12 + 100;   /* the collision level of detail */
        const WObjectDef *def = WObjectDefs_find(rd32(base + 0x44));   /* 0x47ec50 */
        if (def && def->collidetype == 1) {
            /* The definition's radius goes in as an integer, the cylinder's own is 0.25 (as 0x475040
               passes them). */
            WTreeElems_push(v, WTreeCylinder_new(b, (int)i, def->drivethru, (int32_t)def->radius, k_quarter));
            continue;
        }
        if (def && def->collidetype == 2) {
            int nh;
            float rect[16], corners[4][3];
            WTree_ArticleRect(article, &nh, rect);   /* 0x474c40 */
            if (b->transformed && b->vt->matrix) {   /* (the entry's matrix, slot 5) */
                EaglMatrix m;
                b->vt->matrix(b, (int)i, m.m);
                EAGL_MatTransform4(&m, 4, rect, 16, rect, 16);   /* 0x539cc0 */
            }
            for (int k = 0; k < 4; k++) memcpy(corners[k], rect + 4 * k, 12);
            WTreeElems_push(v, WTreeBox_new(b, (int)i, corners, def->drivethru));
            continue;
        }
        if (!e[0]) continue;
        uint16_t lo = rd16(e + 4), hi = rd16(e + 6), vi = rd16(e + 8);
        /* 0x43cc80 then 0x450fc0: the 'pr' records with an index in [lo, hi). */
        uint32_t prk = kind_id(K_PR);
        for (UData *r = UData_finditem(article, prk, lo, hi); r && r != end;) {
            const uint8_t *pr = UData_data(r);
            uint32_t mi = rd32(pr + 4) & 0xffff;
            const R3DMaterial *m = ms && mi < ms->count ? ms->mat[mi] : NULL;
            uint32_t flags = m && m->mt ? rd32(m->mt) : 0;
            if (!only_flagged || (flags & 0xffffff0fu)) {
                const uint8_t *vt = item_exact(article, kind_id(K_VT), vi), *df = item_exact(article, kind_id(K_DF), vi);
                if (vt) build_list(pr, r->count, flags, vt, df, v);
            }
            r++;
            if (r == end || UData_key(r) != prk || !(r->info & UDATA_INDEXED) || (r->id & 0xffff) >= hi) break;
        }
    }
}


/* ---- the collision objects (0x4862b0-0x487000) ---- */

static void obj_free(WTreeElem *e) { free(e); }   /* 0x486660, 0x486cf0 */

static void obj_centre(const WTreeElem *e, float out[3])   /* 0x486400 */
{
    const WTreeObject *o = (const WTreeObject *)e;
    for (int k = 0; k < 3; k++) out[k] = (o->bmax[k] + o->bmin[k]) * k_half;
}

static void obj_bounds(const WTreeElem *e, float mn[3], float mx[3])   /* 0x4864d0 */
{
    const WTreeObject *o = (const WTreeObject *)e;
    memcpy(mn, o->bmin, 12);
    memcpy(mx, o->bmax, 12);
}

static bool obj_contains(const WTreeElem *e, const float p[3])   /* 0x486440: in the box's x, z */
{
    const WTreeObject *o = (const WTreeObject *)e;
    return p[0] >= o->bmin[0] && p[0] <= o->bmax[0] && p[2] >= o->bmin[2] && p[2] <= o->bmax[2];
}

/* The segment crosses one of the box's four sides in the ground plane (min-z, max-x, max-z, min-x
   order of 0x486510's corner pairs). */
static bool obj_crosses(const WTreeElem *e, const float seg[6])   /* 0x486510 */
{
    const WTreeObject *o = (const WTreeObject *)e;
    float side[6] = {o->bmin[0], o->bmin[1], o->bmin[2], o->bmin[0], o->bmin[1], o->bmax[2]};
    if (World_SegmentsCross(seg, side, NULL)) return true;
    side[0] = o->bmax[0], side[1] = o->bmax[1], side[2] = o->bmax[2];
    if (World_SegmentsCross(seg, side, NULL)) return true;
    side[4] = o->bmin[1], side[5] = o->bmin[2], side[3] = o->bmax[0];
    if (World_SegmentsCross(seg, side, NULL)) return true;
    side[0] = o->bmin[0], side[1] = o->bmin[1], side[2] = o->bmin[2];
    return World_SegmentsCross(seg, side, NULL);
}

static const WTreeElemVtbl obj_vt = {obj_free, obj_centre, obj_bounds, obj_contains, obj_crosses,
                                     NULL, NULL, NULL, NULL, NULL, NULL};

/* Slot 8 (0x486b90): an entry of a bundle with knockable objects (its +0x14, 0x489820) answers from
   them (0x476760; the definition's drive-through flag unread there); else solid, or drive-through with
   the flag. */
static int obj_hitkind(const WTreeObject *o, const float vel[3])
{
    if (o->bundle && o->bundle->smack) return WAnimatable_HitKind(o->bundle->smack, o->entry, vel);
    return (o->drivethru != 0) + 2;
}

static void obj_centre(const WTreeElem *e, float out[3]);

static float cyl_radius(const WTreeObject *o) { return o->radius; }   /* 0x486650 */
static float box_radius(const WTreeObject *o) { return o->radius; }   /* 0x486ce0 */

/* The ground-plane line (x0, z0) .. (x1, z1) against the circle (cx, cz, r) (0x489d20): its two
   crossings' parameters (t0 the nearer root), true when either is within 0 .. 1 (none when the line
   misses: the root of a negative is NaN, failing both tests). */
static bool line_circle(float x0, float z0, float x1, float z1, float cx, float cz, float r, float *t0, float *t1)
{
    double dx = (double)x1 - x0, dz = (double)z1 - z0;
    double a = dx * dx + dz * dz;
    double b = ((double)x0 - cx) * dx + ((double)z0 - cz) * dz;
    b += b;
    double pc = (double)z0 * cz + (double)x0 * cx;
    double c = ((double)cz * cz + (double)cx * cx + (double)z0 * z0 + (double)x0 * x0) - (pc + pc) - (double)r * r;
    double q = sqrt(b * b - c * a * 4.0), a2 = a + a;
    *t0 = (float)((-b - q) / a2);
    double u = (q - b) / a2;
    *t1 = (float)u;
    if (*t0 >= k_zero && *t0 <= k_one) return true;
    return u >= (double)k_zero && u <= (double)k_one;
}

/* The segment against the cylinder in the ground plane (slot 6, 0x4866b0): both crossings within the
   segment, the normal from the centre to their middle and the point on the circle there (2); else the
   one crossing (the nearer when it is within, else the farther), the normal to it from the centre and
   the crossing itself (1); 0 when the segment misses. */
static int cyl_collide(const WTreeObject *o, const float seg[6], float n[3], float at[3])
{
    float c[3], t0, t1;
    obj_centre(&o->e, c);
    if (!line_circle(seg[0], seg[2], seg[3], seg[5], c[0], c[2], o->radius, &t0, &t1)) return 0;
    float d[3] = {seg[3] - seg[0], seg[4] - seg[1], seg[5] - seg[2]};   /* 0x5320a0 */
    float t = t1;
    if (k_zero <= t0) {
        if (t0 <= k_one && k_zero <= t1 && t1 <= k_one) {
            float m[3] = {(d[0] * t1 + seg[0] + d[0] * t0 + seg[0]) * k_half, k_zero, (d[2] * t1 + seg[2] + d[2] * t0 + seg[2]) * k_half};
            n[0] = m[0] - c[0], n[1] = 0, n[2] = m[2] - c[2];
            normalize(n, n);
            float p[3] = {n[0] * o->radius + c[0], n[1] * o->radius + c[1], n[2] * o->radius + c[2]};   /* 0x532070 */
            if (at) memcpy(at, p, sizeof p);
            return 2;
        }
        if (t0 <= k_one) t = t0;
    }
    float p[3] = {d[0] * t + seg[0], k_zero, d[2] * t + seg[2]};
    n[0] = p[0] - c[0], n[1] = 0, n[2] = p[2] - c[2];
    normalize(n, n);
    if (at) memcpy(at, p, sizeof p);
    return 1;
}

/* The segment against the box's four sides in the ground plane: the number crossed; the first crossed
   side's normal, or with a second the corner between them (at) and the normal across the segment.
   0x486d40 */
static int box_collide(const WTreeObject *o, const float seg[6], float n[3], float at[3])
{
    int crossed = 0;
    float first[6] = {0};
    n[0] = n[1] = n[2] = 0;
    for (int i = 0; i < 4; i++) {
        const float *a = o->corner[i], *b = o->corner[(i + 1) & 3];
        float side[6] = {a[0], a[1], a[2], b[0], b[1], b[2]};
        if (!World_SegmentsCross(side, seg, at)) continue;   /* 0x489bf0 */
        crossed++;
        float d[3] = {b[0] - a[0], 0, b[2] - a[2]};
        normalize(d, d);
        if (n[0] == k_zero && n[1] == k_zero && n[2] == k_zero) {
            memcpy(first, side, sizeof first);
            n[0] = -d[2];
            n[1] = 0;
            n[2] = d[0];
        } else {
            World_SegmentsCross(side, first, at);
            float v[3] = {seg[3] - seg[0], seg[4] - seg[1], seg[5] - seg[2]};
            normalize(v, v);
            n[2] = -v[0];
            n[0] = v[2];
            n[1] = 0;
        }
    }
    return crossed;
}

static const WTreeObjectVtbl cyl_ovt = {cyl_radius, cyl_collide, obj_hitkind};
static const WTreeObjectVtbl box_ovt = {box_radius, box_collide, obj_hitkind};

/* The bounds of the entry's box corners (the bundle's slot 2, 0x4892d0: moved by the entry's matrix for
   the transformed bundles). */
static void obj_init(WTreeObject *o, W3DObjectBundle *b, int entry, int32_t drivethru, int32_t f20)   /* 0x4862b0 */
{
    o->e.vt = &obj_vt;
    o->e.kind = WTREE_OBJECT;
    o->f20 = f20;
    o->drivethru = drivethru;
    o->bundle = b;
    o->f2c = 1;
    o->entry = (uint16_t)entry;
    float c[8][4];
    W3DObjectBundle_Corners(b, entry, c);
    for (int j = 0; j < 3; j++) {
        o->bmin[j] = 1e9f;
        o->bmax[j] = -1e9f;
    }
    for (int k = 0; k < 8; k++)
        for (int j = 0; j < 3; j++) {
            if (c[k][j] < o->bmin[j]) o->bmin[j] = c[k][j];
            if (c[k][j] > o->bmax[j]) o->bmax[j] = c[k][j];
        }
}

WTreeElem *WTreeCylinder_new(W3DObjectBundle *b, int entry, int32_t drivethru, int32_t defradius, float radius)   /* 0x486600 */
{
    load_consts();
    WTreeObject *o = calloc(1, sizeof *o);
    if (!o) return NULL;
    obj_init(o, b, entry, drivethru, defradius);
    o->ovt = &cyl_ovt;
    o->shape = WOBJ_CYLINDER;
    o->radius = radius < k_quarter ? k_quarter : radius;
    return &o->e;
}

WTreeElem *WTreeBox_new(W3DObjectBundle *b, int entry, const float corners[4][3], int32_t drivethru)   /* 0x486bf0 */
{
    load_consts();
    WTreeObject *o = calloc(1, sizeof *o);
    if (!o) return NULL;
    obj_init(o, b, entry, drivethru, 0);
    o->ovt = &box_ovt;
    o->shape = WOBJ_BOX;
    /* Half the box's diagonal in the ground plane, times 0x5b3f90. */
    float hx = o->bmax[0] - (o->bmin[0] + o->bmax[0]) * k_half, hz = o->bmax[2] - (o->bmin[2] + o->bmax[2]) * k_half;
    o->radius = (float)(sqrt((double)hz * hz + (double)hx * hx) * (double)exe_f32(0x5b3f90));
    memcpy(o->corner, corners, sizeof o->corner);
    return &o->e;
}

/* The convex hull in the ground plane of n points (16 bytes each), into out (room for n + 2); points
   within sqrt(0x5c18e4) of their neighbour are dropped. Each new point outside replaces the run of
   edges it sees. Returns the hull's count. 0x5a0230 */
int WTree_Hull(int n, const float *p, float *out)
{
    load_consts();
    float eps = exe_f32(0x5c18e4);
    memcpy(out, p, 16);
    int h = 1, left = n - 1;
    if (left == 0) return 1;
    for (;;) {
        p += 4;
        float dx = p[0] - out[0], dz = p[2] - out[2];
        if (eps < dx * dx + dz * dz) {
            memcpy(out + 4, p, 16);
            h = 2;
        }
        if (--left == 0) return h;
        if (h != 1) break;
    }
    float *tmp = malloc((size_t)(n + 2) * 16);
    uint8_t *in = malloc((size_t)n + 2);
    if (!tmp || !in) {
        free(tmp);
        free(in);
        return h;
    }
    for (; left; left--) {
        p += 4;
        int outside = 0;
        for (int e = 0; e < h; e++) {
            const float *a = out + 4 * e, *b = out + 4 * ((e + 1) % h);
            in[e] = (p[0] - a[0]) * (b[2] - a[2]) - (p[2] - a[2]) * (b[0] - a[0]) >= k_zero;
            if (!in[e]) outside++;
        }
        if (!outside) continue;
        int s;
        if (!in[0]) {
            if (!in[h - 1]) {
                s = h;   /* back to the start of the run that wraps past the last edge */
                while (s > 0 && !in[s - 1]) s--;
            } else {
                s = 0;
            }
        } else {
            s = 1;
            while (in[s]) s++;
        }
        s += outside;
        memcpy(tmp, p, 16);
        int copy = h + 1 - outside;
        for (int j = 0, i = s; j < copy; j++, i++) memcpy(tmp + 4 * (j + 1), out + 4 * (i % h), 16);
        int nh = h + 2 - outside, w = 1;
        memcpy(out, tmp, 16);
        for (int r = 0; w < nh; r++) {
            const float *a = tmp + 4 * r, *b = tmp + 4 * (r + 1);
            float dx = a[0] - b[0], dz = a[2] - b[2];
            if (dx * dx + dz * dz <= eps) nh--;
            else memcpy(out + 4 * w++, b, 16);
        }
        h = nh;
    }
    free(tmp);
    free(in);
    return h;
}

void WTree_ArticleRect(UData *article, int *nhull, float c[16])
{
    load_consts();
    memset(c, 0, 16 * sizeof *c);
    *nhull = 0;
    UData *rb = UData_find(article, kind_id(K_BASE));
    if (rb == UData_itemsend(article)) return;
    const uint8_t *base = UData_data(rb);
    uint16_t vi = rd16(base + 0x6c + rd32(base + 4) * 12);   /* the collision LOD's vertices */
    UData *rv = UData_find(article, (kind_id(K_VT) & 0xffff0000u) | vi);
    if (rv == UData_itemsend(article)) return;
    int n = (int)rv->count;
    const float *vt = UData_data(rv);
    float *pts = malloc((size_t)(n + 2) * 16);
    if (!pts || n < 1) {
        free(pts);
        return;
    }
    int m = WTree_Hull(n, vt, pts);
    *nhull = m;
    /* The point of the farthest pair found first; the centre is that point (the original averages it
       with itself). */
    int far = 0;
    float best = 0;
    for (int i = 0; i < m; i++)
        for (int j = 0; j < m; j++) {
            float dz = pts[4 * i + 2] - pts[4 * j + 2], dx = pts[4 * i] - pts[4 * j];
            float d = dz * dz + dx * dx;
            if (best < d) {
                far = i;
                best = d;
            }
        }
    float cx = (pts[4 * far] + pts[4 * far]) * k_half, cy = (pts[4 * far + 1] + pts[4 * far + 1]) * k_half,
          cz = (pts[4 * far + 2] + pts[4 * far + 2]) * k_half;
    float *rel = malloc((size_t)m * 16), *rot = malloc((size_t)m * 16);
    if (!rel || !rot) {
        free(rel);
        free(rot);
        free(pts);
        return;
    }
    for (int i = 0; i < m; i++) {
        rel[4 * i] = pts[4 * i] - cx;
        rel[4 * i + 1] = pts[4 * i + 1] - cy;
        rel[4 * i + 2] = pts[4 * i + 2] - cz;
        rel[4 * i + 3] = 1.0f;
    }
    float least = 1e10f, angle = 0, x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    EaglMatrix mat;
    for (float a = 0; a < exe_f32(0x5b3288); a = a + exe_f32(0x5b2604)) {
        EAGL_MatAxisAngle(&mat, a, 0, 1.0f, 0);
        EAGL_MatTransform4(&mat, m, rel, 16, rot, 16);
        float mnx = 1e9f, mnz = 1e9f, mxx = -1e9f, mxz = exe_f32(0x5b4688);   /* 0x474b90 */
        for (int i = 0; i < m; i++) {
            const float *q = rot + 4 * i;
            if (q[0] < mnx) mnx = q[0];
            if (q[2] < mnz) mnz = q[2];
            if (mxx < q[0]) mxx = q[0];
            if (mxz < q[2]) mxz = q[2];
        }
        float area = (mxx - mnx) * (mxz - mnz);
        if (area < least) {
            angle = a;
            x0 = mnx, z0 = mnz, x1 = mxx, z1 = mxz;
            least = area;
        }
    }
    free(rel);
    free(rot);
    free(pts);
    float rect[16] = {x0, 0, z0, 1, x0, 0, z1, 1, x1, 0, z1, 1, x1, 0, z0, 1};
    EAGL_MatAxisAngle(&mat, -angle, 0, 1.0f, 0);
    EAGL_MatTransform4(&mat, 4, rect, 16, c, 16);
    for (int i = 0; i < 4; i++) {
        c[4 * i] = cx + c[4 * i];
        c[4 * i + 1] = 0;
        c[4 * i + 2] = cz + c[4 * i + 2];
    }
}

WTreeElems *WTree_ObjectsNear(const float p[3], float r)
{
    load_consts();
    WTree *t = g_world ? g_world->tree : NULL;
    if (!t) return NULL;
    WTreeElems *out = NULL;
    WTreeCells leaves = {0};
    WTree_LeavesNear(t->root, p, 3.0f, &leaves);
    for (uint32_t i = 0; i < leaves.n; i++) {
        WTreeList *l = leaves.c[i] ? leaves.c[i]->list : NULL;
        for (uint32_t k = 0; l && k < l->n; k++) {
            if (l->e[k].kind != WTREE_OBJECT) continue;
            const WTreeObject *o = (const WTreeObject *)l->e[k].elem;
            /* 0x486480: within r of the box in x and z */
            if (p[0] + r >= o->bmin[0] && p[0] - r <= o->bmax[0] && p[2] + r >= o->bmin[2] && p[2] - r <= o->bmax[2]) {
                if (!out) out = calloc(1, sizeof *out);
                if (out) WTreeElems_push(out, l->e[k].elem);
            }
        }
    }
    free(leaves.c);
    return out;
}

/* ---- the world's tree ---- */

/* <path>.edg: a 4-byte header, then 0x1c-byte records {flag byte (3 pad), p0, p1}. 0x487df0 */
static bool load_edges(const char *path, WTreeElems *v)
{
    size_t n = 0;
    uint8_t *raw = vfs_read_all(path, &n);
    if (!raw) return false;
    if (n >= 4) {
        uint32_t count = (uint32_t)((n - 4) / 0x1c);
        for (uint32_t i = 0; i < count; i++) {
            const uint8_t *rec = raw + 4 + 0x1c * i;
            float pts[8] = {0};
            memcpy(pts, rec + 4, 12);
            memcpy(pts + 4, rec + 0x10, 12);
            WTreeElems_push(v, WTreeEdge_new(pts, rec[0]));
        }
    }
    free(raw);
    return true;
}

/* 0x4883b0: the first vertex of the side polygon a shares with b (two of a's vertices within 0x5b4960 of
   two of b's in each axis; the second when they're the last and the first), -1 if they don't share one;
   0 for the same polygon. */
static int shared_side(const WTreeElem *a, const WTreeElem *b)
{
    if (a == b) return 0;
    float eps = exe_f32(0x5b4960);
    const float *const *va = a->vt->vertices(a), *const *vb = b->vt->vertices(b);
    int m[4], n = 0;
    for (int i = 0; i < a->vt->nvertices(a); i++)
        for (int j = 0; j < b->vt->nvertices(b); j++) {
            bool close = true;
            for (int k = 0; k < 3 && close; k++) {
                float d = va[i][k] - vb[j][k];
                if (d <= 0) d = -d;   /* (0x5b23f8) */
                close = d < eps;
            }
            if (close && n < 4) m[n++] = i;   /* (the original's four slots, unchecked) */
        }
    if (n != 2) return -1;
    return m[0] + 1 == m[1] ? m[0] : m[1];
}

/* 0x488500: the road's edges where there's no .edg. In each cell of a tree of every polygon, each road
   polygon (surface bits 0-3 set) gets an edge along each side it shares with a polygon off the road (bits
   0-3 clear), in its winding's order and with the other's surface as its flag; of two edges in a row with
   the same centre, the first one without a flag goes (else the second without one). */
static void edges_from_polygons(WTreeCell *c, WTreeElems *out)
{
    for (; c; c = c->child[3]) {
        WTreeList *l = c->list;
        for (uint32_t i = 0; l && i < l->n; i++) {
            WTreeElem *f = l->e[i].elem;
            if (!f || f->kind != WTREE_POLY || !(f->flags & 0xf)) continue;
            WTreeElem *made[16];   /* (the original's stack array; more would overrun it) */
            int n = 0;
            for (uint32_t j = 0; j < l->n && n < 16; j++) {
                WTreeElem *g = l->e[j].elem;
                if (!g || g->kind != WTREE_POLY || (g->flags & 0xf)) continue;
                int k = shared_side(f, g);
                if (k == -1) continue;
                const float *v0 = f->vt->vertex(f, k), *v1 = f->vt->vertex(f, k + 1);
                bool cw = (uint8_t)f->w8 != 0;
                float pts[8];
                memcpy(pts, cw ? v0 : v1, 4 * sizeof(float));
                memcpy(pts + 4, cw ? v1 : v0, 4 * sizeof(float));
                made[n++] = WTreeEdge_new(pts, g->flags);   /* 0x485070 */
            }
            for (int k = 0; k + 1 < n; k++) {
                WTreeElem *a = made[k], *b = made[k + 1];
                if (!a || !b) continue;
                float ca[3], cb[3];
                a->vt->centre(a, ca);
                b->vt->centre(b, cb);
                if (ca[0] != cb[0] || ca[1] != cb[1] || ca[2] != cb[2]) continue;
                if (a->w8 == 0) {
                    a->vt->dtor(a);
                    made[k] = NULL;
                } else if (b->w8 == 0) {
                    b->vt->dtor(b);
                    made[k + 1] = NULL;
                }
            }
            for (int k = 0; k < n; k++)
                if (made[k]) WTreeElems_push(out, made[k]);
        }
        edges_from_polygons(c->child[0], out);
        edges_from_polygons(c->child[1], out);
        edges_from_polygons(c->child[2], out);
    }
}

void Track_LoadEdges(void *world)
{
    BWorld *w = world;
    char path[300];
    snprintf(path, sizeof path, "%s.edg", w->name);
    WTreeElems *edges = calloc(1, sizeof *edges);
    if (!edges) return;
    if (!load_edges(path, edges)) {
        /* every bundle's polygons (not only the flagged ones) in a tree of their own */
        WTree *t = WTree_new(8192.0f, 15);   /* 0x4835e0 (0x46000000) */
        WTreeElems *all = calloc(1, sizeof *all);
        if (t && all) {
            for (int i = 0; i < w->nbundles; i++) {
                W3DObjectBundle *b = w->bundles[i];
                if (b && b->collide) PCollideArticleBundle_Build(b->collide, all, false);
            }
            WTree_Add(t, all);   /* 0x483870 */
            edges_from_polygons(t->root, edges);
        }
        WTreeElems_free(all);
        WTree_delete(t);
    }
    WTreeElems *polys = calloc(1, sizeof *polys);
    if (polys) {
        for (int i = 0; i < w->nbundles; i++) {
            W3DObjectBundle *b = w->bundles[i];
            if (b && b->collide) PCollideArticleBundle_Build(b->collide, polys, true);
        }
        if (w->tree) WTree_Add(w->tree, polys);
        WTreeElems_free(polys);
    }
    if (w->tree) WTree_Add(w->tree, edges);
    WTreeElems_free(edges);
}

/* ---- the surface tracker ---- */

static WTree *world_tree(void) { return g_world ? g_world->tree : NULL; }

WTreeElem *WSurface_FindBelow(WSurface *s, const float p[3])
{
    load_consts();
    WTree *t = world_tree();
    if (!s->cell || !WTree_CellContains(s->cell, p) || !s->cell->list) {
        if (!t) return NULL;
        s->cell = WTree_LeafAt(t->root, p);
    }
    WTreeList *l = s->cell ? s->cell->list : NULL;
    if (!l || !l->n) return NULL;
    WTreeElem *best = NULL;
    float bestd = k_far;
    for (uint32_t i = 0; i < l->n; i++) {
        WTreeElem *e = l->e[i].elem;
        if (!e->vt->contains(e, p) || l->e[i].kind != WTREE_POLY || !(e->flags & 0xf)) continue;
        float c[3];
        e->vt->centre(e, c);
        float d = c[1] - p[1];
        if (d <= k_zero) d = -d;
        if (d < bestd) {
            bestd = d;
            best = e;
        }
    }
    return best;
}

uint16_t WSurface_FindAbove(WSurface *s, const float p[3], WTreeElem **out)
{
    load_consts();
    WTree *t = world_tree();
    if (!s->cell || !WTree_CellContains(s->cell, p)) {
        if (!t) return 0;
        s->cell = WTree_LeafAt(t->root, p);
    }
    WTreeList *l = s->cell ? s->cell->list : NULL;
    if (!l || !l->n) return 0;
    WTreeElem *best = NULL;
    float bestd = k_far;
    for (uint32_t i = 0; i < l->n; i++) {
        WTreeElem *e = l->e[i].elem;
        if (!e->vt->contains(e, p) || l->e[i].kind != WTREE_POLY) continue;
        float c[3];
        e->vt->centre(e, c);
        float d = c[1] - p[1];
        if (k_above < d && d < bestd) {
            bestd = d;
            best = e;
        }
    }
    if (!best) return 0;
    if (out) *out = best;
    return best->flags;
}

void WSurface_Update(WSurface *s, const float p[3], bool keep)
{
    WTreeElem *old = s->elem;
    if (old && old->vt->contains(old, p) && keep) {
        s->changed = s->elem != old;
        return;
    }
    WTreeElem *e = WSurface_FindBelow(s, p);
    s->elem = e;
    s->normal_ok = 0;
    if (e) {
        s->none = 0;
        s->surface = e->flags;
        s->changed = e != old;
        return;
    }
    s->none = 1;
    s->surface = 0;
    s->changed = s->elem != old;
}

void WSurface_ComputeNormal(WSurface *s)
{
    load_consts();
    if (!s->elem || s->normal_ok) return;
    s->elem->vt->normal(s->elem, s->normal);
    const float *a = s->elem->vt->vertex(s->elem, 0), *b = s->elem->vt->vertex(s->elem, 1);
    float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]}, t[3], u[3];
    cross(d, s->normal, t);
    cross(t, s->normal, u);
    if (u[0] != k_zero || u[1] != k_zero || u[2] != k_zero) normalize(u, s->slope);
    s->normal_ok = 1;
}

const float *WSurface_Normal(WSurface *s)
{
    WSurface_ComputeNormal(s);
    return s->normal;
}

const float *WSurface_Slope(WSurface *s)
{
    WSurface_ComputeNormal(s);
    return s->slope;
}

const float *WSurface_Vertex(const WSurface *s, int i)
{
    load_consts();
    return s->elem ? s->elem->vt->vertex(s->elem, i) : vzero;
}

void WSurface_Init(WSurface *s)
{
    memset(s, 0, sizeof *s);
    s->f4 = -1;
    s->normal[1] = 1.0f;
    s->slope[2] = 1.0f;
}

void WSurface_Reset(WSurface *s, int16_t node)
{
    s->f4 = -1;
    s->node = node;
    s->surface = 0;
    s->normal_ok = 0;
    s->node_changed = 0;
    s->changed = 0;
    s->none = 0;
    s->cell = NULL;
    s->elem = NULL;
}

float WSurface_HeightAt(const float p[3])
{
    load_consts();
    WSurface s;
    WSurface_Init(&s);
    WSurface_Update(&s, p, true);
    if (s.none) return k_zero;
    const float *v = WSurface_Vertex(&s, 0);
    WSurface_ComputeNormal(&s);
    return (float)plane_height(s.normal, v, p);
}

bool WSurface_FindEdge(WSurface *s, const float seg[6], float out[6], uint16_t *flag)
{
    load_consts();
    WTree *t = world_tree();
    if (!s->elem || !t) return false;
    float r = fabsf(seg[3] - seg[0]) + fabsf(seg[5] - seg[2]);
    WTreeCells leaves = {0};
    WTree_LeavesNear(t->root, seg, r, &leaves);
    WTreeElem *best = NULL;
    float bestd = k_far;
    for (uint32_t i = 0; i < leaves.n; i++) {
        WTreeList *l = leaves.c[i]->list;
        for (uint32_t k = 0; l && k < l->n; k++) {
            if (l->e[k].kind != WTREE_EDGE) continue;
            WTreeElem *e = l->e[k].elem;
            if (!e->vt->crosses(e, seg)) continue;
            /* The nearest by the heights of the two midpoints. */
            float d = (WTreeEdge_point(e, 1)[1] + WTreeEdge_point(e, 0)[1]) * k_half - (seg[4] + seg[1]) * k_half;
            if (d <= k_zero) d = -d;
            if (d < bestd) {
                bestd = d;
                best = e;
            }
        }
    }
    free(leaves.c);
    if (!best) return false;
    memcpy(out, WTreeEdge_point(best, 0), 12);
    memcpy(out + 3, WTreeEdge_point(best, 1), 12);
    if (flag) *flag = best->w8;
    return true;
}

/* ---- the cameras' queries (0x473380 .. 0x473d60) ---- */

void WTree_LeavesAlong(WTreeCell *c, const float seg[6], WTreeCells *out)
{
    load_consts();
    float x0 = seg[3] <= seg[0] ? seg[3] : seg[0], x1 = seg[0] <= seg[3] ? seg[3] : seg[0];
    float z0 = seg[5] <= seg[2] ? seg[5] : seg[2], z1 = seg[2] <= seg[5] ? seg[5] : seg[2];
    while (!is_leaf(c)) {
        float s = level_size[c->id >> 28];
        float cx = ((float)(c->id & 0x3fff) + k_half) * s - origin;
        float cz = ((float)(c->id >> 14 & 0x3fff) + k_half) * s - origin;
        if (x1 < cx || x0 < cx) {
            if (z1 < cz || z0 < cz) {
                if (c->child[2]) WTree_LeavesAlong(c->child[2], seg, out);
                if (cz <= z0 || cz <= z1)
                    if (c->child[0]) WTree_LeavesAlong(c->child[0], seg, out);
            } else if (c->child[0]) {
                WTree_LeavesAlong(c->child[0], seg, out);
            }
            if (x0 < cx && x1 < cx) return;
        }
        if (z1 < cz || z0 < cz) {
            if (c->child[3]) WTree_LeavesAlong(c->child[3], seg, out);
            if (z0 < cz && z1 < cz) return;
        }
        c = c->child[1];
        if (!c) return;
    }
    cells_push(out, c);
}

static WTreeCell *world_root(void)
{
    WTree *t = g_world ? g_world->tree : NULL;
    return t ? t->root : NULL;
}

/* The polygons whose ground-plane outline holds p, in the leaves within r: ceilings (flag 0x8000, no
   surface bits) or the ground (surface bits). */
static WTreeElems *polys_at(const float p[3], float r, bool ceiling)
{
    WTreeElems *out = calloc(1, sizeof *out);
    WTreeCell *root = world_root();
    if (!out || !root) return out;
    WTreeCells leaves = {0};
    WTree_LeavesNear(root, p, r, &leaves);
    for (uint32_t i = 0; i < leaves.n; i++) {
        WTreeList *l = leaves.c[i] ? leaves.c[i]->list : NULL;
        for (uint32_t k = 0; l && k < l->n; k++) {
            WTreeElem *e = l->e[k].elem;
            if (!e->vt->contains(e, p) || l->e[k].kind != WTREE_POLY) continue;
            bool keep = ceiling ? (e->flags & 0x8000) && !(e->flags & 0xf) : (e->flags & 0xf) != 0;
            if (keep) WTreeElems_push(out, e);
        }
    }
    free(leaves.c);
    return out;
}

WTreeElems *WTree_CeilingsAt(const float p[3], float r) { return polys_at(p, r, true); }   /* 0x473380 */
WTreeElems *WTree_GroundAt(const float p[3], float r) { return polys_at(p, r, false); }    /* 0x4735b0 */

WTreeElems *WTree_ObjectsAlong(const float from[3], const float seg[6])   /* 0x473940 */
{
    (void)from;
    WTreeElems *out = NULL;
    WTreeCell *root = world_root();
    if (!root) return NULL;
    WTreeCells leaves = {0};
    WTree_LeavesAlong(root, seg, &leaves);
    for (uint32_t i = 0; i < leaves.n; i++) {
        WTreeList *l = leaves.c[i] ? leaves.c[i]->list : NULL;
        for (uint32_t k = 0; l && k < l->n; k++) {
            if (l->e[k].kind != WTREE_OBJECT) continue;
            WTreeObject *o = (WTreeObject *)l->e[k].elem;
            float n[3], at[3];
            if (!o->ovt->collide(o, seg, n, at)) continue;
            if (!out) out = calloc(1, sizeof *out);
            if (out) WTreeElems_push(out, l->e[k].elem);
        }
    }
    free(leaves.c);
    return out;
}

/* Whether the segment (x0, z0)-(x1, z1) meets the circle of radius r round (cx, cz): either root of
   the quadratic within [0, 1] (a negative discriminant's root is NaN and fails both). 0x489d20 */
static bool segment_meets_circle(float x0, float z0, float x1, float z1, float cx, float cz, float r)
{
    float dx = x1 - x0, dz = z1 - z0;
    float a = dx * dx + dz * dz;
    float b = (x0 - cx) * dx + (z0 - cz) * dz;
    b = b + b;
    float m = z0 * cz + x0 * cx;
    double disc = sqrt((double)(b * b - (((cz * cz + cx * cx + z0 * z0 + x0 * x0) - (m + m)) - r * r) * a * 4.0f));
    double a2 = (double)a + (double)a;
    float t1 = (float)((-(double)b - disc) / a2);
    double t2 = (disc - (double)b) / a2;
    if (t1 >= 0.0f && t1 <= 1.0f) return true;
    return t2 >= 0.0 && t2 <= 1.0;
}

WTreeElems *WTree_EdgesNear(const float p[3], float r)   /* 0x473b00 */
{
    WTreeElems *out = calloc(1, sizeof *out);
    WTreeCell *root = world_root();
    if (!out || !root) return out;
    WTreeCells leaves = {0};
    WTree_LeavesNear(root, p, r, &leaves);
    for (uint32_t i = 0; i < leaves.n; i++) {
        WTreeList *l = leaves.c[i] ? leaves.c[i]->list : NULL;
        for (uint32_t k = 0; l && k < l->n; k++) {
            if (l->e[k].kind != WTREE_EDGE) continue;
            const WTreeElem *e = l->e[k].elem;
            const float *a = WTreeEdge_point(e, 0), *b = WTreeEdge_point(e, 1);
            if (segment_meets_circle(a[0], a[2], b[0], b[2], p[0], p[2], r)) WTreeElems_push(out, l->e[k].elem);
        }
    }
    free(leaves.c);
    return out;
}

WTreeElems *WTree_EdgesAlong(const float seg[6])   /* 0x473d60 */
{
    WTreeElems *out = calloc(1, sizeof *out);
    WTreeCell *root = world_root();
    if (!out || !root) return out;
    WTreeCells leaves = {0};
    WTree_LeavesAlong(root, seg, &leaves);
    for (uint32_t i = 0; i < leaves.n; i++) {
        WTreeList *l = leaves.c[i] ? leaves.c[i]->list : NULL;
        for (uint32_t k = 0; l && k < l->n; k++) {
            if (l->e[k].kind != WTREE_EDGE) continue;
            const WTreeElem *e = l->e[k].elem;
            const float *a = WTreeEdge_point(e, 0), *b = WTreeEdge_point(e, 1);
            float edge[6] = {a[0], a[1], a[2], b[0], b[1], b[2]}, at[3];
            if (World_SegmentsCross(seg, edge, at)) WTreeElems_push(out, l->e[k].elem);   /* 0x489bf0 */
        }
    }
    free(leaves.c);
    return out;
}

double WTree_PlaneHeight(const float n[3], const float v[3], const float p[3]) { return plane_height(n, v, p); }

/* The light at p from the polygon under it (0x474600 -> 0x474660): the tracker moved to p first (a copy);
   of a quad the triangle that holds p (by the signs of its edges' crossings, from the second corner),
   then each channel of its vertices' 5:5:5 colours (bits 10-14, 5-9, 0-4, x 8 / 255) as the height of
   their plane at p (0x474570, 0x489f90), at most 1. (1, 1, 1, 1) without a polygon. */

void WSurface_Light(const WSurface *s, const float p[3], float out[4])
{
    WSurface t = *s;
    WSurface_Update(&t, p, true);   /* 0x473fc0 */
    out[0] = out[1] = out[2] = out[3] = 1.0f;
    WTreeElem *e = t.elem;
    if (!e) return;
    int n = e->vt->nvertices(e);
    const float *v[4] = {0};
    uint16_t col[4] = {0};
    for (int i = 0; i < n && i < 4; i++) {
        v[i] = e->vt->vertex(e, i);
        col[i] = e->vt->colour(e, i);
    }
    const float **pv = v;
    uint16_t *pc = col;
    if (n == 4) {
        float s1 = (p[2] - v[1][2]) * (v[0][0] - v[1][0]) - (p[0] - v[1][0]) * (v[0][2] - v[1][2]);
        if (!(0 < fbits(s1))) {   /* (the float's bits compared as an integer, as the original) */
            float s2 = (p[2] - v[2][2]) * (v[1][0] - v[2][0]) - (p[0] - v[2][0]) * (v[1][2] - v[2][2]);
            float s3 = (p[2] - v[0][2]) * (v[2][0] - v[0][0]) - (p[0] - v[0][0]) * (v[2][2] - v[0][2]);
            if (!(0 < fbits(s2)) && !(0 < fbits(s3))) goto tri;
        }
        /* the other triangle: corners 0, 2, 3 (the first two slots shifted) */
        v[1] = v[0];
        col[1] = col[0];
        pv = v + 1;
        pc = col + 1;
    }
tri:;
    const float k = 0.003921569f;   /* 0x5b3884 */
    float ch[3][3];
    for (int i = 0; i < 3; i++) {
        uint16_t c = pc[i];
        ch[0][i] = (float)(int16_t)((c >> 7) & 0xf8) * k;
        ch[1][i] = (float)(int16_t)((c >> 2) & 0xf8) * k;
        ch[2][i] = (float)((c & 0x1f) << 3) * k;
    }
    for (int c = 0; c < 3; c++) {
        float tri3[9];
        for (int i = 0; i < 3; i++) {
            tri3[3 * i] = pv[i][0];
            tri3[3 * i + 1] = ch[c][i];
            tri3[3 * i + 2] = pv[i][2];
        }
        float a[3] = {tri3[3] - tri3[0], tri3[4] - tri3[1], tri3[5] - tri3[2]};
        float b[3] = {tri3[0] - tri3[6], tri3[1] - tri3[7], tri3[2] - tri3[8]};
        float nrm[3];
        cross(a, b, nrm);
        normalize(nrm, nrm);
        if (0.9999f <= nrm[1]) nrm[1] = 0.99989998f;   /* 0x5b3e0c, 0x3f7ff972 */
        double hgt = WTree_PlaneHeight(nrm, tri3, p);
        out[1 + c] = 1.0 <= hgt ? 1.0f : (float)hgt;
    }
}
