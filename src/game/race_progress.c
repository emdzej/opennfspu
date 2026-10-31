/* The race's progress (see race_progress.h). */
#include "game/race_progress.h"
#include "exe.h"
#include "game/car_drive.h"
#include "game/race_car.h"
#include "game/road.h"
#include "game/sim_rand.h"
#include "pending.h"
#include <math.h>
#include <string.h>

int32_t g_race_laps;
int32_t g_race_over;
int32_t g_race_local;
int32_t g_race_inputs;
int32_t g_hud_message;
int32_t g_race_end_wait[4] = {0, 0, -1, -1};
RaceRank g_race_rank[CAR_MAX];
Car *g_lead_human, *g_first_human, *g_first_rival, *g_last_rival;

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

#define K(va) exe_f32(va)

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }   /* Vec3_Dot */
static int16_t car_node(Car *c) { int16_t n; memcpy(&n, c->b + 8, 2); return n; }
static const float *node_vec(int n, int off) { return (const float *)(const void *)(g_road->nodes + n * ROAD_NODE_SIZE + off); }
static float node_f(int n, int off) { return *node_vec(n, off); }

/* The fraction past the whole number (truncated as __ftol does; negative ones raised by whole turns). */
static float frac(float x)
{
    if (x <= K(0x5b23f8)) {
        while (x < K(0x5b23f8)) x = x + K(0x5b24a8);
        return x;
    }
    return x - (float)(int32_t)x;
}

void Race_CountLap(Car *c)
{
    if (g_race_type == 4) return;
    float d = *CAR_F(c, 0xaf8), *old = CAR_F(c, 0xaf4);
    if (d == *old) return;
    int32_t *wrong = CAR_I(c, 0x534);
    if (*CAR_I(c, CAR_FLAGS) & 1) {
        if (!g_race_linear) {
            /* Back over the line: a lap to make up. */
            if (K(0x5b23f0) < frac(d) && frac(*old) < K(0x5b2604)) {
                *old = d;
                (*wrong)++;
                return;
            }
            if (!(frac(d) < K(0x5b2604)) || !(K(0x5b23f0) < frac(*old))) {
                *old = d;
                return;
            }
            /* Forward over it: the made-up lap first. */
            if (*wrong > 0) {
                (*wrong)--;
                *old = d;
                return;
            }
        } else if (d <= *CAR_F(c, 0xb04)) {
            if (d < *CAR_F(c, 0xb08)) PENDING_NOTE(0x40f5a0, "the car back at the start of a point-to-point race (0x40f5a0)");
            *old = d;
            return;
        }
        (*CAR_I(c, 0x530))++;
    }
    *old = d;
}

/* The car's distance: its laps plus the road's progress at its node, less the laps it owes. */
static float distance(Car *c, int node)
{
    return ((float)*CAR_I(c, 0x530) + Race_Progress(node)) - (float)*CAR_I(c, 0x534);
}

void Race_CarProgress(Car *c)
{
    int32_t tick = g_sim_tick, len = g_road->length, laps = *CAR_I(c, 0xb20);
    if (laps < 8 && *CAR_F(c, 0xad4 + 4 * laps) < *CAR_F(c, 0xd60)) *CAR_F(c, 0xad4 + 4 * laps) = *CAR_F(c, 0xd60);   /* top speed */
    if (*CAR_I(c, 0xb50) == 0) {
        if (g_race_mode == 3) PENDING_NOTE(0x416330, "the network race's progress (0x628c70, 0x4900a0, 0x48d990)");
        int node = car_node(c);
        if (*CAR_I(c, 0xb18) != node) {
            *CAR_I(c, 0xb10) = (*CAR_I(c, 0xb20) - *CAR_I(c, 0x534)) * len + node;
            *CAR_I(c, 0xb18) = node;
            *CAR_I(c, 0xb14) = tick;
            *CAR_F(c, 0xaf8) = distance(c, node);
            Race_CountLap(c);   /* 0x417460 */
            *CAR_F(c, 0xaf8) = distance(c, car_node(c));
        }
        if (*CAR_I(c, 0xb20) != *CAR_I(c, 0x530)) {
            /* A lap done: its time, then the elimination and the finish. */
            *CAR_I(c, 0xb28 + 4 * *CAR_I(c, 0xb20)) = tick - *CAR_I(c, 0xb24);
            *CAR_I(c, 0xb24) = tick;
            *CAR_I(c, 0xb20) = *CAR_I(c, 0x530);
            int32_t last = (g_race_laps - *CAR_I(c, 0x530)) + 2;
            if (g_race_type == 3 && *CAR_I(c, 0xb48) == last) {
                *CAR_I(c, 0xb50) = 3;
                *CAR_I(c, 0xb14) = g_sim_tick;
                *CAR_I(c, 0xb24) = 0;
                *CAR_F(c, 0xaf8) = *CAR_F(c, 0xaf8) - K(0x5b27a8);
                for (int i = 0; i < g_racers.n; i++) {
                    Car *o = g_racers.c[i];
                    if (*CAR_I(o, 0xb50) == 0 && last < *CAR_I(o, 0xb48)) {
                        *CAR_I(o, 0xb50) = 3;
                        *CAR_I(o, 0xb14) = g_sim_tick;
                        *CAR_F(o, 0xaf8) = *CAR_F(o, 0xaf8) - K(0x5b27a8);
                        *CAR_I(o, 0xb24) = 0;
                    }
                }
            }
            if (g_race_laps <= *CAR_I(c, 0xb20) && *CAR_I(c, 0xb50) == 0) {
                int node = car_node(c);
                *CAR_I(c, 0xb50) = 2;
                *CAR_I(c, 0xb14) = tick;
                *CAR_I(c, 0xb10) = (*CAR_I(c, 0xb20) + 1) * len;
                *CAR_F(c, 0xaf8) = g_race_linear ? Race_Progress(node) : (float)*CAR_I(c, 0xb20) + Race_Progress(node);
                *CAR_I(c, 0xb24) = *CAR_I(c, 0xb24) - 0x1c0;
                if (*CAR_I(c, CAR_INDEX) == g_race_local) {
                    PENDING_NOTE(0x48d990, "the network session's best lap");
                    g_hud_message = 0x14;
                }
            }
            if ((*CAR_I(c, CAR_FLAGS) & 4) && g_race_local < g_humans.n && g_humans.c[g_race_local] == c)
                PENDING_NOTE(0x427790, "the HUD's lap message (0x427790)");
        }
    }
    /* Into the ranking of distances (the latest tick first among equals); the rows after it move down. */
    float d = *CAR_F(c, 0xaf8);
    int i = 0;
    for (; i < g_racers.n; i++) {
        const RaceRank *r = &g_race_rank[i];
        if (!(d <= r->dist && (d != r->dist || r->tick <= *CAR_I(c, 0xb14)))) break;
    }
    if (i >= g_racers.n) return;
    for (int k = g_racers.n - 2; k >= i; k--) g_race_rank[k + 1] = g_race_rank[k];
    g_race_rank[i] = (RaceRank){*CAR_I(c, CAR_INDEX), d, *CAR_I(c, 0xb14), *CAR_I(c, CAR_FLAGS) & 4};
}

void CarSim_RaceTask(void *car)
{
    Car *c = car;
    if (g_sim_tick & 1) return;
    if (*CAR_I(c, CAR_FLAGS) & 1) {
        *CAR_F(c, 0xd00) = fabsf(*CAR_F(c, 0xd60)) * K(0x5b27ac) + *CAR_F(c, 0xd00);   /* the odometer */
        if (g_race_type == 4) {
            PENDING_NOTE(0x416190, "the skill test's progress (0x416190)");
            return;
        }
        Race_CarProgress(c);   /* 0x416330 */
        return;
    }
    *CAR_F(c, 0xaf8) = c->b[0x7f] ? Race_Progress(car_node(c)) : 0.0f;
}

/* The car's half extent across the road: its box's half sizes along the road's right (0x4176a0). */
static float across(Car *c)
{
    const float *rr = CAR_F(c, 0x3b8);
    float a = fabsf(dot3(rr, CAR_F(c, 0x364))), b = fabsf(dot3(rr, CAR_F(c, 0x370))), f = fabsf(dot3(rr, CAR_F(c, 0x37c)));
    return f * *CAR_F(c, 0x3b0) + (b * *CAR_F(c, 0x3ac) + a * *CAR_F(c, 0x3a8));
}

/* The lane at offset x from the node's centre line: 7 and up to the right by the lane width (the table
   of 1 / width at 0x5e7550, made by 0x406900), 6 and down to the left; 2 .. 11. */
static int lane_at(int node, float x)
{
    float w = x >= K(0x5b23f8) ? node_f(node, 0x4c) : node_f(node, 0x48);
    int base = x >= K(0x5b23f8) ? 7 : 6;
    long iw = lrintf(w);
    float inv = iw <= 0 ? 0.0f : 1.0f / (float)iw;   /* the table: 0, then 1 / i up to 19 */
    int i = base + (int)lrintf(inv * x);
    return i < 2 ? 2 : i > 11 ? 11 : i;
}

/* The lanes the car covers (+0xea8, a bit per lane) and the one its centre is in (+0x1020). 0x40ec10 */
static void lanes(Car *c)
{
    *CAR_I(c, 0xea8) = 0;
    if (*CAR_I(c, 0xecc) & 4) return;
    int node = car_node(c);
    float lat = *CAR_F(c, 0xed0), ext = *CAR_F(c, 0x538);
    *CAR_I(c, 0x1020) = lane_at(node, lat);
    int a = lane_at(node, (lat - ext) + K(0x5b23f0));
    /* The far side's lane, clamped the same way except that 12 and 13 pass (and leave no lanes). */
    float x = (lat + ext) - K(0x5b23f0);
    float w = x >= K(0x5b23f8) ? node_f(node, 0x4c) : node_f(node, 0x48);
    long iw = lrintf(w);
    float inv = iw <= 0 ? 0.0f : 1.0f / (float)iw;
    int b = (x >= K(0x5b23f8) ? 7 : 6) + (int)lrintf(inv * x);
    if (b < 2) b = 2;
    else if (b >= 12) b = 11;
    if (a >= 0 && a < 14)
        for (int i = a; i <= b; i++) *CAR_I(c, 0xea8) |= 1 << (i & 31);
}

/* On the ground and under control (0x408870). */
static bool steady(Car *c)
{
    uint32_t f = (uint32_t)*CAR_I(c, CAR_FLAGS);
    return !(!(f & 0x800) && c->b[0x7e] != 0 && (!(f & 0x20) || 1 < c->b[0x7e]));
}

static float rank_key(Car *c) { return *CAR_F(c, 0xaf8) - (float)*CAR_I(c, 0xb20); }

/* The cars' order: by distance less laps (+0x528 the place), and by distance (each swap noted in both
   cars, +0x550 and +0x554). Bubble sorts, as the original. 0x417730 */
static void positions(void)
{
    int n = g_ncars;
    bool swapped;
    do {
        swapped = false;
        if (n - 1 < 1) break;
        for (int i = 1; i < n; i++) {
            Car *a = g_ranked.c[i - 1], *b = g_ranked.c[i];
            if (rank_key(b) < rank_key(a)) {
                g_ranked.c[i - 1] = b;
                g_ranked.c[i] = a;
                swapped = true;
            }
        }
    } while (swapped);
    for (int i = 0; i < n; i++) *CAR_I(g_ranked.c[i], 0x528) = i;
    do {
        swapped = false;
        if (n == 1 || n - 1 < 0) return;
        for (int i = 1; i < n; i++) {
            Car *a = g_ordered.c[i - 1], *b = g_ordered.c[i];
            if (*CAR_F(b, 0xaf8) < *CAR_F(a, 0xaf8)) {
                a->p[CARP_SWAPPED] = b;
                b->p[CARP_SWAPPED] = a;
                *CAR_I(b, 0x554) = g_sim_tick;
                *CAR_I(a, 0x554) = g_sim_tick;
                g_ordered.c[i - 1] = b;
                g_ordered.c[i] = a;
                swapped = true;
            }
        }
    } while (swapped);
}

/* From the front of the order: the first human still racing, the first human, the rivals (each given the
   one ahead and its place). 0x40b3f0 */
static void leaders(void)
{
    g_lead_human = g_first_human = g_first_rival = g_last_rival = NULL;
    Car *prev = NULL;
    int place = 0;
    for (int i = g_ncars - 1; i >= 0; i--) {
        Car *c = g_ordered.c[i];
        uint32_t f = (uint32_t)*CAR_I(c, CAR_FLAGS);
        if ((f & 1) && !g_lead_human && *CAR_I(c, 0x10c0) == 0) g_lead_human = c;
        if (f & 8) {
            c->p[CARP_PREVRIVAL] = prev;
            *CAR_I(c, 0x10d0) = place++;
            prev = c;
            g_last_rival = c;
            if (!g_first_rival) g_first_rival = c;
        }
        if ((f & 4) && !g_first_human) g_first_human = c;
    }
}

void Race_TickEven(void)
{
    for (int i = 0; i < g_ncars; i++) {
        Car *c = g_cars[i];
        if (!c->b[0x7f]) continue;
        *CAR_F(c, 0x538) = across(c);
        int node = car_node(c);
        const float *np = node_vec(node, 4), *pos = CAR_F(c, 0x330);
        float d[3] = {pos[0] - np[0], pos[1] - np[1], pos[2] - np[2]};
        *CAR_F(c, 0xed0) = dot3(CAR_F(c, 0x3b8), d);   /* the offset across the road */
        lanes(c);
        if (steady(c)) {
            const float *v = CAR_F(c, 0x33c), *rf = CAR_F(c, 0x3d0);
            *CAR_F(c, 0xec0) = rf[2] * v[2] + rf[1] * v[1] + rf[0] * v[0];   /* the speed along the road */
        }
        if (*CAR_I(c, CAR_FLAGS) & 4) {
            const float *v = CAR_F(c, 0x33c);
            *CAR_F(c, 0xec4) = (float)sqrt((double)v[1] * v[1] + (double)v[0] * v[0] + (double)v[2] * v[2]);
            float along = *CAR_F(c, 0xec0);
            if (along <= K(0x5b23fc) || 3.0f <= along)
                *CAR_I(c, 0xeb0) = along < K(0x5b23f8) ? -1 : 1;
            else
                *CAR_I(c, 0xeb0) = dot3(CAR_F(c, 0x37c), node_vec(node, 0x1c)) <= K(0x5b23f8) ? -1 : 1;
        }
        if (!(*CAR_I(c, CAR_FLAGS) & 1)) Race_CountLap(c);
    }
    positions();   /* 0x417730 */
    /* The generator stepped by the first human's steering (a sixteenth of it), with opponents about. */
    if (g_opponents.n != 0 && g_humans.n > 0) {
        int32_t s = *CAR_I(g_humans.c[0], 0xd94);
        for (int k = (s + (s >> 31 & 15)) >> 4; k > 0; k--) SimRand_Next();
    }
    leaders();   /* 0x40b3f0 */
}

void Race_TickOdd(void)
{
    float per = (float)g_road->length * K(0x5b2460);
    if (1 < g_racers.n && (g_sim_tick & 0x3f) == 1)
        for (int i = 0; i < g_racers.n; i++) {
            Car *c = g_racers.c[i];
            float d = (float)g_race_laps;
            if (*CAR_F(c, 0xaf8) <= d) d = *CAR_F(c, 0xaf8);
            *CAR_I(c, 0xb58) = (int32_t)lrintf(d * per);
            *CAR_I(c, 0xb54) = *CAR_I(c, 0xb58);
        }
    if (g_race_over) return;
    int done = 0;
    for (int i = 0; i < g_humans.n; i++) {
        Car *c = g_humans.c[i];
        int32_t at = *CAR_I(c, 0xb14);
        bool marks = g_race_end_wait[0] == g_race_end_wait[1] && g_race_end_wait[2] == -1 && g_race_end_wait[3] == -1;
        if (1 < *CAR_I(c, 0xb50) && at + 0x140 < g_sim_tick && (marks || at + 0x280 < g_sim_tick)) done++;
    }
    if (g_race_mode != 3 && done == g_humans.n) {
        g_race_over = 1;
        PENDING_NOTE(0x4170f0, "Race_End(2): the results");
    }
}

void Race_WrongWay(void)
{
    for (int i = 0; i < g_humans.n; i++) {
        Car *c = g_humans.c[i];
        if (!c) continue;
        float d = K(0x5b23f8);
        if (!(*CAR_I(c, CAR_FLAGS) & 0x200)) {
            int16_t node;
            memcpy(&node, c->b + 8, 2);
            d = dot3(CAR_F(c, 0x37c), node_vec(node, 0x1c));
            if (g_race_reverse) d = -d;
        } else if (*CAR_I(c, 0xeac) != *CAR_I(c, 0xeb0)) {
            d = K(0x5b2624);
        }
        int32_t *n = CAR_I(c, 0xd30);
        if (*CAR_I(c, 0xb50) == 2 || *CAR_I(c, 0xda8) || *CAR_I(c, 0x544) || g_race_mode == 9 || K(0x5b23f8) <= d) {
            *n = 0;
        } else if (*(int16_t *)(void *)(c->b + 0x418) == 0 && *CAR_I(c, 0x1104) == 0) {
            (*n)++;
            if (0x3f < *n && (*n & 0x1f) == 0 && *n < 0x94) PENDING_NOTE(0x4aac40, "the wrong way's speech");
        }
    }
}
