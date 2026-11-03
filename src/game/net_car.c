/* The network race's car states (the state mode, g_race_network 1: Net_Tick sends the local car's
   every 8 ticks, 0xbe; the others' arrive here) and the remote cars' correction towards them.

   A state (0x24 bytes): +0 the steering as the model uses it (+0xd94), +1 off the ground (+0x424 over a
   half), +2 the brake (+0xd80), +3 the buttons (+0xd8a), +4 the tick, +8 the yaw rate (+0x38c) in
   thousandths, +0xa the orientation as a quaternion (four shorts, 14 bits of fraction), +0x12 the
   velocity in hundredths, +0x18 the position (floats). The car keeps the last one at +0x38 .. +0x5b
   (the same order, the quaternion from +0x42), the lag's average +0x5c, the counts +0x60, +0x64, the lag
   +0x68, the corrections since +0x6c, the snap flag +0x70 (the original's lock +0x74 isn't needed:
   gasm's threads don't preempt). */
#include "game/net.h"
#include "exe.h"
#include "game/car_ground.h"
#include "game/car_physics.h"
#include "game/road.h"
#include "game/sim_math.h"
#include "game/wtree.h"
#include "render/camera_math.h"
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

static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }   /* Vec3_Dot */

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static int16_t rd16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }
static void wr16(uint8_t *p, int v) { int16_t s = (int16_t)v; memcpy(p, &s, 2); }

/* (a * b) >> 16, rounded half up (the 16.16 multiply's: the low word's bit 15 added) */
static int32_t fixmul(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b + 0x8000) >> 16); }

/* The square root of a 16.16 value: a line per leading bit (its slope 0x5df774, its offset 0x5df7f8). 0x561930 */
static int32_t fixsqrt(uint32_t x)
{
    if (!x) return 0;
    int i = 31;
    for (uint32_t v = x; !(v & 0x80000000u); v <<= 1) i--;
    return (int32_t)exe_u32(0x5df7f8 + 4u * (uint32_t)i) + fixmul((int32_t)x, (int32_t)exe_u32(0x5df774 + 4u * (uint32_t)i));
}

/* A 16.16 rotation (rows) as a quaternion's x, y, z, w, 14 bits of fraction: from the trace, or past a
   non-positive one from the largest diagonal term. 0x49fc50 */
static void mat_to_quat(const int32_t m[9], uint8_t out[8])
{
    int32_t q[4];
    static const int next[3] = {1, 2, 0};
    int32_t tr = m[0] + m[4] + m[8];
    if (tr < 1) {
        int i = m[0] < m[4];
        if (m[4 * i] < m[8]) i = 2;
        int j = next[i], k = next[j];
        int32_t s = fixsqrt((uint32_t)(m[4 * i] - m[4 * j] - m[4 * k] + 0x10000));
        q[i] = s >> 1;
        if (s) s = (int32_t)(0x80000000u / (uint32_t)s);
        q[3] = fixmul(m[3 * j + k] - m[3 * k + j], s);
        q[j] = fixmul(m[3 * j + i] + m[3 * i + j], s);
        q[k] = fixmul(m[3 * i + k] + m[3 * k + i], s);
    } else {
        int32_t s = fixsqrt((uint32_t)(tr + 0x10000));
        int32_t inv = (int32_t)(0x80000000u / (uint32_t)s);
        q[3] = s >> 1;
        q[0] = fixmul(m[5] - m[7], inv);
        q[1] = fixmul(m[6] - m[2], inv);
        q[2] = fixmul(m[1] - m[3], inv);
    }
    for (int i = 0; i < 4; i++) wr16(out + 2 * i, q[i] >> 2);
}

/* Its rotation back, 16.16 (the products in 28 bits of fraction). 0x49fe10 */
static void quat_to_mat(const uint8_t in[8], int32_t m[9])
{
    int32_t x = rd16(in), y = rd16(in + 2), z = rd16(in + 4), w = rd16(in + 6);
    int32_t z2 = z * 2, wx = w * x * 2, wy = w * y * 2, xx = x * x * 2, yy = y * y * 2, xy = x * y * 2;
    int32_t one_zz = 0x10000000 - z * z2;
    m[0] = (one_zz - yy) >> 12;
    m[3] = (xy - w * z2) >> 12;
    m[4] = (one_zz - xx) >> 12;
    m[5] = (y * z2 + wx) >> 12;
    m[1] = (xy + w * z2) >> 12;
    m[6] = (x * z2 + wy) >> 12;
    m[7] = (y * z2 - wx) >> 12;
    m[2] = (x * z2 - wy) >> 12;
    m[8] = (0x10000000 - yy - xx) >> 12;
}

/* A float scaled to a short, rounded half away from zero (+-0.5, then __ftol's truncation). */
static int round_short(float v) { return (int)(v + (v < K(0x5b23f8) ? K(0x5b2448) : K(0x5b23f0))); }

void NetCar_GetState(uint8_t out[0x24], const void *car)   /* 0x49ff00 */
{
    Car *c = (Car *)car;
    memset(out, 0, 0x24);
    memcpy(out + 4, &g_sim_tick, 4);
    memcpy(out + 0x18, c->b + 0x330, 12);
    out[2] = c->b[0xd80];
    out[0] = c->b[0xd94];
    out[3] = c->b[0xd8a];
    out[1] = K(0x5b23f0) < *CAR_F(c, 0x424);
    int32_t m[9];
    for (int i = 0; i < 9; i++) m[i] = (int32_t)lrintf(CAR_F(c, 0x364)[i] * K(0x5b3e68));   /* (fistp: to nearest) */
    mat_to_quat(m, out + 0xa);
    wr16(out + 8, round_short(*CAR_F(c, 0x38c) * K(0x5b2428)));
    for (int i = 0; i < 3; i++) wr16(out + 0x12 + 2 * i, round_short(CAR_F(c, 0x33c)[i] * K(0x5b242c)));
}

static void state_matrix(Car *c, float m[9])
{
    int32_t q[9];
    quat_to_mat(c->b + 0x42, q);
    for (int i = 0; i < 9; i++) m[i] = (float)q[i] * K(0x5b253c);
}

/* The car snapped to its last state's orientation and velocity (the position follows from the
   correction). 0x4a01c0 */
static void snap(Car *c)
{
    *CAR_I(c, 0xdac) = 1;
    for (int i = 0; i < 3; i++) CAR_F(c, 0x33c)[i] = (float)rd16(c->b + 0x4a + 2 * i) * K(0x5b3ca0);
    state_matrix(c, CAR_F(c, 0x364));
    Mat3_Orthonormalize(CAR_F(c, 0x364));
    CarSim_Track(c);   /* 0x49aee0 */
    *CAR_I(c, 0xe94) = 0;
    *CAR_I(c, 0xc) = -1;
    CarSim_BodyPoints(c);   /* 0x49ada0 */
}

void NetCar_SetState(void *car, const uint8_t in[0x24])   /* 0x4a0bd0 */
{
    Car *c = car;
    int32_t old = *CAR_I(c, 0x3c), stamp = rd32(in + 4);
    if (stamp <= old) return;
    c->b[0x39] = in[1];
    c->b[0x3a] = in[2];
    c->b[0x38] = in[0];
    c->b[0x3b] = in[3];
    *CAR_I(c, 0x3c) = stamp;
    memcpy(c->b + 0x42, in + 0xa, 14);   /* (the quaternion and the velocity) */
    memcpy(c->b + 0x50, in + 0x18, 12);
    memcpy(c->b + 0x40, in + 8, 2);
    int32_t d = g_sim_tick - stamp;
    int32_t lag = d < 0 ? -d : d;
    if (lag < 1) lag = 1;
    *CAR_I(c, 0x68) = lag;
    *CAR_I(c, 0x6c) = 0;
    *CAR_I(c, 0x5c) = (*CAR_I(c, 0x5c) * 7 + lag) / 8;
    if (lag < 0x41) {
        *CAR_I(c, 0x64) = 0;
    } else if (4 < ++*CAR_I(c, 0x64) || 0x80 < lag) {
        if (!*CAR_I(c, 0x70)) snap(c);
        *CAR_I(c, 0x70) = 1;
    }
    if (*CAR_I(c, 0x68) < 0x20) {
        if (8 < ++*CAR_I(c, 0x60)) *CAR_I(c, 0x70) = 0;
    } else {
        *CAR_I(c, 0x60) = 0;
    }
    if (old == 0) *CAR_I(c, 0xdac) = 1;
}

/* The car's speed term (+0x35c): the larger ground component plus the smaller times 0x5b2468. */
static float ground_speed(const float v[3])
{
    float a = fabsf(v[0]), b = fabsf(v[2]);
    return a <= b ? a * K(0x5b2468) + b : b * K(0x5b2468) + a;
}

/* Put down where it is, after a snap: its forward from its body's (+0x3e8), the axes squared up again,
   the ground's contact, the wheels' travel cleared. 0x4a0090 */
static void settle(Car *c)
{
    CarSim_Track(c);   /* 0x49aee0 */
    float *m = CAR_F(c, 0x364);
    memcpy(m + 3, CAR_F(c, 0x3e8), 12);
    Vec3_Cross(m + 3, m + 6, m);       /* 0x5325f0 */
    Vec3_Cross(m, m + 3, m + 6);
    Mat3_Orthonormalize(m);
    c->b[0x80] = 0x20;
    *CAR_F(c, 0x35c) = ground_speed(CAR_F(c, 0x33c));
    float n[3];
    *CAR_F(c, 0x41c) = CarSim_Ground(c, n);   /* 0x4999e0 */
    float h = *CAR_F(c, 0x424);
    *CAR_F(c, 0x424) = 0;
    *CAR_F(c, 0x334) -= h;
    for (int i = 0; i < 4; i++) {
        uint8_t *w = c->b + 0x820 + 0xc4 * i;
        memset(w, 0, 4);
        memset(w + 8, 0, 4);
        memset(w + 0x1c, 0, 4);
    }
    *CAR_I(c, 0xe94) = 0;
    *CAR_I(c, 0xc) = -1;
    CarSim_BodyPoints(c);   /* 0x49ada0 */
}

/* Towards the state's place: near (within 0x5b2470) a part of the way (0.08, or by the distance past
   0x5b2460) and the orientation blended (unless it faces the other way), else there at once. 0x4a0290 */
static void approach(Car *c, float yawrate, const float pos[3], const float vel[3], const float m[9])
{
    float *p = CAR_F(c, 0x330), d[3];
    for (int i = 0; i < 3; i++) d[i] = pos[i] - p[i];   /* 0x5320a0 */
    float dist = sqrtf(dot3(d, d));                     /* 0x532890 */
    if (dist <= K(0x5b2470)) {
        float k = 0.08f;   /* (0x3da3d70a) */
        if (K(0x5b2460) < dist) k = dist * K(0x5b4a8c);
        *CAR_F(c, 0x35c) = ground_speed(vel);
        for (int i = 0; i < 3; i++) p[i] += d[i] * k;   /* 0x5320d0, 0x532070 */
        float *cm = CAR_F(c, 0x364);
        if (K(0x5b23f8) <= dot3(CAR_F(c, 0x37c), m + 6)) {
            for (int i = 0; i < 9; i++) cm[i] = m[i] * K(0x5b23f4) + cm[i] * K(0x5b4a88);
        } else {
            memcpy(cm, m, 36);
        }
        Mat3_Orthonormalize(cm);
        c->b[0x80] = 0x20;
        return;
    }
    *CAR_F(c, 0x388) = 0;
    *CAR_F(c, 0x390) = 0;
    *CAR_F(c, 0x38c) = yawrate;
    memcpy(CAR_F(c, 0x33c), vel, 12);
    memcpy(CAR_F(c, 0x364), m, 36);
    memcpy(p, pos, 12);
    settle(c);
}

/* A remote car's tick (CarSim_Turn's, the state mode): its last state carried on by the lag (turned by
   the yaw rate over half of it, moved over all of it), brought onto the road (towards its node until a
   polygon is under it, then two steps more), approached; its gear 1, its speed the state's; on the
   ground unless the state says it's off. A lag over 0x100 ticks snaps it next time. 0x4a0820 */
void NetCar_Correct(void *car)
{
    Car *c = car;
    if (!*CAR_I(c, 0x3c)) return;
    float vel[3];
    for (int i = 0; i < 3; i++) vel[i] = (float)rd16(c->b + 0x4a + 2 * i) * K(0x5b3ca0);
    int32_t lag = g_sim_tick - *CAR_I(c, 0x3c);
    float yawrate = (float)rd16(c->b + 0x40) * K(0x5b3060);
    float m[9], pos[3];
    state_matrix(c, m);
    memcpy(pos, c->b + 0x50, 12);
    ++*CAR_I(c, 0x6c);
    float w = yawrate * K(0x5b2828);
    float a = (float)(-(lag / 2)) * w, s, co;   /* (the half lag truncated) */
    Math_SinCosTurns(a, &s, &co);   /* 0x532550, 0x532560 */
    float vx = vel[0], vz = vel[2];
    vel[0] = co * vx - s * vz;
    vel[2] = co * vz + s * vx;
    float t = (float)lag;
    pos[0] += vel[0] * K(0x5b2828) * t;
    pos[1] += vel[1] * K(0x5b2828) * t;
    pos[2] += vel[2] * K(0x5b2828) * t;
    /* 0x49b7e0(0, lag * w, 0): about y only */
    float ry[9], rx[9], rz[9], t1[9], r[9];
    Mat3_RotY(ry, t * w);
    Mat3_RotX(rx, 0);
    Mat3_Mul(rx, ry, t1);
    Mat3_RotZ(rz, 0);
    Mat3_Mul(t1, rz, r);
    Mat3_Mul(m, r, m);
    WSurface sf = c->body;
    WSurface_Update(&sf, pos, true);   /* 0x473fc0 */
    if (sf.surface == 0 && g_road) {
        const float *np = Road_NodePos(g_road, sf.node);
        float dir[3] = {np[0] - pos[0], np[1] - pos[1], np[2] - pos[2]};
        if (dir[0] != K(0x5b23f8) || dir[1] != K(0x5b23f8) || dir[2] != K(0x5b23f8)) Vec3_Unit(dir, dir);
        while (sf.surface == 0) {
            Vec3_AddArray(1, pos, dir, pos);
            WSurface_Update(&sf, pos, true);
        }
        Vec3_AddArray(1, pos, dir, pos);
        Vec3_AddArray(1, pos, dir, pos);
    }
    approach(c, yawrate, pos, vel, m);
    c->b[0xd7e] = 1;
    c->b[0xd82] = 1;
    *CAR_F(c, 0xec4) = sqrtf(dot3(vel, vel));   /* 0x532890 */
    if (!c->b[0x39]) {
        float n[3];
        *CAR_F(c, 0x41c) = CarSim_Ground(c, n);   /* 0x4999e0 */
        float h = *CAR_F(c, 0x424);
        *CAR_F(c, 0x424) = 0;
        *CAR_F(c, 0x334) -= h;
        for (int i = 0; i < 4; i++) {
            uint8_t *wb = c->b + 0x820 + 0xc4 * i;
            memset(wb, 0, 4);
            memset(wb + 8, 0, 4);
            memset(wb + 0x1c, 0, 4);
        }
    }
    CarSim_BodyPoints(c);   /* 0x49ada0 */
    if (lag < 0) lag = -lag;
    if (0x100 < lag) *CAR_I(c, 0x70) = 1;
}
