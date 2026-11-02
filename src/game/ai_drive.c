/* The AI's drive (see ai_drive.h). */
#include "game/ai_drive.h"
#include "exe.h"
#include "game/ai.h"
#include "game/ai_data.h"
#include "game/car_drive.h"
#include "game/car_ground.h"
#include "game/car_respawn.h"
#include "game/race.h"
#include "game/race_car.h"
#include "game/race_progress.h"
#include "game/road.h"
#include "game/sim_math.h"
#include "game/sim_rand.h"
#include "pending.h"
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

/* The task's rate and its ticks and time this turn (0x5e6d00, 0x5e6d78, 0x5e6d7c). */
static int s_rate, s_ticks;
static float s_dt;

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
static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }   /* Vec3_Dot */
static float absf(float v) { return v < K(0x5b23f8) ? -v : v; }
static int cnt(int k) { return g_traf_count[k] / 6; }
/* A car by its index (+0x520): the race's cars, then the traffic objects (the port keeps the pointer
   fields as index + 1). */
static Car *car_by_index(int i)
{
    if (0 <= i && i < g_ncars) return g_cars[i];
    if (g_ncars <= i && i < g_ncars + g_ntraffic) return g_traffic[i - g_ncars];
    return NULL;
}

int AI_Mood(Car *c, uint32_t bits)
{
    (void)c;
    (void)bits;
    PENDING_NOTE(0x40acc0, "the drivers' moods (+0xe3c): none");
    return -1;
}

/* The catch-up factor (0x40af50): early in a race against several rivals, a lone human behind this car
   speeds it by its place among the rivals. */
static float catch_up(Car *c)
{
    if (1 < g_rivals.n && g_race_type != 1 && g_sim_tick < 0x780 && g_humans.n == 1 &&
        *CAR_F(g_humans.c[0], 0xaf8) < F(0xaf8))
        return (float)(g_rivals.n - I(0x10d0)) * K(0x5b23f4) + K(0x5b24a8);
    return K(0x5b24a8);
}

float AI_SpeedFactor(Car *c, float v)
{
    v = absf(v);
    if (0 < I(0xedc)) return K(0x5b23f8);
    float f = K(0x5b24a8);
    if (I(CAR_FLAGS) & 0x10) {
        float g = K(0x5b2468) - v * K(0x5b25ec);
        f = K(0x5b2468);
        if (g < K(0x5b2468)) {
            f = g;
            if (g < K(0x5b23f8)) f = K(0x5b23f8);
        }
    }
    if (I(0x1098) != 0x3f800000) f *= F(0x1098);
    float p = f * F(0x10dc);
    /* (0x657a24 above 0, the wet track's 0x5b25e8: the weather's spec, not ported) */
    if (I(CAR_FLAGS) & 0x20) p *= F(0x1028);
    if (AI_Mood(c, 4) != -1) p *= K(0x5b246c);
    float r = catch_up(c);
    return r == K(0x5b24a8) ? p : r * p;
}

/* The gear for the speed (0x408190): 1 crawling, 0 reversing, else past each ratio's top speed. */
static int gear(Car *c)
{
    if (F(0x35c) < K(0x5b2580)) return 1;
    if (I(0x104c) == -1) return 0;
    int g = 2;
    for (int i = 2; i < *SPEC_I(c, 0x50); i++)
        if (*SPEC_F(c, 0x178 + 4 * (i - 2)) * *SPEC_F(c, 0x94) < F(0x35c)) g = i + 1;
    return g;
}

/* The ground speed's octagonal size (+0xec4). 0x40ef20 */
static void ground_speed(Car *c)
{
    float x = absf(F(0x33c)), z = absf(F(0x344));
    F(0xec4) = z < x ? z * K(0x5b2468) + x : x * K(0x5b2468) + z;
}

/* The grip coming back (+0x10bc) at the personality's rate, at once a while after a hit by nothing. 0x40a960 */
static void grip_back(Car *c)
{
    if (!(F(0x10bc) < K(0x5b24a8))) return;
    float v = (float)s_ticks * AI_PersF(c, 0x40) + F(0x10bc);
    F(0x10bc) = v;
    if (g_sim_tick - I(0x45c) < 0x40 && I(0x454) == 0) F(0x10bc) = K(0x5b24a8);
    else if ((K(0x5b24a8) - AI_PersF(c, 0x3c)) * K(0x5b2468) + AI_PersF(c, 0x3c) < v) F(0x10bc) = K(0x5b24a8);
}

/* The traffic's indicators (+0x1230 left, +0x1232 right) for a lane change ahead. 0x408340 */
static void indicators(Car *c)
{
    if (!(I(CAR_FLAGS) & 0x10)) return;
    if (F(0xeb4) < F(0xed0) - K(0x5b2404)) {
        c->b[0x1230] |= 0x80;
        memset(c->b + 0x1232, 0, 2);
    } else if (F(0xed0) + K(0x5b2404) < F(0xeb4)) {
        c->b[0x1232] |= 0x80;
        memset(c->b + 0x1230, 0, 2);
    } else {
        memset(c->b + 0x1230, 0, 4);
    }
}

/* The lateral offset the car heads for (+0x1074), eased toward its lane's (+0xeb4); out of control the
   lane's at once. 0x40a4d0 */
static void lateral_target(Car *c, bool ooc)
{
    if (ooc) {
        F(0x1074) = F(0xeb4);
        return;
    }
    float by = (float)s_ticks * K(0x5b23f4);
    if (!(F(0xed0) <= F(0xeb4)) && !(F(0x1074) - K(0x5b24a8) <= F(0xed0))) F(0x1074) = F(0xed0);
    else if (F(0x1074) + K(0x5b24a8) < F(0xed0) && F(0xed0) < F(0xeb4)) F(0x1074) = F(0xed0);
    if (F(0xeb4) <= F(0x1074)) {
        if (F(0xeb4) < F(0x1074)) {
            float v = F(0x1074) - by;
            F(0x1074) = v;
            if (v < F(0xeb4)) F(0x1074) = F(0xeb4);
        }
    } else {
        float v = by + F(0x1074);
        F(0x1074) = v;
        if (F(0xeb4) < v) F(0x1074) = F(0xeb4);
    }
}

/* A road's bend `by` nodes on: the node's right against the one there's forward (x and z). 0x40ee30 */
static float bend(Car *c, int by)
{
    int n = car_node(c), m = step(n, by);
    return node_f(n, 0x28) * node_f(m, 0x1c) + node_f(n, 0x30) * node_f(m, 0x24);
}

/* Whether the lane is open at node n (+0x34's bits from the top, lane + 9). 0x40ea60 */
static uint32_t lane_open(int n, int lane)
{
    lane += 9;
    if (lane < 0) lane = 0;
    else if (31 < lane) lane = 31;
    return (uint32_t)node_i(n, 0x34) & 1u << (31 - lane);
}

/* Reversing out: going backwards (+0x104c -1) a while (+0x1048) before forwards again. 0x408e90 */
static void reverse_out(Car *c)
{
    if (I(0x104c) != -1) return;
    if ((F(0xed0) < F(0x1074) - K(0x5b241c) && K(0x5b23f8) < F(0x1060)) ||
        (F(0x1074) + K(0x5b241c) < F(0xed0) && F(0x1060) < K(0x5b23f8)))
        I(0x1050) = g_sim_tick - 0x18;
    if (I(0x1048) < g_sim_tick - I(0x1050)) {
        I(0x104c) = 1;
        I(0x1050) = g_sim_tick;
    }
    if (K(0x5b2470) < F(0xeb8)) I(0xeb8) = 0x41a00000;   /* 20 */
    if (K(0x5b2470) < -F(0xeb8)) I(0xeb8) = (int32_t)0xc1a00000u;
    F(0x1074) = 0;
}

/* Off the road's lanes too long, or not moving forward (+0x3c8): to reverse. 0x40a600 */
static bool stuck(Car *c)
{
    int n = car_node(c);
    if (7 - node_i(n, 0x38) <= I(0x1020) && I(0x1020) <= node_i(n, 0x3c) + 6) return false;
    if (I(0x104c) == -1) I(0x1080) += s_ticks;
    else I(0x1080) = 0;
    return !(I(0x1080) < 9 && K(0x5b25e0) < F(0x3c8));
}

/* Into reverse (for `ticks`) or back to forward when the half of it is over and the car slow. 0x40a8b0 */
static void reverse_for(Car *c, int ticks)
{
    if (I(0x104c) == -1 && I(0x1048) / 2 < g_sim_tick - I(0x1050)) {
        I(0x104c) = 1;
    } else {
        if (I(0x104c) != 1) return;
        if (g_sim_tick - I(0x1050) <= I(0x1048) / 2) return;
        if (K(0x5b2470) <= absf(F(0xec0))) return;
        I(0x104c) = -1;
    }
    I(0x1050) = g_sim_tick;
    I(0x1048) = ticks;
}

/* The point the car aims at (+0x102c, from the car): a set target (+0x1064) or a node ahead by the
   speed, nearer on a bending narrow road, at the lateral target; walked nearer (to four nodes) up to
   five times while that point is beyond the road's lanes on the far side and the lanes there are shut. 0x409120 */
static void aim_point(Car *c)
{
    int node = car_node(c);
    if (F(0x1064) != K(0x5b23f8) || F(0x1068) != K(0x5b23f8) || F(0x106c) != K(0x5b23f8)) {
        for (int k = 0; k < 3; k++) F(0x102c + 4 * k) = F(0x1064 + 4 * k) - F(0x330 + 4 * k);   /* 0x5320a0 */
        F(0x1074) = F(0x1070);
        return;
    }
    float sp = absf(F(0xec0));
    float t = (sp * K(0x5b2618) + K(0x5b2600)) * K(0x5b2420);
    int n = (int)lrintf(t);
    int by = n;
    if (0 <= n && n < cnt(1) && K(0x5b23f8) <= sp) by = cnt(1);
    if (cnt(2) < by) by = cnt(2);
    int at = I(0xeb0) * I(0x104c) * by;
    I(0x10b4) = step(node, at);
    float b = absf(bend(c, at));
    int m = I(0x10b4);
    if (node_f(m, 0x48) * (float)node_i(m, 0x38) + node_f(m, 0x4c) * (float)node_i(m, 0x3c) < K(0x5b2614)) {
        if (K(0x5b2610) < b) by = (by * 0xca3d + (by * 0xca3d >> 31 & 0xffff)) >> 16;
        if (K(0x5b25e8) < b) by = (by * 0xd70a + (by * 0xd70a >> 31 & 0xffff)) >> 16;
        else if (K(0x5b24e8) < b) by = (by * 0xe666 + (by * 0xe666 >> 31 & 0xffff)) >> 16;
        else if (K(0x5b260c) < b) by = (by * 0xf333 + (by * 0xf333 >> 31 & 0xffff)) >> 16;
    }
    float aim[3];
    bool ok;
    int tries = 0;
    do {
        int s = step(node, I(0xeb0) * I(0x104c) * by);
        I(0x10b4) = s;
        const float *sp3 = (const float *)(const void *)(node_at(s) + 4), *sr = (const float *)(const void *)(node_at(s) + 0x28);
        for (int k = 0; k < 3; k++) aim[k] = sr[k] * F(0x1074) + sp3[k];
        const float *np = (const float *)(const void *)(node_at(node) + 4), *nr = (const float *)(const void *)(node_at(node) + 0x28);
        float d[3] = {aim[0] - np[0], aim[1] - np[1], aim[2] - np[2]};
        float x = dot3(nr, d);
        if ((node_f(node, 0x4c) * (float)node_i(node, 0x3c) < x && F(0xed0) < x) ||
            (x < -(node_f(node, 0x48) * (float)node_i(node, 0x38)) && x < F(0xed0))) {
            float side = F(0xed0) <= x ? 1.0f : -1.0f;   /* (the sign's int: 1 or -1) */
            ok = lane_open(node, Race_LaneAt(node, side * K(0x5b2608) + F(0xed0))) != 0 &&
                 lane_open(node, Race_LaneAt(node, side * K(0x5b2404) + F(0xed0))) != 0;
        } else {
            ok = true;
        }
        tries++;
        by = (by * 0xcccc + (by * 0xcccc >> 31 & 0xffff)) >> 16;
        if (by < 4) by = 4;
    } while (tries < 5 && !ok);
    for (int k = 0; k < 3; k++) F(0x102c + 4 * k) = aim[k] - F(0x330 + 4 * k);
}

/* The car's velocities in its own axes (+0xd58 .., +0xd64 ..), the rear's slide (+0xdc0), the heading
   (+0x1054: the forward's turn, backwards when reversing) against the aim's (+0x1058) and their
   difference (+0x1038, -1/2 .. 1/2 turn). 0x408f80 */
static void headings(Car *c)
{
    const float *v = CAR_F(c, 0x33c), *w = CAR_F(c, 0x388);
    F(0xd58) = dot3(v, CAR_F(c, 0x364));
    F(0xd5c) = dot3(v, CAR_F(c, 0x370));
    F(0xd60) = dot3(v, CAR_F(c, 0x37c));
    F(0xd64) = dot3(w, CAR_F(c, 0x364));
    F(0xd68) = dot3(w, CAR_F(c, 0x370));
    F(0xd6c) = dot3(w, CAR_F(c, 0x37c));
    /* (the same value both sides: the slide is never set) */
    I(0xdc0) = absf(F(0xd58)) <= absf(F(0xd58)) * K(0x5b2604) ? 0 : 0x3f000000;
    float fx = F(0x37c), fz = F(0x384);
    if (I(0x104c) != 1) fx = -fx, fz = -fz;
    double k2pi = 1.0 / (2.0 * 3.14159265358979323846);
    float h = (float)(atan2((double)fx, (double)fz) * k2pi);   /* 0x5325d0 */
    F(0x1054) = h;
    aim_point(c);
    float a = (float)(atan2((double)F(0x102c), (double)F(0x1034)) * k2pi);
    float d = h - a;
    if (K(0x5b2448) <= d) {
        if (K(0x5b23f0) < d) d -= K(0x5b24a8);
        F(0x1038) = d;
    } else {
        F(0x1038) = d + K(0x5b24a8);
    }
    F(0x1058) = a;
}

/* Whether the car needs the recovery forces (0x4095d0): not while it is slow within the start's pause,
   nor when it is nearly lined up, fast, within the road's lanes on a gentle bend and not sliding. */
static bool recovering(Car *c)
{
    float b = absf(bend(c, I(0xeb0) << 3));
    if (g_sim_tick < I(0x10a4)) {
        if (K(0x5b2600) <= absf(F(0xec0))) return false;
    } else if (absf(F(0x1038)) <= K(0x5b2468) && I(0x104c) != -1 &&
               g_traf_set[1][8] - K(0x5b2600) <= absf(F(0xec0))) {
        int n = car_node(c);
        if (-(node_f(n, 0x48) * (float)node_i(n, 0x38)) <= F(0xed0) && F(0xed0) <= node_f(n, 0x4c) * (float)node_i(n, 0x3c) &&
            b <= K(0x5b24e8)) {
            return F(0xec4) * K(0x5b261c) < absf(F(0xd58)) && absf(F(0xec0)) < K(0x5b243c);
        }
    }
    return true;
}

/* The engine's push along the car (0x408210 by the speed) when below the target speed going its way,
   else the brakes (the squares table's k, more with the catch-up), and the skid flag +0xd7d by the
   brake count +0xd80. */
static float push(Car *c, bool brake)
{
    float f;
    if (!brake) {
        f = AI_SpeedFactor(c, F(0xec0));
        c->b[0xd80] = s_ticks < (int)c->b[0xd80] ? (uint8_t)(c->b[0xd80] - (uint8_t)s_ticks) : 0;
        if (c->b[0xd80] == 0) c->b[0xd7d] = 0;
        return f;
    }
    const uint8_t *sq = AI_SquaresTable(c);
    float k = 0;
    if (sq) memcpy(&k, sq + 0x80, 4);
    f = k;
    if (K(0x5b24a8) < F(0x108c)) f *= F(0x108c);
    f = -f;
    c->b[0xd80] = (int)c->b[0xd80] + s_ticks * 4 < 0xff ? (uint8_t)(c->b[0xd80] + (uint8_t)(s_ticks * 4)) : 0xff;
    if (3 < c->b[0xd80]) c->b[0xd7d] = 1;
    if (F(0xd60) < K(0x5b23f8)) f = -f;
    return f;
}

/* The forces of the first set (0x409cd0): a sideways push toward the aim, a turn by the heading's
   difference, the drive or the brakes; none while spinning (the 0x800 mood, or the start's pause). */
static void forces_steer(Car *c)
{
    const float *ts = g_traf_set[0];
    int dir = I(0xeb0);
    bool hit = false;
    I(0x1090) = 0;
    float a[3] = {F(0x102c), F(0x1030), F(0x1034)};
    Vec3_Unit(a, a);
    float side[3] = {a[2], a[1], -a[0]};
    float across = dot3(CAR_F(c, 0x33c), side);
    float turn_cap = F(0x1088);
    float sm = F(0xd60) * K(0x5b2634) + K(0x5b24a8);
    float g = F(0x10bc);
    if ((I(CAR_FLAGS) & 8) && I(0x10a0) < g_sim_tick && K(0x5b2428) < absf(F(0x38c)) && K(0x5b2630) < F(0xec4))
        I(0x10a4) = g_sim_tick + 0x180;
    int since = g_sim_tick - I(0x45c);
    if (AI_Mood(c, 0x800) != -1 || (g = sm * g, g_sim_tick < I(0x10a4))) {
        if (g_sim_tick - I(0x45c) < 0x40) I(0x10a4) = g_sim_tick;
        float s = absf(F(0x38c)) + K(0x5b262c);
        if (K(0x5b23f4) <= s) s = K(0x5b23f4);
        if (F(0x38c) <= K(0x5b23f8)) s = -s;
        F(0x38c) = s;
        turn_cap = 0;
        g = 0;
    }
    int lv = g_ai_level;
    if (since < 0x10) I(0x10a0) -= 0x14;
    if (since < (int8_t)exe_u32(0x5cc32c + (uint32_t)lv) && I(0x454) != 0) {
        Car *o = car_by_index(I(0x454) - 1);
        if (o && (*CAR_I(o, CAR_FLAGS) & 0x200)) {
            g *= K(0x5cc330 + 4u * (uint32_t)lv);
            turn_cap = 0;
            hit = true;
        }
    }
    float cap = g * F(0x1084), gain = ts[2] * g;
    if (K(0x5b24a8) < F(0x108c)) cap *= F(0x108c), gain *= F(0x108c);
    float want = F(0x1038) * ts[4] * K(0x5b2628);
    if (ts[5] <= want) want = ts[5];
    if (want <= -ts[5]) want = -ts[5];
    if (-ts[8] < F(0xec0) && F(0xec0) < ts[8]) {
        float l = absf(ts[10] * F(0xec4));
        if (l <= want) want = l;
        if (want <= -l) want = -l;
    }
    float tq = -((F(0xd68) - want) * ts[6]);
    if (turn_cap <= tq) tq = turn_cap;
    if (tq <= -turn_cap) tq = -turn_cap;
    float sd = (float)-dir * (across * (float)dir) * gain;
    if (cap <= sd) sd = cap;
    if (sd <= -cap) sd = -cap;
    float fd = (float)dir;
    bool brake = fd * F(0xeb8) <= fd * F(0xec0) || I(0x544) != 0 || I(0xeac) != I(0xeb0);
    float fw = push(c, brake);
    if (hit) fw = -K(0x5cc340 + 4u * (uint32_t)lv);
    F(0xd54) = fw;
    F(0xd50) = 0;
    F(0xd70) = F(0xd74) = F(0xd78) = 0;
    F(0xd34) = 0;
    F(0xd3c) = 0;
    F(0xd4c) = sd;
    F(0xd38) = tq;
}

/* The forces of the second set, to recover (0x409780): sideways against the slide, a turn toward the
   aim, the drive at the target or the brakes; reversing halves them. */
static void forces_recover(Car *c)
{
    const float *ts = g_traf_set[1];
    bool back = false;
    I(0x1090) = 0;
    /* (0x40ee80: the speed along the road ahead, computed and dropped) */
    int dir = I(0xeb0);
    if ((float)dir * F(0xec0) < K(0x5b23f8) && I(0x104c) != -1) back = true;
    I(0x10a4) = g_sim_tick;
    float want = ts[4] * F(0x1038) * K(0x5b2628);
    if (ts[5] <= want) want = ts[5];
    if (want <= -ts[5]) want = -ts[5];
    float slide = want * K(0x5b241c);
    if (ts[1] <= slide) slide = ts[1];
    if (slide <= -ts[1]) slide = -ts[1];
    if (F(0xec4) <= slide) slide = F(0xec4);
    if (slide <= -F(0xec4)) slide = -F(0xec4);
    if (-ts[8] < F(0xec0) && F(0xec0) < ts[8]) {
        float l = absf(ts[9] * F(0xec4));
        if (l <= slide) slide = l;
        if (slide <= -l) slide = -l;
        float m = absf(ts[10] * F(0xec4));
        if (m <= want) want = m;
        if (want <= -m) want = -m;
    }
    if (I(0x10ac) == 2) want *= K(0x5b2400);
    float tq = -((F(0xd68) - want) * ts[6]);
    if (K(0x5b2614) < absf(F(0xec0))) tq *= K(0x5b23f0);
    if (F(0x1088) <= tq) tq = F(0x1088);
    if (tq <= -F(0x1088)) tq = -F(0x1088);
    float sd = -((F(0xd58) - slide) * ts[2]);
    if (F(0x1084) <= sd) sd = F(0x1084);
    if (sd <= -F(0x1084)) sd = -F(0x1084);
    float v = F(0xd60), lo, hi;
    float tgt;
    if (I(0x104c) == -1 && F(0x1064) == K(0x5b23f8) && F(0x1068) == K(0x5b23f8) && F(0x106c) == K(0x5b23f8))
        tgt = ts[8] * K(0x5b2624);
    else
        tgt = (float)I(0x104c) * F(0xeb8) * (float)dir;
    float fw;
    bool driven = false;
    if (I(0x544) == 0) {
        hi = K(0x5b2580);
        lo = tgt;
        if (K(0x5b23f8) <= tgt) hi = tgt, lo = K(0x5b2628);
        if (lo <= v && v <= hi && I(0xeac) == dir) {
            fw = AI_SpeedFactor(c, v) * (float)I(0x104c);
            c->b[0xd80] = s_ticks < (int)c->b[0xd80] ? (uint8_t)(c->b[0xd80] - (uint8_t)s_ticks) : 0;
            if (c->b[0xd80] == 0) c->b[0xd7d] = 0;
            driven = true;
        }
    }
    if (!driven) fw = absf(F(0xd60)) <= K(0x5b23f8) ? K(0x5b23f8) : push(c, true);
    if (back) {
        float k = K(0x5b260c);
        if (K(0x5b2470) < F(0xec4) && F(0xec4) < K(0x5b251c)) k = K(0x5b2468);
        tq *= k;
        fw *= k;
        sd *= K(0x5b2620);
    }
    F(0xd54) = fw;
    F(0xd4c) = sd;
    F(0xd50) = 0;
    F(0xd70) = F(0xd74) = F(0xd78) = 0;
    F(0xd34) = 0;
    F(0xd38) = tq;
    F(0xd3c) = 0;
}

/* The forces into the velocities (0x40a230): the slope's pull with them (0x49eeb0), the car's own
   and the road's axes' forces into the world's, times the step; the velocity scaled by +0x1090 (an
   integer, as the original reads it) per tick while set; the turn's damped when barely moving forward,
   else stopped and the velocity kept along the car. */
static void integrate(Car *c)
{
    F(0xd54) += F(0xd78);
    CarSim_SlopePull(c);
    F(0xd54) -= F(0xd78);
    float m[9], r[9];
    Mat3_Transpose(CAR_F(c, 0x364), m);
    Mat3_Transpose(CAR_F(c, 0x3b8), r);
    const float *f = CAR_F(c, 0xd4c), *rf = CAR_F(c, 0xd70);
    float w[3];
    for (int k = 0; k < 3; k++) w[k] = dot3(f, m + 3 * k);
    for (int k = 0; k < 3; k++) w[k] = dot3(rf, r + 3 * k) + w[k];
    for (int k = 0; k < 3; k++) F(0xd40 + 4 * k) = w[k];
    float *t = CAR_F(c, 0xd34);
    float tw[3] = {dot3(t, m), dot3(t, m + 3), dot3(t, m + 6)};
    memcpy(t, tw, sizeof tw);
    float *v = CAR_F(c, 0x33c);
    for (int k = 0; k < 3; k++) v[k] = s_dt * F(0xd40 + 4 * k) + v[k];
    for (int i = 0; I(0x1090) != 0 && i < s_ticks; i++) {
        float s = (float)I(0x1090);
        for (int k = 0; k < 3; k++) v[k] = s * v[k];
    }
    for (int k = 0; k < 3; k++) F(0x388 + 4 * k) = s_dt * t[k] + F(0x388 + 4 * k);
    if (K(0x5b24a8) <= absf(F(0xd60))) {
        float k = absf(F(0xd60)) * K(0x5b2638);
        if (K(0x5b24a8) < k) k = K(0x5b24a8);
        F(0x38c) = k * F(0x38c);
        return;
    }
    F(0x38c) = 0;
    float along = dot3(CAR_F(c, 0x37c), v), u[3];
    Vec3_Unit(CAR_F(c, 0x37c), u);   /* 0x5326e0 into the velocity */
    for (int k = 0; k < 3; k++) v[k] = u[k] * along;
}

/* The speed (+0xec0) eased to its target (+0xeb8) by the engine's push or the brakes, in the car's
   direction along the road. 0x408ab0 */
static void ease_speed(Car *c)
{
    float acc = AI_SpeedFactor(c, F(0xec0));
    const uint8_t *sq = AI_SquaresTable(c);
    float brk = 0;
    if (sq) memcpy(&brk, sq + 0x80, 4);
    if (K(0x5b24a8) < F(0x108c)) brk *= F(0x108c);
    if (g_sim_tick < I(0x10a4)) F(0xeb8) = F(0xeb8) * K(0x5b2468);
    float tgt = F(0xeb8), sp = F(0xec0);
    int dir = I(0xeb0);
    bool easing = dir == -1 ? (sp > tgt || tgt == sp) && I(0xeac) == -1 : sp <= tgt && dir == I(0xeac);
    if (easing) {
        F(0xec0) = dir == -1 ? F(0xec0) - s_dt * acc : s_dt * acc + F(0xec0);
        c->b[0xd80] = s_ticks < (int)c->b[0xd80] ? (uint8_t)(c->b[0xd80] - (uint8_t)s_ticks) : 0;
        if (c->b[0xd80] == 0) c->b[0xd7d] = 0;
        F(0xd54) = acc;
        if (dir == -1 ? F(0xeb8) <= F(0xec0) : F(0xec0) <= F(0xeb8)) return;
        F(0xec0) = F(0xeb8);
        return;
    }
    F(0xd54) = brk;
    c->b[0xd80] = (int)c->b[0xd80] + s_ticks * 4 < 0xff ? (uint8_t)(c->b[0xd80] + (uint8_t)(s_ticks * 4)) : 0xff;
    if (3 < c->b[0xd80]) c->b[0xd7d] = 1;
    if (dir == -1) {
        float v = brk * s_dt + F(0xec0);
        F(0xec0) = v;
        if (I(0xeac) != -1) return;
        if (v <= F(0xeb8)) {
            F(0xec0) = v;
            return;
        }
        F(0xec0) = F(0xeb8);
    } else {
        float v = F(0xec0) - brk * s_dt;
        F(0xec0) = v;
        if (dir != I(0xeac)) return;
        F(0xec0) = F(0xeb8) < v ? v : F(0xeb8);
    }
}

/* The sideways speed toward the lateral target (+0xef0, at most the speed), or the car moved there at
   once when fast. 0x408d80 */
static void ease_lateral(Car *c)
{
    float d = F(0xec4) <= K(0x5b2600) ? 0.0f : F(0x1074) - F(0xed0);
    if (K(0x5b24dc) < F(0xec4)) {
        float *p = CAR_F(c, 0x330);
        const float *r = CAR_F(c, 0x3b8);
        for (int k = 0; k < 3; k++) p[k] = r[k] * d + p[k];
        F(0xef0) = 0;
        return;
    }
    float sp = absf(F(0xec0));
    F(0xef0) = d;
    if (d < -sp) F(0xef0) = -sp;
    else if (sp < d) F(0xef0) = sp;
}

/* Along the road (0x4088a0): the speed and the sideways speed eased, the velocity theirs along the car
   and the road's right, no turning, the orientation the road's when moving, the node ahead by the speed. */
static void on_rails(Car *c)
{
    if (I(0xecc) & 0x10) {
        F(0x1074) = F(0xed0);
        I(0xecc) &= ~0x10;
    }
    ease_speed(c);
    ease_lateral(c);
    float a[3], b[3];
    for (int k = 0; k < 3; k++) a[k] = F(0x37c + 4 * k) * ((float)I(0xeb0) * F(0xec0));
    for (int k = 0; k < 3; k++) b[k] = F(0x3b8 + 4 * k) * F(0xef0);
    for (int k = 0; k < 3; k++) F(0x33c + 4 * k) = a[k] + b[k];
    F(0xd60) = F(0xec4);
    float s = (float)I(0xeb0) * F(0xef0);
    F(0xd58) = s;
    I(0xdc0) = absf(s) <= absf(s) * K(0x5b2604) ? 0 : 0x3f000000;
    F(0x388) = F(0x38c) = F(0x390) = 0;
    if (K(0x5b2600) < F(0xec4)) Car_RoadFrame(c, I(0xeb0) == -1);
    float sp = absf(F(0xec0));
    int n = (int)lrintf(sp * K(0x5b2420));
    int by = n;
    if (0 <= n && n < cnt(1) && K(0x5b23f8) <= sp) by = cnt(1);
    if (cnt(2) < by) by = cnt(2);
    I(0x10b4) = step(car_node(c), by * I(0xeb0));
}

/* Thrown up, sideways, flying off the ground, or off the road's edge: back on the road (0x40f5a0). 0x4087b0 */
static void bounds(Car *c)
{
    bool fly = K(0x5b25fc) < F(0x344) || K(0x5b25fc) < F(0x33c) || (K(0x5b25fc) < F(0x35c) && c->b[0x7e] == 0);
    float w = node_f(car_node(c), 0x40);
    bool off = F(0xed0) < -w - K(0x5b25f8) || w + K(0x5b25f8) < F(0xed0);
    if (fly || off) CarSim_Respawn(c, 1);
}

static void stop(Car *c, uint32_t k)
{
    memset(c->b + 0xd34, 0, 12);
    memset(c->b + 0xd4c, 0, 12);
    memset(c->b + 0xd70, 0, 12);
    float s;
    memcpy(&s, &k, 4);
    for (int i = 0; i < 3; i++) F(0x33c + 4 * i) *= s;
    F(0x38c) = F(0x38c) * K(0x5b25f4);
}

void AI_DriveTask(void *car)
{
    Car *c = car;
    uint32_t f = (uint32_t)I(CAR_FLAGS);
    if (f & 1) return;
    if (!Sim_TaskRate(1, 1, F(0x78), I(0), &s_rate, &s_ticks, &s_dt, I(0x10b8) != 0)) return;
    if (f & 2) ground_speed(c);
    if (F(0xec4) < K(0x5b2430)) I(0xeb0) = I(0xeac);
    grip_back(c);
    indicators(c);
    F(0x1060) = dot3(CAR_F(c, 0x3b8), CAR_F(c, 0x33c));   /* 0x40ef90 */
    /* (+0x7e, the wheels down the test reads, is only ever cleared in the exe: every AI car counts as out
       of control and takes the forces; the along-the-road path 0x4088a0 is never reached) */
    bool ooc = Car_OutOfControl(c);
    lateral_target(c, ooc);
    if (I(0xecc) & 4) return;
    if (!c->b[0x7f]) return;
    if (g_race_started != 1) return;
    if (I(0xedc) < 1) {
        int g = gear(c);
        c->b[0xd82] = (uint8_t)g;
        F(0xdb4) = *SPEC_F(c, 0x60 + 4 * g) * F(0x35c);
    } else {
        float v = F(0xdb4) - K(0x5b242c);
        I(0xedc) -= s_ticks;
        if (v < K(0x5b23f8)) v = K(0x5b23f8);
        F(0xdb4) = v;
    }
    if (I(0x42c) == 1) {
        stop(c, 0x3f7ae148u);   /* 0.98 */
        return;
    }
    if (F(0xec4) < K(0x5b2404)) I(0xd60) = 0;
    int16_t hold;
    memcpy(&hold, c->b + 0x418, 2);
    if (hold != 0 || K(0x5b25f0) <= F(0x374)) {
        if (I(0x540) != 0) {
            stop(c, 0x3f7d70a4u);   /* 0.99 */
            return;
        }
        bool slow = (f & 1) && 1 < I(0xb50) && absf(F(0xec0)) < K(0x5b2580);
        if (!slow && F(0xeb8) != K(0x5b23f8)) {
            if (ooc) {
                I(0xecc) |= 0x10;
                reverse_out(c);
                if ((float)I(0xeb0) * F(0xec0) < K(0x5b23f8) && I(0x104c) != -1) F(0x1074) = 0;
                if (stuck(c)) reverse_for(c, 0x40);
                headings(c);
                if (!recovering(c)) forces_steer(c);
                else forces_recover(c);
                integrate(c);
            } else {
                on_rails(c);
            }
            bounds(c);
            if ((f & 8) && I(0x10a0) < g_sim_tick)
                I(0x10a0) = (int32_t)((SimRand_Next() & AI_PersU(c, 0x34)) + AI_PersU(c, 0x30)) + g_sim_tick;
            return;
        }
    }
    stop(c, 0x3f70a3d7u);   /* 0.94 */
}
