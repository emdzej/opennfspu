/* The cars' collisions (see car_collide.h). */
#include "game/car_collide.h"
#include "exe.h"
#include "game/wtree.h"
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

static float bits(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/* The edge's last surface flag (0x628c88), the wall sound's choice. */
static uint32_t g_wall_flag;

/* The car's speed in its frame after a hit (velocity damped by 0x3f7f7cee). 0x493db0 */
static void after_hit(Car *c)
{
    float *v = CAR_F(c, 0x33c), k = bits(0x3f7f7cee);
    for (int j = 0; j < 3; j++) v[j] *= k;
    *CAR_F(c, 0xd58) = dot3(v, CAR_F(c, 0x364));
    *CAR_F(c, 0xd5c) = dot3(v, CAR_F(c, 0x370));
    *CAR_F(c, 0xd60) = dot3(v, CAR_F(c, 0x37c));
    float az = fabsf(v[2]), ax = fabsf(v[0]);
    *CAR_F(c, 0x35c) = ax <= az ? ax * K(0x5b2468) + az : az * K(0x5b2468) + ax;
    if (*CAR_I(c, CAR_FLAGS) & 4) PENDING_NOTE(0x493d40, "the human car's hit effects (0x493d40)");
}

/* Slow glancing hits turn the car along the wall (its yaw +0x38c). 0x493e80 */
static void turn_along(Car *c, float impact, const float n[3])
{
    *CAR_I(c, 0x434) = 0;
    if (!(impact < K(0x5b243c)) && !(*CAR_F(c, 0xd60) < K(0x5b2470))) return;
    float f = dot3(n, CAR_F(c, 0x4a8)), s = dot3(n, CAR_F(c, 0x490));
    float a = fabsf(s), b = fabsf(f);
    float y = (a < b ? a : b) * K(0x5b2468);
    if (K(0x5b23f8) <= f) {
        if (K(0x5b23f8) < s) y = -y;
        *CAR_F(c, 0x38c) = y * K(0x5b23f0);
    } else if (s < K(0x5b23f8)) {
        *CAR_F(c, 0x38c) = -y;
    } else {
        *CAR_F(c, 0x38c) = y;
    }
}

float CarSim_HitWall(Car *c, const float n[3], const float p[3], const float push[3], bool turn)
{
    /* Where the push would put the point: back off the road (no surface) leaves the car where it is. */
    WSurface s = c->body;
    float q[3] = {p[0] + push[0], p[1] + push[1], p[2] + push[2]}, move[3];
    WSurface_Update(&s, q, false);
    if (!(s.surface & 0xf)) memset(move, 0, sizeof move);
    else memcpy(move, push, sizeof move);
    float k = bits(0x3f866666);
    for (int j = 0; j < 3; j++) move[j] *= k;
    float *v = CAR_F(c, 0x33c);
    float into = -dot3(v, n);
    if (!(K(0x5b23f8) < into)) return K(0x5b23f8);
    if (g_wall_flag < 0x10 || 0x60 < g_wall_flag) g_wall_flag = 0x60;
    float size;
    /* The sound by the wall's kind (0x10..0x60) and whether the hit was hard. */
    static const uint32_t hard[7] = {0x40008, 0x40009, 0x4000a, 0x4000b, 0x4000c, 0x4000d, 0x4001f};
    static const uint32_t soft[7] = {0x40015, 0x40016, 0x40017, 0x40018, 0x40019, 0x4001a, 0x4001f};
    int kind = g_wall_flag % 0x10 == 0 ? (int)(g_wall_flag / 0x10) - 1 : 6;
    if (*CAR_F(c, 0x35c) * K(0x5b272c) <= into) {
        *CAR_I(c, 0x438) = (int32_t)hard[kind];
        size = into;
    } else {
        *CAR_I(c, 0x438) = (int32_t)soft[kind];
        size = *CAR_F(c, 0x35c) * K(0x5b49b0);
    }
    /* Off the wall with a little bounce; the car and its wheels' contacts moved out. */
    float b = into * K(0x5b27a4);
    v[0] = n[0] * b + v[0];
    v[1] = n[1] * b + v[1];
    v[2] = n[2] * b + v[2];
    float ax = fabsf(v[0]), az = fabsf(v[2]);
    *CAR_F(c, 0x35c) = ax <= az ? ax * K(0x5b2468) + az : az * K(0x5b2468) + ax;
    float *pos = CAR_F(c, 0x330);
    for (int j = 0; j < 3; j++) pos[j] += move[j];
    for (int w = 0; w < 4; w++) {
        float *cp = CAR_F(c, 0x7f8 + 0xc4 * w);
        cp[0] += move[0];
        cp[2] += move[2];
    }
    if (turn) turn_along(c, into, n);   /* 0x493e80 */
    after_hit(c);                       /* 0x493db0 */
    float r = size * K(0x5b2600);
    return r < K(0x5b23f8) ? -r : r;
}

/* The closest point of the edge's line to p (in the ground plane, y 0). 0x489e40 */
static void closest_on_edge(const float p[3], const float e[6], float out[3])
{
    float dx = e[3] - e[0], dz = e[5] - e[2];
    float t = ((p[0] - e[0]) * dx + (p[2] - e[2]) * dz) / (dx * dx + dz * dz);
    out[1] = 0.0f;
    out[0] = dx * t + e[0];
    out[2] = dz * t + e[2];
}

static float plane_gap(WSurface *s, const float p[3], float y)
{
    const float *v = WSurface_Vertex(s, 0), *n = WSurface_Normal(s);
    float h = ((-((p[0] - v[0]) * n[0]) - (p[2] - v[2]) * n[2]) / n[1] + v[1]) - y;
    return h < K(0x5b23f8) ? -h : h;
}

void CarSim_Edges(Car *c)
{
    float worst = 0.0f;
    float back[3];
    for (int j = 0; j < 3; j++) back[j] = CAR_F(c, 0x33c)[j] * 0.03125f;   /* a tick's travel */
    WSurface s = c->body;
    /* The box's eight probes: its bottom corners at its full length, its top corners at half length. */
    const float *pos = CAR_F(c, 0x330), *right = CAR_F(c, 0x364), *up = CAR_F(c, 0x370), *fw = CAR_F(c, 0x37c);
    float hx = *CAR_F(c, 0x3a8), hy = *CAR_F(c, 0x3ac), hz = *CAR_F(c, 0x3b0);
    float probe[8][3];
    for (int half = 0; half < 2; half++) {
        float l = half ? hz * K(0x5b23f0) : hz;
        for (int k = 0; k < 4; k++) {
            float sz = k < 2 ? l : -l, sx = k == 0 || k == 2 ? -hx : hx;
            float *q = probe[half * 4 + k];
            float sy = half ? hy : -hy;
            for (int j = 0; j < 3; j++) q[j] = pos[j] + up[j] * sy + fw[j] * sz + right[j] * sx;
        }
    }
    for (int i = 0; i < 8; i++) {
        float *q = probe[i];
        WSurface_Update(&s, q, true);
        if ((s.surface & 0xf) && plane_gap(&s, q, pos[1]) <= K(0x5b2404)) continue;   /* on the road */
        /* From the centre (or where it was a tick ago) out to the probe. */
        float from[3] = {pos[0], pos[1], pos[2]};
        WSurface_Update(&s, from, true);
        if (!(s.surface & 0xf) || K(0x5b2404) < plane_gap(&s, from, pos[1])) {
            for (int j = 0; j < 3; j++) from[j] = pos[j] - back[j];
            WSurface_Update(&s, from, true);
        }
        float seg[6] = {from[0], from[1], from[2], q[0], q[1], q[2]}, edge[6];
        uint16_t flag = 0;
        bool hit = WSurface_FindEdge(&s, seg, edge, &flag);   /* 0x474030 */
        g_wall_flag = flag;
        if (!hit) continue;
        float n[3] = {-(edge[5] - edge[2]), 0.0f, edge[3] - edge[0]};
        float cl[3], push[3];
        closest_on_edge(q, edge, cl);
        push[0] = (cl[0] - q[0]) * bits(0x3f8147ae);
        push[1] = 0.0f;
        push[2] = (cl[2] - q[2]) * bits(0x3f8147ae);
        if (n[0] != K(0x5b23f8) || n[1] != K(0x5b23f8) || n[2] != K(0x5b23f8)) {
            float r = 1.0f / sqrtf(n[0] * n[0] + n[2] * n[2]);
            n[0] *= r;
            n[2] *= r;
        }
        float impact = CarSim_HitWall(c, n, q, push, true);   /* 0x493f70 */
        memcpy(CAR_F(c, 0x440), q, 12);
        PENDING_NOTE(0x40a690, "the AI's notice of a wall hit (0x40a690)");
        if (worst < impact) worst = impact;
        if (K(0x5b2470) < impact) {
            *CAR_F(c, 0x430) = impact;
            PENDING_NOTE(0x494d60, "the car's damage from a hit (0x494d60)");
        }
    }
    if (*CAR_F(c, 0x430) <= worst) *CAR_F(c, 0x430) = worst;
}

/* The cars checked against each other this tick (0x628cdc, count 0x628f5c; the simulation step empties
   it, which the port does on the tick's first car). */
static Car *g_collided[CAR_MAX * 2];
static int g_ncollided;
static int32_t g_collided_tick = -1;

void CarSim_Collide(void *car)
{
    Car *c = car;
    if (!c->b[0x7f] || c->b[0x7d]) return;
    if (*CAR_I(c, 0x43c) > 0) (*CAR_I(c, 0x43c))--;
    if (c->b[0x7c]) {
        CarSim_Edges(c);   /* 0x494f90 */
        PENDING_NOTE(0x498d50, "the car against the world's objects (0x498d50)");
    }
    if (g_collided_tick != g_sim_tick) {
        g_collided_tick = g_sim_tick;
        g_ncollided = 0;
    }
    if (g_race_network) return;
    for (int i = 0; i < g_ncollided; i++) {
        Car *o = g_collided[i];
        float r = *CAR_F(o, 0x3b4) + *CAR_F(c, 0x3b4);
        if (fabsf(CAR_F(c, 0x338)[0] - CAR_F(o, 0x338)[0]) < r && fabsf(CAR_F(c, 0x330)[0] - CAR_F(o, 0x330)[0]) < r &&
            fabsf(CAR_F(c, 0x334)[0] - CAR_F(o, 0x334)[0]) < r)
            PENDING_NOTE(0x498040, "the cars against each other (0x498040, 0x496440)");
    }
    if (g_ncollided < CAR_MAX * 2) g_collided[g_ncollided++] = c;
}
