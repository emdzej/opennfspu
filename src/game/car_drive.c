/* The cars' drive (see car_drive.h). */
#include "game/car_drive.h"
#include "exe.h"
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
                int lim = g_race_wet || DESC_I(c, 0x518) ? 0x12 : 0xc;
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
    double z = K(0x5b23f8);
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
    return z * ((double)K(0x5b246c) - (double)a * K(0x5b3284)) + b;
}

static int16_t part_type(const uint8_t *part)
{
    int16_t t;
    memcpy(&t, part + 0x26, 2);
    return t;
}

float CarSim_EngineWear(Car *c)
{
    double sum = *CAR_F(c, 0x4e8);
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
    double sum = K(0x5b23f8);
    for (int i = 0; i < CAR_NPARTS; i++) {
        const uint8_t *part = c->p[CARP_PARTS + i];
        if (part && part_type(part) == type) sum = part_wear(c, part) + sum;
    }
    return (float)sum;
}

float CarSim_BrakeWear(Car *c) { return wear_sum(c, 2); }        /* 0x493980 */
float CarSim_GearboxWear(Car *c) { return wear_sum(c, 3); }      /* 0x493a10 */

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

/* The drag left by the cars ahead (drafting): 1 less the best slipstream of a race car within a third
   of this car's speed (in metres) straight ahead. 0x49d7a0 */
float CarSim_DraftFactor(Car *c)
{
    float best = 0.0f, speed = *CAR_F(c, 0xd60);
    for (int i = 0; i < g_ncars; i++) {
        Car *o = g_cars[i];
        if (!o || *CAR_I(o, 0) == *CAR_I(c, 0) || !(K(0x5b243c) < speed)) continue;
        float d[3] = {CAR_F(o, 0x330)[0] - CAR_F(c, 0x330)[0], CAR_F(o, 0x330)[1] - CAR_F(c, 0x330)[1],
                      CAR_F(o, 0x330)[2] - CAR_F(c, 0x330)[2]};
        float dist = (float)sqrt((double)d[1] * d[1] + (double)d[0] * d[0] + (double)d[2] * d[2]);
        if (!(dist < speed * K(0x5b2680) && K(0x5b2604) < dist)) continue;
        double r = (double)K(0x5b24a8) / dist;
        float u[3] = {(float)(r * d[0]), (float)(d[1] * r), (float)(d[2] * r)};
        double a = (double)u[0] * CAR_F(c, 0x37c)[0] + (double)u[1] * CAR_F(c, 0x37c)[1] + (double)u[2] * CAR_F(c, 0x37c)[2];
        if (!(K(0x5b4a48) < a)) continue;
        double f = (a - K(0x5b4a48)) * K(0x5b4a44) * K(0x5b23f0) * ((double)K(0x5b24a8) - ((double)dist * K(0x5b2600)) / speed);
        if (best <= f) best = (float)f;
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
static double torque_at(Car *c, float rpm)
{
    float redline = spec_f(c, 0x94);
    if (redline <= rpm) rpm = redline;
    if (!(K(0x5b23f8) < rpm)) rpm = K(0x5b23f8);
    int i = (int)(rpm * K(0x5b4a30));
    double a = spec_f(c, 0x9c + 4 * i), b = spec_f(c, 0xa0 + 4 * i);
    return (b - a) * ((double)rpm - (double)(i * 500)) * K(0x5b4a30) + a;
}

float CarSim_Engine(Car *c)
{
    uint8_t *b = c->b;
    uint32_t r = SimRand_Next();
    float redline = spec_f(c, 0x94), jump = redline * K(0x5b272c);
    double noise = (double)(r & 0xffff) * K(0x5b253c);
    double wear = (double)CarSim_EngineWear(c) * K(0x5b4a64);
    float fwd = *CAR_F(c, 0xd60);
    float drag = (float)(CarSim_DraftFactor(c) * (double)(fwd * spec_f(c, 0x1b0) * fwd * fwd));   /* 0x49d7a0 */
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
    double torque = torque_at(c, *rpm) * spec_f(c, 0x190 + 4 * gear);
    torque = ((double)K(0x5b24a8) - (double)CarSim_GearboxWear(c) * K(0x5b3284)) *
             (double)(float)(((double)K(0x5b24a8) - (double)CarSim_EngineWear(c) * K(0x5b4a64)) * (double)(float)torque);
    float aw = wheel < K(0x5b23f8) ? -wheel : wheel;
    float slip = target - aw;
    float as = slip < K(0x5b23f8) ? -slip : slip;
    if (as < K(0x5b4a58) && target <= redline - K(0x5b2524)) slip = 0.0f;
    float d = *rpm - aw;
    if ((K(0x5b4a60) < d && b[0xd84] == 0 && gear != 1) || (gear >= 2 && fwd < K(0x5b2444)) ||
        (gear == 0 && K(0x5b2604) < fwd) || (*CAR_I(c, 0xdb8) == 1 && *cut == 0)) {
        /* The clutch slipping: the engine pulled down to the wheels by what they can take. */
        double f = torque * K(0x5b243c);
        if (f < K(0x5b23f8)) f = -f;
        if (spec_f(c, 0x5c) <= K(0x5b23f8))
            f = (double)K(0x5b242c) - (f - ((double)*CAR_F(c, 0xab0) + *CAR_F(c, 0x9ec)));
        else
            f = (double)K(0x5b3d60) - (f - ((double)*CAR_F(c, 0xab0) + *CAR_F(c, 0x9ec) + *CAR_F(c, 0x928) + *CAR_F(c, 0x864)));
        double tp = (double)b[0xd7c] * K(0x5b4a54);
        if (tp < K(0x5b25f0)) tp = K(0x5b25f0);
        float lim = (float)(((double)K(0x5b24a8) - g_drive_throttle) * K(0x5b242c) + f / tp);
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
            if (g_race_wet) {
                double k = *CAR_F(c, 0xdc0);
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
            double g = (double)CarSim_GearboxWear(c) * K(0x5b3714) + K(0x5b24a8);
            if (K(0x5b2400) <= g) g = K(0x5b2400);
            torque = g * (double)(float)-(torque * spec_f(c, 0x98));
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
        return (float)(torque - drag);
    }
    float v = -(fwd * K(0x5b2644));
    if ((torque <= K(0x5b23f8) || v <= K(0x5b23f8) || torque - v <= K(0x5b23f8)) &&
        (K(0x5b23f8) <= torque || K(0x5b23f8) <= v || K(0x5b23f8) <= torque - v)) {
        *rpm = 0;
        return v - drag;
    }
    return (float)(torque - drag);
}
