/* The track's road (see road.h). */
#include "game/road.h"
#include "exe.h"
#include "pending.h"
#include "real/real.h"
#include "vfs.h"
#include <stdlib.h>
#include <string.h>

Road *g_road;

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

Road *Road_new(void)
{
    Road *r = calloc(1, sizeof *r);
    g_road = r;
    return r;
}

static void add_segment(Road *r, int first, int last)
{
    RoadSegment *ns = realloc(r->seg, (size_t)(r->nseg + 1) * sizeof *ns);
    if (!ns) return;
    r->seg = ns;
    r->seg[r->nseg++] = (RoadSegment){.end = {{first, -1, 0, 0}, {last, -1, 0, 1}}};
}


static float node_x(const Road *r, int n) { return Road_NodePos(r, n)[0]; }
static float node_z(const Road *r, int n) { return Road_NodePos(r, n)[2]; }

/* A segment is split where another segment's end comes within 16 of one of its nodes more than 5 nodes
   from both its ends: it keeps the nodes up to there, a new segment (appended) the rest. Then each
   segment's ends get its index. 0x4807a0 */
static void split_segments(Road *r)
{
    int n0 = r->nseg;
    for (int a = 0; a < n0; a++)
        for (int e = 0; e < 2; e++) {
            int at = r->seg[a].end[e].node, near = 0;
            float px = node_x(r, at), pz = node_z(r, at);
            for (int b = 0; b < r->nseg; b++) {
                if (b == a) continue;
                float best = exe_f32(0x5b46b0);   /* 1e9 */
                for (int k = r->seg[b].first; k < r->seg[b].last; k++) {
                    float dz = pz - node_z(r, k), dx = px - node_x(r, k), d = dz * dz + dx * dx;
                    if (d < best) {
                        near = k;
                        best = d;
                    }
                }
                if (best < exe_f32(0x5b47b8) && 5 < near - r->seg[b].first && 5 < r->seg[b].last - near) {
                    RoadSegment rest = r->seg[b];
                    r->seg[b].last = near;
                    rest.first = near + 1;
                    RoadSegment *ns = realloc(r->seg, (size_t)(r->nseg + 1) * sizeof *ns);
                    if (!ns) return;
                    r->seg = ns;
                    r->seg[r->nseg++] = rest;
                }
            }
        }
    for (int i = 0; i < r->nseg; i++) r->seg[i].f04 = r->seg[i].f14 = i;
}

static int junction_of_end(const Road *r, const RoadEnd *e)
{
    for (int j = 0; j < r->njunctions; j++)
        for (int k = 0; k < r->junctions[j].count; k++)
            if (r->junctions[j].end[k] == e) return j;
    return -1;
}

/* A junction for each segment end not in one yet: the ends within 16 of it (not in one yet, at most
   eight), their nodes' mean, each end's weight 1 / the ends on its side (an integer). 0x4809e0 */
static void build_junctions(Road *r)
{
    float r2 = exe_f32(0x5b47b8);
    for (int a = 0; a < r->nseg; a++)
        for (int e = 0; e < 2; e++) {
            RoadEnd *first = &r->seg[a].end[e];
            if (junction_of_end(r, first) >= 0) continue;
            RoadJunction j = {{first}, 1, 0, 0, {0, 0, 0}};
            float px = node_x(r, first->node), pz = node_z(r, first->node);
            for (int b = 0; b < r->nseg; b++)
                for (int f = 0; f < 2; f++) {
                    RoadEnd *o = &r->seg[b].end[f];
                    float dz = pz - node_z(r, o->node), dx = px - node_x(r, o->node);
                    if (!(dz * dz + dx * dx < r2)) continue;
                    if (junction_of_end(r, o) >= 0) continue;
                    if ((b != a || f != e) && j.count < 8) j.end[j.count++] = o;
                }
            float sx = 0, sy = 0, sz = 0;
            for (int k = 0; k < j.count; k++) {
                const float *q = Road_NodePos(r, j.end[k]->node);
                sx = sx + q[0];
                sy = sy + q[1];
                sz = sz + q[2];
            }
            float cnt = (float)j.count;
            j.centre[0] = sx / cnt;
            j.centre[1] = sy / cnt;
            j.centre[2] = sz / cnt;
            int lasts = 0;
            for (int k = 0; k < j.count; k++) lasts += j.end[k]->dir == 1;
            for (int k = 0; k < j.count; k++) {
                int m = j.end[k]->dir == 1 ? lasts : j.count - lasts;
                j.end[k]->weight = (int32_t)(1.0f / (float)m);
            }
            RoadJunction *nj = realloc(r->junctions, (size_t)(r->njunctions + 1) * sizeof *nj);
            if (!nj) return;
            r->junctions = nj;
            r->junctions[r->njunctions++] = j;
        }
}

/* The junction with an end at node n (the first such), NULL if none. */
static RoadJunction *junction_at(Road *r, int n)
{
    for (int j = 0; j < r->njunctions; j++)
        for (int k = 0; k < r->junctions[j].count; k++)
            if (r->junctions[j].end[k]->node == n) return &r->junctions[j];
    return NULL;
}

/* 0x47ffe0's part after the junctions: each segment's two junctions, and the junction of each end node. */
static void link_junctions(Road *r)
{
    for (int i = 0; i < r->nseg; i++) {
        r->seg[i].junction0 = junction_at(r, r->seg[i].first);
        r->seg[i].junction1 = junction_at(r, r->seg[i].last);
    }
    for (int j = 0; j < r->njunctions; j++)
        for (int k = 0; k < r->junctions[j].count; k++) r->node_end[r->junctions[j].end[k]->node] = (int8_t)j;
}

static int near_cmp(const void *a, const void *b)
{
    const RoadNear *x = a, *y = b;
    if (x->d2 < y->d2) return -1;
    if (y->d2 < x->d2) return 1;
    return (x->node > y->node) - (x->node < y->node);   /* (std::sort leaves ties in no set order) */
}

/* Per junction the nodes of its ends' segments within 90 of its centre (18 for a junction of fewer than
   three ends), nearest first; each node marked with the nearest junction it is within reach of. */
static void near_lists(Road *r)
{
    r->near = calloc((size_t)(r->njunctions ? r->njunctions : 1), sizeof *r->near);
    if (!r->near) return;
    float far = exe_f32(0x5b47bc) * exe_f32(0x5b47bc), close = exe_f32(0x5b47c0) * exe_f32(0x5b47c0);   /* 0x628bb8, 0x628bbc */
    for (int j = 0; j < r->njunctions; j++) {
        RoadJunction *jn = &r->junctions[j];
        RoadNearList *l = &r->near[j];
        float reach = jn->count < 3 ? close : far;
        for (int k = 0; k < jn->count; k++) {
            const RoadSegment *g = &r->seg[jn->end[k]->seg];
            for (int i = g->first; i <= g->last; i++) {
                float dz = jn->centre[2] - node_z(r, i), dx = jn->centre[0] - node_x(r, i), d = dz * dz + dx * dx;
                if (!(d < reach)) continue;
                RoadNear *ne = realloc(l->e, (size_t)(l->n + 1) * sizeof *ne);
                if (!ne) return;
                l->e = ne;
                l->e[l->n++] = (RoadNear){i, d};
                int8_t was = r->node_near[i];
                if (was == -1) {
                    r->node_near[i] = (int8_t)j;
                } else {
                    const RoadJunction *o = &r->junctions[was];
                    float oz = o->centre[2] - node_z(r, i), ox = o->centre[0] - node_x(r, i);
                    if (d < oz * oz + ox * ox) r->node_near[i] = (int8_t)j;
                }
            }
        }
        if (l->n > 1) qsort(l->e, (size_t)l->n, sizeof *l->e, near_cmp);
    }
}

/* From junction j along each segment starting there to the junction at its last node: that junction's
   distance (in nodes) if longer, and on from it (the first junction's own is left, closing the loop);
   the longest into *len. 0x481880 */
static void walk(Road *r, RoadJunction *j, int32_t *len)
{
    for (int k = 0; k < 8; k++) {
        if (!(k < j->count) || j->end[k]->dir != 0) continue;
        int from = j->end[k]->node, s = Road_SegmentOf(r, from);
        if (s < 0) continue;
        int to = r->seg[s].last;
        RoadJunction *n = junction_at(r, to);
        if (!n || n == j) continue;
        int32_t d = j->dist + (to - from) + 1;
        if (!(n->dist < d)) continue;
        if ((uint32_t)*len < (uint32_t)d) *len = d;
        if (n != &r->junctions[0]) {
            n->dist = d;
            walk(r, n, len);
        }
    }
}

/* The race's length (the longest way from the first junction; the node count with one junction) and
   each junction's progress (its distance over the length). 0x4817b0 */
static void race_length(Road *r)
{
    int32_t len = 0;
    if (r->njunctions) walk(r, &r->junctions[0], &len);
    r->length = len;
    if (r->njunctions == 1) r->length = r->nnodes;
    for (int j = 0; j < r->njunctions; j++)
        r->junctions[j].progress = len == 0 ? 0.0f : (float)r->junctions[j].dist / (float)len;
}

/* 0x47ffe0: the segments (one per 'SimT' length, or one for every node), the nodes, the per-node arrays,
   then the junctions. */
static bool build(Road *r, int nnodes, int nlens, const int32_t *lens, const uint8_t *nodes)
{
    if (nlens < 2) {
        add_segment(r, 0, nnodes - 1);
    } else {
        int at = 0;
        for (int i = 0; i < nlens; i++) {
            add_segment(r, at, lens[i] - 1 + at);
            at += lens[i];   /* (last - first) + 1 */
        }
    }
    r->nnodes = nnodes;
    r->nodes = malloc((size_t)nnodes * ROAD_NODE_SIZE);
    if (!r->nodes) return false;
    memcpy(r->nodes, nodes, (size_t)nnodes * ROAD_NODE_SIZE);
    r->node_end = malloc((size_t)nnodes);
    r->node_near = malloc((size_t)nnodes);
    if (!r->node_end || !r->node_near) return false;
    memset(r->node_end, 0xff, (size_t)nnodes);
    memset(r->node_near, 0xff, (size_t)nnodes);
    split_segments(r);     /* 0x4807a0 */
    build_junctions(r);    /* 0x4809e0 */
    link_junctions(r);
    near_lists(r);
    race_length(r);        /* 0x4817b0 */
    /* The last node of the segments that end the race (their last junction's progress 0.999 or more). */
    for (int i = 0; i < r->nseg; i++) {
        float a, b;
        int count, first = 0;
        Road_SegmentRange(r, r->seg[i].last, &a, &b, &count, &first);
        if (exe_f32(0x5b47c8) <= b) r->lastnode = count - 1 + first;
    }
    return true;
}

bool World_LoadSim(UData *root)
{
    UData *d = UData_find(root, 0x53696d44), *t = UData_find(root, 0x53696d54), *end = UData_itemsend(root);   /* 'SimD', 'SimT' */
    if (d == end) {
        REAL_abort(__FILE__, __LINE__, "%s", exe_str(0x5d0458));   /* "Could not find sim data in data file" */
        return false;
    }
    int nlens = 0;
    const int32_t *lens = NULL;
    if (t != end) {
        lens = UData_data(t);
        nlens = (int)t->count;
    }
    if (!g_road) Road_new();
    return build(g_road, (int)d->count, nlens, lens, UData_data(d));
}

static float dist2(const float *a, const float *b)   /* 0x481530: in the ground plane (x, z) */
{
    return (a[2] - b[2]) * (a[2] - b[2]) + (a[0] - b[0]) * (a[0] - b[0]);
}

static float node_d2(const Road *r, int n, const float p[3])
{
    const float *q = Road_NodePos(r, n);
    float dz = q[2] - p[2], dx = q[0] - p[0];
    return dz * dz + dx * dx;
}

/* At a junction: the nearest to p of its near nodes in the band of distances from its centre that can
   be within reach of p (from the centre's distance less 90^2 - 84^2, to it plus that). 0x481030 */
static int junction_nearest(const Road *r, const float p[3], int j)
{
    const RoadNearList *l = &r->near[j];
    const RoadJunction *jn = &r->junctions[j];
    if (l->n == 0) return 0;
    float dz = jn->centre[2] - p[2], dx = jn->centre[0] - p[0], d = dz * dz + dx * dx;
    float k = exe_f32(0x5b47bc) - exe_f32(0x5b2460);
    float band = exe_f32(0x5b47bc) * exe_f32(0x5b47bc) - k * k;
    float lo = d - band;
    if (lo < 0) lo = 0;
    int i = 0;
    if (l->e[0].d2 < lo)
        do {
            if (i == l->n) break;
            if (i + 1 == l->n) {   /* (the original reads one past the list here) */
                i = l->n;
                break;
            }
            i++;
        } while (l->e[i].d2 < lo);
    if (i == l->n) return l->e[l->n - 1].node;
    float best = 1e38f;
    int found = 0;
    for (; i < l->n; i++) {
        if (band + d <= l->e[i].d2) return found;
        float qz = node_z(r, l->e[i].node) - p[2], qx = node_x(r, l->e[i].node) - p[0], q = qz * qz + qx * qx;
        if (q < best) {
            found = l->e[i].node;
            best = q;
        }
    }
    return found;
}

int Road_NearestNode(const Road *r, const float p[3], int near)
{
    int n = r->nnodes;
    if (n <= 0) return -1;
    int cur = near;
    if (exe_f32(0x5b47d0) < node_d2(r, near, p)) {
        float best = 1.9e38f;
        cur = -1;
        for (int i = 0; i < n; i += 8) {
            float d = dist2(Road_NodePos(r, i), p);
            if (d < best) {
                best = d;
                cur = i;
            }
        }
    }
    if (r->node_near[cur] != -1) cur = junction_nearest(r, p, r->node_near[cur]);   /* 0x481030 */
    int prev = -1;
    for (int steps = 1;; steps++) {
        if (prev == cur) return cur;
        float here = cur < n - 1 ? node_d2(r, cur, p) : node_d2(r, cur % n, p);
        int next;
        bool forward;
        if (cur < n - 2) forward = node_d2(r, cur + 1, p) < here;
        else forward = node_d2(r, (cur + 1) % n, p) < here;
        if (forward) {
            next = cur < n - 2 ? cur + 1 : (cur + 1) % n;
        } else if (cur < 1) {
            int back = cur + n - 1, alt = cur + n;
            if (node_d2(r, back % n, p) < here) alt = back;
            next = (n + alt) % n;
        } else {
            next = node_d2(r, cur - 1, p) < here ? cur - 1 : cur;
        }
        prev = cur;
        cur = next;
        if (steps > 999) return cur;
    }
}

int Road_SegmentOf(const Road *r, int n)
{
    for (int i = 0; i < r->nseg; i++)
        if (r->seg[i].first <= n && n <= r->seg[i].last) return i;
    return -1;
}

int Road_Step(const Road *r, int n, int by, int branch)
{
    int s = Road_SegmentOf(r, n);
    if (s < 0) return n;   /* (the original reads before its segments) */
    int to = by + n;
    for (;;) {
        const RoadSegment *g = &r->seg[s];
        if (g->first <= to && to <= g->last) {
            if (to >= r->nnodes) to = r->nnodes - 1;
            if (to < 0) to = 0;
            return to;
        }
        int end = g->last < to ? g->last : g->first;
        if (r->node_end[end] < 0) return end;
        const RoadJunction *j = &r->junctions[r->node_end[end]];
        if (branch < 0) {
            /* The way on: an end of the same kind as the direction asks (the last match). */
            for (int k = 0; k < j->count; k++)
                if (j->end[k]->dir == (by < 1)) branch = k;
            if (branch < 0) return n;
        }
        const RoadEnd *e = j->end[branch];
        s = e->seg;
        to = e->node + (to - end) - 1;   /* (one short going backwards, as the original) */
        if (s < 0) return n;
        branch = -1;
    }
}

const RoadJunction *Road_JunctionPast(const Road *r, int n, int by)
{
    int s = Road_SegmentOf(r, n);
    if (s < 0) return NULL;   /* (the original reads the segment before its first) */
    const RoadSegment *g = &r->seg[s];
    int to = n + by;
    if (g->first <= to && to <= g->last) return NULL;
    int end = g->last < to ? g->last : g->first;
    return r->node_end[end] < 0 ? NULL : &r->junctions[r->node_end[end]];
}

void Road_SegmentRange(const Road *r, int n, float *start, float *end, int *count, int *first)
{
    int s = Road_SegmentOf(r, n);
    if (s < 0) {
        *end = 0.0f;
        *start = 0.0f;
        *count = 1;
        return;
    }
    const RoadSegment *g = &r->seg[s];
    *start = g->junction0 ? g->junction0->progress : 0.0f;
    *end = g->junction1 ? g->junction1->progress : 0.0f;
    if (*end <= exe_f32(0x5b27a8)) *end = 1.0f;
    *count = g->last - g->first + 1;
    *first = g->first;
}

void World_LoadJunctions(const char *path)
{
    size_t n = 0;
    uint8_t *d = vfs_read_all(path, &n);
    if (!d) return;
    if (g_road && n / 0x24 == (size_t)g_road->njunctions)
        for (int j = 0; j < g_road->njunctions; j++)
            for (int k = 0; k < g_road->junctions[j].count; k++) {
                int32_t w;
                memcpy(&w, d + 4 + 0x24 * j + 4 * k, 4);
                g_road->junctions[j].end[k]->weight = w;
            }
    free(d);
}
