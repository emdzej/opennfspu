/* The cars' simulation (see car_physics.h). */
#include "game/car_physics.h"
#include "exe.h"
#include "game/sim_math.h"
#include "pending.h"
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

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/* The wheels' skid marks (0x4a2660 -> 0x478ec0 per wheel, with the quality flag 0x6197e7): pending. */
static void wheel_marks(Car *c)
{
    PENDING_NOTE(0x4a2660, "the wheels' skid marks (0x4a2660, 0x478ec0)");
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
