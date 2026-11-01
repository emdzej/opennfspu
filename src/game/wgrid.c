/* The world's grid: see wgrid.h. */
#include "game/wgrid.h"
#include "eagl/eagl.h"
#include "exe.h"
#include "game/track.h"
#include "game/skid.h"
#include "game/world.h"
#include "game/wtree.h"
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
    free(n->recs);
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

/* The entry's extent for the grid's build: its corners' (slot 2). */
static void entry_bounds(const BWorld *w, W3DObjectBundle *b, int i, float mn[3], float mx[3])
{
    (void)w;
    float c[8][4];
    W3DObjectBundle_Corners(b, i, c);
    for (int k = 0; k < 8; k++)
        for (int j = 0; j < 3; j++) {
            if (c[k][j] < mn[j]) mn[j] = c[k][j];
            if (mx[j] < c[k][j]) mx[j] = c[k][j];
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

WGridNode *WGrid_FindSphere(const WGrid *g, const float s[4])   /* 0x47bbb0 */
{
    if (!(g->origin[0] <= s[0] - s[3] && g->origin[1] <= s[1] - s[3] && g->origin[2] <= s[2] - s[3]))
        return g->root;
    if (!(s[0] + s[3] <= g->size[0] + g->origin[0] && s[1] + s[3] <= g->height + g->origin[1] &&
          s[2] + s[3] <= g->origin[2] + g->size[0]))
        return g->root;
    WGridNode *n = g->root;
    while (n->flags & 1) {
        float c[3];
        WGrid_Centre(g, n, c);
        double ax = fabsf(s[0] - c[0]), az = fabsf(s[2] - c[2]);
        if ((ax <= az ? ax : az) < (double)s[3]) break;   /* (the nearer centre line, in doubles) */
        WGridNode *k = n->child[c[0] <= s[0] ? (c[2] <= s[2] ? 3 : 1) : (c[2] <= s[2] ? 2 : 0)];
        if (!k) break;
        n = k;
    }
    return n;
}

bool WGrid_PolyBox(int n, const float (*p)[4], const float mn[3], const float mx[3])   /* 0x5a0540 */
{
    for (int i = 0; i < n; i++)
        if (mn[0] <= p[i][0] && p[i][0] <= mx[0] && mn[2] <= p[i][2] && p[i][2] <= mx[2]) return true;
    if (n < 2) return false;   /* (the original's next-vertex table has none for one vertex) */
    /* The next vertex by the table for n vertices (0x5c18a8). */
    uint32_t next = exe_u32(0x5c18a8 + 4u * (uint32_t)n);
    /* The box's least corner inside the polygon: on the inner side of every edge. */
    bool in = true;
    for (int i = 0; i < n && in; i++) {
        const float *q = p[exe_u32(next + 4u * (uint32_t)i)];
        float cross = (q[0] - mn[0]) * (p[i][2] - mn[2]) - (q[2] - mn[2]) * (p[i][0] - mn[0]);
        in = !((double)cross < 0.0);   /* (0x5b24c0, a double 0) */
    }
    if (in) return true;
    /* The box's edges (start, direction) against the polygon's. */
    const float sx[4] = {mn[0], mx[0], mx[0], mn[0]}, sz[4] = {mn[2], mn[2], mx[2], mx[2]};
    const float dx[4] = {mx[0] - mn[0], 0.0f, mn[0] - mx[0], 0.0f}, dz[4] = {0.0f, mx[2] - mn[2], 0.0f, mn[2] - mx[2]};
    float zero = f32(0x5b23f8);
    for (int i = 0; i < n; i++) {
        float px = p[i][0], pz = p[i][2];
        const float *q = p[exe_u32(next + 4u * (uint32_t)i)];
        float ex = q[0] - px, ez = q[2] - pz;
        for (int e = 0; e < 4; e++) {
            float t = (sz[e] - pz) * ex - (sx[e] - px) * ez;
            float u = (sz[e] - pz) * dx[e] - (sx[e] - px) * dz[e];
            float den = dx[e] * ez - dz[e] * ex;
            if (zero < den) {
                if (zero <= t && t <= den && zero <= u && u <= den) return true;
            } else if (den < zero) {
                if (t <= zero && den <= t && u <= zero && den <= u) return true;
            } else if (dz[e] == zero) {
                /* Parallel to a box edge along x: the polygon edge's ends within its x span, whatever
                   their z (the original's). */
                if (sx[e] <= px && px <= sx[e] + dx[e]) return true;
                float qx = ex + px;
                if (sx[e] <= qx && qx <= sx[e] + dx[e]) return true;
            } else {
                if (sz[e] <= pz && pz <= sz[e] + dz[e]) return true;
                float qz = ez + pz;
                if (sz[e] <= qz && qz <= sz[e] + dz[e]) return true;
            }
        }
    }
    return false;
}

/* ---- the nodes' records (0x47c490 .. 0x47c7a0) ---- */

/* 0x47c490: the bundle's entry among the node's records, from the front for the fixed bundles and from
   the back for the moving ones; -1 when it isn't there. */
static int rec_find(const WGridNode *n, const W3DObjectBundle *b, uint16_t index)
{
    if (!b) return -1;
    if (!b->b5) {
        for (int i = 0; i < n->count; i++)
            if (n->recs[i].bundle == b && n->recs[i].index == index) return i;
    } else {
        for (int i = n->count - 1; 0 <= i; i--)
            if (n->recs[i].bundle == b && n->recs[i].index == index) return i;
    }
    return -1;
}

/* 0x47c560: the flags ORed into the entry's record, or a new record (0x47c7a0), at the front for a fixed
   bundle's entry and at the back for a moving one's. */
static void rec_add(WGridNode *n, WGridRec r)
{
    int i = rec_find(n, r.bundle, r.index);
    if (0 <= i) {
        n->recs[i].flags |= r.flags;
        return;
    }
    if (n->count == n->room) {
        WGridRec *q = realloc(n->recs, ((size_t)n->room + 4) * sizeof *q);
        if (!q) return;
        n->recs = q;
        n->room += 4;
    }
    int at = r.bundle->b5 ? n->count : 0;
    memmove(&n->recs[at + 1], &n->recs[at], (size_t)(n->count - at) * sizeof *n->recs);
    n->recs[at] = r;
    n->count++;
}

/* 0x47c610: the record taken out, the ones after it moved up. */
static void rec_remove(WGridNode *n, int i)
{
    if (i < 0 || n->count <= i) return;   /* (the original's would run off the end) */
    memmove(&n->recs[i], &n->recs[i + 1], (size_t)(n->count - i - 1) * sizeof *n->recs);
    n->count--;
}

/* ---- filing ---- */

typedef struct { WGridNode **n; uint32_t count, cap; } Nodes;

static void nodes_push(Nodes *v, WGridNode *n)
{
    if (v->count == v->cap) {
        uint32_t cap = v->cap ? v->cap * 2 : 8;
        WGridNode **q = realloc(v->n, cap * sizeof *q);
        if (!q) return;
        v->n = q;
        v->cap = cap;
    }
    v->n[v->count++] = n;
}

/* 0x47c3b0 (all): every point within the node's box (x, z; above its least sides). */
static bool box_holds(const WGrid *g, const WGridNode *n, int count, const float (*p)[4])
{
    float mn[3], mx[3];
    WGrid_Box(g, n, mn, mx);
    for (int i = 0; i < count; i++)
        if (!(mn[0] < p[i][0] && mn[2] < p[i][2] && p[i][0] <= mx[0] && p[i][2] <= mx[2])) return false;
    return true;
}

/* 0x47bd50: down from the root while exactly one child's box holds every point. */
static WGridNode *node_holding(const WGrid *g, int count, const float (*p)[4])
{
    WGridNode *n = g->root;
    while (n->flags & 1) {
        int hits = 0, which = 0;
        for (int q = 0; q < 4 && hits < 2; q++)
            if (n->child[q] && box_holds(g, n->child[q], count, p)) {
                hits++;
                which = q;
            }
        if (hits != 1) return n;
        n = n->child[which];
    }
    return n;
}

/* 0x489860: the nodes the hull covers from n down. One child met: down into it; two or three: down while
   the depth and the tally (the children met times the depth, summed) stay under 8; four: down while the
   node is above level 2; none: the node itself unless the tally is set (then nothing). The nodes not gone
   down from are the ones filed in. */
static void collect(const WGrid *g, WGridNode *n, int nh, const float (*h)[4], uint32_t depth, uint32_t tally,
                    Nodes *out)
{
    if (n->flags & 1) {
        unsigned met = 0;
        int hits = 0;
        for (int q = 0; q < 4; q++) {
            if (!n->child[q]) continue;
            float mn[3], mx[3];
            WGrid_Box(g, n->child[q], mn, mx);
            if (WGrid_PolyBox(nh, h, mn, mx)) {
                hits++;
                met |= 1u << q;
            }
        }
        bool down;
        switch (hits) {
        case 1: down = true; break;
        case 2:
        case 3:
            down = depth + tally < 8;
            tally += (uint32_t)hits * depth;
            break;
        case 4:
            down = (n->id & 0xf0000000u) < 0x20000000u;
            tally += (uint32_t)hits * depth;
            break;
        default: down = (tally & 0xff) != 0; break;   /* (the tally's low byte as the flag) */
        }
        if (down) {
            for (int q = 0; q < 4; q++)
                if (met & 1u << q) collect(g, n->child[q], nh, h, depth + 1, tally, out);
            return;
        }
    }
    nodes_push(out, n);
}

typedef struct WGridPlace { WGridNode *node; float pos[3]; } WGridPlace;   /* (16 bytes an entry) */

static void file_entry(WGrid *g, W3DObjectBundle *b, int i)   /* 0x489460 */
{
    if (!b->b5) {
        float c[8][4], h[10][4];
        W3DObjectBundle_Corners(b, i, c);
        int nh = WTree_Hull(8, &c[0][0], &h[0][0]);
        Nodes list = {0};
        collect(g, node_holding(g, 8, (const float (*)[4])c), nh, (const float (*)[4])h, 1, 0, &list);
        WGridRec r = {b->entries[i].flags & 0x4187feu, b, (uint16_t)i};
        for (uint32_t k = 0; k < list.count; k++) rec_add(list.n[k], r);
        free(list.n);
        return;
    }
    /* A moving entry: the node holding its sphere (slot 3), kept with the sphere's centre; filed again
       only when the centre moved, in the new node, out of the old. */
    float s[4];
    if (b->kind == 0x3039) WLineBundle_Sphere(b, i, s);
    else WArticleBundle_sphere((WArticleBundle *)b, i, s);
    if (!b->place) {
        b->place = calloc(b->count ? b->count : 1, sizeof *b->place);
        if (!b->place) return;
    } else if (b->place[i].pos[0] == s[0] && b->place[i].pos[1] == s[1] && b->place[i].pos[2] == s[2]) {
        return;
    }
    WGridNode *n = WGrid_FindSphere(g, s);
    WGridPlace *pl = &b->place[i];
    WGridNode *old = pl->node;
    pl->node = n;
    memcpy(pl->pos, s, sizeof pl->pos);
    if (old == n) return;
    rec_add(n, (WGridRec){b->entries[i].flags, b, (uint16_t)i});
    if (old) rec_remove(old, rec_find(old, b, (uint16_t)i));
}

void WGrid_FileBundle(WGrid *g, W3DObjectBundle *b)   /* 0x4892a0 */
{
    if (!g || !g->root || !b) return;
    for (uint32_t i = 0; i < b->count; i++) file_entry(g, b, (int)i);
}
