/* The AI's lane driver (the [ai] module's 0x401000 .. 0x403000, the normal state's work for the traffic
   and the police): each tick the three lanes about the car's (the one left, its own, the one right) get
   integer scores (0x5e66a4 ..) from the cars ahead in them (0x401f20), the road's lanes there (closed,
   the edges, the far side's), the humans about and the drivers' moods; the best is the lane it heads for
   (0x4012b0: its lateral target +0xeb4, 0x402a40), and the car ahead in it caps its speed (0x402be0). The
   scores and the cars are globals the original shares between the steps; here they are this file's. */
#include "game/ai_lane.h"
#include "exe.h"
#include "game/ai.h"
#include "game/ai_data.h"
#include "game/ai_drive.h"
#include "game/audio.h"
#include "game/car_drive.h"
#include "game/race.h"
#include "game/race_car.h"
#include "game/race_progress.h"
#include "game/road.h"
#include "game/sim_rand.h"
#include "render/r3dcar.h"
#include <math.h>
#include <string.h>

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
#define K(va) exe_f32(va)
#define F(off) (*CAR_F(c, off))
#define I(off) (*CAR_I(c, off))

extern int32_t g_sim_tick;   /* 0x606ac4 */

/* Left, own, right (0x5e6658 ..): the nearest car ahead in each lane, its distance, the two speed
   caps it sets; the scores as floats, the lane chosen and which of the three. */
static Car *s_ahead[3];
static float s_gap[3], s_cap[3], s_cap2[3], s_scoref[3];
static int s_lane, s_pick;
static int32_t s_score[3];

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
static int16_t car_node(Car *c)
{
    int16_t n;
    memcpy(&n, c->b + 8, 2);
    return n;
}
static int step(int n, int by) { return Road_Step(g_road, n, by, -1); }   /* 0x481670 */
static float absf(float v) { return v <= K(0x5b23f8) ? -v : v; }
static int dir_of_race(void) { return g_race_reverse ? -1 : 1; }
/* The score plus a value, truncated as __ftol does (the x87's sum of the integer and the float). */
static int32_t add(int32_t s, double v) { return (int32_t)((double)s + v); }
static int32_t addk(int32_t s, uint32_t va) { return add(s, (double)K(va)); }
static int32_t subk(int32_t s, uint32_t va) { return add(s, -(double)K(va)); }

/* How far a is ahead of b along the road (0x40e8c0): by their nodes, or near (within 0x5b24c8 ..
   0x5b240c) by the ground offset along a's road forward. */
float AI_Ahead(Car *a, Car *b)
{
    int len = g_road->nnodes;
    int d = car_node(a) - car_node(b);
    if (0 < d && len / 2 < d) d -= len;
    else if (d < 0 && d < -(len / 2)) d += len;
    float g = (float)d * K(0x5b2460);
    if (g <= K(0x5b240c) && K(0x5b24c8) <= g) {
        const float *pa = CAR_F(a, 0x330), *pb = CAR_F(b, 0x330);
        g = *CAR_F(a, 0x3d8) * (pa[2] - pb[2]) + *CAR_F(a, 0x3d0) * (pa[0] - pb[0]);
    }
    return g;
}

static float progress_dist(Car *c) { return (float)g_road->length * F(0xaf8) * K(0x5b2460); }   /* 0x40ea40 */

/* A rival far enough ahead in the race (0x402e30): within reach of the place's lead, by the rivals'
   count; never on the road's edge lanes. */
static bool ahead_in_race(Car *c, float by)
{
    float p = progress_dist(c);
    if (!(I(CAR_FLAGS) & 8)) return false;
    int n = car_node(c);
    if (I(0x1020) == 6 - node_i(n, 0x38) || I(0x1020) == node_i(n, 0x3c) + 7) return false;
    float k = K(0x5b2488);
    if (g_rivals.n < 5) k = K(0x5b2414);
    if (g_rivals.n < 2) k = K(0x5b2484);
    if (g_rivals.n == 0) k = K(0x5b23f8);
    return (float)(I(0x528) * 0x54) + p <= k + by;
}

/* The lane's bit in the node's open lanes (0x40eab0: lane + 9 from the top; below 0 the sign bit). */
static uint32_t lane_bit(int lane)
{
    lane += 9;
    if (lane < 0) return 0x80000000u;
    if (31 < lane) lane = 31;
    return 1u << (31 - lane);
}

/* The moods' effect on the lanes (0x4013d0): the horn by mood 8 (or held off), a flash by 0x1000, a
   car to keep away from (0x200) or to stay beside (0x20). */
static void moods(Car *c)
{
    int m = AI_Mood(c, 8);
    if (m == -1 || g_race_type == 1) {
        if (c->b[0xd86]) Audio_HornHold(c, false);
    } else {
        Audio_HornHold(c, true);
    }
    if (AI_Mood(c, 0x1000) != -1 && g_race_type != 1) {
        static const int8_t horn[4][3] = {{4, 0x10, 8}, {2, 0x20, 0xc}, {7, 0x28, 0x10}, {1, 0x20, 0}};
        const int8_t *h = horn[g_sim_tick >> 3 & 3];
        Audio_Horn(c, h[0], h[1], h[2]);
    }
    m = AI_Mood(c, 0x200);   /* away from a car far behind or ahead: a side by the mood's time */
    if (m != -1 && 0 <= m && m < g_ncars && K(0x5b240c) < absf(AI_Gap(c, g_cars[m]))) {
        int t = AI_MoodTime(c);
        if (((t + (t >> 31 & 0x1f)) >> 5 & 1) == 0) s_score[2] = addk(s_score[2], 0x5b2408);
        else s_score[0] = addk(s_score[0], 0x5b2408);
    }
    m = AI_Mood(c, 0x20);   /* beside a car ahead: toward its lane */
    if (m != -1 && 0 <= m && m < g_ncars) {
        Car *o = g_cars[m];
        if ((float)I(0xeb0) * AI_Ahead(c, o) < K(0x5b2404)) {
            if (I(0x1020) < *CAR_I(o, 0x1020)) s_score[2] = addk(s_score[2], 0x5b2400);
            else if (*CAR_I(o, 0x1020) < I(0x1020)) s_score[0] = addk(s_score[0], 0x5b2400);
        }
    }
}

/* The driver's notes of a human about (0x401580): hit by it (0), passed by it (1), alongside in its
   lane ahead or behind (2, 3), swapped places with it (4, 5), hooted at near by day (6). */
static void notes(Car *c, Car *o)
{
    float g = absf(AI_Gap(c, o));
    if (progress_dist(c) < K(0x5b2414)) return;
    int oi = *CAR_I(o, CAR_INDEX), t = g_sim_tick;
    if (t - *CAR_I(o, 0x45c) < 0xf && *CAR_I(o, 0x454) != 0 && *CAR_I(o, 0x454) == I(CAR_INDEX) + 1) AI_MoodNote(c, oi, 0, t);
    bool skip = false;
    if ((float)I(0xeb0) * *CAR_F(o, 0xec0) + K(0x5b2410) < (float)I(0xeb0) * F(0xec0)) {
        if (K(0x5b240c) <= g) skip = true;
        else AI_MoodNote(c, oi, 1, t);
    }
    if (!skip && g < K(0x5b240c) && *CAR_I(o, 0x1020) == I(0x1020))
        AI_MoodNote(c, oi, progress_dist(o) <= progress_dist(c) ? 3 : 2, t);
    if (o->p[CARP_SWAPPED] == c && t - I(0x554) < 0xf) AI_MoodNote(c, oi, progress_dist(c) <= progress_dist(o) ? 5 : 4, t);
    if (g < K(0x5b2408) && o->b[0xd86] && g_r3d_night == 0) AI_MoodNote(c, oi, 6, t);
}

/* The cars ahead in the three lanes (0x401f20): the ranking walked in the car's direction, the first
   active car in each lane (its covered lanes, +0xea8) between the car's length and the personality's
   look ahead (+8); its caps the speed it allows behind it, by how far ahead (two notches less for
   the police and the traffic in their own lanes' reach). */
static void cars_ahead(Car *c)
{
    uint32_t seen = 0;
    int found = 0;
    s_cap[0] = s_cap[1] = s_cap[2] = F(0xeb8);
    float look = AI_PersF(c, 8);
    int rank = I(0x528);
    int lane = I(0x1020);
    uint32_t bl = 1u << ((lane - 1) & 31), bc = 1u << (lane & 31), br = 1u << ((lane + 1) & 31);
    int n = g_ncars;
    for (int i = 0; i < 3; i++) s_cap2[i] = s_cap[i];
    for (int k = 0; found != 3; k++) {
        if (n <= k) return;
        Car *o = g_ranked.c[(I(0xeb0) * k + n + rank) % n];
        if (o == c || !o->b[0x7f]) continue;
        float g = AI_Ahead(o, c);
        float d = (float)I(0xeb0) * g;
        float hi, lo;
        if (K(0x5b2440) <= absf(F(0xec0))) hi = 1.25f, lo = 0.5555556f;
        else hi = 0.8333333f, lo = 0.4f;
        if (d < K(0x5b243c)) hi = 0.16666667f, lo = 0.1f;
        n = g_ncars;
        if (!(F(0x3b0) < d && d < look)) continue;
        int u;
        bool neg;
        if (I(0xeb0) == 1) {
            u = (int)lrintf(g) - 2;
            if ((I(CAR_FLAGS) & 0x28) && u < 1) u = 1;
            neg = u < 0;
        } else {
            u = (int)lrintf(g) + 2;
            if ((I(CAR_FLAGS) & 0x28) && 1 < u) u = -1;
            neg = 0 < u;
        }
        float w = (float)(neg ? 0 : u);
        float c1 = w * hi + *CAR_F(o, 0xec0), c2 = w * lo + *CAR_F(o, 0xec0);
        uint32_t cov = (uint32_t)*CAR_I(o, 0xea8);
        if ((cov & bc) && !(seen & bc)) {
            seen |= bc;
            found++;
            s_ahead[1] = o, s_gap[1] = d, s_cap[1] = c1, s_cap2[1] = c2;
        }
        if (0 <= lane - 1 && lane - 1 < 14 && (cov & bl) && !(seen & bl)) {
            seen |= bl;
            found++;
            s_ahead[0] = o, s_gap[0] = d, s_cap[0] = c1, s_cap2[0] = c2;
        }
        if (0 <= lane + 1 && lane + 1 < 14 && (cov & br) && !(seen & br)) {
            seen |= br;
            found++;
            s_ahead[2] = o, s_gap[2] = d, s_cap[2] = c1, s_cap2[2] = c2;
        }
    }
}

/* Against oncoming lanes (0x401a00): with traffic or police about on a two-way road, the lanes on the
   far side lose 12 (the traffic's) or 4. */
static void far_side(Car *c)
{
    if (ahead_in_race(c, 60.0f)) return;
    double k = (I(CAR_FLAGS) & 0x10) ? -12.0 : -4.0;
    if (g_roadcars.n == 0 && g_chasers.n == 0) return;
    if (node_i(1, 0x38) == 0 || (I(0xecc) & 2)) return;   /* (0x5e7544) */
    if (I(0xeb0) == 1) {
        if (I(0x1020) - 1 < 7) s_score[0] = add(s_score[0], k);
        if (I(0x1020) < 7) s_score[1] = add(s_score[1], k);
    } else {
        if (6 < I(0x1020)) s_score[1] = add(s_score[1], k);
        if (6 < I(0x1020) + 1) s_score[2] = add(s_score[2], k);
    }
}

/* The cars ahead's lanes lose (0x402200): near ones by 0x5b2450, and by how much faster the car goes
   or wants to go than they allow. */
static void slow_lanes(Car *c)
{
    float want = F(0xeb8), sp = F(0xec0);
    bool far = K(0x5b2454) <= progress_dist(c);
    for (int k = 0; k < 3; k++)
        if (s_ahead[k] && s_gap[k] < K(0x5b2408)) s_score[k] = subk(s_score[k], 0x5b2450);
    for (int k = 0; k < 3; k++) {
        if (!s_ahead[k]) continue;
        if (I(0xeb0) == 1) {
            if (s_cap[k] <= sp) s_score[k] = add(s_score[k], (double)((sp - s_cap[k]) * K(0x5b244c)));
            if (s_cap[k] <= want && far) s_score[k] = add(s_score[k], (double)((want - s_cap[k]) * K(0x5b2448)));
            if (s_cap2[k] <= sp) s_score[k] = add(s_score[k], (double)((sp - s_cap2[k]) * K(0x5b2448)));
            if (s_cap2[k] <= want && far) s_score[k] = add(s_score[k], (double)((want - s_cap2[k]) * K(0x5b2444)));
        } else {
            if (sp <= s_cap[k]) s_score[k] = add(s_score[k], (double)((s_cap[k] - sp) * K(0x5b244c)));
            if (want <= s_cap[k]) s_score[k] = add(s_score[k], (double)((s_cap[k] - want) * K(0x5b2448)));
            if (sp <= s_cap2[k]) s_score[k] = add(s_score[k], (double)((s_cap2[k] - sp) * K(0x5b2448)));
            if (want <= s_cap2[k]) s_score[k] = add(s_score[k], (double)((s_cap2[k] - want) * K(0x5b2444)));
        }
    }
}

/* A rival's open lanes (0x4024f0): the lanes no car blocks just ahead (0x5b245c .. 0x5b2458) gain 4. */
static void rival_lanes(Car *c)
{
    if (!(I(CAR_FLAGS) & 8) || ahead_in_race(c, 0)) return;
    float gain[3] = {4.0f, 4.0f, 4.0f};
    uint32_t seen = 0;
    int found = 0, rank = I(0x528), dir = I(0xeb0), n = g_ncars;
    int idx = dir * dir;
    for (int k = dir; found != 3; k++, idx += dir) {
        if (n <= k) break;
        Car *o = g_ranked.c[(n + idx + rank) % n];
        if (o == c || !o->b[0x7f]) continue;
        float g = AI_Ahead(o, c);
        n = g_ncars;
        if (!(K(0x5b245c) <= g && g < K(0x5b2458))) continue;
        int lane = I(0x1020);
        uint32_t cov = (uint32_t)*CAR_I(o, 0xea8), b = 1u << (lane & 31);
        if ((b & cov) && !(b & seen)) seen |= b, found++, gain[1] = 0;
        if (0 < lane && (b = 1u << ((lane - 1) & 31), cov & b) && !(seen & b)) seen |= b, found++, gain[0] = 0;
        if (0 <= lane + 1 && lane + 1 < 14 && (b = 1u << ((lane + 1) & 31), cov & b) && !(b & seen)) seen |= b, found++, gain[2] = 0;
    }
    for (int k = 0; k < 3; k++) s_score[k] = add(s_score[k], (double)gain[k]);
}

/* Beside another car (0x4026b0, not the traffic): the lane it is in loses 0x5b2460. */
static void beside(Car *c)
{
    if (I(CAR_FLAGS) & 0x10) return;
    float reach = F(0x3b0) * K(0x5b2464);
    for (int i = 0; i < g_ncars; i++) {
        Car *o = g_cars[i];
        if (o == c || !o->b[0x7f]) continue;
        float g = absf(AI_Ahead(o, c));
        if (!(g <= K(0x5b241c) && g < reach)) continue;
        if (*CAR_I(o, 0x1020) == I(0x1020) - 1) s_score[0] = subk(s_score[0], 0x5b2460);
        else if (*CAR_I(o, 0x1020) == I(0x1020) + 1) s_score[2] = subk(s_score[2], 0x5b2460);
    }
}

/* Closed lanes (0x401760): a lane shut here loses 0x5b241c; when the lanes change ahead (a 73-tick
   watch, +0x101c) one shut along the way loses 0x5b2410. */
static void closed_lanes(Car *c)
{
    bool l = true, m = true, r = true;
    uint32_t bl = lane_bit(I(0x1020) - 1), bm = lane_bit(I(0x1020)), br = lane_bit(I(0x1020) + 1);
    int dir = I(0xeb0), n = car_node(c);
    int by = (int)lrintf(absf(F(0xec0)) * K(0x5b2404) * K(0x5b2420));
    int a = step(n, by * dir), b = step(n, (by + 1) * dir);
    int32_t open = node_i(n, 0x34);
    if (!(node_i(n, 0x34) & (int32_t)bl)) s_score[0] = subk(s_score[0], 0x5b241c);
    if (!(node_i(n, 0x34) & (int32_t)bm)) s_score[1] = subk(s_score[1], 0x5b241c);
    if (!(node_i(n, 0x34) & (int32_t)br)) s_score[2] = subk(s_score[2], 0x5b241c);
    if (open != node_i(a, 0x34) || open != node_i(b, 0x34)) I(0x101c) = 0x49;
    if (0 < I(0x101c)) {
        int st = (int)lrintf((float)by * K(0x5b2418));
        if (st == 0) st = 1;
        I(0x101c) -= g_ai_ticks;
        for (int k = 0, at = 0; k < by; k += st, at += st * dir) {
            int m2 = step(n, at);
            if (l && !(node_i(m2, 0x34) & (int32_t)bl)) s_score[0] = subk(s_score[0], 0x5b2410), l = false;
            if (m && !(node_i(m2, 0x34) & (int32_t)bm)) s_score[1] = subk(s_score[1], 0x5b2410), m = false;
            if (r && !(node_i(m2, 0x34) & (int32_t)br)) s_score[2] = subk(s_score[2], 0x5b2410), r = false;
        }
    }
}

/* Off the lanes ahead (0x401b70): in the lanes here but not those some way on, the lane back in gains. */
static void lanes_ahead(Car *c)
{
    int by = (int)lrintf(absf(F(0xec0)) * K(0x5b2424));
    if (by < 6) by = 5;
    int a = step(car_node(c), I(0xeb0) * by), n = car_node(c), lane = I(0x1020);
    if ((lane < 7 - node_i(a, 0x38) || node_i(a, 0x3c) + 6 < lane) && 7 - node_i(n, 0x38) <= lane && lane <= node_i(n, 0x3c) + 6) {
        if (lane < 7) s_score[2] = addk(s_score[2], 0x5b2410);
        else s_score[0] = addk(s_score[0], 0x5b2410);
    }
}

/* The road's edge lanes (0x401c60): an edge lane gains (0x5b2438; the traffic's 0x5b2434, or 0x5b2400
   with a racer moving ahead of it in its lane) and loses 0x5b242c when the car is too wide for it; the
   lanes past the edges lose 0x5b2428. */
static void edges(Car *c)
{
    float e = K(0x5b2438);
    if (I(CAR_FLAGS) & 0x10) {
        e = K(0x5b2434);
        if (s_ahead[1] && (*CAR_I(s_ahead[1], CAR_FLAGS) & 1) && K(0x5b2430) < *CAR_F(s_ahead[1], 0xec4)) e = K(0x5b2400);
    }
    int n = car_node(c), lane = I(0x1020);
    int lo = 6 - node_i(n, 0x38), hi = node_i(n, 0x3c) + 7;
    float wl = node_f(n, 0x40) - node_f(n, 0x48) * (float)node_i(n, 0x38);
    float wr = node_f(n, 0x44) - node_f(n, 0x4c) * (float)node_i(n, 0x3c);
    if (lane - 1 == lo || lane - 1 == hi) {
        s_score[0] = add(s_score[0], (double)e);
        if (wl < F(0x3a8)) s_score[0] = subk(s_score[0], 0x5b242c);
    }
    if (lane + 1 == lo || lane + 1 == hi) {
        s_score[2] = add(s_score[2], (double)e);
        if (wr < F(0x3a8)) s_score[2] = subk(s_score[2], 0x5b242c);
    }
    if (lane == lo || lane == hi) {
        s_score[1] = add(s_score[1], (double)e);
        if ((lane <= 6 && wl < F(0x3a8)) || (7 <= lane && wr < F(0x3a8))) s_score[1] = subk(s_score[1], 0x5b242c);
    }
    if (lane - 1 < lo || hi < lane - 1) s_score[0] = subk(s_score[0], 0x5b2428);
    if (lane < lo || hi < lane) s_score[1] = subk(s_score[1], 0x5b2428);
    if (lane + 1 < lo || hi < lane + 1) s_score[2] = subk(s_score[2], 0x5b2428);
}

/* A racer's lane by its place (0x402f00): in a race of laps, once its distance from the road's first node
   (0x40e7a0, either way round) reaches its place in the order times 0x5b2410, its own lane gains 0x5b248c
   when it is an edge lane, else the neighbour towards the nearer edge does. (The count walked is the race's
   cars, the order's index past it when not found.) */
static void racer_lane(Car *c)
{
    if (!(I(CAR_FLAGS) & 1) || I(0xb50) <= 1) return;
    int len = g_road->nnodes, d = car_node(c);
    if (0 < d && len / 2 < d) d -= len;
    else if (d < 0 && d < -(len / 2)) d += len;
    double from = (double)d * (double)K(0x5b2460);
    if (from <= (double)K(0x5b23f8)) from = -from;
    int place = 0;
    while (place < g_ncars && g_ordered.c[place] != c) place++;
    if (from < (double)place * (double)K(0x5b2410)) return;
    int n = car_node(c), lane = I(0x1020);
    if (lane == 6 - node_i(n, 0x38) || lane == node_i(n, 0x3c) + 7) s_score[1] = addk(s_score[1], 0x5b248c);
    else if (6 < lane) s_score[2] = addk(s_score[2], 0x5b248c);
    else s_score[0] = addk(s_score[0], 0x5b248c);
}

/* The first human within 0x5b25cc (0x407c80). */
static Car *human_near(Car *c)
{
    for (int i = 0; i < g_humans.n; i++) {
        Car *h = g_humans.c[i];
        float z = absf(F(0x338) - *CAR_F(h, 0x338)), x = absf(F(0x330) - *CAR_F(h, 0x330));
        float d = x <= z ? x * K(0x5b2468) + z : z * K(0x5b2468) + x;
        if (d <= K(0x5b25cc)) return h;
    }
    return NULL;
}

/* The lane chosen (0x4012b0): the best score of the three (the own lane on ties, then the left), its
   lane clamped to 0 .. 13; the lateral target and the speed cap. */
static void choose(Car *c);

void AI_LaneDrive(Car *c)
{
    /* 0x401000 */
    memset(s_ahead, 0, sizeof s_ahead);
    memset(s_gap, 0, sizeof s_gap);
    s_lane = 0;
    memset(s_score, 0, sizeof s_score);
    /* 0x401060 */
    uint32_t at = (uint32_t)step(car_node(c), I(0x520) << 1);
    uint32_t f = (uint32_t)I(CAR_FLAGS);
    if (!(f & 4) && (!(f & 8) || (at & ~0xfu) == at) && (!(f & 0x10) || (at & ~1u) == at)) {
        if ((SimRand_Next() * AI_PersU(c, 0x1c) & 0xffff0000u) == 0x10000)
            I(0xec8) = (int32_t)AI_PersU(c, 0xc + 4 * (int)(SimRand_Next() & 3));   /* 0x403000's */
    }
    moods(c);
    for (int i = 0; i < g_ncars; i++) {
        Car *o = g_cars[i];
        if (o != c && o->b[0x7f] && (*CAR_I(o, CAR_FLAGS) & 4)) notes(c, o);
    }
    racer_lane(c);
    Car *h = human_near(c);
    if ((I(CAR_FLAGS) & 0x10) && I(0xeb0) != dir_of_race() && h && K(0x5b23fc) < s_cap[1]) {
        uint32_t r = SimRand_Next();
        if (g_race_players != 1 && ((r * 0x7d) & 0x1fffe000u) * 8 < 0x50000 && K(0x5b23f4) < absf(F(0xec0)))
            Audio_Horn(c, 2, 0x20, 8);   /* oncoming traffic hoots at a human now and then */
    }
    far_side(c);
    cars_ahead(c);
    slow_lanes(c);
    rival_lanes(c);
    beside(c);
    closed_lanes(c);
    lanes_ahead(c);
    edges(c);
    if (I(CAR_FLAGS) & 8) {
        I(0x107c) = 0x40a00000;   /* 5 */
        F(0x1078) = K(0x5b23f0) - (float)I(0xeb0) * F(0xec8);
    }
    choose(c);
}

/* Dodging the car ahead in the chosen lane (0x402920): heading for the side of it with more room. */
static bool dodge(Car *c, Car *o)
{
    float room = F(0x3a8) * K(0x5b246c);
    int n = car_node(o);
    float lo, hi;
    if (s_lane < 7) {
        float w = node_f(n, 0x48);
        hi = (float)(s_lane - 6) * w;
        lo = hi - w;
    } else {
        float w = node_f(n, 0x4c);
        lo = (float)(s_lane - 7) * w;
        hi = w + lo;
    }
    float left = *CAR_F(o, 0xed0) - *CAR_F(o, 0x538);
    float l = left - lo;
    float right = *CAR_F(o, 0x538) + *CAR_F(o, 0xed0);
    float r = hi - right;
    if (r <= room && l <= room) return false;
    if (r <= l) F(0xeb4) = left - l * K(0x5b23f0);
    else F(0xeb4) = r * K(0x5b23f0) + right;
    return true;
}

/* A rival's set lane (0x402790, +0x1078 for +0x107c's weight): kept when its lanes stay the best. */
static bool set_lane(Car *c)
{
    int pick[2];
    float offs[2] = {(F(0x1078) - F(0x3a8)) - K(0x5b2468), F(0x3a8) + F(0x1078) + K(0x5b2468)};
    bool keep = true;
    for (int s = 0; s < 2; s++) {
        int l = Race_LaneAt(car_node(c), offs[s]);
        int u = l == I(0x1020) ? 1 : (I(0x1020) <= l ? 2 : 0);
        float v[3] = {s_scoref[0], s_scoref[1], s_scoref[2]};
        v[u] = F(0x107c) + v[u];
        float best = v[0] < v[1] ? v[1] : v[0];
        int b = v[0] < v[1];
        if (best < v[2]) b = 2;
        pick[s] = b;
        if (b != u) keep = false;
    }
    (void)pick;
    if (!keep) return false;
    F(0xeb4) = F(0x1078);
    return true;
}

/* The lateral target for the chosen lane (0x402a40): its centre (or the road's edge lane's inner side),
   kept within the road's sides by the car's half width; a dodge round the car ahead in it first. */
static void lane_target(Car *c)
{
    Car *o = s_ahead[s_pick];
    int n = car_node(c);
    if (F(0x107c) <= K(0x5b23f8) || !set_lane(c)) {
        if (o) {
            float g = absf(AI_Gap(o, c));
            if (g < K(0x5b2470) && !ahead_in_race(c, 1000.0f) && dodge(c, o)) return;
        }
        float t;
        if (s_lane == 6 - node_i(n, 0x38) || s_lane == node_i(n, 0x3c) + 7) {
            if (s_lane < 7) t = node_f(n, 0x48) * (float)(s_lane - 6) - F(0x3a8);
            else t = node_f(n, 0x4c) * (float)(s_lane - 7) + F(0x3a8);
        } else if (s_lane < 7) {
            float w = node_f(n, 0x48);
            t = (float)(s_lane - 6) * w - w * K(0x5b23f0);
        } else {
            t = ((float)(s_lane - 7) + K(0x5b23f0)) * node_f(n, 0x4c);
        }
        F(0xeb4) = t;
        float mx = node_f(n, 0x44) - F(0x3a8);
        if (mx < F(0xeb4)) F(0xeb4) = mx;
        float mn = F(0x3a8) - node_f(n, 0x40);
        if (F(0xeb4) < mn) F(0xeb4) = mn;
    }
}

/* The speed cap (0x402be0): the car ahead in its own lane's, or while changing lanes the target lane
   side's when the change is slow against the gap; at least crawling; nearly stopped behind a car
   stopped ahead while changing. */
static void speed_cap(Car *c)
{
    float cap = s_cap[1];
    Car *o = s_ahead[1];
    if (s_lane != I(0x1020) && o && *(int16_t *)(void *)(o->b + 0x41a) == 0) {
        float g = absf(AI_Ahead(c, o));
        float dv = absf(F(0xec0) - *CAR_F(o, 0xec0));
        float dl = absf(F(0xeb4) - F(0xed0));
        float lat = absf(CAR_F(c, 0x3b8)[0] * F(0x33c) + CAR_F(c, 0x3b8)[1] * F(0x340) + CAR_F(c, 0x3b8)[2] * F(0x344));   /* 0x40ef90 */
        cap = s_cap[1];
        if (dl * dv * K(0x5b2480) < lat * g) {
            cap = s_cap[0];
            if (I(0x1020) < s_lane) cap = s_cap[2];
        }
    }
    if (I(0xeb0) == 1) {
        if (cap < F(0xeb8)) F(0xeb8) = cap;
        if (F(0xeb8) < K(0x5b244c)) I(0xeb8) = (int32_t)0xbfa66666u;   /* -1.3 */
    } else {
        if (F(0xeb8) < cap) F(0xeb8) = cap;
        if (K(0x5b247c) < F(0xeb8)) I(0xeb8) = 0x3fa66666;   /* 1.3 */
    }
    if (s_lane != I(0x1020) && o && absf(*CAR_F(o, 0xec0)) < K(0x5b241c)) {
        if (I(0xeb0) == 1) {
            if (F(0xeb8) <= K(0x5b2478)) F(0xeb8) = K(0x5b2478);
        } else if (!(F(0xeb8) < K(0x5b2474))) {
            F(0xeb8) = K(0x5b2474);
        }
    }
}

static void choose(Car *c)
{
    for (int k = 0; k < 3; k++) s_scoref[k] = (float)s_score[k];
    s_lane = I(0x1020);
    float best = s_scoref[1];
    if (s_scoref[1] < s_scoref[0]) {
        s_lane = I(0x1020) - 1;
        best = s_scoref[0];
    }
    s_pick = s_scoref[1] >= s_scoref[0];
    if (best < s_scoref[2]) {
        s_pick = 2;
        s_lane = I(0x1020) + 1;
    }
    if (s_lane < 0) s_lane = 0;
    else if (13 < s_lane) s_lane = 13;
    lane_target(c);
    speed_cap(c);
}
