/* The cars' collisions (see car_collide.h). */
#include "game/car_collide.h"
#include "game/traffic.h"
#include "game/car_cars.h"
#include "exe.h"
#include "game/car_damage.h"
#include "game/car_impulse.h"
#include "game/wtree.h"
#include "pending.h"
#include <math.h>
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

static float bits(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/* The edge's last surface flag (0x628c88), the wall sound's choice. */
static uint32_t g_wall_flag;


/* The car's side and forward speeds again along +0x490 and +0x4a8, the wheels' +0x870 words and
   +0xdb8, +0xdc0 cleared: a human car after a hit or a hard landing. 0x493d40 */
void Car_HitSpeeds(Car *c)
{
    *CAR_F(c, 0xd58) = dot3(CAR_F(c, 0x33c), CAR_F(c, 0x490));
    *CAR_F(c, 0xd60) = dot3(CAR_F(c, 0x33c), CAR_F(c, 0x4a8));
    *CAR_I(c, 0xdc0) = 0;
    for (int w = 0; w < 4; w++) *CAR_I(c, 0x870 + 0xc4 * w) = 0;
    *CAR_I(c, 0xdb8) = 0;
}
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
    if (*CAR_I(c, CAR_FLAGS) & 4) Car_HitSpeeds(c);   /* 0x493d40 */
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
            CarSim_ImpactDamage(c, n, 1);   /* 0x494d60 */
        }
    }
    if (*CAR_F(c, 0x430) <= worst) *CAR_F(c, 0x430) = worst;
}

/* The cars checked against each other this tick (0x628cdc, count 0x628f5c; the simulation step empties
   it). */

/* ---- the world's objects (0x4981f0-0x498d50) ---- */

static void unit3(float v[3])   /* 0x532640 */
{
    double r = (double)K(0x5b24a8) / sqrt((double)v[1] * v[1] + (double)v[0] * v[0] + (double)v[2] * v[2]);
    v[0] = (float)(r * v[0]);
    v[1] = (float)(v[1] * r);
    v[2] = (float)(v[2] * r);
}

/* Within r of the car's box in its right and forward axes (ground plane). 0x4981f0 */
static bool near_box(Car *c, const float p[3], float r)
{
    float d[3] = {p[0] - CAR_F(c, 0x330)[0], 0, p[2] - CAR_F(c, 0x330)[2]};
    if (r + *CAR_F(c, 0x3a8) < fabsf(dot3(d, CAR_F(c, 0x364)))) return false;
    return !(r + *CAR_F(c, 0x3b0) < fabsf(dot3(d, CAR_F(c, 0x37c))));
}

/* The car pushed out along n by the gap between the object's point a and the car's point b: the
   position and the wheels' contacts move, the velocity takes the push 32 times over and is held to the
   wheels' speed, the impact (3 x the closing speed) turns and slows the car; a hard one (over 10) dents
   the zone the push came from. 0x4982f0 */
static void contact(Car *c, const float n[3], const float a[3], const float b[3])
{
    float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    float t = dot3(n, d);
    for (int k = 0; k < 3; k++) d[k] = n[k] * t;
    float *pos = CAR_F(c, 0x330), *vel = CAR_F(c, 0x33c);
    for (int k = 0; k < 3; k++) pos[k] = d[k] + pos[k];
    for (int w = 0; w < 4; w++) {
        CAR_F(c, 0x7f8 + 0xc4 * w)[0] += d[0];
        CAR_F(c, 0x7f8 + 0xc4 * w)[2] += d[2];
    }
    float vn = dot3(vel, n), impact = -vn * K(0x5b2600);
    if (impact < K(0x5b23f8)) impact = -impact;
    *CAR_F(c, 0x430) = impact;
    for (int k = 0; k < 3; k++) vel[k] = d[k] * K(0x5b2644) + vel[k];
    float ax = fabsf(vel[0]), az = fabsf(vel[2]);
    float sp = ax <= az ? ax * K(0x5b2468) + az : az * K(0x5b2468) + ax;
    float k = *CAR_F(c, 0x35c) / sp;
    if (k < K(0x5b24a8)) {
        for (int j = 0; j < 3; j++) vel[j] *= k;
        *CAR_F(c, 0x430) = k * *CAR_F(c, 0x430);
    }
    turn_along(c, -vn, n);   /* 0x493e80 */
    after_hit(c);            /* 0x493db0 */
    if (K(0x5b241c) < *CAR_F(c, 0x430)) {
        float r = dot3(n, CAR_F(c, 0x364)), f = dot3(n, CAR_F(c, 0x37c));
        int zone;
        if (f < K(0x5b49d0)) zone = 1;
        else if (K(0x5b2610) < f) zone = 5;
        else if (r < K(0x5b49d0)) zone = 3;
        else if (K(0x5b2610) < r) zone = 7;
        else if (K(0x5b23f8) <= f) zone = K(0x5b23f8) <= r ? 4 : 6;
        else zone = K(0x5b23f8) <= r ? 2 : 0;
        CarSim_ZoneDamage(c, *CAR_F(c, 0x430), zone, 1);   /* 0x494470 */
    }
}

/* The car against a cylinder: pushed out along the line from its centre once nearer than the box's
   extent that way (its half width or length over the line's share, 10 when the share is under 0.1) plus
   the cylinder's radius; a fast car heading at it from close by is pushed through instead. 0x498a10 */
static void hit_cylinder(Car *c, WTreeObject *o)
{
    float ctr[3], pos[3] = {CAR_F(c, 0x330)[0], 0, CAR_F(c, 0x330)[2]};
    o->e.vt->centre(&o->e, ctr);
    ctr[1] = 0;
    float n[3] = {pos[0] - ctr[0], 0, pos[2] - ctr[2]};
    float dist = sqrtf(n[1] * n[1] + n[0] * n[0] + n[2] * n[2]);
    if (!(K(0x5b27a8) < dist)) return;
    unit3(n);
    float r = o->ovt->radius(o);
    float a = fabsf(dot3(n, CAR_F(c, 0x364)));
    float ext = a <= K(0x5b2604) ? 10.0f : *CAR_F(c, 0x3a8) / a;
    float b = fabsf(dot3(n, CAR_F(c, 0x37c)));
    float e2 = b <= K(0x5b2604) ? K(0x5b241c) : *CAR_F(c, 0x3b0) / b;
    if (e2 < ext) ext = e2;
    if (!(dist < ext + r)) return;
    if (K(0x5b2400) < *CAR_F(c, 0x35c)) {
        float v[3] = {CAR_F(c, 0x33c)[0], CAR_F(c, 0x33c)[1], CAR_F(c, 0x33c)[2]};
        unit3(v);
        if (K(0x5b23f0) < dot3(n, v) && dist < K(0x5b260c))
            for (int k = 0; k < 3; k++) n[k] = n[k] * -1.0f;
    }
    float on[3], car[3];
    for (int k = 0; k < 3; k++) {
        on[k] = n[k] * r + ctr[k];
        car[k] = n[k] * -ext + pos[k];
    }
    contact(c, n, on, car);
    *CAR_I(c, 0x438) = o->f20 | 0x50000;
    *CAR_I(c, 0x434) = 0;
    memcpy(CAR_F(c, 0x440), on, 12);
}


/* The car against a box: the four sides of the car's box (corners at its centre plus or minus half its
   length and width, and up or down by half its height, paired by 0x5d0b6c) tested against the box's
   sides; a car side across two box sides (a corner in the car) is pushed at once, else the side whose
   deepest body point is shallowest, under an eighth of the car's speed (at least 0.1). 0x498600 */
static void hit_box(Car *c, WTreeObject *o)
{
    const float *pos = CAR_F(c, 0x330), *rt = CAR_F(c, 0x364), *up = CAR_F(c, 0x370), *fw = CAR_F(c, 0x37c);
    float hx = *CAR_F(c, 0x3a8), hy = *CAR_F(c, 0x3ac), hz = *CAR_F(c, 0x3b0);
    float corner[4][3];
    for (int k = 0; k < 3; k++) {
        float f = fw[k] * hz, r = rt[k] * hx, d = up[k] * -hy;
        corner[0][k] = pos[k] + d + f - r;
        corner[1][k] = pos[k] - d + f + r;
        corner[2][k] = pos[k] - d - f - r;
        corner[3][k] = pos[k] + d - f + r;
    }
    bool found = false;
    float best = 999.0f, bn[3] = {0, 0, 0}, bat[3] = {0, 0, 0};
    for (int s = 0; s < 4; s++) {
        int a = (int)exe_u32(0x5d0b6c + 8u * (unsigned)s), b = (int)exe_u32(0x5d0b70 + 8u * (unsigned)s);
        float seg[6] = {corner[a][0], 0, corner[a][2], corner[b][0], 0, corner[b][2]}, n[3], at[3];
        int hit = o->ovt->collide(o, seg, n, at);
        if (hit > 1) {
            memcpy(bn, n, 12);
            memcpy(bat, at, 12);
            found = true;
            break;
        }
        if (hit == 1) {
            float lim = *CAR_F(c, 0x35c) * K(0x5b272c), deep = -99.0f;
            if (lim < K(0x5b2604)) lim = 0.1f;
            for (int i = 0; i < *CAR_I(c, 0x32c); i++) {
                const float *p = CAR_F(c, 0x1dc + 12 * i);
                float d[3] = {p[0] - at[0], p[1] - at[1], p[2] - at[2]}, v = -dot3(d, n);
                if (K(0x5b49c4) < v && deep < v) deep = v;
            }
            if (deep < best && K(0x5b49c4) < deep && deep < lim) {
                memcpy(bn, n, 12);
                memcpy(bat, at, 12);
                best = deep;
                found = true;
            }
        }
    }
    if (!found) return;
    CarSim_BodyImpulse(c, bn, bat, bits(0x3dcccccd));   /* 0x496580 */
    *CAR_I(c, 0x438) = o->f20 | 0x50000;
    memcpy(CAR_F(c, 0x440), pos, 12);
}

void CarSim_Objects(Car *c)
{
    if (*CAR_I(c, 0x43c) != 0) return;
    WTreeElems *near = WTree_ObjectsNear(CAR_F(c, 0x330), 5.0f);   /* 0x4737d0 */
    if (!near) return;
    for (uint32_t i = 0; i < near->n; i++) {
        WTreeObject *o = (WTreeObject *)near->e[i];
        float ctr[3];
        o->e.vt->centre(&o->e, ctr);
        if (!near_box(c, ctr, o->ovt->radius(o))) continue;
        int kind = o->ovt->hitkind(o, CAR_F(c, 0x33c));
        if (kind == 2) {
            /* The original tells the two apart by RTTI (dynamic_cast to each). */
            if (o->shape == WOBJ_CYLINDER)
                hit_cylinder(c, o);
            else if (o->shape == WOBJ_BOX)
                hit_box(c, o);
        } else if (kind == 3) {
            if (K(0x5b2580) < *CAR_F(c, 0x35c)) {
                *CAR_F(c, 0x430) = 15.0f;
                *CAR_I(c, 0x438) = 0x6000f;
                memcpy(CAR_F(c, 0x440), CAR_F(c, 0x330), 12);
            }
        } else if (kind == 1) {
            *CAR_I(c, 0x438) = *CAR_I(c, 0x44c) | 0x50000;
            memcpy(CAR_F(c, 0x440), CAR_F(c, 0x330), 12);
            *CAR_F(c, 0x430) = 15.0f;
        }
    }
    WTreeElems_free(near);   /* (the vector and its array) */
}

static Car *g_collided[CAR_MAX + TRAFFIC_MAX];   /* 0x628cdc (room for 138, unchecked) */
static int g_ncollided;

void CarSim_ClearCollided(void) { g_ncollided = 0; }

void CarSim_Collide(void *car)
{
    Car *c = car;
    if (!c->b[0x7f] || c->b[0x7d]) return;
    if (*CAR_I(c, 0x43c) > 0) (*CAR_I(c, 0x43c))--;
    if (c->b[0x7c]) {
        CarSim_Edges(c);   /* 0x494f90 */
        CarSim_Objects(c);  /* 0x498d50 */
    }
    if (g_race_network) return;
    for (int i = 0; i < g_ncollided; i++) {
        Car *o = g_collided[i];
        float r = *CAR_F(o, 0x3b4) + *CAR_F(c, 0x3b4);
        if (fabsf(CAR_F(c, 0x338)[0] - CAR_F(o, 0x338)[0]) < r && fabsf(CAR_F(c, 0x330)[0] - CAR_F(o, 0x330)[0]) < r &&
            fabsf(CAR_F(c, 0x334)[0] - CAR_F(o, 0x334)[0]) < r)
        {
            /* The spins in radians for the impulses, back to turns after. */
            for (int k = 0; k < 3; k++) CAR_F(c, 0x388)[k] *= 6.2831855f, CAR_F(o, 0x388)[k] *= 6.2831855f;   /* 0x40c90fdb */
            CarSim_Cars(c, o);   /* 0x498040 */
            for (int k = 0; k < 3; k++) CAR_F(c, 0x388)[k] *= 0.15915494f, CAR_F(o, 0x388)[k] *= 0.15915494f;   /* 0x3e22f981 */
            CarSim_ClampSpin(c);   /* 0x496440 */
            CarSim_ClampSpin(o);
        }
    }
    if (g_ncollided < CAR_MAX + TRAFFIC_MAX) g_collided[g_ncollided++] = c;
}
