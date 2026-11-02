/* The police (the [ai] module's AIHigh_Cop, 0x403e90) and the cars they pursue (the humans' and the
   racers' controllers share a base, 0x403d10 / 0x405c90).

   The track's police script (GameData\Track\Cops\<track>.cop, AI_LoadCopScript 0x403040) lists events
   by road node, sorted: a police car waiting in the purgatory comes on when a pursued racer passes an
   event's node (0x4054d0) with fewer police of its kind on it than its record wants, either patrolling
   the road from there (type 1) or as a roadblock (type 3). A patrolling car that sees a speeder (the
   pursued car's mode +0x78: speeding, the wrong side, touching or ramming the police, 0x4039f0) takes a
   place in its formation (0x403c30) and chases it; out of reach it goes back to the purgatory.

   The controllers keep the original's words (ai_internal.h). The pursued's (from +0x14): the formation's
   six pairs (cop number, car index) from +0x1c, each cop number's place from +0x4c (cop numbers 6 .. 8
   land on +0x64 .. +0x6c, which the base also uses: the original's), +0x68 the tick of the last
   change, +0x70 the police of each kind on it, +0x78 its mode, +0x84 the chance factor, +0x88 the
   node last checked, +0x8c the heat's time, +0x90 the heat table and +0x9c its record (exe addresses),
   +0x94 the level (never raised), +0xa0 / +0xa8 the heat's sum and rate, +0xac the ticks without
   police. The police's: +0x18 its kind (1 a SuperCop, flag 0x40), +0x1c its number, +0x20 the chase's
   end, +0x24 the car to chase after a block, +0x54, +0x58 the pursued (its car index + 1), +0x5c sent
   back, +0x60 its place, +0x64, +0x68 the formation. */
#include "game/ai_police.h"
#include "game/ai.h"
#include "game/ai_internal.h"
#include "game/ai_lane.h"
#include "game/car_drive.h"
#include "game/race_car.h"
#include "game/race_progress.h"
#include "game/race_car.h"
#include "game/road.h"
#include "game/sim_rand.h"
#include "game/track.h"
#include "exe.h"
#include "pending.h"
#include "vfs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
#define K(va) exe_f32(va)
#define P(h, off) (HW(h, off))

static float absf(float v) { return v < K(0x5b23f8) ? -v : v; }
static int dir_of_race(void) { return g_race_reverse ? -1 : 1; }
static int16_t car_node(Car *c)
{
    int16_t n;
    memcpy(&n, c->b + 8, 2);
    return n;
}
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
/* The road distance from node b to node a, the short way round (0x40e860; 0x40e7a0 with a car's node). */
static float node_gap(int a, int b)
{
    int len = g_road->nnodes, d = a - b;
    if (0 < d && len / 2 < d) return (float)(d - len) * K(0x5b2460);
    if (d < 0 && d < -(len / 2)) d += len;
    return (float)d * K(0x5b2460);
}
static int idx_of(Car *c) { return *CAR_I(c, CAR_INDEX); }
static AiHigh *ctl_of(Car *c) { return AIi_Ctl(idx_of(c)); }
/* A controller word holding another controller (its car's index + 1). */
static AiHigh *ctl_ref(int32_t v) { return v ? AIi_Ctl(v - 1) : NULL; }
static int32_t ref_of(AiHigh *h) { return h ? idx_of(h->car) + 1 : 0; }
/* The heat record's words (exe data). */
static int32_t rec_i(const AiHigh *p, int off) { return (int32_t)exe_u32((uint32_t)P(p, 0x9c).i + (uint32_t)off); }
static float rec_f(const AiHigh *p, int off) { return K((uint32_t)P(p, 0x9c).i + (uint32_t)off); }

/* ---- the police script (0x5e752c: 0x34c bytes) ---- */

enum { DIR_MAX = 100, COPREC = 0x48 };
static struct {
    int32_t count;          /* [0] */
    float spacing;          /* [1]: 1 / count */
    int32_t rec[DIR_MAX];   /* [2 ..]: the records' offsets in the file */
    int32_t last[DIR_MAX];  /* [0x66 ..]: the tick each last went off */
    int32_t cursor[9];      /* [0xca ..]: each car's record */
} s_dir;
static uint8_t *s_cop;      /* the file (0x5e66b0) */
static size_t s_cop_size;

static int32_t cop_word(int32_t rec, int i)
{
    int32_t v = 0;
    if (rec >= 0 && (size_t)rec + 4u * (size_t)i + 4 <= s_cop_size) memcpy(&v, s_cop + rec + 4 * i, 4);
    return v;
}
static int cmp_node(const void *a, const void *b)   /* 0x40e650 */
{
    return cop_word(*(const int32_t *)a, 1) - cop_word(*(const int32_t *)b, 1);
}

/* The script from the file (0x40e410): every record's place by its type (1, 2: 0x14 bytes; 3: 0x48;
   5: 0x40 and 0x14 a point; any other none, the next read at the same place), sorted by node. */
static void dir_init(bool with_file)
{
    memset(&s_dir, 0, sizeof s_dir);
    for (int i = 0; i < DIR_MAX; i++) s_dir.last[i] = -0xa00;
    if (!with_file || !s_cop || s_cop_size < 4) return;
    int32_t n = cop_word(0, 0), at = 4;
    for (int i = 0; i < n && s_dir.count < DIR_MAX; i++) {   /* 0x40e4a0 */
        int32_t type = cop_word(at, 0), size = 0;
        if (type == 5) size = cop_word(at, 0xe) * 0x14 + 0x40;
        else if (type == 1 || type == 2) size = 0x14;
        else if (type == 3) size = 0x48;
        s_dir.rec[s_dir.count++] = at;
        at += size;
    }
    if (s_dir.count) qsort(s_dir.rec, (size_t)s_dir.count, sizeof *s_dir.rec, cmp_node);   /* (crt qsort) */
    if (s_dir.count) s_dir.spacing = K(0x5b24a8) / (float)s_dir.count;
}

void Police_LoadScript(void)
{
    free(s_cop);
    s_cop = NULL;
    s_cop_size = 0;
    if (!g_race_cops) return;
    char path[100];
    snprintf(path, sizeof path, "GameData\\Track\\Cops\\%s.cop", g_track_name);   /* "%s%s.cop" 0x5cbd50 */
    s_cop = vfs_read_all(path, &s_cop_size);
    dir_init(s_cop != NULL);
}

void Police_ResetScript(void)   /* 0x4030b0 */
{
    if (s_cop && g_race_cops) dir_init(true);
}

void Police_FreeScript(void)   /* 0x4030d0 */
{
    free(s_cop);
    s_cop = NULL;
    s_cop_size = 0;
}

/* The car's cursor moved to node n (0x40e510): on along the sorted records while they are before it
   (never past the last), back while after it (never before the first); the record there if it is at
   n, a while since it went off and not of type 2, else -1. */
static int dir_find(int car, int n)
{
    if (!s_dir.count || car < 0 || car >= 9) return -1;
    int32_t *cur = &s_dir.cursor[car];
    if (cop_word(s_dir.rec[*cur], 1) < n && 1 < s_dir.count) {
        while (*cur != s_dir.count - 1) {
            if (++*cur == s_dir.count) *cur = 0;
            if (!(cop_word(s_dir.rec[*cur], 1) < n)) break;
        }
    } else if (n < cop_word(s_dir.rec[*cur], 1) && 1 < s_dir.count) {
        while (*cur != 0) {
            if (--*cur == -1) *cur = s_dir.count - 1;
            if (!(n < cop_word(s_dir.rec[*cur], 1))) break;
        }
    }
    int i = *cur;
    if (cop_word(s_dir.rec[i], 1) == n && 0xa00 < g_sim_tick - s_dir.last[i] && cop_word(s_dir.rec[i], 0) != 2) return i;
    return -1;
}

/* No police car on the road (not parked, +0xecc bit 2) within d of node n (0x403120). */
static bool clear_of_police(int n, float d)
{
    for (int i = 0; i < g_chasers.n; i++) {
        Car *c = g_chasers.c[i];
        if (*CAR_I(c, 0xecc) & 4) continue;
        if (absf(node_gap(car_node(c), n)) < d) return false;
    }
    return true;
}

/* A human within 0x5b25d0 of node n (0x407d20). */
static Car *human_at(int n)
{
    for (int i = 0; i < g_humans.n; i++)
        if (absf(node_gap(car_node(g_humans.c[i]), n)) < K(0x5b25d0)) return g_humans.c[i];
    return NULL;
}

/* ---- the pursued (0x403d10, 0x405c90) ---- */

/* The heat's time (0x405af0, on +0x8c): t ticks, its rate 1 / t (scaled for two and four laps). */
static void heat_time(AiHigh *p, int32_t t)
{
    P(p, 0x8c).f = (float)t;
    float r = K(0x5b24a8) / (float)t;
    if (g_race_laps == 2) r *= K(0x5b2480);
    else if (g_race_laps == 4) r *= K(0x5b2520);
    P(p, 0xa8).f = r;
}
static int32_t heat_ticks(const AiHigh *p) { return (int32_t)lrintf((float)0xbe * rec_f(p, 0x24)) << 5; }   /* (0x5e7540: 0xbe) */
static float dir_spacing(void) { return s_dir.spacing; }

void Police_PursuedInit(AiHigh *p)
{
    /* 0x403d10 */
    for (int k = 0; k < 6; k++) P(p, 0x1c + 8 * k).i = -1, P(p, 0x20 + 8 * k).i = -1;
    P(p, 0x18).i = 0;
    P(p, 0x64).i = 0;
    P(p, 0x6c).i = 0;
    P(p, 0x68).i = -0x280;
    P(p, 0x70).i = P(p, 0x74).i = P(p, 0x78).i = 0;
    /* 0x405c90 */
    P(p, 0x90).i = 0x5cbd20;
    P(p, 0x8c).i = 0;
    P(p, 0x94).i = 0;
    P(p, 0x98).i = 0;
    P(p, 0x9c).i = (int32_t)exe_u32(0x5cbd24);
    P(p, 0xa4).i = 0;
    P(p, 0xac).i = 0;
    P(p, 0xa0).i = 0;
    P(p, 0xa8).i = 0;
    P(p, 0x7c).i = P(p, 0x80).i = 0;
    if (g_race_cops) {
        P(p, 0x84).f = dir_spacing() * rec_f(p, 0x20);
        P(p, 0x88).i = car_node(p->car);
        P(p, 0x94).i = 0;
        if (P(p, 0x98).i < 0) P(p, 0x98).i = 0;
        P(p, 0x9c).i = (int32_t)exe_u32((uint32_t)P(p, 0x90).i + 4);
        heat_time(p, heat_ticks(p));
        P(p, 0xa4).i = 0;
    }
}

/* The pursued's mode (0x4039f0, the vtable's third): once a while after the last change and in its
   first lap, touching a police car (4 when hard, 3 from none), speeding (1), fast on the wrong side or
   the wrong way (2); kept only when a police car is on the road. */
static void pursued_mode(AiHigh *p)
{
    Car *c = p->car;
    int mode = P(p, 0x78).i;
    float lim = K(0x5b265c);   /* 0x40afc0: the speed limit */
    if (g_sim_tick - P(p, 0x68).i < 0x280) return;
    if (1 < *CAR_I(c, 0xb50)) return;
    Car *by = AIi_CarByIndex(*CAR_I(c, 0x454) - 1);
    if (g_sim_tick - *CAR_I(c, 0x45c) < 0xf && by && (*CAR_I(by, CAR_FLAGS) & 0x20)) {
        if (K(0x5b2470) < *CAR_F(c, 0x458)) mode = 4;
        else if (mode == 0) mode = 3;
        else if (mode != 4) return;
    } else if (mode != 0 && mode != 4) {
        return;
    }
    float v = *CAR_F(c, 0xec0);
    if (absf(lim) < absf(v) && mode == 0) mode = 1;
    bool fast = false, check = true;
    if (node_i(1, 0x38) != 0) {   /* (0x5e7544: a road both ways) */
        if (K(0x5b24c0) <= (float)g_race_side * v) check = *CAR_I(c, 0x1020) <= 6;
        else check = 7 <= *CAR_I(c, 0x1020);
    } else {
        check = (float)dir_of_race() * v < K(0x5b23f8);
    }
    if (check) fast = K(0x5b2404) < absf(v);
    if (fast && mode == 0) mode = 2;
    if (0x1ff < g_sim_tick && mode != 0)
        for (int i = 0; i < g_ncars; i++) {
            uint32_t f = (uint32_t)*CAR_I(g_cars[i], CAR_FLAGS);
            if ((f & 0x220) && g_cars[i]->b[0x7f]) {
                P(p, 0x78).i = mode;
                break;
            }
        }
}

/* Its level (0x405a20): in its first lap the mode (the level never changes there); after it the
   first level again, the heat's time anew, no mode. */
static void pursued_level(AiHigh *p)
{
    int32_t lvl = P(p, 0x94).i;
    if (*CAR_I(p->car, 0xb50) < 2) {
        pursued_mode(p);
        if (lvl != P(p, 0x94).i) P(p, 0x84).f = dir_spacing() * rec_f(p, 0x20);
        return;
    }
    P(p, 0x94).i = 0;
    if (P(p, 0x98).i < 0) P(p, 0x98).i = 0;
    P(p, 0x9c).i = (int32_t)exe_u32((uint32_t)P(p, 0x90).i + 4);
    heat_time(p, heat_ticks(p));
    P(p, 0xa4).i = 0;
    P(p, 0x78).i = 0;
}

/* Which police may chase (0x405b30, +0xecc bit 3): those chasing or blocking already, then the others
   in turn while the racers' records want more of their kind, three in all for one racer (four, two
   with the split screen). */
static void assign_police(void)
{
    int32_t want[2] = {0, 0}, have[2] = {0, 0};
    int left = g_racers.n == 1 ? 3 : g_race_players != 1 ? 4 : 2;
    for (int i = 0; i < g_racers.n; i++) {
        AiHigh *r = ctl_of(g_racers.c[i]);
        if (!r) continue;
        want[0] += rec_i(r, 0xc) + rec_i(r, 0);
        want[1] += rec_i(r, 0x10) + rec_i(r, 4);
    }
    for (int i = 0; i < g_chasers.n; i++) {
        Car *c = g_chasers.c[i];
        AiHigh *h = ctl_of(c);
        if (!h) continue;
        int32_t e = P(h, 0x20).i;
        if (!(*CAR_I(c, 0xecc) & 4) || e == 1 || e == 2) {
            have[P(h, 0x18).i & 1]++;
            left--;
            *CAR_I(c, 0xecc) |= 8;
        } else {
            *CAR_I(c, 0xecc) &= ~8;
        }
    }
    for (int k = 0; 0 < left && k < g_chasers.n; k++) {
        Car *c = g_chasers.c[k];
        AiHigh *h = ctl_of(c);
        if (!h || (*CAR_I(c, 0xecc) & 8)) continue;
        int t = P(h, 0x18).i & 1;
        if (have[t] < want[t]) {
            have[t]++;
            *CAR_I(c, 0xecc) |= 8;
            left--;
        }
    }
}

void Police_PursuedTick(AiHigh *p)   /* 0x405dd0 */
{
    if (g_chasers.n == 0) return;
    assign_police();
    int dt = g_ai_ticks;
    bool ahead = K(0x5b23f8) <= (float)*CAR_I(p->car, 0xeb0) * *CAR_F(p->car, 0xec0);
    if (P(p, 0x74).i + P(p, 0x70).i < 1) {
        P(p, 0xac).i += dt;
    } else {
        P(p, 0xac).i = 0;
        if (-2 < (int32_t)lrintf(P(p, 0x8c).f)) {
            P(p, 0x8c).f = ahead ? P(p, 0x8c).f - (float)dt : P(p, 0x8c).f - (float)dt * K(0x5b23f0);
            if ((int32_t)lrintf(P(p, 0x8c).f) < ((int32_t)lrintf((float)0xbe * rec_f(p, 0x24)) - 4) * 0x20)
                P(p, 0xa0).f = (float)dt * P(p, 0xa8).f + P(p, 0xa0).f;
        }
    }
    pursued_level(p);
}

/* The formation: a police car's place moved up past the cars behind the one ahead of it (0x403c60),
   released (0x403c00), taken at the back (0x403c30). */
static int form_bubble(AiHigh *p, int num, int car)
{
    int k = P(p, 0x4c + 4 * num).i;
    AiHigh *self = AIi_Ctl(car);
    int n = self ? car_node(self->car) : 0;
    while (0 < k) {
        int32_t pa = P(p, 0x14 + 8 * k).i, pb = P(p, 0x18 + 8 * k).i;   /* the pair ahead (k - 1) */
        if (pa != -1) {
            AiHigh *o = pb != -1 ? AIi_Ctl(pb) : NULL;
            if (o && K(0x5b24c8) <= node_gap(car_node(o->car), n) * (float)*CAR_I(p->car, 0xeb0)) return k;
            P(p, 0x4c + 4 * pa).i = k;
        }
        k--;
        P(p, 0x4c + 4 * num).i = k;
        P(p, 0x1c + 8 * (k + 1)).i = pa;
        P(p, 0x20 + 8 * (k + 1)).i = pb;
        P(p, 0x1c + 8 * k).i = num;
        P(p, 0x20 + 8 * k).i = car;
    }
    return k;
}
static void form_release(AiHigh *p, int num, int type)
{
    int k = P(p, 0x4c + 4 * num).i;
    P(p, 0x70 + 4 * type).i--;
    P(p, 0x1c + 8 * k).i = -1;
    P(p, 0x20 + 8 * k).i = -1;
}
static void form_take(AiHigh *p, int num, int car, int type)
{
    P(p, 0x70 + 4 * type).i++;
    P(p, 0x44).i = num;
    P(p, 0x48).i = car;
    P(p, 0x4c + 4 * num).i = 5;
    form_bubble(p, num, car);
}

/* ---- the chase (AIState_Chase, vtable 0x5b26c8: 0x40cb80) ----

   Its words: +8 the blend (the state's target field), from +0x10 the follow base's (0x403680): +0x10
   the car followed (its index + 1), +0x14 the gap to it, +0x18 its node ahead by that, +0x1c its offset
   and +0x28 its place smoothed, +0x34 / +0x38 its lateral offset, +0x3c its lane, +0x40 its speed;
   then +0x44 the tick it may turn, +0x48 the pursued, +0x4c its place in the formation (x, y, z) and
   +0x58 / +0x5c that place's side (z, x), +0x60 / +0x64 the pursued's and its own direction, +0x68 how
   far ahead the pursued is, +0x6c the lateral gap, +0x70 / +0x74 an event and its end, +0x78 the ticks
   held in place, +0x7c the ram budget, +0x80 / +0x84 the window no human may be in, +0x88 the
   formation, +0x8c the ram's start, +0x90 the ticks blocked. */

#define CW(s, off) (SW(s, off))
static Car *cw_car(AiState *s, int off) { return AIi_CarByIndex(CW(s, off).i - 1); }
static void vsub(const float *a, const float *b, float *o) { o[0] = a[0] - b[0], o[1] = a[1] - b[1], o[2] = a[2] - b[2]; }   /* 0x5320a0 */

/* The followed car taken (0x4036b0). */
static void follow_init(AiState *s, Car *t)
{
    Car *c = s->car;
    CW(s, 0x10).i = idx_of(t) + 1;
    CW(s, 0x14).f = AI_Gap(c, t);
    CW(s, 0x18).i = car_node(t);
    float v[3];
    vsub(CAR_F(c, 0x330), CAR_F(t, 0x330), v);
    for (int k = 0; k < 3; k++) CW(s, 0x1c + 4 * k).f = v[k], CW(s, 0x28 + 4 * k).f = CAR_F(t, 0x330)[k];
    CW(s, 0x34).f = *CAR_F(c, 0xed0) - *CAR_F(t, 0xed0);
    CW(s, 0x38).f = *CAR_F(t, 0xed0);
    CW(s, 0x3c).i = *CAR_I(t, 0x1020);
    CW(s, 0x40).f = *CAR_F(t, 0xec0);
}

/* Each tick the followed car's gap, offset, lateral offset and speed eased toward it by the blend, its
   node and lane from them (0x403750; the gap's and the offset's first values have the other sign: the
   original's). */
static void follow_update(AiState *s)
{
    Car *c = s->car, *t = cw_car(s, 0x10);
    if (!t) return;
    float b = s->target;
    float g = (AI_Gap(t, c) - CW(s, 0x14).f) * b + CW(s, 0x14).f;
    CW(s, 0x14).f = g;
    CW(s, 0x18).i = Road_Step(g_road, car_node(c), (int)lrintf(g * K(0x5b2420)), -1);
    float v[3], d[3];
    vsub(CAR_F(t, 0x330), CAR_F(c, 0x330), v);
    for (int k = 0; k < 3; k++) d[k] = v[k] - CW(s, 0x1c + 4 * k).f;
    for (int k = 0; k < 3; k++) {
        CW(s, 0x1c + 4 * k).f = d[k] * b + CW(s, 0x1c + 4 * k).f;
        CW(s, 0x28 + 4 * k).f = CAR_F(c, 0x330)[k] + CW(s, 0x1c + 4 * k).f;   /* 0x532070 */
    }
    float l = ((*CAR_F(t, 0xed0) - *CAR_F(c, 0xed0)) - CW(s, 0x34).f) * b + CW(s, 0x34).f;
    CW(s, 0x34).f = l;
    CW(s, 0x38).f = l + *CAR_F(c, 0xed0);
    CW(s, 0x3c).i = Race_LaneAt(CW(s, 0x18).i, CW(s, 0x38).f);
    CW(s, 0x40).f = (*CAR_F(t, 0xec0) - CW(s, 0x40).f) * b + CW(s, 0x40).f;
}

/* How far ahead of the car a place on the road at node n is (0x40e980): by nodes, near by the ground
   along the car's forward. */
static float ahead_of(Car *c, int n, const float *at)
{
    float g = node_gap(car_node(c), n);
    if (g <= K(0x5b240c) && K(0x5b24c8) <= g) {
        float v[3];
        vsub(CAR_F(c, 0x330), at, v);
        g = *CAR_F(c, 0x3d8) * v[2] + *CAR_F(c, 0x3d0) * v[0];
    }
    return g;
}

static void chase_look(AiState *s)   /* 0x40cd90 */
{
    Car *c = s->car;
    follow_update(s);
    CW(s, 0x64).i = *CAR_F(c, 0xec0) < K(0x5b23f8) ? -1 : 1;
    CW(s, 0x60).i = CW(s, 0x40).f < K(0x5b23f8) ? -1 : 1;
    CW(s, 0x6c).f = *CAR_F(c, 0xed0) - CW(s, 0x38).f;
    float at[3] = {CW(s, 0x28).f, CW(s, 0x2c).f, CW(s, 0x30).f};
    CW(s, 0x68).f = ahead_of(c, CW(s, 0x18).i, at);
    memset(c->b + 0x1064, 0, 16);
    if (CW(s, 0x70).i && CW(s, 0x74).i <= g_sim_tick) CW(s, 0x70).i = CW(s, 0x74).i = 0;
    if (*CAR_I(c, 0x1098) != 0x3f800000) CW(s, 0x7c).i -= g_ai_ticks;
    *CAR_F(c, 0x1098) = *CAR_F(c, 0x109c) = *CAR_F(c, 0x1078) = *CAR_F(c, 0x107c) = 1.0f;
}

/* The pursued and the police car's place by it (0x40ccc0): the side of the pursued that place is on. */
static void chase_aim(AiState *s, Car *p, const float off[3])
{
    if (CW(s, 0x48).i != idx_of(p) + 1) follow_init(s, p);
    CW(s, 0x48).i = idx_of(p) + 1;
    for (int k = 0; k < 3; k++) CW(s, 0x4c + 4 * k).f = off[k];
    CW(s, 0x58).i = CW(s, 0x5c).i = 0;
    if (off[0] < -*CAR_F(p, 0x3a8)) CW(s, 0x5c).i = -1;
    else if (*CAR_F(p, 0x3a8) < off[0]) CW(s, 0x5c).i = 1;
    if (off[2] < -*CAR_F(p, 0x3b0)) CW(s, 0x58).i = -1;
    else if (*CAR_F(p, 0x3b0) < off[2]) CW(s, 0x58).i = 1;
    CW(s, 0x70).i = CW(s, 0x74).i = CW(s, 0x78).i = 0;
    chase_look(s);
}

static void chase_event(AiState *s, int ev, int dt)   /* 0x40cd70 */
{
    if (CW(s, 0x70).i == 0) {
        CW(s, 0x70).i = ev;
        CW(s, 0x74).i = g_sim_tick + dt;
    }
}

/* The ram (0x40cea0): with budget left and its time come, the engine and speed raised (3, 2.5); back to 1
   with a human just ahead within the window or just behind. */
static void ram(AiState *s, bool check)
{
    Car *c = s->car;
    if (!(0 < CW(s, 0x7c).i && CW(s, 0x8c).i <= g_sim_tick)) return;
    *CAR_F(c, 0x1098) = 3.0f;
    *CAR_F(c, 0x109c) = 2.5f;
    if (!check) return;
    for (int i = 0; i < g_humans.n; i++) {
        float g = AI_Gap(c, g_humans.c[i]) * (float)*CAR_I(c, 0xeb0);
        if (K(0x5b23f8) < g && g < CW(s, 0x80).f) *CAR_F(c, 0x1098) = *CAR_F(c, 0x109c) = 1.0f;
        if (!(K(0x5b23f8) <= g || g <= -CW(s, 0x84).f)) *CAR_F(c, 0x1098) = *CAR_F(c, 0x109c) = 1.0f;
    }
}

/* The lanes between two lanes open at a node (0x40eb10: the node's closed-lane bits +0x34). */
static uint32_t open_between(int n, int a, int b)
{
    uint32_t m = (uint32_t)node_i(n, 0x34);
    int lo = 0x16 - a, hi = 0x16 - b;
    if (hi < lo) return ~(~0u << ((lo - hi + 1) & 31)) & (uint32_t)((int32_t)~m >> (hi & 31));
    return ~(~0u << ((hi - lo + 1) & 31)) & (uint32_t)((int32_t)~m >> (lo & 31));
}

/* Blocked from the pursued's lane (0x40da20): near it, the road open between the lanes at its node but
   not at the police car's, the pursued's lane favoured three times; else the ticks blocked counted. */
static void lanes_block(AiState *s)
{
    Car *c = s->car, *p = cw_car(s, 0x48);
    if (!p || !(absf(CW(s, 0x68).f) <= K(0x5b24e0))) return;
    int n = car_node(c), l = *CAR_I(c, 0x1020), pn = car_node(p), pl = *CAR_I(p, 0x1020);
    if (!(6 - node_i(n, 0x38) <= l && l <= node_i(n, 0x3c) + 7 && 6 - node_i(pn, 0x38) <= pl && pl <= node_i(pn, 0x3c) + 7)) return;
    uint32_t a = open_between(pn, pl, l), b = open_between(n, pl, l);
    if (a) {
        if (!b) {
            CW(s, 0x90).i = 0;
            for (int k = 0; k < 3; k++) AI_LaneBump(c, pl);
            return;
        }
        CW(s, 0x90).i += g_ai_ticks;
        return;
    }
    CW(s, 0x90).i = 0;
}

static void lane_steps(AiState *s)
{
    AI_LaneReset(s->car);
    lanes_block(s);
    AI_LaneScore(s->car);
    AI_LaneChoose(s->car);
}
static void turn_to(AiState *s)
{
    if (CW(s, 0x44).i < g_sim_tick) *CAR_I(s->car, 0xeac) = CW(s, 0x68).f <= K(0x5b23f8) ? 1 : -1;
}

/* Alongside or past it the same way (0x40d170): toward its place in the formation (the side it is on,
   or a car's width past it), the speed by how far it is from that place, held there a while. */
static void chase_pass(AiState *s)
{
    Car *c = s->car, *p = cw_car(s, 0x48);
    float sx = 0, sz = 0, dir = (float)*CAR_I(c, 0xeb0);
    ram(s, true);
    float wx, wz;
    if (CW(s, 0x70).i == 0) {
        wx = (float)CW(s, 0x5c).i;
        wz = (float)CW(s, 0x58).i;
    } else {
        wx = wz = 0;
        ram(s, false);
    }
    float lx = dir * CW(s, 0x6c).f, lz = dir * CW(s, 0x68).f;
    if (lx < -*CAR_F(p, 0x3a8)) sx = -1;
    else if (*CAR_F(p, 0x3a8) < lx) sx = 1;
    float f4 = K(0x5b2624);
    if (-(*CAR_F(p, 0x3b0) - K(0x5b2580)) <= lz) f4 = lz <= *CAR_F(p, 0x3b0) + K(0x5b2580) ? 0.0f : K(0x5b24a8);
    float e = *CAR_F(p, 0x3b0) + K(0x5b2580);
    if (lz < -e) sz = -1;
    else if (e < lz) sz = 1;
    if (sx == wx && f4 == wz) CW(s, 0x78).i += g_ai_ticks;
    else CW(s, 0x78).i = 0;
    int mode = 0, side = 0;
    bool steer = true;
    if (wz * sz == K(0x5b2624)) {
        side = (int)wx;
        if (side == 0) side = -1;
    } else if (wx * sx == K(0x5b2624) && sz == K(0x5b23f8)) {
        mode = -1;
        steer = false;
    } else if (wx == 0 && sz == K(0x5b23f8) && CW(s, 0x70).i == 0) {
        steer = false;
    } else if (wz == 1 && sz == K(0x5b24a8) && sx == K(0x5b23f8) && CW(s, 0x68).f < K(0x5b2470)) {
        mode = -2;
    }
    float v = absf(CW(s, 0x40).f), a = CW(s, 0x68).f;
    if (f4 < wz || mode == 1) {
        v *= K(0x5b26e0);
    } else if (wz < f4 || mode == -1 || (K(0x5b2470) < a && (K(0x5b241c) <= a || sx != K(0x5b23f8) || wx == 0))) {
        float d = absf(a), k;
        if (d < K(0x5b2408)) k = K(0x5b2610);
        else if (d < K(0x5b242c)) k = K(0x5b24e8);
        else if (d < K(0x5b248c)) k = K(0x5b260c);
        else k = d < K(0x5b2484) ? K(0x5b2618) : K(0x5b2678);
        v *= k;
    } else if (mode == -2) {
        int f = CW(s, 0x88).i;
        v *= f == 0 ? K(0x5b26ec) : f == 1 ? K(0x5b26e8) : K(0x5b26e4);
    }
    AIi_SpeedTarget(c);
    *CAR_F(c, 0xeb8) = absf(*CAR_F(c, 0xeb8));
    if (*CAR_F(c, 0xeb8) <= v) v = *CAR_F(c, 0xeb8);
    if (v < K(0x5b2400)) v = K(0x5b2400);
    *CAR_F(c, 0xeb8) = dir * v;
    AI_LaneReset(c);
    if (steer) {
        float x, k = 15.0f;
        if (CW(s, 0x70).i == 0) {
            float o = side == 0 ? CW(s, 0x4c).f : *CAR_F(p, 0x3a8) + K(0x5b2460);
            x = o * dir + CW(s, 0x38).f;
            int n = car_node(c);
            float lo = -(node_f(n, 0x48) * (float)node_i(n, 0x38)), hi = node_f(n, 0x4c) * (float)node_i(n, 0x3c);
            if (x <= lo) x = lo;
            if (hi <= x) x = hi;
        } else {
            x = CW(s, 0x38).f;
            k = 30.0f;
        }
        *CAR_F(c, 0x1078) = x;
        *CAR_F(c, 0x107c) = k;
    }
    lanes_block(s);
    AI_LaneScore(c);
    AI_LaneChoose(c);
}

/* Closing in from behind or the other way (0x40d6a0): the speed capped by the octagonal distance by the
   formation (its six bands), the pursued's place the target when near. */
static void chase_close(AiState *s, bool near)
{
    Car *c = s->car, *p = cw_car(s, 0x48);
    float x = absf(*CAR_F(c, 0x330) - *CAR_F(p, 0x330)), z = absf(*CAR_F(c, 0x338) - *CAR_F(p, 0x338));
    float d = x <= z ? x * K(0x5b2468) + z : z * K(0x5b2468) + x;
    CW(s, 0x78).i = 0;
    turn_to(s);
    AIi_SpeedTarget(c);
    int f = CW(s, 0x88).i;
    float cap;
    if (f == 0 || f == 1 || f == 2) {
        float b[6];   /* the caps beyond 0x5b248c, 0x5b242c, 0x5b251c, 0x5b24dc, 0x5b241c, 0x5b2460 */
        if (f == 2) {
            const uint32_t v[6] = {0x5b26d4, 0x5b26f4, 0x5b251c, 0x5b2410, 0x5b2450, 0x5b2460};
            for (int k = 0; k < 6; k++) b[k] = K(v[k]);
        } else {
            const uint32_t v[6] = {0x5b26f0, 0x5b251c, 0x5b2410, 0x5b25f8, 0x5b2470, 0x5b2600};
            for (int k = 0; k < 6; k++) b[k] = K(v[k]);
            if (f == 1) b[4] = K(0x5b241c);
        }
        const uint32_t lim[6] = {0x5b248c, 0x5b242c, 0x5b251c, 0x5b24dc, 0x5b241c, 0x5b2460};
        cap = K(0x5b23f8);
        for (int k = 0; k < 6; k++)
            if (K(lim[k]) < d) {
                cap = b[k];
                break;
            }
    } else {
        cap = K(0x5b24dc);
    }
    if (*CAR_I(c, 0xeb0) == 1) {
        if (!(cap <= *CAR_F(c, 0xeb8))) cap = *CAR_F(c, 0xeb8);
    } else {
        cap = -cap;
        if (!(*CAR_F(c, 0xeb8) <= cap)) cap = *CAR_F(c, 0xeb8);
    }
    *CAR_F(c, 0xeb8) = cap;
    lane_steps(s);
    if (near) {
        memcpy(c->b + 0x1064, CAR_F(p, 0x330), 12);
        *CAR_F(c, 0x1070) = *CAR_F(p, 0xed0);
        *CAR_F(c, 0x1078) = *CAR_F(p, 0xed0);
        *CAR_F(c, 0x107c) = 15.0f;
    }
}

static void chase_tick(AiState *s)   /* 0x40cf90 */
{
    Car *c = s->car, *p = cw_car(s, 0x48);
    if (!p) return;
    chase_look(s);
    float a = absf(CW(s, 0x68).f * K(0x5b26dc)), dv = absf(*CAR_F(c, 0xec0) - *CAR_F(p, 0xec0));
    bool far = dv < a && K(0x5b26d8) < absf(CW(s, 0x68).f);
    float ps = absf(*CAR_F(p, 0xec0));
    bool follow = far;
    if (!far) {
        if (CW(s, 0x60).i * CW(s, 0x64).i == 1 && K(0x5b2478) <= ps) {
            chase_pass(s);
        } else if (K(0x5b23f8) <= (float)CW(s, 0x64).i * CW(s, 0x68).f && K(0x5b2478) <= ps) {
            follow = true;
        } else {
            chase_close(s, !(K(0x5b2650) < dv) && !(K(0x5b26d4) < absf(CW(s, 0x68).f)));
        }
    }
    if (follow) {
        CW(s, 0x78).i = 0;
        turn_to(s);
        ram(s, true);
        AIi_SpeedTarget(c);
        lane_steps(s);
    }
    if (g_sim_tick < CW(s, 0x8c).i) {
        *CAR_F(c, 0x1098) = 0.5f;
        *CAR_F(c, 0x109c) = 0.75f;
    }
}

static void chase_done(AiState *s)   /* AIState_Chase_v1 0x40cc30 */
{
    Car *c = s->car;
    memset(c->b + 0x1064, 0, 16);
    *CAR_F(c, 0x1078) = 0;
    *CAR_F(c, 0x107c) = 0;
    *CAR_F(c, 0x1098) = *CAR_F(c, 0x109c) = 1.0f;
}
static bool chase_ready(AiState *s) { (void)s; return false; }   /* 0x403e10 */
static const AiStateVtbl chase_vt = {chase_tick, chase_done, chase_ready};

static AiState chase_new(Car *c, Car *p, const float off[3], int32_t budget, float w80, float w84, int form, float blend)
{
    AiState s = {.vt = &chase_vt, .car = c, .target = blend};
    follow_init(&s, p);
    CW(&s, 0x7c).i = budget;
    chase_aim(&s, p, off);
    *CAR_F(c, 0x1098) = *CAR_F(c, 0x109c) = 1.0f;
    CW(&s, 0x80).f = w80;
    CW(&s, 0x84).f = w84;
    CW(&s, 0x88).i = form;
    CW(&s, 0x8c).i = 0;
    CW(&s, 0x44).i = 0;
    CW(&s, 0x90).i = 0;
    return s;
}

/* A state's words from +8 (the base struct names +8 and +0xc for the idle state). */
static AiWord *ow(AiState *s, int off)
{
    if (off == 8) return (AiWord *)(void *)&s->target;
    if (off == 0xc) return (AiWord *)(void *)&s->hold;
    return &SW(s, off);
}

/* ---- the roadblock (AIState_Offroad, vtable 0x5b26fc: 0x40dbc0) ----

   Its words: +8 the node, +0xc its place, +0x18 its orientation (+0x3c its last row again), +0x48 the
   node it drives for once a racer is near, +0x4c that node's point, +0x58 the gap to it, +0x5c set off,
   +0x60 its speed, +0x64 the reaction's factor. Flag 0x800 on the car while it lasts. */
static void block_tick(AiState *s)   /* 0x40dcd0 */
{
    Car *c = s->car;
    if (ow(s, 0x5c)->i == 0) {
        memset(c->b + 0x33c, 0, 12);
        memset(c->b + 0x388, 0, 12);
        *CAR_F(c, 0xeb8) = 0;
        for (int k = 0; k < 3; k++) CAR_F(c, 0x330)[k] = ow(s, 0xc + 4 * k)->f;
        for (int k = 0; k < 9; k++) CAR_F(c, 0x364)[k] = ow(s, 0x18 + 4 * k)->f;
        memset(c->b + 0x1064, 0, 16);
        return;
    }
    ow(s, 0x58)->f = node_gap(car_node(c), ow(s, 0x48)->i);
    *CAR_F(c, 0xeb8) = ow(s, 0x60)->f;
    if (ow(s, 0x58)->f <= K(0x5b23f8)) {
        *CAR_I(c, 0xeac) = 1;
    } else {
        *CAR_I(c, 0xeac) = -1;
        *CAR_F(c, 0xeb8) = -*CAR_F(c, 0xeb8);
    }
    AI_LaneReset(c);
    AI_LaneScore(c);
    AI_LaneChoose(c);
    for (int k = 0; k < 3; k++) CAR_F(c, 0x1064)[k] = ow(s, 0x4c + 4 * k)->f;
    *CAR_F(c, 0x1070) = 0;
}
static void block_done(AiState *s)   /* AIState_Offroad_v1 0x40db40 */
{
    memset(s->car->b + 0x1064, 0, 16);
    *CAR_I(s->car, CAR_FLAGS) &= ~0x800;
}
static bool block_ready(AiState *s) { (void)s; return false; }
static const AiStateVtbl block_vt = {block_tick, block_done, block_ready};

static AiState block_new(Car *c, int node, const int32_t pos[3], const int32_t rot[9], int32_t speed, int32_t react, int32_t to)
{
    AiState s = {.vt = &block_vt, .car = c};
    ow(&s, 8)->i = node;
    ow(&s, 0x5c)->i = 0;
    for (int k = 0; k < 3; k++) ow(&s, 0xc + 4 * k)->i = pos[k];
    for (int k = 0; k < 9; k++) ow(&s, 0x18 + 4 * k)->i = rot[k];
    ow(&s, 0x60)->f = (float)speed * K(0x5b2708);
    for (int k = 0; k < 3; k++) ow(&s, 0x3c + 4 * k)->i = ow(&s, 0x30 + 4 * k)->i;
    ow(&s, 0x64)->i = react;
    ow(&s, 0x48)->i = to;
    const float *np = Road_NodePos(g_road, to);
    for (int k = 0; k < 3; k++) ow(&s, 0x4c + 4 * k)->f = np[k];
    *CAR_I(c, CAR_FLAGS) |= 0x800;
    return s;
}

/* A racer near enough to set it off (0x40dc70): within its speed times the factor (at least 0x5b2470). */
static void block_check(AiState *s, Car *r)
{
    float a = absf(AI_Ahead(s->car, r)), lim = *CAR_F(r, 0x35c) * ow(s, 0x64)->f;
    if (lim <= K(0x5b2470)) lim = K(0x5b2470);
    if (a < lim) ow(s, 0x5c)->i = 1;
}

/* The block put at node n (0x407a20): stopped there in the direction, at its place and orientation. */
static void place_block(Car *c, int n, int dir, const int32_t pos[3], const int32_t rot[9])
{
    memcpy(c->b + 8, &(int16_t){(int16_t)n}, 2);
    if (node_i(1, 0x38) == 0) dir = dir_of_race();
    *CAR_I(c, 0xeb0) = dir;
    *CAR_I(c, 0xeac) = dir;
    *CAR_F(c, 0xeb8) = 0;
    *CAR_F(c, 0xec0) = 0;
    *CAR_I(c, 0x544) = 0;   /* 0x40a9f0 */
    *CAR_I(c, 0x540) = 0;
    float zero[3] = {0, 0, 0};
    CarSim_Place(c, n, zero, *CAR_I(c, 0xeb0));
    memcpy(c->b + 0x330, pos, 12);
    memcpy(c->b + 0x364, rot, 36);
    memcpy(c->b + 0x490, rot, 36);
    memset(c->b + 0x33c, 0, 12);
    AIi_Settle(c, n);
}

/* ---- turning round (AIState_GotoSlice, vtable 0x5b2720: 0x40e1a0): toward node +8 ---- */
static void goto_tick(AiState *s)   /* 0x40e230 (its +0xc speed caps: the police pass 0) */
{
    Car *c = s->car;
    float g = -node_gap(car_node(c), ow(s, 8)->i);   /* 0x40e800 */
    *CAR_I(c, 0xeac) = g < K(0x5b23f8) ? -1 : 1;
    AIi_SpeedTarget(c);
    AI_LaneReset(c);
    AI_LaneScore(c);
    AI_LaneChoose(c);
    if (ow(s, 0xc)->i) PENDING_NOTE(0x40e230, "the turn's speed caps (+0xc)");
}
static void goto_done(AiState *s) { (void)s; }
static bool goto_ready(AiState *s) { (void)s; return false; }
static const AiStateVtbl goto_vt = {goto_tick, goto_done, goto_ready};
static AiState goto_new(Car *c, int node, int caps)
{
    memset(c->b + 0x1064, 0, 16);
    c->b[0x7f] = 1;
    AiState s = {.vt = &goto_vt, .car = c};
    ow(&s, 8)->i = node;
    ow(&s, 0xc)->i = caps;
    return s;
}

/* ---- the police ---- */

/* Its pursuit dropped (the original's repeated steps): out of the formation, the chase's end kept only
   when it gave up (1). */
static void drop(AiHigh *h)
{
    AiHigh *p = ctl_ref(P(h, 0x58).i);
    if (p) form_release(p, P(h, 0x1c).i, P(h, 0x18).i);
    if (P(h, 0x20).i != 1) P(h, 0x20).i = 0;
    P(h, 0x58).i = 0;
}
static void to_state(AiHigh *h, AiState s, int phase)
{
    AIi_SetState(h, s);
    h->phase = phase;
}
static void turn_back(Car *c)
{
    *CAR_I(c, 0xeac) = -dir_of_race();
    *CAR_I(c, 0x104c) = 1;
}

/* Too far from the car it follows (0x407240): more than 0x5b25c8 away (octagonally) unless a 0x400 car. */
static bool too_far(Car *c)
{
    Car *t = AIi_CarByIndex(*CAR_I(c, 0xed4) - 1);
    if (!t || (*CAR_I(c, CAR_FLAGS) & 0x400)) return false;
    float z = absf(*CAR_F(c, 0x338) - *CAR_F(t, 0x338)), x = absf(*CAR_F(c, 0x330) - *CAR_F(t, 0x330));
    float d = x <= z ? x * K(0x5b2468) + z : z * K(0x5b2468) + x;
    return K(0x5b25c8) < d;
}

/* The pursued car the police car should chase (0x4052b0): of the racers wanting more of its kind the
   nearest, else the nearest in any mode; taking its place in that one's formation. 1 when it changed. */
static int pick(AiHigh *h)
{
    int e = P(h, 0x20).i;
    if (e == 1 || e == 4 || e == 2) return 0;
    AiHigh *was = ctl_ref(P(h, 0x58).i), *best = NULL;
    float d = 10000.0f;
    int t = P(h, 0x18).i & 1;
    for (int i = 0; i < g_racers.n; i++) {
        AiHigh *r = ctl_of(g_racers.c[i]);
        if (!r) continue;
        int32_t want = P(r, 0x78).i ? rec_i(r, 4 * t) : 0, have = P(r, 0x70 + 4 * t).i;
        if (was && was == r) have--;
        if (have < want) {
            float g = absf(AI_Gap(h->car, r->car));
            if (g < d) d = g, best = r;
        }
    }
    if (!best)
        for (int i = 0; i < g_racers.n; i++) {
            AiHigh *r = ctl_of(g_racers.c[i]);
            if (!r) continue;
            float g = absf(AI_Gap(h->car, r->car));
            if (g < d && P(r, 0x78).i != 0) d = g, best = r;
        }
    if (best && best != was) {
        if (was) form_release(was, P(h, 0x1c).i, P(h, 0x18).i);
        P(h, 0x58).i = ref_of(best);
        form_take(best, P(h, 0x1c).i, idx_of(h->car), P(h, 0x18).i);
        P(h, 0x68).i = rec_i(best, 0x18 + 4 * P(h, 0x18).i);
        return 1;
    }
    if (was && !best) drop(h);
    return 0;
}

/* The event for this police car (0x4054d0): of the racers from the last in the order, one wanting more
   police of its kind (with none in its mode: wanting one when no police is within 500 nodes, twice the
   chance) a while after its last change; the records at the nodes it passed since the last look (31
   nodes on in its direction, five at most), one taken by chance (its factor, always with police on it
   already, never with a human near). Its record's offset, -1 if none. */
static int32_t event(AiHigh *h)
{
    for (int i = g_ncars - 1; 0 <= i; i--) {
        Car *c = g_ordered.c[i];
        if (!c || !(*CAR_I(c, CAR_FLAGS) & 1)) continue;
        AiHigh *r = ctl_of(c);
        if (!r) continue;
        int t = P(h, 0x18).i & 1;
        float mult = P(r, 0x84).f;
        int32_t have = P(r, 0x70 + 4 * t).i, want;
        if (P(r, 0x78).i == 0) {
            mult += mult;
            want = (0 < rec_i(r, 4 * t) && clear_of_police(car_node(r->car), 500.0f)) ? 1 : 0;
        } else {
            want = rec_i(r, 4 * t);
        }
        if (g_ai_level == 2) mult = 1.0f;
        if (!(0x1bf < g_sim_tick - P(r, 0x68).i && have < want)) continue;
        int dir = *CAR_F(c, 0xec0) < K(0x5b23f8) ? -1 : 1;
        int n = Road_Step(g_road, car_node(c), dir * 0x1f, -1);
        int last = P(r, 0x88).i, lo = n, hi = last;
        P(r, 0x88).i = n;
        if (last < n) lo = last, hi = n;
        if (!(lo < hi) || 5 < hi - lo) continue;
        for (int k = lo; k < hi; k++) {
            int ri = dir_find(idx_of(c), k);
            if (ri == -1) continue;
            int chance = (int)lrintf(mult * K(0x5b242c));
            uint32_t rnd = SimRand_Next();
            if (!human_at(k) && (have != 0 || (int)((rnd * 100) >> 16) < chance)) {
                s_dir.last[ri] = g_sim_tick;   /* 0x40e600 */
                return ri < s_dir.count ? s_dir.rec[ri] : -1;
            }
        }
    }
    return -1;
}

/* A patrol coming on at node n (0x4078f0): in its direction (the race's on a one-way road), at the
   road's edge parked (outside the edge lane, dir2's side) or in the outer lane driving at its speed
   target; then on the road there (0x4076f0). */
static void place_patrol(Car *c, int n, int dir, int dir2, int drive)
{
    memcpy(c->b + 8, &(int16_t){(int16_t)n}, 2);
    if (node_i(1, 0x38) == 0) dir = dir_of_race();
    *CAR_I(c, 0xeb0) = dir;
    *CAR_I(c, 0xeac) = dir;
    if (!drive) {
        if (dir2 == -1) *CAR_F(c, 0xeb4) = -(node_f(n, 0x48) * (float)node_i(n, 0x38)) - K(0x5b2580);
        else *CAR_F(c, 0xeb4) = node_f(n, 0x4c) * (float)node_i(n, 0x3c) + K(0x5b2580);
        *CAR_F(c, 0xeb8) = 0;
        *CAR_F(c, 0xec0) = 0;
    } else {
        if (dir == 1 || node_i(n, 0x38) == 0) {
            float w = node_f(n, 0x4c);
            *CAR_F(c, 0xeb4) = (float)node_i(n, 0x3c) * w - w * K(0x5b23f0);
        } else {
            float w = node_f(n, 0x48);
            *CAR_F(c, 0xeb4) = w * K(0x5b23f0) + -w * (float)node_i(n, 0x38);
        }
        AIi_SpeedTarget(c);
        *CAR_F(c, 0xec0) = *CAR_F(c, 0xeb8);
    }
    *CAR_F(c, 0x1074) = *CAR_F(c, 0xeb4);
    *CAR_F(c, 0xed0) = *CAR_F(c, 0xeb4);
    AIi_Place(c);
}

/* The police car's place in its pursued's formation (0x403c60) and that place's offset (0x5cbee8: six
   places of three floats per formation). */
static void form_offset(AiHigh *h, float off[3])
{
    AiHigh *p = ctl_ref(P(h, 0x58).i);
    if (p) {
        int k = form_bubble(p, P(h, 0x1c).i, idx_of(h->car));
        if (k != P(h, 0x60).i) P(h, 0x60).i = k;
    }
    uint32_t at = 0x5cbee8u + 12u * (uint32_t)(P(h, 0x60).i + P(h, 0x68).i * 6);
    for (int k = 0; k < 3; k++) off[k] = K(at + 4u * (uint32_t)k);
}

/* Into the chase of its pursued (0x40cb80 from AIHigh_Cop): the formation's ram budget (0x5cbeb0), the
   kind's windows (0x5cbfc0), the skill's blend (0x5cbedc). */
static void start_chase(AiHigh *h)
{
    AiHigh *p = ctl_ref(P(h, 0x58).i);
    if (!p) return;
    float off[3];
    form_offset(h, off);
    int f = P(h, 0x68).i, t = P(h, 0x18).i & 1;
    AiState st = chase_new(h->car, p->car, off, (int32_t)exe_u32(0x5cbeb0u + 0x14u * (uint32_t)f),
                           K(0x5cbfc0u + 8u * (uint32_t)t), K(0x5cbfc4u + 8u * (uint32_t)t), f, K(0x5cbedcu + 4u * (uint32_t)g_ai_level));
    to_state(h, st, 4);
}

/* A ram now and then (0x4038b0): by chance (p a second), the target beside the lane by more than its
   width yet within 0x5b2404, ahead by more than two lengths yet within 0x5b240c. */
static bool ram_chance(AiHigh *h, float pr, Car *t)
{
    Car *c = h->car;
    uint32_t r = SimRand_Next();
    if (!((int)((r * 1000) >> 16) < (int)lrintf((float)g_ai_ticks * pr * K(0x5b24b8) * K(0x5b2428)))) return false;
    float lat = absf(*CAR_F(c, 0xed0) - *CAR_F(t, 0xed0));
    if (!(*CAR_F(t, 0x3a8) + K(0x5b24a8) < lat && lat < K(0x5b2404))) return false;
    float a = AI_Ahead(c, t) * (float)*CAR_I(c, 0xeb0), b = *CAR_F(t, 0x3b0) + K(0x5b24a8);
    return b + b < a && a < K(0x5b240c);
}

/* Chasing an opponent, now and then a pause (0x4051a0, +0x10a4) when a human is a level above it. */
static void opponent_pause(AiHigh *h)
{
    AiHigh *p = ctl_ref(P(h, 0x58).i);
    Car *c = h->car;
    if (h->phase != 4 || !p || !(*CAR_I(p->car, CAR_FLAGS) & 8) || g_sim_tick < *CAR_I(c, 0x10a4)) return;
    if (2 <= (int32_t)lrintf(P(p, 0x8c).f)) return;
    float r = (float)SimRand_Next() * K(0x5b253c);
    for (int i = 0; i < g_humans.n; i++) {
        AiHigh *u = ctl_of(g_humans.c[i]);
        if (u && P(p, 0x94).i < P(u, 0x94).i && r < (float)g_ai_ticks * K(0x5b2538)) *CAR_I(c, 0x10a4) = g_sim_tick + 0x280;
    }
}

/* AIHigh_Cop's chase (its state 4). */
static void chasing(AiHigh *h)
{
    Car *c = h->car;
    AiState *s = &h->state;
    *CAR_I(c, 0xecc) |= 2;
    AiHigh *p = ctl_ref(P(h, 0x58).i);
    if (!p) {
        turn_back(c);
        drop(h);
        to_state(h, AIi_Normal(c), 2);
        P(h, 0x54).i = 0;
        AIi_StateTick(&h->state);
        return;
    }
    AiState old;
    if (0xa0 < CW(s, 0x90).i) {
        /* Blocked a while: turning round toward the followed car's node (state 9). The original goes
           on with the chase's words after freeing it; the port with a copy. */
        old = *s;
        Car *t = AIi_CarByIndex(*CAR_I(c, 0xed4) - 1);
        to_state(h, goto_new(c, t ? car_node(t) : car_node(c), 0), 9);
        s = &old;
    }
    int t = P(h, 0x18).i & 1;
    if (rec_i(p, 4 * t) == 0 && rec_i(p, 0xc + 4 * t) == 0) CW(s, 0x7c).i = 0;
    P(h, 0x64).i = -1;
    if (CW(s, 0x8c).i <= g_sim_tick && 0 < g_humans.n) {
        bool late = false;
        for (int i = 0; i < g_humans.n; i++) {
            AiHigh *u = ctl_of(g_humans.c[i]);
            if (u && 800 < P(u, 0xac).i && K(0x5b2470) < absf(*CAR_F(g_humans.c[i], 0xec0))) late = true;
        }
        if (late) CW(s, 0x8c).i = g_sim_tick + 0x3c0;
    }
    int was = P(h, 0x60).i;
    float off[3];
    form_offset(h, off);
    if (P(h, 0x60).i != was && CW(s, 0x70).i == 0) chase_aim(s, p->car, off);
    uint32_t f = 0x14u * (uint32_t)P(h, 0x68).i;
    if ((int32_t)exe_u32(0x5cbea0 + f) < CW(s, 0x78).i && absf(CW(s, 0x6c).f) < K(0x5cbea4 + f) && absf(CW(s, 0x68).f) < K(0x5cbea8 + f))
        chase_event(s, 1, (int)exe_u32(0x5cbeac + f));
    if (ram_chance(h, 0.25f, p->car) && CW(s, 0x70).i == 0) {
        const float zero[3] = {0, 0, 0};
        chase_aim(s, p->car, zero);
        chase_event(s, 1, 0x20);
    }
    if (pick(h)) {
        AiHigh *q = ctl_ref(P(h, 0x58).i);
        form_offset(h, off);
        if (q) chase_aim(s, q->car, off);
    }
    p = ctl_ref(P(h, 0x58).i);
    int e = P(h, 0x20).i;
    if (e == 1 || e == 4 || !p || 1 < *CAR_I(p->car, 0xb50)) {
        if (e == 1 || e == 4) turn_back(c);
        drop(h);
        to_state(h, AIi_Normal(c), 2);
    }
    if (P(h, 0x5c).i == 0 && !too_far(c)) {
        AIi_StateTick(&h->state);   /* (+0x54 is never set: its respawn is dead) */
        return;
    }
    P(h, 0x5c).i = 0;
    drop(h);
    to_state(h, AIi_Purgatory(c), 1);
    AIi_StateTick(&h->state);
}

void Police_Init(AiHigh *h, int number)
{
    /* 0x403860 */
    P(h, 0x1c).i = number;
    P(h, 0x18).i = (*CAR_I(h->car, CAR_FLAGS) & 0x40) ? 1 : 0;
    P(h, 0x20).i = 0;
    P(h, 0x24).i = 0;
    P(h, 0x50).i = 0;
    P(h, 0x54).i = 0;
    P(h, 0x30).i = -1;
    /* 0x403e90 */
    P(h, 0x58).i = 0;
    P(h, 0x60).i = 0;
    P(h, 0x5c).i = 0;
    P(h, 0x68).i = 0;
    P(h, 0x64).i = -1;
}

/* AIHigh_Cop (0x403ed0): 0 to the purgatory (idle for the 0x400 cars); 1 parked until its event (a
   patrol: driving on or parked; a roadblock); 2 patrolling until it chases a speeder or is too far;
   3 parked (a block's end or a patrol's: chasing again, or back); 4 chasing, 5 blocking, 9 turning. */
void Police_Tick(AiHigh *h)
{
    Car *c = h->car;
    *CAR_I(c, 0x534) = 0;
    *CAR_I(c, 0x530) = 0;
    opponent_pause(h);
    switch (h->phase) {
    case 0:
        drop(h);
        *CAR_I(c, 0xecc) &= ~2;
        if (*CAR_I(c, CAR_FLAGS) & 0x400) {
            to_state(h, AIi_Idle(c), 3);
            return;
        }
        to_state(h, AIi_Purgatory(c), 1);
        return;
    case 1: {
        *CAR_I(c, 0xecc) &= ~2;
        P(h, 0x54).i = 0;
        P(h, 0x64).i = -1;
        if (P(h, 0x20).i == 4) P(h, 0x20).i = 0;
        /* (+0x20 is never 2 in the original: its chase after a block, 0x407b50, is never reached) */
        if (!((*CAR_I(c, 0xecc) & 8) && P(h, 0x20).i != 1)) break;
        int32_t at = event(h);
        if (at < 0) break;
        int32_t r[COPREC / 4];
        for (int k = 0; k < COPREC / 4; k++) r[k] = cop_word(at, k);   /* (0x48 bytes, whatever the type) */
        if (g_race_players == 1 && 1 < g_humans.n && absf(AI_Gap(g_humans.c[0], g_humans.c[1])) < K(0x5b2524))
            r[0] = r[4] = r[2] = r[3] = 1;
        if (r[0] == 1) {
            SimRand_Next();
            int dir = dir_of_race();
            drop(h);
            if (r[4] == 0) to_state(h, AIi_Idle(c), 3);
            else to_state(h, AIi_Normal(c), 2);
            place_patrol(c, r[1], dir, dir, r[4]);
            AIi_StateTick(&h->state);
            return;
        }
        if (r[0] == 3 && (r[2] == dir_of_race() || r[2] == 0)) {
            drop(h);
            to_state(h, block_new(c, r[1], &r[3], &r[6], r[15], r[16], r[17]), 5);
            place_block(c, r[1], 1, &r[3], &r[6]);
            AIi_StateTick(&h->state);
            return;
        }
        break;
    }
    case 2:
        P(h, 0x64).i = -1;
        if (P(h, 0x20).i != 1 && P(h, 0x20).i != 4) *CAR_I(c, 0xecc) &= ~2;
        if (P(h, 0x20).i != 1 && P(h, 0x20).i != 4 && pick(h)) start_chase(h);
        if (P(h, 0x5c).i == 0 && !too_far(c)) break;
        P(h, 0x5c).i = 0;
        drop(h);
        to_state(h, AIi_Purgatory(c), 1);
        AIi_StateTick(&h->state);
        return;
    case 3:
        if (P(h, 0x20).i == 1 || P(h, 0x20).i == 4) {
            turn_back(c);
            drop(h);
            to_state(h, AIi_Normal(c), 2);
        }
        if (P(h, 0x5c).i == 0) {
            /* (+0x54 and +0x20 == 2 are never set in the original: their branches, a respawn 0x40f5a0
               and a chase from a block, are dead) */
            *CAR_I(c, 0xecc) &= ~2;
            if (pick(h) && P(h, 0x20).i != 1 && P(h, 0x20).i != 4) {
                start_chase(h);
                break;
            }
            if (!too_far(c)) break;
        } else {
            P(h, 0x5c).i = 0;
        }
        drop(h);
        to_state(h, AIi_Purgatory(c), 1);
        AIi_StateTick(&h->state);
        return;
    case 4:
        chasing(h);
        return;
    case 5: {
        /* (+0x10e0 by +0x7e, which the original never sets: 0) */
        *CAR_F(c, 0x10e0) = c->b[0x7e] ? 1.5f : 0.0f;
        P(h, 0x64).i = -1;
        for (int i = 0; i < g_racers.n; i++) {
            AiHigh *r = ctl_of(g_racers.c[i]);
            if (r && P(r, 0x78).i != 0 && h->state.vt == &block_vt) block_check(&h->state, g_racers.c[i]);
        }
        int n = car_node(c);
        float x = *CAR_F(c, 0xed0);
        if (-(node_f(n, 0x48) * (float)node_i(n, 0x38)) <= x && x <= node_f(n, 0x4c) * (float)node_i(n, 0x3c)) {
            *CAR_F(c, 0x10e0) = 0;
            drop(h);
            to_state(h, AIi_Normal(c), 2);
        }
        if (P(h, 0x5c).i == 0 && !too_far(c)) break;
        *CAR_F(c, 0x10e0) = 0;
        P(h, 0x5c).i = 0;
        drop(h);
        to_state(h, AIi_Purgatory(c), 1);
        AIi_StateTick(&h->state);
        return;
    }
    case 9:
        drop(h);
        if (h->state.vt == &goto_vt && absf(node_gap(car_node(c), ow(&h->state, 8)->i)) < 10.0f)   /* 0x40e390 */
            to_state(h, AIi_Normal(c), 2);
        break;
    default:
        break;
    }
    AIi_StateTick(&h->state);
}
