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
#include "game/car_drive.h"
#include "game/race_car.h"
#include "game/race_progress.h"
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
    if (h->phase == 4) PENDING_NOTE(0x4051a0, "the police's ram timer in the chase");
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
        if (P(h, 0x20).i == 2) {
            PENDING_NOTE(0x407b50, "the police's chase after a roadblock (0x407b50)");
            break;
        }
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
            PENDING_NOTE(0x40dbc0, "the police's roadblock (AIState_Offroad, 0x407a20)");
        }
        break;
    }
    case 2:
        P(h, 0x64).i = -1;
        if (P(h, 0x20).i != 1 && P(h, 0x20).i != 4) *CAR_I(c, 0xecc) &= ~2;
        if (P(h, 0x20).i != 1 && P(h, 0x20).i != 4 && pick(h))
            PENDING_NOTE(0x40cb80, "the police's chase (AIState_Chase)");
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
            if (P(h, 0x54).i != 0) {
                turn_back(c);
                drop(h);
                to_state(h, AIi_Normal(c), 2);
                if (P(h, 0x54).i == 1) PENDING_NOTE(0x40f5a0, "the police car's respawn after a block");
                P(h, 0x54).i = 0;
                AIi_StateTick(&h->state);
                return;
            }
            if (P(h, 0x20).i == 2) {
                PENDING_NOTE(0x40cb80, "the police's chase from a block");
                break;
            }
            *CAR_I(c, 0xecc) &= ~2;
            if (pick(h) && P(h, 0x20).i != 1 && P(h, 0x20).i != 4) {
                PENDING_NOTE(0x40cb80, "the police's chase (AIState_Chase)");
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
    default:
        PENDING_NOTE(0x403ed0, "the police's chase, roadblock and turning states (4, 5, 9)");
        break;
    }
    AIi_StateTick(&h->state);
}
