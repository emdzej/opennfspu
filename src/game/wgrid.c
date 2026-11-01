/* The world's grid: see wgrid.h. */
#include "game/wgrid.h"
#include "eagl/eagl.h"
#include "exe.h"
#include "game/track.h"
#include "game/world.h"
#include <stdlib.h>
#include <string.h>

static float f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* The original takes the nodes from a pool (0x483510); the line bundle sorts its cells by the nodes'
   addresses and finds them by a binary search on them. The port numbers the nodes in the order made,
   which is that order while the pool hands out rising addresses. */
static WGridNode *node_new(WGrid *g, uint32_t id, WGridNode *parent, bool inner)   /* 0x47c960 / 0x47c110 */
{
    WGridNode *n = calloc(1, sizeof *n);
    if (!n) return NULL;
    n->flags = inner ? 1 : 0;
    n->id = id;
    n->parent = parent;
    n->seq = g->nseq++;
    return n;
}

static void node_free(WGridNode *n)   /* 0x47c9a0 */
{
    if (!n) return;
    for (int q = 0; q < 4; q++) node_free(n->child[q]);
    free(n);
}

void WGrid_Centre(const WGrid *g, const WGridNode *n, float out[3])   /* 0x47c2c0 */
{
    int l = WGrid_Level(n);
    float s = g->size[l], half = f32(0x5b23f0);
    out[0] = ((float)((n->id & 0x3fff) >> (14 - l)) + half) * s + g->origin[0];
    out[1] = g->origin[1];
    out[2] = ((float)((n->id >> 14 & 0x3fff) >> (14 - l)) + half) * s + g->origin[2];
}

void WGrid_Box(const WGrid *g, const WGridNode *n, float mn[3], float mx[3])   /* 0x47c1e0 */
{
    int l = WGrid_Level(n);
    float s = g->size[l];
    mn[1] = g->origin[1];
    mn[0] = (float)((n->id & 0x3fff) >> (14 - l)) * s + g->origin[0];
    mn[2] = (float)((n->id >> 14 & 0x3fff) >> (14 - l)) * s + g->origin[2];
    mx[0] = s + mn[0];
    mx[1] = g->height + mn[1];
    mx[2] = s + mn[2];
}

/* A child's id (0x47c020): a level down, the quadrant's bits (x bit 0, z bit 1) added at that level's place. */
static uint32_t child_id(uint32_t id, unsigned q)
{
    uint32_t l = (id >> 28) + 1;
    uint32_t x = ((q & 1u) << (14 - l) | id) & 0x3fff;
    uint32_t z = (id >> 14 & 0x3fff) | (((q & 2u) << (13 - l)) & 0x3fff);
    return l << 28 | z << 14 | x;
}

static unsigned quadrant(const float c[3], const float *p)
{
    if (c[0] <= p[0]) return c[2] <= p[2] ? 3 : 1;
    return c[2] <= p[2] ? 2 : 0;
}

typedef struct { const float **p; uint32_t n, cap; } Points;

static void points_push(Points *v, const float *p)
{
    if (v->n == v->cap) {
        uint32_t cap = v->cap ? v->cap * 2 : 64;
        const float **q = realloc(v->p, cap * sizeof *q);
        if (!q) return;
        v->p = q;
        v->cap = cap;
    }
    v->p[v->n++] = p;
}

/* 0x47bdc0: the node's points by quadrant; a child for each quadrant (so all four or none) while the node
   is above level 13 and either at level 0 or 1 or holds more than 31 points; a child that made none of its
   own is kept as a leaf (0x47c160). The children made. */
static int split(WGrid *g, WGridNode *n, const Points *pts)
{
    float c[3];
    WGrid_Centre(g, n, c);
    g->calls++;
    int made = 0;
    uint32_t lv = n->id & 0xf0000000u;
    for (unsigned q = 0; q < 4; q++) {
        Points sub = {0};
        for (uint32_t i = 0; i < pts->n; i++)
            if (quadrant(c, pts->p[i]) == q) points_push(&sub, pts->p[i]);
        n->child[q] = NULL;
        if (lv < 0xd0000000u && (lv < 0x10000001u || 0x1f < pts->n)) {
            WGridNode *k = node_new(g, child_id(n->id, q), n, true);
            if (k) {
                if (split(g, k, &sub) == 0) {
                    WGridNode *leaf = node_new(g, k->id, k->parent, false);
                    n->child[q] = leaf;
                    node_free(k);
                } else {
                    n->child[q] = k;
                }
            }
            made++;
        }
        free(sub.p);
    }
    return made;
}

/* The entry's box as the bundles' slot 2 gives it (0x4892d0): the box (slot 4, 0x477ed0) moved by the
   entry's matrix for the transformed bundles (the scene's and the instances' placings, 0x4825b0 and
   0x478050, the animated ones' animation); the mid-ground's (0x47ced0) is the box spread from its centre
   by 2.25 (0x5b4774). Its corners' least and most. */
static void entry_bounds(const BWorld *w, W3DObjectBundle *b, int i, float mn[3], float mx[3])
{
    float bmin[3], bmax[3];
    WArticleBundle_box((WArticleBundle *)b, i, bmin, bmax);
    if (w->nbundles > 3 && b == w->bundles[3]) {
        float k = f32(0x5b4774);
        for (int j = 0; j < 3; j++) {
            float m = (bmin[j] + bmax[j]) * 0.5f;   /* (0x5b2778, a double half) */
            float lo = (bmin[j] - m) * k + m, hi = (bmax[j] - m) * k + m;
            if (lo < mn[j]) mn[j] = lo;
            if (hi < mn[j]) mn[j] = hi;
            if (mx[j] < lo) mx[j] = lo;
            if (mx[j] < hi) mx[j] = hi;
        }
        return;
    }
    EaglMatrix m;
    bool moved = true;
    if (b == w->instances || b == w->scene) {
        const WPlaced *p = &(b == w->instances ? ((WInstanceBundle *)b)->placed : ((WSceneBundle *)b)->placed)[i];
        EAGL_MatFromRotTrans(&m, p->rot, p->pos);
    } else if (b->transformed && b->vt->matrix) {
        b->vt->matrix(b, i, m.m);
    } else {
        moved = false;
    }
    for (int k = 0; k < 8; k++) {
        float v[4] = {k & 1 ? bmax[0] : bmin[0], k & 2 ? bmax[1] : bmin[1], k & 4 ? bmax[2] : bmin[2], 1.0f};
        if (moved) EAGL_MatTransform4(&m, 1, v, 16, v, 16);
        for (int j = 0; j < 3; j++) {
            if (v[j] < mn[j]) mn[j] = v[j];
            if (mx[j] < v[j]) mx[j] = v[j];
        }
    }
}

/* The article's vertices of the grid's level of detail: Base +8's record (from Base +100, 12 bytes each),
   its vertex list ('vt' with the record's +8 as its index; the original's test of that index against
   0xffff can't fail), 16 bytes a vertex, as they are in the article. */
static void article_points(UData *article, Points *pts)
{
    uint32_t base_id = exe_u32(0x5e5084 + 16), vt_id = exe_u32(0x5e5084);
    UData *end = UData_itemsend(article), *rb = UData_find(article, base_id);
    if (rb == end) return;
    const uint8_t *base = UData_data(rb);
    if (!base) return;
    const uint8_t *e = base + 100 + rd32(base + 8) * 12;
    UData *rv = UData_find(article, (vt_id & 0xffff0000u) | rd16(e + 8));
    if (rv == end) return;
    const uint8_t *vt = UData_data(rv);
    if (!vt) return;
    for (uint32_t k = 0; k < rv->count; k++) points_push(pts, (const float *)(const void *)(vt + 16 * (size_t)k));
}

WGrid *WGrid_Build(const void *world)
{
    const BWorld *w = world;
    WGrid *g = calloc(1, sizeof *g);
    if (!g || !w) return g;
    float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
    Points pts = {0};
    for (uint32_t i = 0; i < w->nobjects; i++) {
        W3DObjectBundle *b = w->objects[i].bundle;
        int e = w->objects[i].index;
        entry_bounds(w, b, e, mn, mx);
        if (b->kind == 0x41727469) article_points(b->entries[e].article, &pts);   /* (the article bundles) */
    }
    float dz = mx[2] - mn[2], dx = mx[0] - mn[0];
    float s = dz <= dx ? dx : dz;
    float half = f32(0x5b23f0), one = f32(0x5b24a8);
    g->origin[0] = mn[0] - (s - dx) * half;
    g->origin[1] = mn[1] - one;
    g->origin[2] = mn[2] - (s - dz) * half;
    g->height = (mx[1] - mn[1]) + f32(0x5b242c);
    g->size[0] = s;
    g->inv[0] = one / s;
    for (int l = 1; l < 14; l++) {
        g->size[l] = g->size[l - 1] * half;
        g->inv[l] = one / g->size[l];
    }
    g->root = node_new(g, 0, NULL, true);
    if (g->root) split(g, g->root, &pts);
    free(pts.p);
    return g;
}

void WGrid_Free(WGrid *g)
{
    if (!g) return;
    node_free(g->root);
    free(g);
}

WGridNode *WGrid_Find(const WGrid *g, const float p[3], int maxlevel)
{
    if (!g || !g->root) return NULL;
    if (!(g->origin[0] <= p[0] && g->origin[1] <= p[1] && g->origin[2] <= p[2])) return NULL;
    if (!(p[0] <= g->size[0] + g->origin[0] && p[1] <= g->height + g->origin[1] && p[2] <= g->origin[2] + g->size[0])) return NULL;
    WGridNode *n = g->root;
    for (int l = 0; (n->flags & 1) && l < maxlevel; l++) {
        float c[3];
        WGrid_Centre(g, n, c);
        WGridNode *k = n->child[quadrant(c, p)];
        if (!k) break;
        n = k;
    }
    return n;
}
