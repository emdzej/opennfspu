/* The cars' drive (see car_drive.h). */
#include "game/car_drive.h"
#include "exe.h"
#include "game/car_ground.h"
#include "game/sim_math.h"
#include "game/sim_rand.h"
#include "pending.h"
#include <math.h>
#include <string.h>

/* The controls of the car being driven, as the drive model reads them (0x628fb0 throttle, 0x628fac
   brake, 0x628fb4 the steering's size), 0..1. */
float g_drive_throttle, g_drive_brake, g_drive_steer;
/* The front and the rear wheels on the ground (0x628f9c, 0x628f98), set by the drive task. */
int32_t g_drive_front_down = 1, g_drive_rear_down = 1;
int32_t g_race_demo;        /* 0x5e67b0 */
int32_t g_race_players;     /* 0x657404 */
int32_t g_race_mode;        /* 0x6573e8 */

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
#define K(va) exe_f32(va)

/* The race car description's TRANSMISSION (+0xc of 0x65808c + 0x580 i; 1 automatic). */
static int32_t transmission(Car *c) { return DESC_I(c, 0xc); }

void CarSim_Controls(Car *c)
{
    uint8_t *b = c->b;
    int32_t *started = CAR_I(c, 0xdb0);
    if (*started) (*started)++;
    if (((*CAR_I(c, CAR_FLAGS) & 4) && 1 < *CAR_I(c, 0xb50)) || *started == 0) {
        /* Not driving (yet): no throttle, full brake, no shift. */
        b[0xd7f] = 0;
        b[0xd83] = 0;
        b[0xd84] = 0;
        *CAR_I(c, 0xd98) = 0;
        b[0xd80] = 0xff;
    } else {
        uint8_t throttle = b[0xd7c];
        b[0xd7f] = throttle;
        b[0xd80] = b[0xd7d];
        if ((int8_t)b[0xd84] > 0) b[0xd84]--;
        if (g_sim_tick > 0x1c0) {
            bool automatic = transmission(c) == 1;
            if (automatic) {
                if (g_sim_tick < 0x1c4) {
                    b[0xd7e] = 2;   /* first gear at the start */
                } else {
                    int8_t gear = (int8_t)b[0xd82];
                    if ((int8_t)b[0xd7e] == gear && *CAR_I(c, 0xb50) == 0) {
                        if (*CAR_I(c, 0x544) == 0 && *CAR_I(c, 0x540) == 0) {
                            float v = *CAR_F(c, 0x35c);
                            if (v < K(0x5b23f8)) v = -v;
                            /* Stopped with the brake held: into reverse; with the throttle in reverse:
                               back to first. */
                            if (v < K(0x5b23f4) && 0x80 < b[0xd7d] && throttle == 0 && gear >= 2 && *CAR_I(c, 0xd98) == 0) {
                                b[0xd7e] = 0;
                                *CAR_I(c, 0xd98) = 1;
                            } else if (v < K(0x5b23f4) && 0x80 < throttle && gear == 0 && *CAR_I(c, 0xd98) != 0) {
                                b[0xd7e] = 2;
                                *CAR_I(c, 0xd98) = 0;
                            }
                        }
                    } else {
                        *CAR_I(c, 0xd98) = 0;
                    }
                }
            }
            int8_t want = (int8_t)b[0xd7e], gear = (int8_t)b[0xd82];
            if (want != gear) {
                const uint8_t *sp = CAR_SPECSB(c);
                if (automatic || *CAR_I(c, 0xda8)) {
                    if (want < 2) {
                        b[0xd83] = 0;
                        b[0xd81] = (uint8_t)gear;
                        b[0xd82] = (uint8_t)want;
                        b[0xd84] = sp[0x54];
                    } else if (want == 2 && gear < 2) {
                        /* Out of neutral or reverse: the gear for the speed (the highest whose speed at
                           the specs' +0x94 is below the car's forward speed). */
                        b[0xd81] = (uint8_t)gear;
                        int32_t ngears;
                        float base;
                        memcpy(&ngears, sp + 0x50, 4);
                        memcpy(&base, sp + 0x94, 4);
                        int g = 2;
                        for (int k = 2; k < ngears; k++) {
                            float ratio;
                            memcpy(&ratio, sp + 0x178 + 4 * (k - 2), 4);
                            if (base * ratio < *CAR_F(c, 0xd60)) g = k;
                        }
                        b[0xd83] = 0;
                        b[0xd82] = (uint8_t)g;
                        b[0xd84] = sp[0x54];
                    }
                } else {
                    b[0xd83] = want < gear && 1 < want ? 1 : 0;
                    b[0xd81] = (uint8_t)gear;
                    b[0xd82] = (uint8_t)want;
                    b[0xd84] = sp[0x54];
                }
            }
        }
        /* The steering towards the wheel's position: at most 12 a tick (18 on wet roads or for the
           small cars) with the description's +0x14 set, at once without; halfway when replaying. */
        int32_t *steer = CAR_I(c, 0xd94), want = *CAR_I(c, 0xd90);
        if (*CAR_I(c, 0xda8) == 0) {
            if (DESC_I(c, 0x14) == 0) {
                *steer = want;
            } else {
                int lim = g_race_rally || DESC_I(c, 0x518) ? 0x12 : 0xc;
                int d = want - *steer;
                if (d < 0) d = -d < lim ? d : -lim;
                else if (lim <= d) d = lim;
                *steer += d;
            }
        } else {
            *steer = (want + *steer) / 2;
        }
    }
    if (*started < 0x40) {
        b[0xd7f] = 0;
        b[0xd80] = 0xff;
    } else if (g_sim_tick < 0x1c0) {
        b[0xd80] = 0xff;   /* held at the start until the countdown ends */
    } else if (*CAR_I(c, 0x540) || *CAR_I(c, 0x544)) {
        if (*CAR_I(c, 0xd98) == 0) {
            b[0xd7f] = 0;
            b[0xd80] = 0x80;
        } else {
            b[0xd7f] = 0x80;
            b[0xd80] = 0;
        }
        b[0xd83] = 0;
    }
    if (*CAR_I(c, 0xd2c) == 2) b[0xd7f] = 0;
    if (g_race_demo && *CAR_I(c, 0xda8)) {
        float side = (float)*CAR_I(c, 0xeb0) * *CAR_F(c, 0xed0);
        *CAR_I(c, 0xd94) = side <= K(0x5b23f8) ? 0x7c : -0x7c;
        b[0xd85] = 1;
    }
    float full = g_race_players > 0 ? 240.0f : 252.0f;
    g_drive_throttle = (float)b[0xd7f] / full;
    if (K(0x5b24a8) <= g_drive_throttle) g_drive_throttle = 1.0f;
    g_drive_brake = (float)b[0xd80] / full;
    if (K(0x5b24a8) <= g_drive_brake) g_drive_brake = 1.0f;
    g_drive_steer = (float)*CAR_I(c, 0xd94) / (full * K(0x5b23f0));
    if (g_drive_steer < K(0x5b23f8)) g_drive_steer = -g_drive_steer;
    if (transmission(c) == 1 && b[0xd82] == 0 && *CAR_I(c, 0xd98) == 1) {
        /* In reverse (automatic): the pedals swap. */
        g_drive_throttle = (float)b[0xd80] / full;
        if (K(0x5b24a8) <= g_drive_throttle) g_drive_throttle = 1.0f;
        g_drive_brake = (float)b[0xd7f] / full;
        if (K(0x5b24a8) <= g_drive_brake) g_drive_brake = 1.0f;
    }
}

/* ---- the parts' condition ---- */

/* A part's wear: the mean damage of the zones it covers (its ten flags at +0x48 over the car's zones
   from +0x4e4) scaled by its +0x60, plus its +0x164. The body of 0x493810, 0x493980, 0x493a10,
   0x493b30. */
static double part_wear(Car *c, const uint8_t *part)
{
    float z = K(0x5b23f8);
    int k = 0;
    for (int j = 0; j < 10; j++)
        if (part[0x48 + j]) {
            z += *CAR_F(c, 0x4e4 + 4 * j);
            k++;
        }
    if (k > 1) z /= k;
    float a, b;
    memcpy(&a, part + 0x60, 4);
    memcpy(&b, part + 0x164, 4);
    return z * (K(0x5b246c) - a * K(0x5b3284)) + b;
}

static int16_t part_type(const uint8_t *part)
{
    int16_t t;
    memcpy(&t, part + 0x26, 2);
    return t;
}

float CarSim_EngineWear(Car *c)
{
    float sum = *CAR_F(c, 0x4e8);
    int n = 1;
    for (int i = 0; i < CAR_NPARTS; i++) {
        const uint8_t *part = c->p[CARP_PARTS + i];
        if (part && part_type(part) == 1) {
            n++;
            sum = part_wear(c, part) + sum;
        }
    }
    return (float)(sum / n);
}

static float wear_sum(Car *c, int16_t type)
{
    float sum = K(0x5b23f8);
    for (int i = 0; i < CAR_NPARTS; i++) {
        const uint8_t *part = c->p[CARP_PARTS + i];
        if (part && part_type(part) == type) sum = part_wear(c, part) + sum;
    }
    return (float)sum;
}

float CarSim_BrakeWear(Car *c) { return wear_sum(c, 2); }        /* 0x493980 */
float CarSim_GearboxWear(Car *c) { return wear_sum(c, 3); }      /* 0x493a10 */

/* The suspension's: the mean wear of its springs, shocks and sway bars (types 4, 5, 8). 0x493b30 */
float CarSim_SuspensionWear(Car *c)
{
    float sum = K(0x5b23f8);
    int n = 0;
    for (int i = 0; i < CAR_NPARTS; i++) {
        const uint8_t *part = c->p[CARP_PARTS + i];
        int16_t t = part ? part_type(part) : 0;
        if (t == 4 || t == 5 || t == 8) {
            n++;
            sum = part_wear(c, part) + sum;
        }
    }
    if (n > 1) sum /= n;
    return (float)sum;
}

/* The body's: its first nine zones summed, scaled. 0x493c00 */
float CarSim_BodyWear(Car *c)
{
    float sum = K(0x5b23f8);
    for (int k = 0; k < 9; k++) sum += *CAR_F(c, 0x4e4 + 4 * k);
    return (float)(sum * K(0x5b49b0));
}

/* ---- the slopes ---- */

void CarSim_SlopePull(Car *c)
{
    if ((*CAR_I(c, CAR_FLAGS) & 0x10) || (g_race_mode == 3 && g_race_network)) return;
    if (c->b[0xd80] < 0x40) *CAR_F(c, 0xd54) = *CAR_F(c, 0x380) * K(0x5b2438) + *CAR_F(c, 0xd54);
    float side = *CAR_F(c, 0xd58);
    if (side < K(0x5b23f8)) side = -side;
    if (K(0x5b24a8) < side || *CAR_F(c, 0x374) < K(0x5b4a6c))
        *CAR_F(c, 0xd4c) = *CAR_F(c, 0x368) * K(0x5b4a68) + *CAR_F(c, 0xd4c);
}

/* ---- the engine and gearbox ---- */

static float spec_f(Car *c, int off) { return *SPEC_F(c, off); }

/* The drag left by the cars ahead (drafting): 1 less the best slipstream of a race car (the racers'
   list, 0x5e8ae8: not the traffic or the police) within a third of this car's speed (in metres)
   straight ahead. In float steps, as the x87 at single precision computes them (0x532890 the length,
   0x532640 the unit vector, 0x532680 the dot product). 0x49d7a0 */
float CarSim_DraftFactor(Car *c)
{
    float best = 0.0f, speed = *CAR_F(c, 0xd60);
    for (int i = 0; i < g_racers.n; i++) {
        Car *o = g_racers.c[i];
        if (*CAR_I(o, 0) == *CAR_I(c, 0) || !(K(0x5b243c) < speed)) continue;
        float d[3] = {CAR_F(o, 0x330)[0] - CAR_F(c, 0x330)[0], CAR_F(o, 0x330)[1] - CAR_F(c, 0x330)[1],
                      CAR_F(o, 0x330)[2] - CAR_F(c, 0x330)[2]};
        float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (!(dist < speed * K(0x5b2680) && K(0x5b2604) < dist)) continue;
        float r = K(0x5b24a8) / sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        float u[3] = {r * d[0], d[1] * r, d[2] * r};
        float a = u[0] * CAR_F(c, 0x37c)[0] + u[1] * CAR_F(c, 0x37c)[1] + u[2] * CAR_F(c, 0x37c)[2];
        if (!(K(0x5b4a48) < a)) continue;
        float f = (a - K(0x5b4a48)) * K(0x5b4a44) * K(0x5b23f0) * (K(0x5b24a8) - dist * K(0x5b2600) / speed);
        if (best <= f) best = f;
    }
    return K(0x5b24a8) - best;
}

/* The automatic gearbox: up a gear past the redline (with some randomness), down when the lower gear
   stays under 80 % of it, off the throttle and not sliding. 0x49d8e0 */
void CarSim_AutoShift(Car *c)
{
    uint32_t r = SimRand_Next();
    const uint8_t *sp = CAR_SPECSB(c);
    int8_t gear = (int8_t)c->b[0xd82];
    float redline = spec_f(c, 0x94), low = redline * K(0x5b24e8);
    if (gear < 2) return;
    float slide = *CAR_F(c, 0xdc0);
    if (slide < K(0x5b23f8)) slide = -slide;
    int32_t ngears;
    memcpy(&ngears, sp + 0x50, 4);
    /* The top gear counts only when the car can reach its redline in it. */
    int top = ngears - (spec_f(c, 0x90) < spec_f(c, 0x168 + 4 * ngears) * redline ? 1 : 0) - 1;
    int up = gear < top ? gear + 1 : gear, down = gear > 2 ? gear - 1 : gear;
    float v = *CAR_F(c, 0x35c);
    if (*CAR_F(c, 0xd60) < K(0x5b23f8)) v = -v;
    float rpm = v * spec_f(c, 0x60 + 4 * gear);
    if (*CAR_F(c, 0xdb4) < rpm * K(0x5b2480)) rpm = *CAR_F(c, 0xdb4);
    float rpm_down = v * spec_f(c, 0x60 + 4 * down);
    if (g_drive_throttle <= K(0x5b23f0)) {
        float a = rpm < K(0x5b23f8) ? -rpm : rpm;
        if (a <= redline) {
            if (low <= rpm_down || down < 2 || K(0x5b2604) <= slide || down == gear) return;
            c->b[0xd83] = 1;
            c->b[0xd82] = (uint8_t)down;
            c->b[0xd84] = sp[0x54];
            return;
        }
    }
    int next;
    if (rpm <= redline * K(0x5b25e8) + (float)(r & 0xffff) * K(0x5b2604) * redline * K(0x5b253c) || top <= gear) {
        if (low <= rpm_down || down == gear) return;
        c->b[0xd83] = 1;
        c->b[0xd81] = (uint8_t)gear;
        next = down;
    } else {
        if (up == gear) return;
        c->b[0xd83] = 0;
        c->b[0xd81] = (uint8_t)gear;
        next = up;
    }
    c->b[0xd82] = (uint8_t)next;
    c->b[0xd84] = sp[0x54];
}

/* The torque curve at rpm (held to [0, redline]): the specs' table every 500 rpm from +0x9c,
   interpolated. */
static float torque_at(Car *c, float rpm)
{
    float redline = spec_f(c, 0x94);
    if (redline <= rpm) rpm = redline;
    if (!(K(0x5b23f8) < rpm)) rpm = K(0x5b23f8);
    int i = (int)(rpm * K(0x5b4a30));
    float a = spec_f(c, 0x9c + 4 * i), b = spec_f(c, 0xa0 + 4 * i);
    return (rpm - (float)(i * 500)) * K(0x5b4a30) * (b - a) + a;   /* (0x49e726: in this order) */
}

float CarSim_Engine(Car *c)
{
    uint8_t *b = c->b;
    uint32_t r = SimRand_Next();
    float redline = spec_f(c, 0x94), jump = redline * K(0x5b272c);
    /* (in float steps throughout, as the x87 at single precision computes them) */
    float noise = (float)(r & 0xffff) * K(0x5b253c);
    float wear = CarSim_EngineWear(c) * K(0x5b4a64);
    float fwd = *CAR_F(c, 0xd60);
    float drag = CarSim_DraftFactor(c) * (fwd * spec_f(c, 0x1b0) * fwd * fwd);   /* 0x49d7a0 */
    int8_t gear = (int8_t)b[0xd82];
    float *rpm = CAR_F(c, 0xdb4);
    /* The engine speed the throttle asks for. */
    float top = redline + (gear == 1 || !g_drive_rear_down ? K(0x5b4a60) : K(0x5b242c));
    float target = top * g_drive_throttle;
    if (top <= target) target = top;
    /* The rev limiter: a cut of some ticks at the redline. */
    int32_t *cut = CAR_I(c, 0x548);
    if (redline <= *rpm && *rpm < redline + K(0x5b4a60)) {
        *rpm = redline + K(0x5b251c);
        uint32_t q = SimRand_Next();
        if (gear < 2 || !g_drive_rear_down) {
            if (!(*CAR_I(c, CAR_FLAGS) & 4) && g_sim_tick < 0x1a0) *cut = ((int32_t)((q & 0xffff) * 0x14) >> 16) + 10;
            else *cut = ((int32_t)((q & 0xffff) << 1) >> 16) + 2;
        } else {
            *cut = ((int32_t)((q & 0xffff) << 1) >> 16) + gear * 3;
        }
    }
    if (*cut > 0) {
        g_drive_throttle = 0;
        target = 0;
        (*cut)--;
    }
    /* The clutch (+0x54c) by the clutch type (specs +0x4c: 1, 2). */
    int32_t ctype = *SPEC_I(c, 0x4c);
    if (ctype == 1 || ctype == 2) {
        float hi = redline * (ctype == 2 ? K(0x5b4a5c) : K(0x5b23f0));
        float lo = redline * (ctype == 2 ? K(0x5b49d4) : K(0x5b2468));
        float d = *rpm - lo, t = K(0x5b23f8);
        if (K(0x5b23f8) < d && (t = d / (hi - lo), K(0x5b24a8) <= t)) t = K(0x5b24a8);
        float base = (*rpm / redline) * K(0x5b23f0);
        if (ctype == 1) base = g_drive_throttle * base;
        float want = t * g_drive_throttle;
        if (want < base) want = base;
        if (b[0xd84] == 0) {
            float *cl = CAR_F(c, 0x54c);
            if (ctype == 2) {
                if (want <= *cl) {
                    float m = *cl - K(0x5b2604);
                    if (want <= m) want = m;
                }
            } else if (want <= *cl) {
                float m = *cl - K(0x5b26a0);
                if (want <= m) want = m;
            } else {
                float m = *cl + K(0x5b26a0);
                if (m < want) want = m;
            }
            *cl = want;
        }
    }
    *CAR_I(c, 0xdbc) = 0;
    *CAR_I(c, 0xdb8) = 0;
    if (gear == 1 || (int8_t)b[0xd84] > 0 || !g_drive_rear_down) {
        /* Free of the wheels (neutral, shifting, the driven wheels off the ground). */
        float v;
        bool keep = false;
        if (wear <= noise) {
            if (target <= *rpm || b[0xd84] != 0) {
                if ((int8_t)b[0xd84] < 1 || b[0xd81] == 1) {
                    if (*rpm < target) {
                        keep = true;
                        v = *rpm;
                    } else {
                        v = *rpm - K(0x5b2484);
                        if (v <= target) v = target;
                    }
                } else if (b[0xd83] == 0) {
                    v = *rpm - (gear > 3 ? K(0x5b2484) : K(0x5b242c));
                    if (v <= K(0x5b23f8)) v = K(0x5b23f8);
                } else {
                    /* A downshift's blip by the gear asked for (0x5d0c70 off the brake, 0x5d0c90 on it). */
                    int8_t want = (int8_t)b[0xd7e];
                    v = *rpm + K((b[0xd80] < 0x41 ? 0x5d0c70u : 0x5d0c90u) + 4u * (uint32_t)want);
                    if (redline < v) v = redline;
                }
            } else {
                v = *rpm + K(0x5b4a60);
                if (target < v) v = target;
            }
        } else {
            v = *rpm - K(0x5b242c);
            if (v <= K(0x5b23f8)) v = K(0x5b23f8);
        }
        if (!keep) *rpm = v;
        float *cl = CAR_F(c, 0x54c);
        float m = *cl - K(0x5b2604);
        *cl = m < K(0x5b23f8) ? K(0x5b23f8) : m;
        return 0.0f - drag;
    }
    if (DESC_I(c, 0xc) == 1 || *CAR_I(c, 0xda8)) CarSim_AutoShift(c);   /* 0x49d8e0 */
    gear = (int8_t)b[0xd82];
    int8_t rg = (b[0xd84] == 0 || b[0xd83] != 0) ? gear : (int8_t)b[0xd81];
    float wheel = spec_f(c, 0x60 + 4 * rg) * fwd;   /* the engine speed the wheels turn it at */
    float torque = torque_at(c, *rpm) * spec_f(c, 0x190 + 4 * gear);
    torque = (K(0x5b24a8) - CarSim_EngineWear(c) * K(0x5b4a64)) * torque;
    torque = (K(0x5b24a8) - CarSim_GearboxWear(c) * K(0x5b3284)) * torque;
    float aw = wheel < K(0x5b23f8) ? -wheel : wheel;
    float slip = target - aw;
    float as = slip < K(0x5b23f8) ? -slip : slip;
    if (as < K(0x5b4a58) && target <= redline - K(0x5b2524)) slip = 0.0f;
    float d = *rpm - aw;
    if ((K(0x5b4a60) < d && b[0xd84] == 0 && gear != 1) || (gear >= 2 && fwd < K(0x5b2444)) ||
        (gear == 0 && K(0x5b2604) < fwd) || (*CAR_I(c, 0xdb8) == 1 && *cut == 0)) {
        /* The clutch slipping: the engine pulled down to the wheels by what they can take. */
        float f = torque * K(0x5b243c);
        if (f < K(0x5b23f8)) f = -f;
        if (spec_f(c, 0x5c) <= K(0x5b23f8))
            f = K(0x5b242c) - (f - (*CAR_F(c, 0xab0) + *CAR_F(c, 0x9ec)));
        else
            f = K(0x5b3d60) - (f - (*CAR_F(c, 0xab0) + *CAR_F(c, 0x9ec) + *CAR_F(c, 0x928) + *CAR_F(c, 0x864)));
        float tp = (float)b[0xd7c] * K(0x5b4a54);
        if (tp < K(0x5b25f0)) tp = K(0x5b25f0);
        float lim = (K(0x5b24a8) - g_drive_throttle) * K(0x5b242c) + f / tp;
        if (*SPEC_I(c, 0x54) < 5 && gear > 2) *rpm -= d * K(0x5b49d4);
        if (g_drive_throttle < K(0x5b23f0) || noise < wear) {
            *rpm -= d * K(0x5b49d4);
        } else {
            if (jump < d) {
                if (K(0x5b23f8) < spec_f(c, 0x5c)) *CAR_I(c, 0xdbc) = 2;
                *CAR_I(c, 0xdb8) = 2;
            }
            if (lim < d) d = lim;
            *rpm -= d;
        }
    } else if (K(0x5b23f8) <= slip) {
        if (slip == K(0x5b23f8)) {
            torque = drag;
            *rpm = wheel;
        } else {
            if (noise < wear) {
                *rpm -= K(0x5b242c);
                torque = K(0x5b23f8);
            } else {
                if (d <= K(0x5b2484)) {
                    if (K(0x5b4a4c) <= d) *rpm = wheel;
                    else *rpm += K(0x5b2484);
                } else {
                    *rpm -= K(0x5b2484);
                }
                torque = torque * g_drive_throttle;
            }
            if (target < *rpm) *rpm = target;
            if (g_race_rally) {
                float k = *CAR_F(c, 0xdc0);
                if (k < K(0x5b23f8)) k = -k;
                k += K(0x5b24a8);
                if (K(0x5b2580) <= k) k = K(0x5b2580);
                torque = k * torque * K(0x5b3f64);
            }
        }
    } else {
        /* Engine braking. */
        float step = spec_f(c, 0x170 + 4 * gear) * K(0x5b4a50), dd = -d;
        if (aw <= redline + K(0x5b24e0)) {
            if (*cut > 0) torque *= K(0x5b2404);
            float g = CarSim_GearboxWear(c) * K(0x5b3714) + K(0x5b24a8);
            if (K(0x5b2400) <= g) g = K(0x5b2400);
            torque = g * -(torque * spec_f(c, 0x98));
            if (gear == 0) {
                if (dd <= step) dd = step;
            } else if (step < dd) {
                dd = step;
            }
            *rpm += dd;
            if (*rpm <= target) *rpm = target;
        } else {
            torque = -torque;
            if (gear == 0) {
                if (dd <= step) dd = step;
            } else if (step < dd) {
                dd = step;
            }
            *rpm = dd + *rpm;
        }
    }
    if (K(0x5b23f8) <= *rpm) {
        float lim = redline * K(0x5b3f64);
        if (lim < *rpm) *rpm = lim;
        return torque - drag;
    }
    float v = -(fwd * K(0x5b2644));
    if ((torque <= K(0x5b23f8) || v <= K(0x5b23f8) || torque - v <= K(0x5b23f8)) &&
        (K(0x5b23f8) <= torque || K(0x5b23f8) <= v || K(0x5b23f8) <= torque - v)) {
        *rpm = 0;
        return v - drag;
    }
    return (float)(torque - drag);
}

/* ---- the tyres' loads ---- */

int32_t g_race_alt_grip;   /* 0x606ae0: the second grip table where there is no roof over the car */

float CarSim_AeroWear(Car *c) { return wear_sum(c, 6); }   /* 0x493aa0 */

void CarSim_TyreLoads(Car *c)
{
    float load = *CAR_F(c, 0x4a0) * spec_f(c, 0xf4) * K(0x5b25bc);   /* the weight along the ground's normal */
    bool alt = g_race_alt_grip && WSurface_FindAbove(&c->body, CAR_F(c, 0x330), NULL) == 0;   /* 0x473290 */
    float front = spec_f(c, 0xf0);
    bool assisted = (*CAR_I(c, CAR_FLAGS) & 8) || *CAR_I(c, 0xda8);
    for (int i = 0; i < 4; i++) {
        int w = 0xc4 * i;
        int32_t tyre = *SPEC_I(c, 0x1c0);
        uint32_t surf = (uint32_t)*CAR_I(c, 0x844 + w) & 0xf;
        float grip;
        /* The grip by tyre and surface (three tyres per surface): 0x5d0e44 in the open, 0x5d0d48 else. */
        if (alt) {
            if (assisted && tyre == 0) tyre = 1;
            grip = K(0x5d0e44 + 4u * (uint32_t)(tyre + (int32_t)surf * 3));
            *CAR_I(c, 0x838 + w) = 1;
        } else {
            if (assisted && tyre == 1) tyre = 0;
            grip = K(0x5d0d48 + 4u * (uint32_t)(tyre + (int32_t)surf * 3));
            *CAR_I(c, 0x838 + w) = 0;
        }
        float share = i < 2 ? front : K(0x5b24a8) - front;
        *CAR_F(c, 0x864 + w) = i < 2 ? grip * front * load * K(0x5b23f0) : share * grip * load * K(0x5b23f0);
    }
    /* The loads moved by braking and accelerating (front to rear)... */
    float move = (*CAR_F(c, 0xd54) * spec_f(c, 0xf8) * K(0x5b4a9c) * load * K(0x5b4a98)) / spec_f(c, 0x11c);
    if (c->b[0xd7f] && K(0x5b23f8) < *CAR_F(c, 0xd60)) {
        float s = *CAR_F(c, 0xdc0);
        if (s < K(0x5b23f8)) s = -s;
        s = K(0x5b24a8) - s * K(0x5b241c);
        if (s < K(0x5b23f8)) s = K(0x5b23f8);
        move = s * move;
    }
    float fl = *CAR_F(c, 0x864) - move, fr = *CAR_F(c, 0x928) - move;
    *CAR_F(c, 0x864) = fl;
    *CAR_F(c, 0x928) = fr;
    *CAR_F(c, 0x9ec) = move + *CAR_F(c, 0x9ec);
    *CAR_F(c, 0xab0) = move + *CAR_F(c, 0xab0);
    if (g_race_rally) {
        *CAR_F(c, 0x864) = fl * K(0x5b27a4);
        *CAR_F(c, 0x928) = fr * K(0x5b27a4);
    }
    /* ... and by cornering (left to right). */
    float side = (*CAR_F(c, 0xd4c) * spec_f(c, 0xf8) * K(0x5b4a9c) * load * K(0x5b23f4)) / spec_f(c, 0x11c);
    float other = side / spec_f(c, 0x130);
    if (side <= K(0x5b23f8)) {
        *CAR_F(c, 0x864) -= side;
        *CAR_F(c, 0x9ec) -= side;
        *CAR_F(c, 0x928) = other + *CAR_F(c, 0x928);
    } else {
        *CAR_F(c, 0x864) -= other;
        *CAR_F(c, 0x9ec) -= other;
        *CAR_F(c, 0x928) = side + *CAR_F(c, 0x928);
        other = side;
    }
    uint32_t g = 0x3f947ae1u;   /* the gravity scale: 1.16, more with the rear's downforce */
    memcpy(CAR_F(c, 0x84), &g, 4);
    *CAR_F(c, 0xab0) = other + *CAR_F(c, 0xab0);
    if (K(0x5b2400) < *CAR_F(c, 0xd60)) {
        /* Downforce with the speed (less with the aero parts' wear). */
        float a = K(0x5b24a8) - CarSim_AeroWear(c) * K(0x5b3284);
        float df = a * spec_f(c, 0x10c) * *CAR_F(c, 0xd60);
        *CAR_F(c, 0x864) = df + *CAR_F(c, 0x864);
        *CAR_F(c, 0x928) = df + *CAR_F(c, 0x928);
        float dr = a * spec_f(c, 0x110) * *CAR_F(c, 0xd60);
        *CAR_F(c, 0x9ec) = dr + *CAR_F(c, 0x9ec);
        *CAR_F(c, 0xab0) = dr + *CAR_F(c, 0xab0);
        if (K(0x5b4a0c) < *CAR_F(c, 0x3a0)) *CAR_F(c, 0x84) = dr * K(0x5b2608) + *CAR_F(c, 0x84);
    }
}

/* ---- the tyres ---- */

float CarSim_TyreWear(Car *c) { return wear_sum(c, 9); }   /* 0x4938f0 */

/* A wheel object's fields (W = car + 0x7b8 + 0xc4 i): +0x8c the surface, +0x90 the sideways and +0x98
   the lengthways force, +0x9c the sideways and +0xa4 the lengthways speed of the ground under the wheel
   (scaled by -16), +0xa8 the drive or brake torque in, +0xac the load, +0xb4 the steering angle, +0xb8
   the sliding, +0xbc locked, +0xc0 a front wheel. */
#define WF(c, w, off) CAR_F(c, 0x7b8 + 0xc4 * (w) + (off))
#define WI(c, w, off) CAR_I(c, 0x7b8 + 0xc4 * (w) + (off))

/* (0x532550 / 0x532560 take the angle times 2 pi, which the x87 at single precision rounds to a float
   before fsin / fcos; their results stay unrounded) */
static double turns_to_rad(double t)
{
    double k;
    const uint8_t *p = exe_data(0x5bb2c0, 8);
    if (p) memcpy(&k, p, 8);
    else k = 6.283185307179586;
    return (float)(t * k);
}

/* atan2 in turns (0x5bb2c8 is 1 / 2 pi). 0x5325d0 */
static double atan2_turns(float y, float x)
{
    double k;
    const uint8_t *p = exe_data(0x5bb2c8, 8);
    if (p) memcpy(&k, p, 8);
    else k = 0.15915494309189535;
    return atan2((double)y, (double)x) * k;
}

/* A wheel locked (or held by the handbrake): the friction circle against its sliding. 0x4a1a50 */
static void tyre_locked(Car *c, int w)
{
    float cap = (K(0x5b24a8) - CarSim_TyreWear(c) * K(0x5b3284)) * *WF(c, w, 0xac);
    float a = *WF(c, w, 0x9c), b = *WF(c, w, 0xa4);
    if (a < K(0x5b23f8)) a = -a;
    if (b < K(0x5b23f8)) b = -b;
    float mag = a <= b ? a * K(0x5b2468) + b : b * K(0x5b2468) + a;
    float k = K(0x5d0f50 + 4u * (uint32_t)*SPEC_I(c, 0x1c0));
    float *slide = WF(c, w, 0xb8);
    if (mag <= cap) *slide = 0;
    else *slide = (*slide * K(0x5b2600) + (mag - cap)) * K(0x5b2468);
    float s = *slide * K(0x5b49b4);
    *slide = K(0x5b24a8) < s ? K(0x5b24a8) : s;
    float m = mag < K(0x5b23f8) ? -mag : mag;
    float f = a;   /* (a near-still wheel keeps the sideways speed's size as its factor) */
    if (K(0x5b27a8) < m) f = (cap - cap / k) / mag;
    *WF(c, w, 0x90) = f * *WF(c, w, 0x9c);
    *WF(c, w, 0x98) = f * *WF(c, w, 0xa4);
}

/* The friction circle of a rolling wheel, with the traction control (the description's TRACTION, +0x20)
   holding the driven wheels' spin. 0x4a1ba0 */
static void tyre_grip(Car *c, int w)
{
    float l = *WF(c, w, 0x98) * (g_race_rally ? K(0x5b23f0) : K(0x5b25e8)), s = *WF(c, w, 0x90);
    if (l < K(0x5b23f8)) l = -l;
    if (s < K(0x5b23f8)) s = -s;
    float big = l, small = s;
    if (l < s) {
        big = s;
        small = l;
    }
    float mag = small * K(0x5b2468) + big;
    float cap = (K(0x5b24a8) - CarSim_TyreWear(c) * K(0x5b3284)) * *WF(c, w, 0xac);
    int32_t front = *WI(c, w, 0xc0);
    float lf = *WF(c, w, 0x98);
    if (lf < K(0x5b23f8)) lf = -lf;
    bool spin = (cap < lf && 0x80 < c->b[0xd7f]) || *CAR_I(c, front ? 0xdbc : 0xdb8) == 2;
    float k = !front && spin ? 4.0f : K(0x5d0f50 + 4u * (uint32_t)*SPEC_I(c, 0x1c0));
    float *slide = WF(c, w, 0xb8);
    bool tc = DESC_I(c, 0x20) != 0;
    if (mag <= cap) {
        *slide = 0;
    } else {
        bool held = false;
        if (tc && !front) {
            float a = *CAR_F(c, 0xdc0);
            if (a < K(0x5b23f8)) a = -a;
            held = a < K(0x5b2638);
        }
        bool slides = !held && (*CAR_I(c, 0xda8) == 0 || (*WI(c, w, 0x8c) < 2 && *SPEC_I(c, 0x1c0) != 2));
        if (slides) {
            float r = (mag - cap) / k;
            if (cap / k <= r) r = cap / k;
            float f = (cap - r) / mag;
            *slide = (*slide * K(0x5b2600) + (mag - cap) * (front ? K(0x5b23f0) : K(0x5b240c))) * K(0x5b2468);
            *WF(c, w, 0x90) *= f;
            *WF(c, w, 0x98) *= f;
        } else {
            *slide = 0;
            if ((int8_t)c->b[0xd82] > 2 || *CAR_I(c, 0xda8)) *WF(c, w, 0x90) = (cap / mag) * *WF(c, w, 0x90);
            *WF(c, w, 0x98) = (cap / mag) * *WF(c, w, 0x98);
        }
    }
    if (!front && spin && !tc) *slide = mag;
    float v = *slide;
    if (K(0x5b24a8) < v) v = K(0x5b24a8);
    *slide = v;
    if (tc) *slide = v * K(0x5b23f0);
}

/* Rotates (x, z) by t turns. */
static void rotate(float *x, float *z, float t)
{
    /* (0x4a1f39: the sine kept as a float, the cosine on the stack; each product a float) */
    float sn = (float)sin(turns_to_rad(t));
    double cs = cos(turns_to_rad(t));
    float a = *x, b = *z;
    *x = (float)(cs * a) - sn * b;
    *z = sn * a + (float)(cs * b);
}

void CarSim_Tyre(Car *c, int w)
{
    float load = *WF(c, w, 0xac);
    bool braking = false;
    float steer = spec_f(c, 0xfc) * *WF(c, w, 0xb4) * K(0x5b4aa4);
    *WF(c, w, 0xb8) = 0;
    *WI(c, w, 0xbc) = 0;
    *WF(c, w, 0x98) = 0;
    *WF(c, w, 0x90) = 0;
    if (steer != K(0x5b23f8)) rotate(WF(c, w, 0x9c), WF(c, w, 0xa4), steer * K(0x5b2640));   /* into the wheel's frame */
    float *drive = WF(c, w, 0xa8), vz = *WF(c, w, 0xa4);
    int8_t gear = (int8_t)c->b[0xd82];
    if (K(0x5b23f8) <= *drive || K(0x5b23f8) <= vz) {
        if (K(0x5b23f8) < *drive && K(0x5b23f8) < vz && (g_drive_throttle <= K(0x5b2468) || gear < 2)) {
            if (vz < *drive) *drive = vz;
            braking = true;
        }
    } else if (g_drive_throttle <= K(0x5b2468) || gear != 0) {
        if (*drive < vz) *drive = vz;
        braking = true;
    }
    float f = spec_f(c, 0xf4) * *drive;
    *drive = f;
    float fwd = *CAR_F(c, 0xd60), afwd = fwd < K(0x5b23f8) ? -fwd : fwd;
    bool over;
    if (braking) {
        float af = f < K(0x5b23f8) ? -f : f;
        over = !(af <= *WF(c, w, 0xac));
    } else {
        over = false;
    }
    if (!over && c->b[0xd85] && *WI(c, w, 0xc0) == 0 && K(0x5b23f0) < afwd) over = true;   /* the handbrake */
    if (over) {
        if (c->b[0xd85]) {
            tyre_locked(c, w);
            *WI(c, w, 0xbc) = 1;
            return;
        }
        /* Locking under hard braking without ABS (the description's +0x24). */
        float lim = K(0x5b26a8) - *WF(c, w, 0xac);
        if (DESC_I(c, 0x24) == 0 && fwd <= lim + lim && 0xeb < c->b[0xd80] && (K(0x5b2400) <= afwd || *CAR_I(c, 0xdb8)) &&
            (*CAR_I(c, 0xda8) == 0 || (*WI(c, w, 0x8c) < 2 && *SPEC_I(c, 0x1c0) != 2))) {
            tyre_locked(c, w);
            *WI(c, w, 0xbc) = 1;
            return;
        }
        float cap = *WF(c, w, 0xac);
        if (cap < f) *drive = cap;
        else if (f < -cap) *drive = -cap;
        if (DESC_I(c, 0x24) && fwd < K(0x5b24dc)) {
            /* ABS: locked one tick in four. */
            if ((g_sim_tick & 3) == 0) {
                tyre_locked(c, w);
                *WI(c, w, 0xbc) = 1;
                return;
            }
            *WF(c, w, 0xb8) = 0;
        }
    }
    /* The sideways force by the slip angle. */
    float vx = *WF(c, w, 0x9c);
    vz = *WF(c, w, 0xa4);
    /* (the original's x87 runs at single precision, Direct3D's setting: each step rounds to a float) */
    float ang, a;
    if ((vz < K(0x5b23f8) ? -vz : vz) == K(0x5b23f8)) {
        ang = K(0x5b23f8);
        a = K(0x5b23f8);
    } else {
        ang = (float)atan2_turns(vx, vz * K(0x5b2448));
        if (K(0x5b23f8) < vz && K(0x5b23f8) < vx) {
            ang = (K(0x5b23f0) - ang) * K(0x5b26b0);
        } else {
            if (K(0x5b23f8) < vz && vx < K(0x5b23f8)) ang = K(0x5b2448) - ang;
            ang = ang * K(0x5b26b0);
        }
        a = ang;
    }
    float aa = ang < K(0x5b23f8) ? -ang : ang;
    if (*WI(c, w, 0xc0) == 0) {
        float m = K(0x5b24a8);
        if (K(0x5b24a8) < aa && (m = aa, K(0x5b2580) < m)) m = K(0x5b2580);
        float lat = m * load * K(0x5b23f0);
        if (a < K(0x5b23f8)) lat = -lat;
        float al = lat < K(0x5b23f8) ? -lat : lat;
        if (vx <= K(0x5b23f8)) *WF(c, w, 0x90) = -al < vx ? vx : -al;
        else *WF(c, w, 0x90) = vx <= al ? vx : al;
    } else {
        if (K(0x5b26a8) < aa) aa = K(0x5b26a8);
        float lat = aa * load * K(0x5b49b0);
        if (a < K(0x5b23f8)) lat = -lat;
        *WF(c, w, 0x90) = lat;
        float ax = vx < K(0x5b23f8) ? -vx : vx, az = vz < K(0x5b23f8) ? -vz : vz;
        if (az + ax < K(0x5b2644)) {
            float al = lat < K(0x5b23f8) ? -lat : lat;
            if (vx <= K(0x5b23f8)) *WF(c, w, 0x90) = -al < vx ? vx : -al;
            else *WF(c, w, 0x90) = vx <= al ? vx : al;
        }
    }
    *WF(c, w, 0x94) = 0;
    *WF(c, w, 0x98) = *WF(c, w, 0xa8);
    tyre_grip(c, w);   /* 0x4a1ba0 */
    if (g_race_rally && *WI(c, w, 0xc0)) *WF(c, w, 0x90) *= K(0x5b2480);
    if (steer != K(0x5b23f8)) {
        /* Back into the car's frame. */
        float t = steer * K(0x5b4aa0);
        float sn = (float)sin(turns_to_rad(t));
        double cs = cos(turns_to_rad(t));
        float x = *WF(c, w, 0x90), z = *WF(c, w, 0x98);
        *WF(c, w, 0x90) = (float)(cs * x) - sn * z;
        *WF(c, w, 0x98) = sn * x + (float)(cs * z);
    }
}

/* ---- the drive task ---- */

/* The ground's gravity in the car's ground frame (0x628fa0). */
static float g_drive_gravity[3];

static float dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : hi < v ? hi : v; }

/* 0x49ef50 */
static void drive(Car *c)
{
    g_drive_front_down = 1;
    g_drive_rear_down = 1;
    if (*CAR_I(c, 0x83c) && *CAR_I(c, 0x900) && K(0x5b26a0) < *CAR_F(c, 0x424)) g_drive_front_down = 0;
    if (*CAR_I(c, 0x9c4) && *CAR_I(c, 0xa88) && K(0x5b26a0) < *CAR_F(c, 0x424)) g_drive_rear_down = 0;
    const float *side = CAR_F(c, 0x490), *up = CAR_F(c, 0x49c), *fw = CAR_F(c, 0x4a8);
    const float *vel = CAR_F(c, 0x33c), *av = CAR_F(c, 0x388);
    *CAR_F(c, 0xd58) = dot(vel, side);
    *CAR_F(c, 0xd5c) = dot(vel, up);
    *CAR_F(c, 0xd60) = dot(vel, fw);
    *CAR_F(c, 0xd64) = dot(av, side);
    *CAR_F(c, 0xd68) = dot(av, up);
    *CAR_F(c, 0xd6c) = dot(av, fw);
    float fwd = *CAR_F(c, 0xd60), speed = *CAR_F(c, 0x35c);
    *CAR_F(c, 0xdc0) = fwd <= K(0x5b2400) ? 0.0f : *CAR_F(c, 0xd58) / fwd;   /* the slide angle */
    /* The ground's speed under the wheels (with the yaw's part at the axles). */
    float k = K(0x5b2644) * K(0x5b2448);
    float ux = *CAR_F(c, 0xd58) * k, uy = *CAR_F(c, 0xd5c) * k, uz = *CAR_F(c, 0xd60) * k;
    float yaw = spec_f(c, 0x1b4) * *CAR_F(c, 0x38c) * K(0x5b2644) * K(0x5b2448);
    if (K(0x5b241c) <= speed) yaw = (speed + K(0x5b26f4)) * yaw * K(0x5b4a84);
    else yaw = (speed + K(0x5b2408)) * yaw * K(0x5b4a2c);
    float g[3] = {0.0f, -9.8f, 0.0f};
    uint32_t gb = 0xc11ccccdu;
    memcpy(&g[1], &gb, 4);
    g_drive_gravity[0] = dot(g, side);
    g_drive_gravity[1] = dot(g, up);
    g_drive_gravity[2] = dot(g, fw);
    for (int w = 0; w < 4; w++) {
        *WF(c, w, 0x9c) = w < 2 ? yaw + ux : ux - yaw;
        *WF(c, w, 0xa0) = uy;
        *WF(c, w, 0xa4) = uz;
    }
    CarSim_Controls(c);   /* 0x49db10 */
    /* The steering: the driver's, the slope's pull and the front wheels' damage. */
    int slope = clampi((int)rintf(g_drive_gravity[0] * K(0x5b2608)), -0x40, 0x40);
    float wobble = *CAR_F(c, 0x4ec) * K(0x5b2468) - *CAR_F(c, 0x4e4) * K(0x5b2468);
    float shake = 0.0f;
    if ((g_sim_tick & 3) == 0) {
        uint32_t r = SimRand_Next();
        shake = ((float)(r & 0xffff) * K(0x5b253c) - K(0x5b23f0)) * CarSim_TyreBump(c) * K(0x5b23f0);
        if (speed < K(0x5b243c)) shake = shake * speed * K(0x5b49ec);
        wobble = shake + wobble;
    }
    int steer = clampi((int)rintf(wobble) + *CAR_I(c, 0xd94) + slope, -0x7f, 0x7f);
    /* The force feedback (+0x55c..+0x564). */
    if (!g_drive_front_down) {
        memset(CAR_F(c, 0x55c), 0, 12);
    } else {
        float x = ((*CAR_F(c, 0x934) + *CAR_F(c, 0x870)) * K(0x5b2404)) / spec_f(c, 0xf4);
        if (K(0x5b24e8) <= x) x = K(0x5b24e8);
        *CAR_F(c, 0x564) = x;
        *CAR_F(c, 0x55c) = K(0x5b24a8) - x;
        float y = shake * K(0x5b2484) + *CAR_F(c, 0x564) * K(0x5b275c);
        *CAR_F(c, 0x564) = y < K(0x5b23f8) ? -y : y;
        *CAR_F(c, 0x560) = ((float)slope + wobble) * *CAR_F(c, 0x55c) * K(0x5b242c);
        if (speed <= K(0x5b251c)) *CAR_F(c, 0x55c) = *CAR_F(c, 0x55c) * speed * K(0x5b2484);
        else *CAR_F(c, 0x55c) *= K(0x5b2764);
    }
    /* The front wheels' angles (less lock at speed). */
    float lock = spec_f(c, 0xfc);
    if (speed < K(0x5b2408)) lock = (K(0x5b36e8) - speed * K(0x5b4a2c)) * lock;
    if (K(0x5b2470) <= lock) lock = K(0x5b2470);
    lock = (K(0x5b4a80) / spec_f(c, 0xfc)) * lock;
    float toe = spec_f(c, 0x100);
    float *a0 = WF(c, 0, 0xb4), *a1 = WF(c, 1, 0xb4);
    *a0 = ((float)steer + toe) * (K(0x5b24a8) - *CAR_F(c, 0x4e4) * K(0x5b4a7c)) * lock * K(0x5b4a78);
    *a1 = ((float)steer - toe) * (K(0x5b24a8) - *CAR_F(c, 0x4ec) * K(0x5b4a7c)) * lock * K(0x5b4a78);
    if (K(0x5b26d4) < fwd) {
        float d = fwd * K(0x5b4a84);
        *a0 /= d;
        *a1 /= d;
    }
    /* (in doubles for the x87's exponent range: the filter decays through the denormals, where the
       halves stay exact until the store) */
    for (int w = 0; w < 2; w++)
        *CAR_F(c, 0xac8 + 4 * w) = (float)((double)*WF(c, w, 0xb4) * K(0x5b23f0) + (double)*CAR_F(c, 0xac8 + 4 * w) * K(0x5b23f0));
    float torque = CarSim_Engine(c);   /* 0x49e230 */
    if (!g_drive_front_down && !g_drive_rear_down) {
        memset(CAR_F(c, 0xd4c), 0, 12);
        for (int w = 0; w < 4; w++) *WF(c, w, 0xb8) = 0;
        return;
    }
    *CAR_I(c, 0xd2c) = 0;
    /* The brakes (held to what stops the car this tick), shared front and rear by the specs' +0x108. */
    float brake = (K(0x5b24a8) - CarSim_BrakeWear(c) * K(0x5b3284)) * spec_f(c, 0x104) * g_drive_brake;
    float af = fwd < K(0x5b23f8) ? -fwd : fwd;
    if (af * K(0x5b2644) < brake) brake = af * K(0x5b2644);
    if (K(0x5b23f8) < fwd) brake = -brake;
    float fbrake = brake * spec_f(c, 0x108);
    CarSim_TyreLoads(c);   /* 0x4a16e0 */
    memset(CAR_F(c, 0xd4c), 0, 12);
    /* The torque to the wheels by the specs' front share (+0x5c) and their loads. */
    float share = spec_f(c, 0x5c);
    float l0 = *WF(c, 0, 0xac), l1 = *WF(c, 1, 0xac), l2 = *WF(c, 2, 0xac), l3 = *WF(c, 3, 0xac);
    *WF(c, 0, 0xa8) = (torque * share * l0) / (l1 + l0);
    *WF(c, 1, 0xa8) = (l1 * share * torque) / (l1 + l0);
    *WF(c, 2, 0xa8) = ((K(0x5b24a8) - share) * l2 * torque) / (l2 + l3);
    *WF(c, 3, 0xa8) = ((K(0x5b24a8) - share) * l3 * torque) / (l2 + l3);
    for (int w = 0; w < 4; w++) {
        *WI(c, w, 0xbc) = 0;
        *WF(c, w, 0x98) = 0;
        *WF(c, w, 0x90) = 0;
    }
    float fb = fbrake * K(0x5b23f0), rb = (brake - fbrake) * K(0x5b23f0);
    *WF(c, 0, 0xa8) += fb;
    *WF(c, 1, 0xa8) += fb;
    *WF(c, 2, 0xa8) += rb;
    *WF(c, 3, 0xa8) += rb;
    if (g_drive_front_down) {
        CarSim_Tyre(c, 0);
        CarSim_Tyre(c, 1);
    }
    if (g_drive_rear_down) {
        CarSim_Tyre(c, 2);
        CarSim_Tyre(c, 3);
    }
    /* The forces and the yaw torque in the ground frame, then into the world. */
    float *F = CAR_F(c, 0xd4c);
    F[1] = 0;
    float lon = *WF(c, 3, 0x98) + *WF(c, 2, 0x98) + *WF(c, 1, 0x98) + *WF(c, 0, 0x98);
    float rearlat = *WF(c, 3, 0x90) + *WF(c, 2, 0x90);
    F[0] = *WF(c, 1, 0x90) + *WF(c, 0, 0x90) + rearlat;
    F[2] = lon * spec_f(c, 0x1bc);
    float t[3] = {0.0f, 0.0f, 0.0f};
    t[1] = ((*WF(c, 1, 0x90) + *WF(c, 0, 0x90)) - rearlat) * spec_f(c, 0x1b8) -
           (((*WF(c, 2, 0x98) + *WF(c, 0, 0x98)) - *WF(c, 1, 0x98)) - *WF(c, 3, 0x98)) * spec_f(c, 0x1b8);
    float rt[9];
    Mat3_Transpose(CAR_F(c, 0x490), rt);
    CarSim_SlopePull(c);   /* 0x49eeb0 */
    float *acc = CAR_F(c, 0xd40), *tw = CAR_F(c, 0xd34);
    acc[0] = dot(F, rt);
    acc[1] = dot(F, rt + 3);
    acc[2] = dot(F, rt + 6);
    tw[0] = dot(t, rt);
    tw[1] = dot(t, rt + 3);
    tw[2] = dot(t, rt + 6);
    float *v = CAR_F(c, 0x33c), *w = CAR_F(c, 0x388);
    float dt = K(0x5b24b8);
    v[0] = acc[0] * dt + v[0];
    v[1] = acc[1] * dt + v[1];
    v[2] = acc[2] * dt + v[2];
    w[0] = tw[0] * dt + w[0];
    w[2] = tw[2] * dt + w[2];
    w[1] = tw[1] * (DESC_I(c, 0x518) ? K(0x5b24b8) : K(0x5b4a74)) + w[1];
    *CAR_F(c, 0xe08) = *CAR_F(c, 0xe08) * K(0x5b260c) + F[0] * K(0x5b2468);
    *CAR_F(c, 0xe04) = F[2] * K(0x5b2604) + *CAR_F(c, 0xe04) * K(0x5b25e8);
    if (g_race_rally) {
        float s = *CAR_F(c, 0xdc0);
        if (s < K(0x5b23f8)) s = -s;
        if (K(0x5b2400) < s) s = K(0x5b2400);
        float m = K(0x5b24a8) - s * K(0x5b4a30);
        for (int j = 0; j < 3; j++) v[j] *= m;
    }
    if (*CAR_I(c, 0x464) == 3 && K(0x5b4a70) < speed) {
        uint32_t m = 0x3f7eb852u;
        float f;
        memcpy(&f, &m, 4);
        for (int j = 0; j < 3; j++) v[j] *= f;
        return;
    }
    int8_t gear = (int8_t)c->b[0xd82];
    if (gear > 1 && g_drive_throttle < K(0x5b2468) && speed < K(0x5b2408) && K(0x5b2464) < spec_f(c, 0xf4)) {
        /* Coasting slowly in gear: the yaw bleeds the speed. */
        float y = *CAR_F(c, 0x38c);
        if (y < K(0x5b23f8)) y = -y;
        float f = K(0x5b24a8) - y * K(0x5b3f6c);
        v[0] = f * v[0];
        v[2] = f * v[2];
        return;
    }
    if (gear == 1) {
        float gz = g_drive_gravity[2];
        if (gz < K(0x5b23f8)) gz = -gz;
        if (gz < K(0x5b23f0)) {
            /* Rolling in neutral on the level. */
            float f = 0.99f;
            int s = *CAR_I(c, 0xd94);
            if (s < 1) s = -s;
            if (K(0x5b2470) <= af && s < 0x21) f = 0.998f;
            for (int j = 0; j < 3; j++) {
                v[j] *= f;
                w[j] *= f;
            }
            return;
        }
    }
    if (c->b[0xd85] && speed < K(0x5b24a8))
        for (int j = 0; j < 3; j++) v[j] *= 0.5f;
}

void CarSim_Drive(void *car)
{
    Car *c = car;
    if (K(0x5b23f0) <= *CAR_F(c, 0x3a0)) {
        drive(c);
        return;
    }
    /* Over on its side or roof: no grip. */
    *CAR_I(c, 0xdb8) = 0;
    *CAR_I(c, 0xdc0) = 0;
    for (int w = 0; w < 4; w++) *WI(c, w, 0xb8) = 0;
    float *rpm = CAR_F(c, 0xdb4);
    *rpm = *rpm <= K(0x5b24e0) ? 0.0f : *rpm - K(0x5b24e0);
    if (*CAR_F(c, 0x424) < K(0x5b23f0)) {
        uint32_t m = 0x3f733333u;
        float f;
        memcpy(&f, &m, 4);
        for (int j = 0; j < 3; j++) CAR_F(c, 0x33c)[j] *= f;
        if (*CAR_F(c, 0x3a0) < K(0x5b23f4)) {
            *CAR_F(c, 0x38c) *= K(0x5b25f4);
            *CAR_F(c, 0xdc4) *= K(0x5b25f4);
            *CAR_F(c, 0xdc8) *= K(0x5b25f4);
        }
    }
}

void CarSim_ResetDrive(Car *c)
{
    uint8_t *b = c->b;
    bool automatic = DESC_I(c, 0xc) == 1;
    b[0xd7e] = b[0xd82] = automatic ? 2 : 1;   /* first gear (automatic) or neutral */
    *CAR_I(c, 0xd98) = 0;
    b[0xd89] = b[0xd88] = b[0xd8b] = b[0xd8c] = 0;
    b[0xd7c] = b[0xd7d] = 0;
    *CAR_I(c, 0xd90) = 0;
    b[0xd83] = b[0xd84] = b[0xd7f] = b[0xd80] = 0;
    *CAR_I(c, 0xd94) = 0;
    b[0xd81] = 1;
    b[0xd85] = 0;
    for (int w = 0; w < 4; w++) {
        c->surf[w] = c->body;   /* 0x4730f0: the body's tracker */
        int t = 0x7bc + 0xc4 * w;
        memset(b + t + 0x30, 0, 0x30);   /* the point, contact and normal */
        *CAR_F(c, t + 0x4c) = 1.0f;      /* the normal's y */
        memset(b + t + 0x60, 0, 0x1c);
        *CAR_I(c, t + 0x80) = 0;
        *CAR_I(c, t + 0x84) = 0;
        *CAR_I(c, t + 0x88) = 1;
        *CAR_I(c, t + 0xb4) = 0;
    }
    for (int w = 0; w < 4; w++) *WF(c, w, 0xb4) = 0;
    *WI(c, 0, 0xc0) = 1;   /* the front wheels */
    *WI(c, 1, 0xc0) = 1;
    *WI(c, 2, 0xc0) = 0;
    *WI(c, 3, 0xc0) = 0;
    memset(CAR_F(c, 0xac8), 0, 8);
    *CAR_F(c, 0xe08) = *CAR_F(c, 0xe04) = 0;
    memset(CAR_F(c, 0xd34), 0, 0x3c);    /* +0xd34 .. +0xd6c: the torques, forces, local speeds */
    memset(CAR_F(c, 0x103c), 0, 12);
    *CAR_I(c, 0xdb0) = *CAR_I(c, 0xdb0) ? 0x3c : 1;
    *CAR_F(c, 0xdb4) = 0;
    *CAR_I(c, 0xdbc) = 0;
    *CAR_F(c, 0xdc0) = 0;
    *CAR_F(c, 0x54c) = 0;
    *CAR_I(c, 0xdb8) = 0;
    *CAR_I(c, 0xd2c) = 0;
    *CAR_I(c, 0x540) = 0;
    *CAR_F(c, 0xdc4) = *CAR_F(c, 0xdc8) = 0;
    *CAR_I(c, 0xe94) = 0;
    Car_SetBranch(c, -1);
}
