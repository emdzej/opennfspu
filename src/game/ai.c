/* The AI (see ai.h). */
#include "game/ai.h"
#include "game/ai_internal.h"
#include "game/ai_police.h"
#include "game/track_table.h"
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
#include "game/ai_data.h"
#include "game/ai_lane.h"
#include "game/ai_drive.h"
#include "game/race_progress.h"
#include "render/camera_math.h"
#include "pending.h"
#include "vfs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define K(va) exe_f32(va)

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

int32_t g_ai_level = 1;   /* (the front end's difficulty option; its middle) */
int32_t g_ai_drive;

/* The task rates the controllers last asked for (0x5e6650, 0x5e6654, 0x5e66a0). */
static int ai_rate;
int g_ai_ticks;
static float ai_dt;


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
    float wide = (node_f(node, 0x44) + node_f(node, 0x40)) / g_track_road_width;
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
        if (g_track_zone[z][0] < node && node < g_track_zone[z][1]) f = g_track_zone_f[z];
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
    float wide = (node_f(node, 0x44) + node_f(node, 0x40)) / g_track_road_width;
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

/* A state (the pool's 0xa0-byte objects, 0x5e6e38: ten, 0x40b590): its car, the idle state's target
   offset and hold. Its slots: the tick, the deleting destructor's work (when a controller replaces it)
   and whether it is done (the purgatory's: ready to come back). */
static void no_done(AiState *s) { (void)s; }
static bool not_ready(AiState *s) { (void)s; return false; }   /* 0x403e10 */

/* The cars in the purgatory (0x5e747c, the traffic's). */
static int s_purgatory;

Car *AIi_CarByIndex(int i)
{
    if (0 <= i && i < g_ncars) return g_cars[i];
    if (g_ncars <= i && i < g_ncars + g_ntraffic) return g_traffic[i - g_ncars];
    return NULL;
}
/* The car a traffic car keeps near (+0xed4, as its index + 1). */
static Car *followed(Car *c) { return AIi_CarByIndex(*CAR_I(c, 0xed4) - 1); }
static void follow(Car *c, Car *o) { *CAR_I(c, 0xed4) = o ? *CAR_I(o, CAR_INDEX) + 1 : 0; }
static float absf(float v) { return v < K(0x5b23f8) ? -v : v; }
/* The octagonal ground distance (x and z) the AI measures with. */
static float octo(const float *a, const float *b)
{
    float z = absf(a[2] - b[2]), x = absf(a[0] - b[0]);
    return x <= z ? x * K(0x5b2468) + z : z * K(0x5b2468) + x;
}
/* rand () * 1000 / 65536, as the inlined generator's ((r * 125) & 0x1fffe000) * 8 compares it. */
static uint32_t rand_k(void) { return ((SimRand_Next() * 0x7d) & 0x1fffe000u) * 8; }
static int dir_of_race(void) { return g_race_reverse ? -1 : 1; }

float AI_Gap(Car *a, Car *b)
{
    int len = g_road->nnodes;
    int d = car_node(a) - car_node(b);
    if (0 < d && len / 2 < d) return (float)(d - len) * K(0x5b2460);
    if (d < 0 && d < -(len / 2)) d += len;
    return (float)d * K(0x5b2460);
}

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
static const AiStateVtbl racing_vt = {racing_tick, no_done, not_ready};

static void none_tick(AiState *s) { (void)s; }   /* AIState_None (0x5b25a0): slot 0 0x514260 */
static const AiStateVtbl none_vt = {none_tick, no_done, not_ready};

/* The AI's speed target (+0xeb8, +0xebc) for the traffic (0x40afd0's): 16.5 along the race, or by the
   followed car's speed when coming the other way (the first band's 22.2 is stored and not taken: the
   original's slip); by the car's own factor (+0xeec), at least 13.4 under it and 8.9 (both when
   forward), by +0x1018, at least 8.9; signed by the direction. */
void AIi_SpeedTarget(Car *c)
{
    uint32_t f = (uint32_t)*CAR_I(c, CAR_FLAGS);
    *CAR_F(c, 0xeb8) = 0;
    if (f & 0x20) {
        /* The police: chasing (+0xecc bit 1) at the chase's factor (+0x109c, its +0x84 at least 1) of
           its speed (+0x1024); else 16.5 (against the race the followed car's band, below the first
           still 16.5: the original keeps that), the SuperCops paced like the traffic, at least
           8.888889. */
        if (*CAR_I(c, 0xecc) & 2) {
            float k = *CAR_F(c, 0x109c) * K(0x5cc358);
            *CAR_F(c, 0x108c) = k;
            *CAR_F(c, 0x84) = k <= 1.0f ? 1.0f : k;
            *CAR_F(c, 0xeb8) = *CAR_F(c, 0x1024) * *CAR_F(c, 0x108c) * (float)*CAR_I(c, 0xeb0);
            *CAR_F(c, 0xebc) = *CAR_F(c, 0xeb8);
            return;
        }
        int dir = *CAR_I(c, 0xeb0);
        float v = 16.5f;
        Car *t = followed(c);
        if (dir != dir_of_race() && t) {
            float a = absf(*CAR_F(t, 0xec0));
            if (a < K(0x5b2554)) v = 16.5f;
            else if (a < K(0x5b2650)) v = 13.333333f;
            else if (a < K(0x5b2410)) v = 8.888889f;
            else if (a < K(0x5b264c)) v = 4.4444447f;
            else v = a < K(0x5b2648) ? 2.2222223f : 2.77f;
        }
        if (f & 0x10) {
            float p = v * *CAR_F(c, 0xeec);
            if (v > K(0x5b23f8)) {
                if (p < v - K(0x5b2658)) p = v - K(0x5b2658);
                if (p < K(0x5b2654)) p = K(0x5b2654);
            }
            v = p * *CAR_F(c, 0x1018);
        }
        if (v < K(0x5b2430)) v = 8.888889f;
        *CAR_F(c, 0xeb8) = (float)dir * v;
        *CAR_F(c, 0xebc) = *CAR_F(c, 0xeb8);
        return;
    }
    if (!(f & 0x10)) return;
    int dir = *CAR_I(c, 0xeb0);
    float v = K(0x5b2660);
    Car *t = followed(c);
    if (dir != dir_of_race() && t) {
        float a = absf(*CAR_F(t, 0xec0));
        if (a < K(0x5b2554)) v = K(0x5b2660);
        else if (a < K(0x5b2650)) v = 13.333333f;   /* (0x41555555) */
        else if (a < K(0x5b2410)) v = 8.888889f;    /* (0x410e38e4) */
        else if (a < K(0x5b264c)) v = 4.4444447f;   /* (0x408e38e4) */
        else v = a < K(0x5b2648) ? 2.2222223f : 2.77f;   /* (0x400e38e4, 0x403147ae) */
    }
    float p;
    if (v > K(0x5b23f8)) {
        float lo = v - K(0x5b2658);
        p = v * *CAR_F(c, 0xeec);
        if (p < lo) p = lo;
        if (p < K(0x5b2654)) p = K(0x5b2654);
    } else {
        p = v * *CAR_F(c, 0xeec);
    }
    p *= *CAR_F(c, 0x1018);
    if (p < K(0x5b2430)) p = 8.888889f;
    *CAR_F(c, 0xeb8) = p * (float)dir;
    *CAR_F(c, 0xebc) = *CAR_F(c, 0xeb8);
}

/* AIState_Normal (vtable 0x5b2668, 0x40b630: the set target cleared, active): the speed target, then the
   lane driver (0x401000, 0x401060, 0x4012b0). 0x40b600 */
static void normal_tick(AiState *s)
{
    AIi_SpeedTarget(s->car);   /* 0x40afd0 */
    AI_LaneDrive(s->car);   /* 0x401000, 0x401060, 0x4012b0 */
}
static const AiStateVtbl normal_vt = {normal_tick, no_done, not_ready};
static void normal_init(AiState *s, Car *c)   /* 0x40b630 */
{
    *s = (AiState){.vt = &normal_vt, .car = c, .target = 0, .hold = 0};
    memset(c->b + 0x1064, 0, 16);
    c->b[0x7f] = 1;
}

/* AIState_Idle (vtable 0x5b252c): holding the car's offset, or easing toward the target one at the AI's
   speed target halved. 0x40cad0 */
static void idle_tick(AiState *s)
{
    Car *c = s->car;
    if (s->hold != 0) {
        *CAR_F(c, 0xeb8) = 0;
        *CAR_F(c, 0xeb4) = *CAR_F(c, 0xed0);
        return;
    }
    float d = *CAR_F(c, 0xed0) - s->target;
    if (K(0x5b2624) < d && d < K(0x5b24a8)) {
        *CAR_F(c, 0xeb8) = 0;
        *CAR_F(c, 0xeb4) = s->target;
        return;
    }
    AIi_SpeedTarget(c);
    *CAR_F(c, 0xeb8) = *CAR_F(c, 0xeb8) * K(0x5b2468);
    *CAR_F(c, 0xeb4) = s->target;
}
static void idle_done(AiState *s) { *CAR_I(s->car, CAR_FLAGS) &= ~0x400; }   /* 0x405160 */
static const AiStateVtbl idle_vt = {idle_tick, idle_done, not_ready};

/* AIState_Purgatory (vtable 0x5b2710): parked away from the road (off the start by the car's index),
   inactive, following the first car, a countdown (+0xee8) by the traffic's count (0x5cc4a0), five times
   for a bus. Its tick counts down once the race is a while in; it is ready at the end of it when fewer
   than the cap (0x5cc480) are about and the followed car is not on the tracks' sections where none may
   come (canyon's, industrial's: the original's node ranges). 0x40de30, 0x40e160, 0x40e080 */
static void purgatory_tick(AiState *s)
{
    Car *c = s->car;
    *CAR_I(c, 0x1100) = 0;
    *CAR_I(c, 0x43c) = 0;
    if (!(*CAR_I(c, CAR_FLAGS) & 0x20) && 0x3bf < g_sim_tick) *CAR_I(c, 0xee8) -= g_ai_ticks;
}
static bool in_range(int n, int lo, int hi) { return lo <= n && n <= hi; }
static bool purgatory_ready(AiState *s)
{
    Car *c = s->car;
    if (0 < *CAR_I(c, 0xee8)) return false;
    /* (0x657418: TRAFFIC_DENSITY, option 0x16, which no front end writes: 0) */
    int cap = (int)exe_u32(0x5cc480 + 4u * (uint32_t)(0 + (g_race_players == 1) * 4));
    if (!(g_roadcars.n - s_purgatory < cap)) {
        *CAR_I(c, 0xee8) = 1;
        return false;
    }
    Car *t = followed(c);
    int16_t n = 0;
    if (t) memcpy(&n, t->b + 8, 2);
    if (!strncasecmp(g_track_name, "cany", 4))
        return !in_range(n, 0x6b1, 0x6ea) && !in_range(n, 0x732, 0x756);
    if (!strncasecmp(g_track_name, "indu", 4))
        return !in_range(n, 0x676, 0x725) && !in_range(n, 0x8df, 0x902) && !in_range(n, 0x938, 0x96b) && !in_range(n, 0x99d, 0x9ce);
    return true;
}
/* Leaving it (0x40dfd0): active, the race's direction, following the last viewed car in the ranking. */
static void purgatory_done(AiState *s)
{
    Car *c = s->car;
    *CAR_I(c, 0x1100) = 0;
    *CAR_I(c, 0x43c) = 0;
    *CAR_I(c, 0xecc) &= ~4;
    if (*CAR_I(c, CAR_FLAGS) & 0x10) s_purgatory--;
    *CAR_I(c, 0xeb0) = 1;
    *CAR_I(c, 0xeac) = 1;
    for (int i = g_ncars - 1; i >= 0; i--)
        if (g_ranked.c[i]->b[CAR_FLAGS + 1] & 1) {   /* (flags 0x100) */
            follow(c, g_ranked.c[i]);
            break;
        }
    c->b[0x7f] = 1;
}
static const AiStateVtbl purgatory_vt = {purgatory_tick, purgatory_done, purgatory_ready};
static void purgatory_init(AiState *s, Car *c)   /* 0x40de30 */
{
    *s = (AiState){.vt = &none_vt, .car = c, .target = 0, .hold = 0};
    float off[3] = {0, (float)*CAR_I(c, CAR_INDEX) * K(0x5b241c), 0};
    CarSim_Place(c, 0, off, 1);
    c->b[0x7f] = 0;
    s->vt = &purgatory_vt;
    follow(c, g_cars[0]);
    int32_t k = (int32_t)exe_u32(0x5cc4a0 + 4u * (uint32_t)g_roadcars.n);
    *CAR_I(c, 0xee8) = ((int32_t)SimRand_Next() * k >> 16) + 1;
    const char *name = (const char *)c->b + CAR_NAME;
    if (!strcmp(name, exe_str(0x5cc450)) || !strcmp(name, exe_str(0x5cc448))) *CAR_I(c, 0xee8) *= 5;   /* "SBUS", "TBUS" */
    *CAR_I(c, 0xecc) |= 4;
    if (*CAR_I(c, CAR_FLAGS) & 0x10) s_purgatory++;
}

static AiHigh *ctl[CAR_MAX];   /* 0x5e6774 */

AiHigh *AIi_Ctl(int i) { return 0 <= i && i < CAR_MAX ? ctl[i] : NULL; }

AiState AIi_Normal(Car *c)
{
    AiState s;
    normal_init(&s, c);
    return s;
}
AiState AIi_Idle(Car *c) { return (AiState){.vt = &idle_vt, .car = c, .target = 0, .hold = 1}; }
AiState AIi_Purgatory(Car *c)
{
    AiState s;
    purgatory_init(&s, c);
    return s;
}

void AIi_SetState(AiHigh *h, AiState s)
{
    h->state.vt->done(&h->state);
    h->state = s;
}

/* The driver's moods by the AI's ticks (0x40abe0), then the state. 0x40b530 */
void AIi_StateTick(AiState *s)
{
    Car *c = s->car;
    if (c->b[0x7f] && !(*CAR_I(c, CAR_FLAGS) & 4) && *(int16_t *)(void *)(c->b + 0x41a) == 0)
        AI_MoodsTick(c, g_ai_ticks);
    s->vt->tick(s);
}

/* AIHigh_Opponent (vtable 0x5b2560): into the racing state at once (0x40caa0: AI-driven +0xda8, active),
   then that. 0x405930 */
static void opponent_tick(AiHigh *h)
{
    if (h->phase != 0) {
        /* (phase 2 would run the heat 0x405dd0 and 0x4057a0 first: never, the phase is 0xb) */
        AIi_StateTick(&h->state);
        return;
    }
    h->state = (AiState){.vt = &racing_vt, .car = h->car, .target = 0, .hold = 0};
    *CAR_I(h->car, 0xda8) = 1;
    h->car->b[0x7f] = 1;
    h->phase = 0xb;
}

/* ---- the traffic's coming back: 0x407880 ---- */

/* Where a traffic car comes back (0x4072f0): near a viewed car at random, or past the viewed cars just
   ahead of it in the ranking that go the same way, 60 to 65 nodes on in a random direction (the
   followed car's when it is moving); its own direction at random, the race's on a one-way road. */
static void come_back_node(Car *c)
{
    follow(c, g_viewed.c[(int)(SimRand_Next() * (uint32_t)g_viewed.n) >> 16]);
    Car *t = followed(c);
    if (!t) return;
    int dir = rand_k() > 0x1f3ffff ? 1 : -1;
    if (K(0x5b2408) < absf(*CAR_F(t, 0xec0))) dir = *CAR_I(t, 0xeb0);
    int p = *CAR_I(t, 0x528), n = g_ncars;
    for (int k = 0; k < n; k++) {
        Car *o = g_ranked.c[(n + p) % n];
        Car *f = followed(c);
        if (o != c && o != f && (o->b[CAR_FLAGS + 1] & 1)) {
            int16_t fn;
            memcpy(&fn, f->b + 8, 2);
            int m = Road_Step(g_road, fn, dir * 0x42, -1);
            const float *np = Road_NodePos(g_road, m);
            if (K(0x5b25cc) < octo(np, CAR_F(o, 0x330)) || AI_Gap(o, f) * (float)dir <= K(0x5b23f8)) break;
            follow(c, o);
            n = g_ncars;
        }
        p += dir;
    }
    *CAR_I(c, 0xeb0) = rand_k() < 0x1f40000 ? -1 : 1;
    if (node_i(1, 0x38) == 0) *CAR_I(c, 0xeb0) = dir_of_race();   /* (0x5e7544: no lanes the other way) */
    *CAR_I(c, 0xeac) = *CAR_I(c, 0xeb0);
    Car *f = followed(c);
    int16_t base;
    memcpy(&base, f->b + 8, 2);
    int m = Road_Step(g_road, base, (((int)(SimRand_Next() * 6) >> 16) + 0x3c) * dir, -1);
    if (m == base) m = 2;
    int16_t m16 = (int16_t)m;
    memcpy(c->b + 8, &m16, 2);
}

/* Its lateral place (0x407600): a lane's centre at random on its side, plus its offset (+0xec8). */
static void come_back_lane(Car *c)
{
    int16_t n;
    memcpy(&n, c->b + 8, 2);
    if (*CAR_I(c, 0xeb0) == 1) {   /* (0x5e753c) */
        float w = node_f(n, 0x4c);
        *CAR_F(c, 0xeb4) = (float)(((int)(SimRand_Next() * (uint32_t)node_i(n, 0x3c)) >> 16) + 1) * w - w * K(0x5b2468);
    } else {
        *CAR_F(c, 0xeb4) = (K(0x5b23f0) - (float)(((int)(SimRand_Next() * (uint32_t)node_i(n, 0x38)) >> 16) + 1)) * node_f(n, 0x48);
    }
    float v = *CAR_F(c, 0xec8) + *CAR_F(c, 0xeb4);
    *CAR_F(c, 0xeb4) = v;
    *CAR_F(c, 0x1074) = v;
}

/* On the road there (0x4076f0): active, placed at its node and offset, moving along it at its speed,
   turned by the angle (0 here), the drive's fields reset (0x406d30), its offset, extent and lanes. */
void AIi_Place(Car *c)
{
    c->b[0x7f] = 1;
    *CAR_F(c, 0x1074) = *CAR_F(c, 0xeb4);
    *CAR_F(c, 0xeb8) = *CAR_F(c, 0xec0);
    *CAR_I(c, 0x544) = 0;   /* 0x40a9f0 */
    *CAR_I(c, 0x540) = 0;
    float off[3] = {*CAR_F(c, 0xeb4), 1.0f, 0};
    int16_t n;
    memcpy(&n, c->b + 8, 2);
    CarSim_Place(c, n, off, *CAR_I(c, 0xeb0));
    float *v = CAR_F(c, 0x33c);
    if (*CAR_F(c, 0xec0) == K(0x5b23f8)) {
        v[0] = v[1] = v[2] = 0;
    } else {
        float s = absf(*CAR_F(c, 0xec0));
        for (int k = 0; k < 3; k++) v[k] = *CAR_F(c, 0x37c + 4 * k) * s;
    }
    if (*CAR_I(c, CAR_FLAGS) & 4) CarSim_ResetDrive(c);   /* 0x49e010 */
    float m[9], o[9];
    Mat3_RotY(m, 0);
    Mat3_Mul(CAR_F(c, 0x364), m, o);
    memcpy(CAR_F(c, 0x364), o, sizeof o);
    memcpy(CAR_F(c, 0x490), o, sizeof o);
    AIi_Settle(c, n);
}

void AIi_Settle(Car *c, int n)
{
    /* 0x406d30 */
    *CAR_I(c, 0x104c) = 1;
    memset(c->b + 0x388, 0, 12);
    memset(c->b + 0x418, 0, 2);
    *CAR_I(c, 0xee4) = 0;
    *CAR_I(c, 0xedc) = 0;
    *CAR_I(c, 0x1050) = 0;
    *CAR_I(c, 0x1048) = 0;
    *CAR_I(c, 0x10a4) = 0;
    *CAR_I(c, 0x10a0) = 0;
    *CAR_I(c, 0x10b0) = 0;
    const float *np = Road_NodePos(g_road, n);   /* 0x4173e0 */
    float d[3] = {CAR_F(c, 0x330)[0] - np[0], CAR_F(c, 0x330)[1] - np[1], CAR_F(c, 0x330)[2] - np[2]};
    float lat = dot3(CAR_F(c, 0x3b8), d);
    *CAR_F(c, 0x1074) = lat;
    *CAR_F(c, 0xeb4) = lat;
    *CAR_F(c, 0xed0) = lat;
    *CAR_F(c, 0x538) = Race_CarAcross(c);   /* 0x4176a0 */
    Race_CarLanes(c);                        /* 0x40ec10 */
}

static void come_back(Car *c)   /* 0x407880 */
{
    SimRand_Next();
    *CAR_I(c, 0xec8) = (int32_t)AI_PersU(c, 0xc + 4 * (int)(SimRand_Next() & 3));   /* 0x403000 */
    static const uint32_t pace[4] = {0x3f800000u, 0x3f666666u, 0x3f4ccccdu, 0x3f333333u};   /* 0x40b390 */
    *CAR_I(c, 0xeec) = (int32_t)pace[SimRand_Next() & 3];
    come_back_node(c);
    come_back_lane(c);
    AIi_SpeedTarget(c);
    *CAR_F(c, 0xec0) = *CAR_F(c, 0xeb8);
    AIi_Place(c);
    memset(c->b + 0x4e4, 0, 10 * 4);
}

/* Too far from the car it follows (0x407090): at a linear road's ends, or more than 0x5b25c4 away from
   it and every viewed car (else it follows the first viewed car near enough). */
static bool too_far(Car *c)
{
    Car *t = followed(c);
    if (!t || (*CAR_I(c, CAR_FLAGS) & 0x400)) return false;
    if (g_race_linear) {
        int16_t n;
        memcpy(&n, c->b + 8, 2);
        if (0 <= n && n < 6) return true;
        int r = g_road->lastnode - n;
        if (0 <= r && r < 6) return true;
    }
    if (!(K(0x5b25c4) < octo(CAR_F(c, 0x330), CAR_F(t, 0x330)))) return false;
    for (int i = 0; i < g_viewed.n; i++)
        if (octo(CAR_F(c, 0x330), CAR_F(g_viewed.c[i], 0x330)) <= K(0x5b25c4)) {
            follow(c, g_viewed.c[i]);
            return false;
        }
    follow(c, NULL);
    return true;
}

/* The nearest police car not in the purgatory and its controller when close (0x405f30). */
/* The police car near the traffic car (0x405f30): the nearest on the road; slow and within 0x5b257c
   (close, unless it chases nobody: none), or moving and within it. */
static AiHigh *police_near(AiHigh *h, int *close)
{
    *close = 0;
    if (g_chasers.n == 0) return NULL;
    Car *best = NULL;
    float d = 20000.0f;
    for (int i = 0; i < g_chasers.n; i++) {
        Car *p = g_chasers.c[i];
        if (*CAR_I(p, 0xecc) & 4) continue;
        float g = absf(AI_Gap(h->car, p));
        if (g < d) d = g, best = p;
    }
    if (!best) return NULL;
    float v = absf(*CAR_F(best, 0xec0));
    AiHigh *ph = ctl[*CAR_I(best, CAR_INDEX)];
    if (!(K(0x5b2580) <= v || K(0x5b257c) <= d)) {
        *close = 1;
        return ph && HW(ph, 0x58).i ? ph : NULL;
    }
    if (K(0x5b2580) < v && d < K(0x5b257c)) return ph;
    return NULL;
}

/* AIHigh_Traffic (vtable 0x5b2588, 0x4064f0): first to the purgatory (or idle for the 0x400 cars);
   back on the road when ready (0x407880), back to the purgatory when too far; pulling over for the
   police near (idle at a lane's edge, or once its first check passed, on). 0x406080 */
static void traffic_tick(AiHigh *h)
{
    Car *c = h->car;
    *CAR_I(c, 0x534) = 0;
    *CAR_I(c, 0x530) = 0;
    AiState s;
    int close;
    switch (h->phase) {
    case 0: {
        float off[3] = {0, 100.0f, 0};
        if (!(*CAR_I(c, CAR_FLAGS) & 0x400)) {
            purgatory_init(&s, c);
            AIi_SetState(h, s);
            h->phase = 1;
        } else {
            AIi_SetState(h, (AiState){.vt = &idle_vt, .car = c, .target = 0, .hold = 1});
            h->phase = 3;
        }
        CarSim_Place(c, 0, off, 1);
        return;
    }
    case 1:
        if (h->state.vt->ready(&h->state) && HW(h, 0x1c).i == 0) {
            normal_init(&s, c);
            AIi_SetState(h, s);
            h->phase = 2;
            come_back(c);
            AIi_StateTick(&h->state);
            return;
        }
        break;
    case 2:
        if (too_far(c) || HW(h, 0x1c).i != 0) {
            purgatory_init(&s, c);
            AIi_SetState(h, s);
            h->phase = 1;
            AIi_StateTick(&h->state);
            return;
        }
        if (!police_near(h, &close)) {
            HW(h, 0x18).i = 0;
            AIi_StateTick(&h->state);
            return;
        }
        if (HW(h, 0x18).i == 0) {
            uint32_t r = (SimRand_Next() * 10) >> 16;
            if (r < 8) {
                int16_t n;
                memcpy(&n, c->b + 8, 2);
                AIi_SetState(h, (AiState){.vt = &idle_vt, .car = c, .target = 0, .hold = 1});
                h->phase = 3;
                if (r != 0) {
                    float t = *CAR_I(c, 0xeb0) == 1 ? node_f(n, 0x4c) * (float)node_i(n, 0x3c) : node_f(n, 0x48) * (float)node_i(n, 0x38);
                    h->state.hold = 0;   /* 0x40cb60 */
                    h->state.target = t;
                }
            } else {
                HW(h, 0x18).i = 1;
            }
            AIi_StateTick(&h->state);
            return;
        }
        break;
    case 3:
        if (too_far(c)) {
            purgatory_init(&s, c);
            AIi_SetState(h, s);
            h->phase = 1;
            AIi_StateTick(&h->state);
            return;
        }
        if (police_near(h, &close) || (*CAR_I(c, CAR_FLAGS) & 0x400)) break;
        normal_init(&s, c);
        AIi_SetState(h, s);
        h->phase = 2;
        break;
    /* (a phase 6, ready to drive on again, is never entered: nothing sets it) */
    }
    AIi_StateTick(&h->state);
}

void AI_MakeControllers(void)
{
    s_purgatory = 0;   /* (0x40b4f0, 0x40b510: the pool and the count cleared) */
    int cops = 0;
    for (int i = 0; i < g_ncars; i++) {
        free(ctl[i]);
        ctl[i] = NULL;
        Car *c = g_cars[i];
        uint32_t f = (uint32_t)*CAR_I(c, CAR_FLAGS);
        AiHigh *h = calloc(1, sizeof *h);
        if (!h) continue;
        h->car = c;
        h->state = (AiState){.vt = &none_vt, .car = c, .target = 0, .hold = 0};
        HW(h, 0x14).i = car_node(c);
        if (f & 4) {
            h->kind = 1;   /* AIHigh_Human (0x405740) */
            Police_PursuedInit(h);
        } else if (f & 8) {
            h->kind = 2;   /* AIHigh_Opponent (0x4059a0) */
            Police_PursuedInit(h);
            h->always = 1;
        } else if (f & 0x10) {
            h->kind = 3;   /* AIHigh_Traffic (0x4064f0) */
        } else if (f & 0x20) {
            h->kind = 4;   /* AIHigh_Cop (0x403e90) */
            Police_Init(h, cops++);
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
        if (!h->always && !Sim_TaskRate(1, 0, *CAR_F(c, 0x78), *CAR_I(c, 0), &ai_rate, &g_ai_ticks, &ai_dt, *CAR_I(c, 0x10b8) != 0))
            continue;
        if (h->kind == 1) {
            Police_PursuedTick(h);   /* AIHigh_Human 0x405720 */
            AIi_StateTick(&h->state);
        } else if (h->kind == 2) opponent_tick(h);
        else if (h->kind == 4) Police_Tick(h);
        else if (h->kind == 3) traffic_tick(h);
    }
}
