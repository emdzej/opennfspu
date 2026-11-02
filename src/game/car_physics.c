/* The cars' simulation (see car_physics.h). */
#include "game/car_physics.h"
#include "game/skid.h"
#include "exe.h"
#include "game/sim_math.h"
#include "pending.h"
#include "render/camera_math.h"
#include "render/r3dcar.h"
#include "game/wtree.h"
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

static float f32u(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/* The wheels' skid marks (0x4a2660 -> 0x478ec0 per wheel): the front wheels' over 0.1 by 1.11, the rear's
   over 0.5 by 2, of their slip (+0x870); on ground kinds 9 and 6 (and 1 with +0x838 set, as 6) 0.7
   strong from nothing. Each at the wheel's contact point (+0x7f8) along the car's +0x348. */
static void wheel_marks(Car *c)
{
    for (int w = 0; w < 4; w++) {
        int base = 0x844 + 0xc4 * w;
        float thr = w < 2 ? f32u(0x3dcccccdu) : 0.5f, scale = w < 2 ? f32u(0x3f8e353fu) : 2.0f;
        int kind = *CAR_I(c, base);
        float slip;
        if (kind == 9) {
            slip = f32u(0x3f333333u), thr = 0, scale = 1.0f;
        } else if (kind == 6 || (*CAR_I(c, base - 0xc) != 0 && kind == 1)) {
            kind = 6;
            slip = f32u(0x3f333333u), thr = 0, scale = 1.0f;
        } else {
            slip = *CAR_F(c, base + 0x2c);
        }
        SkidTrail_Step(c->p[CARP_WHEELS + w], CAR_F(c, 0x7f8 + 0xc4 * w), slip, kind, CAR_F(c, 0x348), thr, scale);
    }
}

void CarSim_Move(void *car)
{
    Car *c = car;
    if (!c->b[0x7f] || c->b[0x7d]) return;
    float dt = K(0x5b2828);
    float d[3] = {*CAR_F(c, 0x33c) * dt, *CAR_F(c, 0x340) * dt, *CAR_F(c, 0x344) * dt};
    *CAR_F(c, 0x330) = d[0] + *CAR_F(c, 0x330);
    *CAR_F(c, 0x334) = d[1] + *CAR_F(c, 0x334);
    *CAR_F(c, 0x338) = d[2] + *CAR_F(c, 0x338);
    if (*CAR_I(c, CAR_FLAGS) & 0x80) return;
    for (int w = 0; w < 4; w++) {
        float *p = CAR_F(c, 0x7f8 + 0xc4 * w);
        Vec3_AddArray(1, p, d, p);
    }
    wheel_marks(c);
    CarSim_WheelSpin(c);
}

void CarSim_Turn(void *car)
{
    Car *c = car;
    if (!c->b[0x7f] || c->b[0x7d]) return;
    float dt = K(0x5b2828);
    float rx[9], ry[9], rz[9], xy[9], r[9];
    float ax = *CAR_F(c, 0x388) * dt, az = *CAR_F(c, 0x390) * dt;
    Mat3_RotY(ry, *CAR_F(c, 0x38c) * dt);
    Mat3_RotX(rx, ax);
    Mat3_Mul(rx, ry, xy);
    Mat3_RotZ(rz, az);
    Mat3_Mul(xy, rz, r);
    float *m = CAR_F(c, 0x364);
    Mat3_Mul(m, r, m);
    PENDING_NOTE(0x4900a0, "the network game's car correction (0x657408, 0x4900a0, 0x4a0820)");
    CarSim_BodyPoints(c);
    if (--c->b[0x80] == 0) {
        Mat3_Orthonormalize(m);
        c->b[0x80] = 0x20;
    }
}

void CarSim_BodyPoints(Car *c)
{
    float t[9];
    Mat3_Transpose(CAR_F(c, 0x364), t);
    for (int i = 0; i < *CAR_I(c, 0x32c); i++) {
        const float *p = CAR_F(c, 0x8c + 12 * i);
        float *w = CAR_F(c, 0x1dc + 12 * i);
        w[0] = dot3(p, t) + *CAR_F(c, 0x330);
        w[1] = dot3(p, t + 3) + *CAR_F(c, 0x334);
        w[2] = dot3(p, t + 6) + *CAR_F(c, 0x338);
    }
}

/* Each wheel's spin (+0x840, turns, wrapped below 1): with +0xdac == 1 the car's wheel speed +0x35c, else,
   for a wheel off the ground (+0x874 zero), the engine's: +0xd60 for the undriven wheels (the front pair
   when +0x5c of the specs is under 0.01, the rear when over 0x5b3f80) and in neutral (+0xd82 == 1), else
   +0xdb4 over the gear's ratio (+0x60 + 4 gear); both over +0x148 of the specs. */
void CarSim_WheelSpin(Car *c)
{
    for (int w = 0; w < 4; w++) {
        float *spin = CAR_F(c, 0x840 + 0xc4 * w);
        const uint8_t *specs = CAR_SPECSB(c);
        float s148, s5c;
        memcpy(&s148, specs + 0x148, 4);
        memcpy(&s5c, specs + 0x5c, 4);
        float rate;
        bool turn = false;
        if (*CAR_I(c, 0xdac) == 1) {
            rate = *CAR_F(c, 0x35c) / s148;
            turn = true;
        } else if (spin[0xd] == 0.0f) {
            bool front = w == 0 || w == 1;
            int8_t gear = (int8_t)c->b[0xd82];
            if ((s5c < K(0x5b27a8) && front) || (K(0x5b3f80) < s5c && !front) || gear == 1) {
                rate = *CAR_F(c, 0xd60) / s148;
            } else {
                float ratio;
                memcpy(&ratio, specs + 0x60 + 4 * gear, 4);
                rate = *CAR_F(c, 0xdb4) / (ratio * s148);
            }
            turn = true;
        }
        if (turn) *spin = rate * K(0x5b2828) + *spin;
        if (K(0x5b24a8) < *spin) *spin = *spin - (float)lrintf(*spin);
    }
}

void CarSim_LoadBodyPoints(Car *c)
{
    if (*CAR_I(c, CAR_FLAGS) & 0x80) return;   /* (a traffic object's come from its body, 0x4a1060) */
    const R3DCar *r = c->p[CARP_R3D];
    int n = r ? (int)r->nbound : 0;
    if (n > 48) n = 48;
    *CAR_I(c, 0x32c) = n;
    float *p = CAR_F(c, 0x8c);
    for (int i = 0; i < n; i++) memcpy(p + 3 * i, r->bound[i], 12);
    /* Lowest first (a bubble sort by height). */
    for (int pass = 0; pass < n; pass++)
        for (int j = 0; j + 1 < n; j++)
            if (p[3 * (j + 1) + 1] < p[3 * j + 1]) {
                float t[3];
                memcpy(t, p + 3 * j, 12);
                memcpy(p + 3 * j, p + 3 * (j + 1), 12);
                memcpy(p + 3 * (j + 1), t, 12);
            }
    /* The four lowest as the corners: front left, front right, rear left, rear right (by the signs of
       x and z); a corner with no point stays at the origin. */
    float corner[4][3] = {{0}};
    float zero = K(0x5b23f8);
    for (int i = 0; i < 4; i++) {
        const float *q = p + 3 * i;
        if (q[0] < zero && zero < q[2]) memcpy(corner[0], q, 12);
        else if (zero < q[0] && zero < q[2]) memcpy(corner[1], q, 12);
        else if (q[0] < zero && q[2] < zero) memcpy(corner[2], q, 12);
        else if (zero < q[0] && q[2] < zero) memcpy(corner[3], q, 12);
    }
    memcpy(p, corner, sizeof corner);
}

void CarSim_Init(Car *c, int32_t id, float mass, float hx, float hy, float hz)
{
    *CAR_I(c, 0) = id;
    c->b[0x7e] = 0;
    c->b[0x7d] = 0;
    c->b[0x7c] = 1;
    /* (+0x74: the car's critical section, 0x531f60; the port runs on one thread and needs none) */
    memset(c->b + 0x5c, 0, 0x18);         /* +0x5c .. +0x70 */
    memset(c->b + 0x330, 0, 0x18);        /* the position and velocity */
    *CAR_F(c, 0x35c) = 0;
    *CAR_F(c, 0x3a8) = hx;
    *CAR_F(c, 0x3ac) = hy;
    *CAR_F(c, 0x3b0) = hz;
    float r = sqrtf(hy * hy + hx * hx);
    *CAR_F(c, 0x3b4) = sqrtf(hz * hz + r * r);
    float m = (*CAR_I(c, CAR_FLAGS) & 0x10) ? hx * hy * hz * K(0x5b368c) : mass;
    if ((*CAR_I(c, CAR_FLAGS) & 4) && g_race_heavy) m *= K(0x5b240c);
    else if (DESC_I(c, 0x518)) m *= K(0x5b49b0);   /* TINYCAR */
    *CAR_F(c, 0x354) = m;
    *CAR_F(c, 0x358) = K(0x5b24a8) / m;
    float inertia = m * K(0x5b2460);
    *CAR_F(c, 0x394) = inertia;
    *CAR_F(c, 0x398) = K(0x5b24a8) / inertia;
    memset(c->b + 0x388, 0, 12);          /* the angular velocity */
    *CAR_F(c, 0x39c) = 0;
    *CAR_F(c, 0x3a0) = 1.0f;
    *CAR_F(c, 0x3a4) = 0;
    *CAR_I(c, 0x460) = 1;
    *CAR_I(c, 0x464) = 1;
    *CAR_F(c, 0x84) = 1.0f;               /* the gravity scale */
    memset(c->b + 0x418, 0, 2);
    memset(c->b + 0x420, 0, 12);          /* +0x420 .. +0x428 (+0x41c, the last ground height, stays) */
    *CAR_F(c, 0x360) = 0;
    *CAR_F(c, 0x40c) = 0;
    memset(c->b + 0x42c, 0, 0x14);        /* +0x42c .. +0x43c */
    memset(c->b + 0x454, 0, 12);          /* +0x454 .. +0x45c */
    memset(c->b + 0x4e4, 0, 0x28);        /* the damage zones */
    c->b[0x80] = 0;
    memset(c->b + 0x41a, 0, 2);
    c->b[0x7f] = 1;
    *CAR_I(c, 0x3c) = 0;
    CarSim_LoadBodyPoints(c);             /* 0x49ab30 */
    CarSim_BodyPoints(c);                 /* 0x49ada0 */
}
