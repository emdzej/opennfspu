/* The AI (see ai.h). */
#include "game/ai.h"
#include "exe.h"
#include "game/car_drive.h"
#include "game/car_ground.h"
#include "game/race.h"
#include "game/race_car.h"
#include "game/road.h"
#include "game/sim_math.h"
#include "game/sim_rand.h"
#include "game/track.h"
#include "game/wtree.h"
#include "pending.h"
#include "vfs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define K(va) exe_f32(va)

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

int32_t g_ai_level = 1;   /* (the front end's difficulty option; its middle) */

/* The task rates the controllers last asked for (0x5e6650, 0x5e6654, 0x5e66a0). */
static int ai_rate, ai_ticks;
static float ai_dt;

/* The track's AI tuning (Track_Table 0x4a2ba0, not ported: its defaults): the road's width the speeds
   scale by (0x5e6e34) and ten speed zones, node ranges with factors (0x5e74ac, 0x5e74fc). */
static float road_width = 12.0f;
static int32_t zone[10][2];
static float zone_factor[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

/* The racing line (the AIDataRecord_LineSpeed_t at 0x5e671c): the file, its points from +4. */
static uint8_t *line;
static size_t line_size;

static float rand01(void) { return (float)SimRand_Next() * K(0x5b253c); }

static const uint8_t *node_at(int n) { return g_road->nodes + (size_t)n * ROAD_NODE_SIZE; }
static float node_f(int n, int off)
{
    float v;
    memcpy(&v, node_at(n) + off, 4);
    return v;
}
static int32_t node_i(int n, int off)
{
    int32_t v;
    memcpy(&v, node_at(n) + off, 4);
    return v;
}
static void node_vec(int n, int off, float out[3]) { memcpy(out, node_at(n) + off, 12); }
static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float len3(const float v[3]) { return sqrtf(dot3(v, v)); }   /* 0x532890 */
static void unit3(float v[3])   /* Vec3_Normalize */
{
    float l = len3(v);
    if (l != 0) v[0] /= l, v[1] /= l, v[2] /= l;
}
static int16_t car_node(Car *c)
{
    int16_t n;
    memcpy(&n, c->b + 8, 2);
    return n;
}
static float desc_f(Car *c, int off)
{
    float v;
    memcpy(&v, CAR_DESCB(c) + off, 4);
    return v;
}
static float aispec_f(Car *c, int off)
{
    float v;
    memcpy(&v, (const uint8_t *)c->p[CARP_AISPECS] + off, 4);
    return v;
}
/* A junction end's weight: the builder's integer, or the .jnc file's float (read as a float). */
static float end_weight(const RoadEnd *e)
{
    float w;
    memcpy(&w, &e->weight, 4);
    return w;
}
static int clampnode(int n)
{
    int last = g_road->nnodes - 1;
    if (n < 0) return 0;
    return last < n ? last : n;
}

void AI_Load(void)
{
    AI_Free();
    char path[160];
    /* "%s%s%d.lsp" (0x5cbda8): the directory (0x65b2e4), the track, reversed or not. */
    snprintf(path, sizeof path, "GameData\\Track\\racer\\work\\%s%d.lsp", g_track_name, g_race_reverse);
    line = vfs_read_all(path, &line_size);
    PENDING_NOTE(0x40ad40, "the AI's tuning, personalities, scripts and glue (tuning.bin, prsonal.bin, scripts.bin, glue.bin)");
    PENDING_NOTE(0x4a2ba0, "Track_Table: the track's AI speed zones and road width");
}

void AI_Free(void)
{
    free(line);
    line = NULL;
    line_size = 0;
}

void AI_LinePoint(int n, float out[2])
{
    if (line && 4 + 8 * (size_t)n + 8 <= line_size) {
        memcpy(out, line + 4 + 8 * (size_t)n, 8);
        return;
    }
    out[0] = node_f(n, 4);   /* (port: no line file; the road's centre) */
    out[1] = node_f(n, 0xc);
}

/* The junction ahead within `by` nodes of node n along its segment, NULL if the segment goes on. 0x481590 */
static const RoadJunction *junction_ahead(int n, int by)
{
    const Road *r = g_road;
    int s = Road_SegmentOf(r, n);
    if (s < 0) return NULL;
    const RoadSegment *g = &r->seg[s];
    int to = by + n, at = g->first;
    if (g->first <= to && to <= g->last) return NULL;
    if (g->last < to) at = g->last;
    int j = r->node_end[at];
    return 0 <= j ? &r->junctions[j] : NULL;
}

void AI_Plan(Car *c)
{
    const Road *r = g_road;
    int node = car_node(c);
    float fw[3];
    node_vec(node, 0x1c, fw);
    float wide = (node_f(node, 0x44) + node_f(node, 0x40)) / road_width;
    if (K(0x5b24a8) <= wide) wide = 1.0f;
    float s = *CAR_F(c, 0x35c) * K(0x5b2694) * *SPEC_F(c, 0x13c);
    if (K(0x5b2690) <= s) s = 23.0f;
    int look = (int)lrintf(aispec_f(c, 0x20 + 4 * (int)lrintf(s)));
    /* The way at the junction ahead: of the ends going the race's way, by the level the first one, the
       one the random number and the ends' weights pick, or by the weights' sum. */
    const RoadJunction *j = junction_ahead(node, g_race_reverse ? -look : look);
    /* (+0xe94 holds the junction's pointer; the port keeps its index plus one, a pointer being wider
       natively) */
    int32_t *last = CAR_I(c, 0xe94);
    if (j && *last != (int32_t)(j - r->junctions) + 1) {
        float pick = rand01();
        uint32_t rev = g_race_reverse != 0;
        int same = 0;
        float sum = 0;
        if (g_ai_level < 2)
            for (int k = 0; k < j->count; k++)
                if ((uint32_t)j->end[k]->dir == rev) same++;
        *last = (int32_t)(j - r->junctions) + 1;
        for (int k = 0; k < j->count; k++) {
            const RoadEnd *e = j->end[k];
            if ((uint32_t)e->dir != rev) continue;
            float w = end_weight(e);
            bool take = false;
            if (2 <= g_ai_level) {
                sum += w;
                take = pick <= sum;
            } else if (g_ai_level == 0 || g_ai_level == 1) {
                float t1 = K(0x5cc390 + 4u * (unsigned)g_ai_level);
                float a = K(0x5cc398 + 8u * (unsigned)g_ai_level), b = K(0x5cc39c + 8u * (unsigned)g_ai_level);
                if (same == 1) take = true;
                else if (same == 2) take = (K(0x5b23f0) <= w) == (pick <= t1);
                else
                    take = same != 3 || (pick <= a && K(0x5b261c) <= w) ||
                           (a < pick && pick <= b && w < K(0x5b261c) && K(0x5b25f0) <= w) || (b < pick && w < K(0x5b25f0));
            }
            if (take) {
                *CAR_I(c, 0xe98) = k;
                *CAR_I(c, 0xc) = e->seg;
                break;
            }
        }
    }
    /* The bends ahead: the widest turn of the road's way from here over the look-ahead, each side. */
    float right = 0, left = 0;
    for (int k = 2; k <= look; k++) {
        int n = clampnode(Road_Step(r, node, g_race_reverse ? -k : k, *CAR_I(c, 0xe98)));
        float v[3];
        node_vec(n, 0x28, v);
        float side = dot3(v, fw);
        node_vec(n, 0x1c, v);
        float a = (float)(acos((double)dot3(v, fw)) * (double)K(0x5b2688));
        if (K(0x5b23f8) <= side) {
            if (right < a) right = a;
        } else if (left < a)
            left = a;
    }
    float f = 1.0f;
    for (int z = 0; z < 10; z++)
        if (zone[z][0] < node && node < zone[z][1]) f = zone_factor[z];
    float bend = f * (right + left) * *SPEC_F(c, 0x144);
    if (K(0x5b25fc) <= bend) bend = K(0x5b25fc);
    float bendi = bend * K(0x5b2680);
    float *target = CAR_F(c, 0xeb8);
    *target = aispec_f(c, 0x80 + 4 * (int)lrintf(bendi));
    /* Slower with damage (0x4938d0: the mean of two zones) and on poor grip by surface and tyres. */
    float dmg = (*CAR_F(c, 0x4ec) + *CAR_F(c, 0x4e4)) * K(0x5b23f0);
    *target = *target / (dmg * K(0x5b267c) + K(0x5b24a8));
    int surf = *CAR_I(c, 0x464), tyres = *SPEC_I(c, 0x1c0);
    bool dry = !g_race_alt_grip || WSurface_FindAbove(&c->body, CAR_F(c, 0x330), NULL) != 0;   /* 0x473290 */
    float grip = 1.0f, g;
    if (tyres == 2) {
        g = K((dry ? 0x5d0d50u : 0x5d0e4cu) + 0xcu * (unsigned)surf);
        float lo = K(dry ? 0x5b2678 : 0x5b23f0);
        if (g < K(0x5b24a8)) grip = g < lo ? lo : g;
    } else {
        g = K((dry ? 0x5d0d48u : 0x5d0e44u) + 4u * (unsigned)(surf * 3 + tyres));
        float lo = K(dry ? 0x5b24e8 : 0x5b2618);
        if (g < K(0x5b24a8)) grip = g < lo ? lo : g;
    }
    *target = grip * *target;
    /* The driver's skill for the bend's kind (fast, medium, slow), drawn again when the kind changes: its
       own (the skill plus or minus its range) by chance, else the default skill's. */
    float range = desc_f(c, 0x170);
    float fallback = rand01() * (range + range) + (desc_f(c, 0x160) - desc_f(c, 0x170));
    int32_t *kind = CAR_I(c, 0xe9c);
    float *skill = CAR_F(c, 0xea0);
    int want = bendi <= K(0x5b2470) ? (bendi <= K(0x5b241c) ? 2 : 1) : 0;
    if (*kind != want) {
        int sk = 0x154 + 4 * want, sr = 0x164 + 4 * want;
        bool own = want == 2 ? !(rand01() <= desc_f(c, sk)) : desc_f(c, sk) < rand01();
        if (own) {
            float sp = desc_f(c, sr);
            *skill = rand01() * (sp + sp) + (desc_f(c, sk) - desc_f(c, sr));
        } else {
            *skill = fallback;
        }
    }
    *kind = want;
    float v = *skill * *target * wide;
    *target = v;
    /* Held back when far ahead of the first car and that car is slow (not in a network race). */
    if (g_race_mode != 3) {
        Car *lead = g_cars[0];
        float d = *CAR_F(c, 0xaf8) - *CAR_F(lead, 0xaf8);
        if (K(0x5cc384 + 4u * (unsigned)g_ai_level) < fabsf(d) &&
            K(0x5b25f8) < ((*CAR_F(lead, 0xaf8) - *CAR_F(lead, 0xb00)) * K(0x5b2674)) / (float)g_sim_tick && K(0x5b23f8) < d)
            *target = v * K(0x5cc378 + 4u * (unsigned)g_ai_level);
    }
}

void AI_Steer(Car *c)
{
    const Road *r = g_road;
    int8_t gear = (int8_t)c->b[0xd82];
    int g = 1 < gear ? gear : 2;
    float ratio = *SPEC_F(c, 0x60 + 4 * g);
    float d = ratio * *CAR_F(c, 0xeb8) - ratio * *CAR_F(c, 0xd60);
    int t = (int)(d / *SPEC_F(c, 0x94) * K(0x5b26b0));
    if (0xfd < t) t = 0xfe;
    c->b[0xd9c] = (uint8_t)t;
    c->b[0xd9d] = 0;
    if (gear < 2 && 0x1c0 < g_sim_tick) {
        /* Into first at the start, after a random delay. */
        c->b[0xd84] = (uint8_t)((int)((SimRand_Next() << 3) >> 16) + 4);
        c->b[0xd7e] = 2;
        c->b[0xd82] = 2;
    }
    if (d < K(0x5b23f8)) {
        float b = d + d;
        if (b < K(0x5b23f8)) b = -b;
        int br = (int)(b / *SPEC_F(c, 0x94) * K(0x5b26b0));
        if (0xfd < br) br = 0xfe;
        c->b[0xd9d] = (uint8_t)br;
        if ((uint8_t)br) c->b[0xd9c] = 0;
    }
    /* The look-ahead in nodes: the wheels' speed's share, at least a few (more on a wide road), less
       when slow after the start. */
    int node = car_node(c);
    float wide = (node_f(node, 0x44) + node_f(node, 0x40)) / road_width;
    if (K(0x5b24a8) <= wide) wide = 1.0f;
    float L = fabsf(*SPEC_F(c, 0x140) * *CAR_F(c, 0xd60)) * K(0x5b26ac);
    if (wide < K(0x5b24a8)) {
        L *= wide;
        if (L < K(0x5b2580)) L = K(0x5b2580);
    } else if (L < K(0x5b2404))
        L = K(0x5b2404);
    if (*CAR_F(c, 0x35c) < K(0x5b26a8) && 0x380 < g_sim_tick) L = *CAR_F(c, 0x35c) * K(0x5b26a4) * L;
    int ahead = (int)L;
    const float *pos = CAR_F(c, 0x330);
    float at[3] = {pos[0], pos[1], pos[2]};
    float aim[3] = {0, 0, 0};
    for (int k = 0; k < 4; k++) {
        int n = clampnode(Road_Step(r, node, g_race_reverse ? -ahead - k : k + ahead, *CAR_I(c, 0xe98)));
        float p[2];
        AI_LinePoint(n, p);
        aim[0] += p[0];
        aim[1] = node_f(n, 8) + aim[1] + K(0x5b24a8);
        aim[2] += p[1];
    }
    for (int k = 0; k < 3; k++) aim[k] *= 0.25f;
    /* Round the two cars ahead in the race's order. */
    int place = *CAR_I(c, 0x528), from = place + 2;
    if (g_ranked.n - 1 < from) from = g_ranked.n - 1;
    for (int i = from; place < i && i < g_ranked.n; i--) {
        Car *o = g_ranked.c[i];
        if (o == c) continue;
        const float *op = CAR_F(o, 0x330);
        float rel[3] = {op[0] - at[0], op[1] - at[1], op[2] - at[2]};
        float dir[3] = {aim[0] - at[0], aim[1] - at[1], aim[2] - at[2]};
        unit3(dir);
        float f = dot3(rel, dir);
        bool human = (*CAR_I(c, CAR_FLAGS) & 1) != 0;
        if (human || f <= K(0x5b2460) || K(0x5b242c) <= f) {
            if (K(0x5b2624) < f && (f < K(0x5b263c) || (f < K(0x5b2470) && *CAR_F(o, 0xd60) < *CAR_F(c, 0xd60) - K(0x5b2600)))) {
                float off[3] = {dir[0] * f - rel[0], dir[1] * f - rel[1], dir[2] * f - rel[2]};
                float k;
                if (!human) {
                    k = f * K(0x5b26a0) + K(0x5b2580);
                    if (K(0x5b2600) <= k) k = K(0x5b2600);
                } else {
                    k = f * K(0x5b26a0) + K(0x5b25e4);
                    if (K(0x5b2580) <= k) k = K(0x5b2580);
                }
                k = (*CAR_F(o, 0x3a8) + *CAR_F(c, 0x3a8)) * k;
                if (len3(off) < k) {
                    unit3(off);
                    for (int m = 0; m < 3; m++) aim[m] = op[m] + off[m] * k;
                }
            }
        } else {
            int on = car_node(o);
            float lp[2];
            AI_LinePoint(on, lp);
            float p[3] = {lp[0], op[1], lp[1]};
            float to[3] = {p[0] - op[0], p[1] - op[1], p[2] - op[2]};
            float side[3];
            node_vec(on, 0x28, side);
            float f2 = dot3(to, side);
            float k = f2 * K(0x5b26a0) + K(0x5b2404);
            if (K(0x5b2608) <= k) k = K(0x5b2608);
            k = (*CAR_F(o, 0x3a8) + *CAR_F(c, 0x3a8)) * k;
            if (f2 < k) {
                float off[3] = {side[0] * (k - f2), side[1] * (k - f2), side[2] * (k - f2)};
                const float *base = (float)(ahead * 6) <= f2 ? aim : p;
                float b0[3] = {base[0], base[1], base[2]};
                for (int m = 0; m < 3; m++) aim[m] = b0[m] + off[m];
            }
        }
    }
    int32_t state = *CAR_I(c, 0xb50);
    if (state == 2) {
        /* Finished: off the throttle, full brakes. */
        c->b[0xd9c] = 0;
        c->b[0xd9d] = 0xff;
    } else if (state == 3) {
        /* Out of the race: to the road's side, slowly. */
        int half = (int)lrintf(*CAR_F(c, 0x35c)) / 2;
        int by = half < 0xf && half < 1 ? 1 : half < 0xf ? half : 0xf;
        if (g_race_reverse) by = -by;
        int n = Road_Step(r, node, by, *CAR_I(c, 0xe98));
        float *side = CAR_F(c, 0x1078), off;
        if (*side <= K(0x5b23f8)) {
            if (K(0x5b23f8) <= *side) {
                float a[3], b[3], x[3];
                node_vec(node, 0x28, a);
                node_vec(n, 0x28, b);
                Vec3_Cross(a, b, x);   /* 0x5325f0 */
                float turn = (g_race_reverse ? K(0x5b2624) : K(0x5b24a8)) * x[1];
                float w = K(0x5b23f8) <= turn ? -(node_f(n, 0x48) * (float)node_i(n, 0x38)) : node_f(n, 0x4c) * (float)node_i(n, 0x3c);
                float s = K(0x5b2580);
                if (fabsf(w) <= K(0x5b2400)) s = K(0x5b241c) / fabsf(w);
                off = s * w;
                *side = off;
            } else {
                float lim = -(node_f(n, 0x48) * (float)node_i(n, 0x38));
                if (lim < *side) *side = lim;
                off = *side;
            }
        } else {
            float lim = node_f(n, 0x4c) * (float)node_i(n, 0x3c);
            if (*side < lim) *side = lim;
            off = *side;
        }
        float sv[3], np[3];
        node_vec(n, 0x28, sv);
        node_vec(n, 4, np);
        for (int m = 0; m < 3; m++) aim[m] = np[m] + sv[m] * off;
        c->b[0xd9c] = 0;
        c->b[0xd9d] = 0x28;
    }
    float dir[3] = {aim[0] - at[0], aim[1] - at[1], aim[2] - at[2]};
    unit3(dir);
    int st = (int)lrintf(dot3(dir, CAR_F(c, 0x364)) * K(0x5b26b0));
    if (0x7e < st) st = 0x7e;
    else if (st < -0x7e) st = -0x7e;
    *CAR_I(c, 0xda0) = st;
    if (*CAR_I(c, 0xdb8) == 1 && 0x78 < abs(st)) c->b[0xd9c] = (uint8_t)((int8_t)c->b[0xd9c] / 2);
    /* Stuck: slow after the start for too long, or facing the wrong way, asks for a reset (+0xd8b 5). */
    if (K(0x5b2580) <= *CAR_F(c, 0xd60) || !g_race_started || ((*CAR_I(c, CAR_FLAGS) & 1) && 1 < state)) *CAR_I(c, 0xea4) = 0;
    else (*CAR_I(c, 0xea4))++;
    if (c->b[0xd8b] == 5) c->b[0xd8b] = 0;
    if (*CAR_I(c, 0xdb0) == 0) {
        if (0x3f < *CAR_I(c, 0x1100)) return;
        c->b[0xd8b] = 5;
        return;
    }
    float back = dot3(CAR_F(c, 0x37c), CAR_F(c, 0x3d0));
    if (!g_race_reverse ? back < K(0x5b269c) : K(0x5b2698) < back) {
        if (*CAR_F(c, 0x35c) < K(0x5b241c)) {
            c->b[0xd8b] = 5;
            return;
        }
    }
    if (0x9f < *CAR_I(c, 0xea4)) {
        *CAR_I(c, 0xea4) = 0;
        c->b[0xd8b] = 5;
    }
}

/* ---- the controllers and their states ---- */

/* A state (the pool's 0xa0-byte objects): its car. */
typedef struct AiState AiState;
typedef struct {
    void (*tick)(AiState *s);
} AiStateVtbl;
struct AiState {
    const AiStateVtbl *vt;
    Car *car;   /* +4 */
};

/* AIState_Racing (vtable 0x5b26b8): the car on the road, its target and controls, its drive. 0x40ca40 */
static void racing_tick(AiState *s)
{
    Car *c = s->car;
    CarSim_Track(c);   /* 0x49aee0 */
    AI_Plan(c);        /* 0x40b670 */
    AI_Steer(c);       /* 0x40c000 */
    c->b[0xd7d] = c->b[0xd9d];
    c->b[0xd7c] = c->b[0xd9c];
    *CAR_I(c, 0xd90) = *CAR_I(c, 0xda0);
    CarSim_Drive(c);   /* 0x49fb20 */
}
static const AiStateVtbl racing_vt = {racing_tick};

static void none_tick(AiState *s) { (void)s; }   /* AIState_None (0x5b25a0): slot 0 0x514260 */
static const AiStateVtbl none_vt = {none_tick};

/* A controller (AIHigh_*): its car, its state, the state's kind (0xb racing), ticked always (+0x10) or by
   the task rate. */
typedef struct {
    int kind;          /* 0 none, 1 human, 2 opponent */
    Car *car;          /* +0x04 */
    AiState state;     /* +0x08 (the pool's) */
    int32_t phase;     /* +0x0c */
    int32_t always;    /* +0x10 */
    int32_t node;      /* +0x14: the car's node when made */
} AiHigh;

static AiHigh *ctl[CAR_MAX];   /* 0x5e6774 */

/* The car's low-level driver (+0xe3c: the personality's scripts, 0x40abe0), not ported. Then the state. 0x40b530 */
static void state_tick(AiState *s)
{
    Car *c = s->car;
    if (c->b[0x7f] && !(*CAR_I(c, CAR_FLAGS) & 4) && *(int16_t *)(void *)(c->b + 0x41a) == 0)
        PENDING_NOTE(0x40abe0, "the AI's scripted driving moods (+0xe3c)");
    s->vt->tick(s);
}

/* AIHigh_Opponent (vtable 0x5b2560): into the racing state at once (0x40caa0: AI-driven +0xda8, active),
   then that. 0x405930 */
static void opponent_tick(AiHigh *h)
{
    if (h->phase != 0) {
        if (h->phase == 2) PENDING_NOTE(0x405dd0, "the opponent's catch-up (0x405dd0, 0x4057a0)");
        state_tick(&h->state);
        return;
    }
    h->state = (AiState){&racing_vt, h->car};
    *CAR_I(h->car, 0xda8) = 1;
    h->car->b[0x7f] = 1;
    h->phase = 0xb;
}

void AI_MakeControllers(void)
{
    for (int i = 0; i < g_ncars; i++) {
        free(ctl[i]);
        ctl[i] = NULL;
        Car *c = g_cars[i];
        uint32_t f = (uint32_t)*CAR_I(c, CAR_FLAGS);
        AiHigh *h = calloc(1, sizeof *h);
        if (!h) continue;
        h->car = c;
        h->state = (AiState){&none_vt, c};
        h->node = car_node(c);
        if (f & 4) {
            h->kind = 1;   /* AIHigh_Human (0x405740) */
            PENDING_NOTE(0x405740, "the human's AI controller (AIHigh_Human: its catch-up and AI drive)");
        } else if (f & 8) {
            h->kind = 2;   /* AIHigh_Opponent (0x4059a0) */
            h->always = 1;
        } else if (f & 0x30) {
            PENDING_NOTE(0x403e90, "the cops' and traffic's AI controllers");
        }
        ctl[i] = h;
    }
}

void AI_TickAll(void)
{
    for (int i = 0; i < g_ncars; i++) {
        AiHigh *h = ctl[i];
        if (!h) continue;
        Car *c = g_cars[i];
        if (!h->always && !Sim_TaskRate(1, 0, *CAR_F(c, 0x78), *CAR_I(c, 0), &ai_rate, &ai_ticks, &ai_dt, *CAR_I(c, 0x10b8) != 0))
            continue;
        if (h->kind == 2) opponent_tick(h);
    }
}
