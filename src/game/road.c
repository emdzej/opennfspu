/* The track's road (see road.h). */
#include "game/road.h"
#include "exe.h"
#include "pending.h"
#include "real/real.h"
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
    r->seg[r->nseg++] = (RoadSegment){first, -1, 0, 0, last, -1, 0, 1, NULL, NULL};
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
    r->node_segment = malloc((size_t)nnodes);
    r->node_junction = malloc((size_t)nnodes);
    if (!r->node_segment || !r->node_junction) return false;
    memset(r->node_segment, 0xff, (size_t)nnodes);
    memset(r->node_junction, 0xff, (size_t)nnodes);
    PENDING_NOTE(0x4807a0, "the road's junctions (0x4807a0, 0x4809e0, the near-node lists, 0x4817b0, 0x481a10)");
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
    if (r->node_junction[cur] != -1) PENDING_NOTE(0x481030, "the road junction's nearest node");
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
    if (s < 0) return n;   /* (the original reads past its segments) */
    int to = by + n;
    const RoadSegment *g = &r->seg[s];
    if (g->first <= to && to <= g->last) {
        if (to >= r->nnodes) to = r->nnodes - 1;
        if (to < 0) to = 0;
        return to;
    }
    /* Off the segment's end: on through the junction there, else stopped at the end. */
    int end = g->last < to ? g->last : g->first;
    if (r->node_junction[end] >= 0)
        PENDING_NOTE(0x481670, "the road's step through a junction (the junctions aren't built)");
    (void)branch;
    return end;
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
    /* Each end's junction carries the race's progress there (+0x28). */
    PENDING_NOTE(0x481a10, "the progress at the segment's junctions (taken as 0 and 1)");
    *start = 0.0f;
    *end = 0.0f;
    if (*end <= exe_f32(0x5b27a8)) *end = 1.0f;
    *count = r->seg[s].last - r->seg[s].first + 1;
    *first = r->seg[s].first;
}
