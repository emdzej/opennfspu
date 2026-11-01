/* The cars' contact with the ground (see car_ground.h). */
#include "game/car_ground.h"
#include "game/race_car.h"
#include "game/car_impulse.h"
#include "exe.h"
#include "game/car_physics.h"
#include "game/road.h"
#include "game/sim_math.h"
#include "game/sim_rand.h"
#include "pending.h"
#include <math.h>
#include <string.h>

int32_t g_sim_tick;
int32_t g_race_rally;
int32_t g_race_network;
int32_t g_race_rollover_events;
int32_t g_race_heavy;


static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
#define K(va) exe_f32(va)

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }   /* Vec3_Dot */

static void cross3(const float a[3], const float b[3], float out[3])   /* 0x5325f0 */
{
    float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

static void unit3(const float v[3], float out[3])   /* 0x532640 */
{
    double r = (double)K(0x5b24a8) / sqrt((double)v[1] * v[1] + (double)v[0] * v[0] + (double)v[2] * v[2]);
    out[0] = (float)(r * v[0]);
    out[1] = (float)(v[1] * r);
    out[2] = (float)(v[2] * r);
}

/* Angles in turns (2 pi as the double at 0x5bb2c0). 0x532550, 0x532560 */
static double turns_rad(float t)
{
    double k;
    const uint8_t *p = exe_data(0x5bb2c0, 8);
    if (p) memcpy(&k, p, 8);
    else k = 6.283185307179586;
    return t * k;
}

static const float *car_up(Car *c) { return CAR_F(c, 0x370); }

/* ---- the simulation's task rates ---- */

bool Sim_TaskRate(int salt, int mode, float dist, int id, int *rate, int *ticks, float *dt, bool full)
{
    if (g_sim_tick <= 15) {
        /* The first ticks: level 6 (every tick). */
        *rate = (int32_t)exe_u32(0x5cc718);
        *dt = K(0x5cc750);
        *ticks = (int32_t)exe_u32(0x5cc734);
        return true;
    }
    int d = (int)dist;   /* __ftol: truncation */
    d = (d + (d >> 31 & 0xf)) >> 4;
    if (d > 19) d = 19;
    if (full) d = 0;
    int level = (int8_t)exe_data(0x5cc770 + (uint32_t)(mode * 0x14 + d), 1)[0];
    *rate = (int32_t)exe_u32(0x5cc700 + 4u * (unsigned)level);
    *dt = K(0x5cc738 + 4u * (unsigned)level);
    *ticks = (int32_t)exe_u32(0x5cc71c + 4u * (unsigned)level);
    uint32_t mask = exe_u32(0x5cc754 + 4u * (unsigned)level);
    return ((uint32_t)(salt * id + g_sim_tick / 2) & mask) == mask;
}

/* ---- the wheels ---- */

int CarSim_ContactMode(Car *c, WSurface *s, const float p[3])
{
    if (!(s->surface & 0xf)) return 1;
    const float *v = WSurface_Vertex(s, 0), *n = WSurface_Normal(s);
    float h = ((-((p[0] - v[0]) * n[0]) - (p[2] - v[2]) * n[2]) / n[1] + v[1]) - *CAR_F(c, 0x334);
    if (h < K(0x5b23f8)) h = -h;
    return h <= K(0x5b2404) ? 0 : 2;
}

float CarSim_SurfaceNoise(Car *c, int wheel)
{
    if (*CAR_F(c, 0x3a0) < K(0x5b49d8)) return K(0x5b23f8);
    int w = 0xc4 * wheel;
    uint32_t surf = *(uint32_t *)(void *)(c->b + 0x844 + w) & 0xf;
    float amp = 0.0f, period = K(0x5b23f0);
    if (surf == 3 || surf == 9 || surf == 5) {
        period = K(0x5b2464);
        amp = 0.1f;
    } else if (surf == 2 || surf == 7) {
        period = K(0x5b2468);
        amp = 0.03f;
    } else if (surf == 4 || surf == 10) {
        period = K(0x5b23f0);
        amp = 0.03f;
    }
    float *from = CAR_F(c, 0x82c + w), *to = CAR_F(c, 0x830 + w), *t = CAR_F(c, 0x834 + w);
    double nt = (double)*CAR_F(c, 0x35c) * K(0x5b24b8) + *t;
    *t = (float)nt;
    if ((period < nt && K(0x5b23f8) < period) || amp < K(0x5b27a8)) {
        /* The next bump: a random height in [-amp / 2 ... amp / 6] (the generator's 16 bits). */
        *t = 0;
        *from = *to;
        uint32_t r = SimRand_Next();
        *to = amp * K(0x5b49d4) - (float)r * amp * K(0x5b253c);
    }
    return (float)(((double)*t / period) * ((double)*to - *from) + *from);
}

float CarSim_TyreBump(Car *c)
{
    double sum = K(0x5b23f8);
    int n = 0;
    for (int i = 0; i < CAR_NPARTS; i++) {
        const uint8_t *part = c->p[CARP_PARTS + i];
        if (!part) continue;
        int16_t type;
        memcpy(&type, part + 0x26, 2);
        if (type != 4 && type != 5 && type != 8) continue;
        n++;
        /* The mean of the damage zones the part covers (its ten flags at +0x48, the car's zones from
           +0x4e4). */
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
        sum = z * ((double)K(0x5b246c) - (double)a * K(0x5b3284)) + b + sum;
    }
    if (n > 1) sum /= n;
    return (float)sum;
}

/* The ride heights: of the four corner points the lowest and the mean (most cars), of all the body
   points the lowest and the mean of the three lowest (upside down, traffic). 0x49a870 */
static void ride_heights(Car *c, const float n[3], const float at[3])
{
    float low[4] = {1000.0f, 1000.0f, 1000.0f, 1000.0f};
    bool upright = K(0x5b4a0c) < *CAR_F(c, 0x3a0) && !(*CAR_I(c, CAR_FLAGS) & 0x80);
    int count = upright ? 4 : *CAR_I(c, 0x32c);
    for (int i = 0; i < count; i++) {
        const float *p = CAR_F(c, 0x1dc + 12 * i);
        float d[3] = {p[0] - at[0], p[1] - at[1], p[2] - at[2]};
        float h = dot3(n, d);
        /* Into the four lowest (an insertion that drops the highest). */
        if (!(low[0] < h)) {
            low[3] = low[2];
            low[2] = low[1];
            low[1] = low[0];
            low[0] = h;
        } else if (!(low[1] < h)) {
            low[3] = low[2];
            low[2] = low[1];
            low[1] = h;
        } else if (!(low[2] < h)) {
            low[3] = low[2];
            low[2] = h;
        } else if (h <= low[3]) {
            low[3] = h;
        }
    }
    if (upright) {
        float v = ((low[3] + low[2] + low[1] + low[0]) * K(0x5b2468) + low[0]) * K(0x5b23f0);
        *CAR_F(c, 0x428) = v;
        *CAR_F(c, 0x424) = v;
        return;
    }
    *CAR_F(c, 0x424) = low[0];
    *CAR_F(c, 0x428) = (low[2] + low[1] + low[0]) * K(0x5b2680);
}

/* The local angular velocity held within +-0.75. 0x49bbf0 */
static void clamp_spin(Car *c)
{
    for (int k = 0; k < 3; k++) {
        float *v = CAR_F(c, 0xd64 + 4 * k);
        if (K(0x5b260c) < *v) *v = 0.75f;
        else if (*v < K(0x5b3f68)) *v = -0.75f;
    }
}

float CarSim_Ground(Car *c, float out[3])
{
    const uint8_t *specs = CAR_SPECSB(c);
    float ride, travel;
    memcpy(&ride, specs + 0x120, 4);
    memcpy(&travel, specs + 0x124, 4);
    float scale[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float pts[4][3], contact[4][3], sum[3] = {0, 0, 0};
    int airborne = 0;
    for (int i = 0; i < 4; i++) {
        const float *b = CAR_F(c, 0x1dc + 12 * i);
        pts[i][0] = b[0];
        pts[i][1] = ride * *CAR_F(c, 0x374) + b[1];
        pts[i][2] = b[2];
    }
    for (int i = 0; i < 4; i++) {
        int t = 0x7bc + 0xc4 * i;   /* the wheel's tracker */
        WSurface *s = &c->surf[i];
        memcpy(contact[i], pts[i], sizeof pts[i]);
        memcpy(CAR_F(c, t + 0x30), pts[i], sizeof pts[i]);
        WSurface_Update(s, pts[i], true);   /* 0x473fc0 */
        const float *sn = WSurface_Normal(s);
        float n[3] = {sn[0], sn[1], sn[2]};
        memcpy(c->b + t + 0x24, &s->surface, 2);
        *CAR_I(c, t + 0x88) = s->surface;
        int kind = s->surface & 0xf;
        if (g_race_rally && (kind == 1 || kind == 10)) *CAR_I(c, t + 0x88) = 2;
        if (CarSim_ContactMode(c, s, contact[i]) == 0) {   /* 0x4943f0 */
            const float *v = WSurface_Vertex(s, 0);
            contact[i][1] = (-((pts[i][0] - v[0]) * n[0]) - (pts[i][2] - v[2]) * n[2]) / n[1] + v[1];
            contact[i][1] = CarSim_SurfaceNoise(c, i) + contact[i][1];
            if (i < 2) {
                /* The front wheels' tyre bumps. */
                double a = CarSim_TyreBump(c);
                double ph = sin(((double)i * K(0x5b23f0) + *CAR_F(c, t + 0x84)) * K(0x5b4a08));
                contact[i][1] = (float)(ph * a * K(0x5b4a00) + contact[i][1]);
            }
        } else {
            /* Off the polygons: the car's own ground plane (point +0x400, normal +0x3e8), level ground
               when it has none. */
            if (*CAR_F(c, 0x3ec) <= K(0x5b2604)) {
                n[0] = K(0x5b23f8);
                n[1] = 1.0f;
                n[2] = 0.0f;
            } else {
                n[0] = *CAR_F(c, 0x3e8);
                n[1] = *CAR_F(c, 0x3ec);
                n[2] = *CAR_F(c, 0x3f0);
            }
            contact[i][1] = (-((pts[i][0] - *CAR_F(c, 0x400)) * n[0]) - (pts[i][2] - *CAR_F(c, 0x408)) * n[2]) / n[1] +
                            *CAR_F(c, 0x404);
        }
        sum[0] += contact[i][0];
        sum[1] += contact[i][1];
        sum[2] += contact[i][2];
        memcpy(CAR_F(c, t + 0x3c), contact[i], sizeof contact[i]);
        memcpy(CAR_F(c, t + 0x48), n, sizeof n);
        if (g_sim_tick < 2 || (g_race_network && *CAR_I(c, 0xdac))) {
            *CAR_F(c, t + 0x6c) = 0;
            *CAR_F(c, t + 0x64) = 0;
            continue;
        }
        /* The suspension's travel at the wheel (along the car's up), held to its range. */
        const float *at = CAR_F(c, t + 0x30);
        float d[3] = {contact[i][0] - at[0], contact[i][1] - at[1], contact[i][2] - at[2]};
        float x = dot3(d, car_up(c));
        if (*CAR_F(c, 0x374) < K(0x5b25f0)) x = K(0x5b2444);
        float *comp = CAR_F(c, t + 0x64);
        if (x <= K(0x5b23f8)) {
            float lim = travel * (*CAR_I(c, t + 0x80) ? K(0x5b49fc) : K(0x5b49f8));
            if (x <= lim) x = lim;
            *comp = (x + *comp) * K(0x5b23f0);
        } else {
            float q = (travel - ride) * (i < 2 ? K(0x5b25e8) : K(0x5b260c));
            if (q + q < x) scale[i] = 1.25f;
            if (q < x) x = q;
            *comp = x;
        }
        /* The wheel's camber from the ride height and the compression. */
        float cam = ride * K(0x5b49b4);
        if (x > K(0x5b23f8)) cam = *comp * K(0x5b49b4) + cam;
        float ang = (float)asin(cam);
        *CAR_F(c, t + 0x6c) = i == 0 || i == 2 ? ang : -ang;
    }
    float mean[3] = {sum[0] * K(0x5b2468), sum[1] * K(0x5b2468), sum[2] * K(0x5b2468)};

    /* The springs: each wheel's push along the car's axes by how far its contact is above its point. */
    float extra = K(0x5b23f8);
    for (int i = 0; i < 4; i++) {
        int t = 0x7bc + 0xc4 * i;
        extra = K(0x5b23f8);
        float x = *CAR_F(c, t + 0x40) - pts[i][1];
        *CAR_F(c, t + 0x60) = x;
        if (x < K(0x5b49f4)) x = K(0x5b49f4);
        float f[3];
        if (*CAR_F(c, 0x3a0) <= K(0x5b23f8)) {
            f[0] = -(x * *CAR_F(c, 0x368));
            f[1] = -(x * *CAR_F(c, 0x374));
            f[2] = -(x * *CAR_F(c, 0x380));
        } else {
            f[0] = x * *CAR_F(c, 0x368);
            f[1] = x * *CAR_F(c, 0x374);
            f[2] = x * *CAR_F(c, 0x380);
        }
        if (K(0x5b49f0) <= x) {
            *CAR_F(c, t + 0x68) = f[1] / *CAR_F(c, 0x374);
            const float *v = CAR_F(c, 0x33c);
            float vx = fabsf(v[0]), vz = fabsf(v[2]);
            if (K(0x5b27a8) < vx && K(0x5b27a8) < vz) {
                /* Sideways grip on slopes: against the ground velocity, in the contact's plane. */
                float lat[3] = {-v[0], 0, -v[2]}, tmp[3];
                unit3(lat, lat);
                cross3(CAR_F(c, t + 0x48), lat, tmp);
                cross3(tmp, CAR_F(c, t + 0x48), lat);
                float k;
                if (*CAR_F(c, t + 0x80) == 0.0f) k = fabsf(dot3(CAR_F(c, 0xd40), CAR_F(c, 0x364))) * K(0x5b26a4);
                else k = fabsf(dot3(v, CAR_F(c, 0x364))) * K(0x5b49ec);
                for (int j = 0; j < 3; j++) lat[j] *= k;
                extra = dot3(lat, car_up(c));
            }
            *CAR_F(c, t + 0x80) = 0;
        } else {
            *CAR_I(c, t + 0x80) = 1;
            airborne++;
            if (!DESC_I(c, 0x518)) f[1] *= K(0x5b3f00);
            *CAR_F(c, t + 0x68) = f[1];
        }
        *CAR_F(c, t + 0x54) = f[0] * scale[i];
        *CAR_F(c, t + 0x58) = (float)((double)f[1] * scale[i] + extra);
        *CAR_F(c, t + 0x5c) = f[2] * scale[i];
    }

    /* The ground's normal from the contacts' diagonals (front minus rear, left minus right). */
    float e[3], w[3];
    for (int k = 0; k < 3; k++) {
        e[k] = ((contact[1][k] + contact[0][k]) - (contact[3][k] + contact[2][k])) * K(0x5b23f0);
        w[k] = ((contact[3][k] + contact[1][k]) - (contact[2][k] + contact[0][k])) * K(0x5b23f0);
    }
    float z = K(0x5b23f8);
    if ((w[0] == z && w[1] == z && w[2] == z) || (e[0] == z && e[1] == z && e[2] == z)) {
        out[0] = 0.0f;
        out[1] = 1.0f;
        out[2] = 0.0f;
    } else {
        float g[3];
        unit3(e, e);
        unit3(w, w);
        cross3(e, w, g);
        unit3(g, g);
        if (g[1] < z) {
            out[0] = -g[0];
            out[1] = -g[1];
            out[2] = -g[2];
        } else {
            memcpy(out, g, sizeof g);
        }
        if (g[1] < K(0x5b23f0)) airborne = 4;
    }

    if (*CAR_F(c, 0x3a0) < K(0x5b2698)) {
        *CAR_F(c, 0xdc8) *= K(0x5b23f0);
        *CAR_F(c, 0xdc4) *= K(0x5b23f0);
    } else if (airborne <= 3) {
        /* The body's pitch and roll from the springs' pushes. */
        const uint8_t *sp = specs;
        float s114, s118, s128, s12c, s134, s138;
        memcpy(&s114, sp + 0x114, 4);
        memcpy(&s118, sp + 0x118, 4);
        memcpy(&s128, sp + 0x128, 4);
        memcpy(&s12c, sp + 0x12c, 4);
        memcpy(&s134, sp + 0x134, 4);
        memcpy(&s138, sp + 0x138, 4);
        bool tiny = DESC_I(c, 0x518) != 0;
        float div = tiny ? 4.0f : 8.0f;
        const float *av = CAR_F(c, 0x388);
        float *loc = CAR_F(c, 0xd64);
        loc[0] = dot3(av, CAR_F(c, 0x364));
        loc[1] = dot3(av, CAR_F(c, 0x370));
        float lz = dot3(av, CAR_F(c, 0x37c));
        loc[2] = lz;
        uint32_t flags = (uint32_t)*CAR_I(c, CAR_FLAGS);
        float d54 = *CAR_F(c, 0xd54), d4c = *CAR_F(c, 0xd4c);
        float *dc4 = CAR_F(c, 0xdc4), *dc8 = CAR_F(c, 0xdc8);
        if (*CAR_I(c, 0xdac)) {
            *dc4 = 0;
        } else if (flags & 0x20) {
            float v = d54;
            if (v > K(0x5b241c)) v = K(0x5b241c);
            else if (v < K(0x5b49e8)) v = K(0x5b49e8);
            *dc4 = *dc4 * K(0x5b24e8) + v * s128 * K(0x5b272c);
        } else if (!tiny) {
            float a = s128 * d54 * (d54 <= K(0x5b23f8) ? K(0x5b49e4) : K(0x5b272c));
            *dc4 = *dc4 * K(0x5b24e8) + a;
        } else {
            *dc4 = d54 * s128;
        }
        float w0 = *CAR_F(c, 0x814), w1 = *CAR_F(c, 0x8d8), w2 = *CAR_F(c, 0x99c), w3 = *CAR_F(c, 0xa60);
        float pitch = ((w2 + w3) - (w0 + w1)) / div - *dc4;
        if (pitch <= K(0x5b23f8)) {
            if (pitch <= -s134) pitch = -s134;
        } else if (s134 < pitch) {
            pitch = s134;
        }
        if (*CAR_I(c, 0xdac)) {
            *dc8 = 0;
        } else if (flags & 0x20) {
            *dc8 = *dc8 * K(0x5b24e8) + s12c * d4c * K(0x5b2638);
        } else if (!tiny) {
            *dc8 = *dc8 * K(0x5b24e8) + s12c * d4c * K(0x5b2468);
        } else {
            *dc8 = s12c * d4c;
        }
        double roll = ((double)K(0x5b24a8) - (double)c->b[0xd7f] * K(0x5b49e0)) * *dc8 + ((double)(w3 + w1) - (double)(w2 + w0)) / div;
        if (roll <= K(0x5b23f8)) {
            if (roll <= -s138) roll = -s138;
        } else if (s138 < roll) {
            roll = s138;
        }
        loc[0] = s114 * loc[0] * K(0x5b49dc) + pitch;
        loc[2] = (float)((double)lz * s118 + roll);
        clamp_spin(c);   /* 0x49bbf0 */
        float rt[9];
        Mat3_Transpose(CAR_F(c, 0x364), rt);
        float *wav = CAR_F(c, 0x388);
        wav[0] = dot3(loc, rt);
        wav[1] = dot3(loc, rt + 3);
        wav[2] = dot3(loc, rt + 6);
        if (airborne > 0) {
            bool left = *CAR_I(c, 0x83c) || *CAR_I(c, 0x9c4), right = *CAR_I(c, 0x900) || *CAR_I(c, 0xa88);
            if (!left || !right) {
                /* A car on two wheels: pushed back over towards its wheels. */
                float b[3];
                b[0] = (float)(sin(turns_rad(loc[2])) * *CAR_F(c, 0x3a8) * K(0x5b2628));
                b[1] = (float)(cos(turns_rad(loc[2])) * *CAR_F(c, 0x3ac) * K(0x5b2628));
                b[2] = 0.0f;
                if ((left && loc[2] < K(0x5b23f8)) || (right && K(0x5b23f8) < loc[2])) b[1] = -b[1];
                float d[3] = {dot3(b, rt), dot3(b, rt + 3), dot3(b, rt + 6)};
                float *pos = CAR_F(c, 0x330);
                for (int k = 0; k < 3; k++) pos[k] = d[k] * K(0x5b24b8) + pos[k];
            }
        }
    }
    int16_t lifted = airborne < 2 ? 0 : 1;   /* two or more wheels off the ground */
    memcpy(c->b + 0x418, &lifted, 2);
    CarSim_BodyPoints(c);              /* 0x49ada0 */
    ride_heights(c, out, mean);        /* 0x49a870 */
    return mean[1];
}

/* ---- the force task ---- */

/* The car's frame on the ground (+0x490 side, +0x49c the ground's normal, +0x4a8 forward): the car's own
   rows while it is level enough (and not a human's car), else made from the normal. 0x49b9a0 */
static void ground_frame(Car *c, const float n[3], float up_dot)
{
    float *side = CAR_F(c, 0x490), *nrm = CAR_F(c, 0x49c), *fwd = CAR_F(c, 0x4a8);
    if (!(*CAR_I(c, CAR_FLAGS) & 1) && K(0x5b25e8) < up_dot) {
        memcpy(side, CAR_F(c, 0x364), 12);
        memcpy(nrm, n, 12);
        memcpy(fwd, CAR_F(c, 0x37c), 12);
        return;
    }
    memcpy(nrm, n, 12);
    if (up_dot < K(0x5b23f0) && K(0x5b23f0) < fabsf(dot3(CAR_F(c, 0x364), n))) {
        /* On its side: forward from the normal and the car's forward. */
        cross3(nrm, CAR_F(c, 0x37c), side);
        unit3(side, side);
        cross3(side, nrm, fwd);
        unit3(fwd, fwd);
        return;
    }
    cross3(CAR_F(c, 0x364), nrm, fwd);
    unit3(fwd, fwd);
    cross3(nrm, fwd, side);
    unit3(side, side);
}

/* The wheels' compressions moved with the body by dy, held to the suspension's range. 0x49ae30 */
static void shift_compression(Car *c, float dy)
{
    if (*CAR_I(c, CAR_FLAGS) & 0x80) return;
    const uint8_t *sp = CAR_SPECSB(c);
    float travel, ride;
    memcpy(&travel, sp + 0x124, 4);
    memcpy(&ride, sp + 0x120, 4);
    for (int i = 0; i < 4; i++) {
        float *comp = CAR_F(c, 0x820 + 0xc4 * i);
        float v = -(dy * *CAR_F(c, 0x374)) + *comp;
        if (v <= K(0x5b23f8)) {
            if (v <= -travel) v = -travel;
        } else {
            float q = (travel - ride) * (i < 2 ? K(0x5b25e8) : K(0x5b260c));
            if (q < v) v = q;
        }
        *comp = v;
    }
}

/* The angular velocity held within 0.75 turns a second (2.25 for the small cars, 6.75 for slow
   traffic). 0x49bb10 */
static void clamp_turn(Car *c)
{
    float lim = K(0x5b49c0);
    if (!DESC_I(c, 0x518)) {
        lim = K(0x5b260c);
        if ((*CAR_I(c, CAR_FLAGS) & 0x80) && *CAR_F(c, 0x354) < K(0x5b260c)) lim = K(0x5b49bc);
    }
    for (int k = 0; k < 3; k++) {
        float *v = CAR_F(c, 0x388 + 4 * k);
        if (lim < *v) *v = lim;
        else if (*v < -lim) *v = -lim;
    }
}

/* The surface table (0x5d0b90, 8 bytes per surface kind): the friction and the bounce. */
static float surface_friction(int kind) { return K(0x5d0b94 + 8u * (unsigned)(kind & 0xf)); }
static float surface_bounce(int kind) { return K(0x5d0b98 + 8u * (unsigned)(kind & 0xf)); }

void CarSim_Forces(void *car)
{
    Car *c = car;
    if (!c->b[0x7f] || !c->b[0x7c] || c->b[0x7d]) return;
    int rate, ticks;
    float dt;
    if (!Sim_TaskRate(1, 3, *CAR_F(c, 0x78), *CAR_I(c, 0), &rate, &ticks, &dt, *CAR_I(c, 0x10b8) != 0)) return;
    uint32_t flags = (uint32_t)*CAR_I(c, CAR_FLAGS);
    float n[3];
    float h;
    if (flags & 0x80) {
        PENDING_NOTE(0x499830, "the traffic cars' ground contact");
        n[0] = 0;
        n[1] = 1;
        n[2] = 0;
        h = *CAR_F(c, 0x41c);
    } else {
        h = CarSim_Ground(c, n);   /* 0x4999e0 */
    }
    float lowest = *CAR_F(c, 0x424);
    float ground_vy = (h - *CAR_F(c, 0x41c)) * (float)rate;
    *CAR_F(c, 0x41c) = h;
    *CAR_F(c, 0x39c) = dot3(CAR_F(c, 0x364), n);
    *CAR_F(c, 0x3a0) = dot3(CAR_F(c, 0x370), n);
    *CAR_F(c, 0x3a4) = dot3(CAR_F(c, 0x37c), n);
    ground_frame(c, n, *CAR_F(c, 0x3a0));
    /* Gravity, once the car is off the ground (or always for the small cars). */
    float g = K(0x5b23f8);
    if (K(0x5b2738) < *CAR_F(c, 0x428) || DESC_I(c, 0x518)) g = (float)ticks * K(0x5b4a20) * *CAR_F(c, 0x84);
    float *vel = CAR_F(c, 0x33c);
    float vy = vel[1] - g;
    vel[1] = vy;
    float ax = fabsf(vel[0]), az = fabsf(vel[2]);
    *CAR_F(c, 0x35c) = ax <= az ? ax * K(0x5b2468) + az : az * K(0x5b2468) + ax;
    float old = *CAR_F(c, 0x420);
    *CAR_F(c, 0x420) = ground_vy;
    *CAR_F(c, 0x40c) = (*CAR_F(c, 0x40c) * K(0x5b3090) + (ground_vy - old)) * K(0x5b272c);
    float rel = ground_vy - vy;
    int kind = c->b[0x2c] & 0xf;   /* the body tracker's surface */
    if ((K(0x5b4a0c) <= *CAR_F(c, 0x3a0) && !(flags & 0x80)) || K(0x5b27b4) <= lowest) {
        if (rel <= K(0x5b2460)) {
            if (lowest < K(0x5b2738)) {
                /* On the ground: the body lifted out of it, falling stopped at the ground's speed. */
                *CAR_F(c, 0x424) = 0;
                *CAR_F(c, 0x334) -= lowest;
                if (!(flags & 0x400) && K(0x5b49d8) < *CAR_F(c, 0x3a0)) memset(c->b + 0x418, 0, 2);
                if (vy <= K(0x5b23f8) || ground_vy <= K(0x5b23f8)) vel[1] = ground_vy;
                else vel[1] = vy < ground_vy ? ground_vy : vy;
                shift_compression(c, -lowest);
            }
        } else if (lowest < K(0x5b4a18)) {
            /* A hard landing: out of the ground and bounced by the surface. */
            if (flags & 1) PENDING_NOTE(0x493d40, "the human car's landing (0x493d40)");
            Car_AddEvent(c, 1, -1, 0, 5, rel, 0);   /* 0x40f080: the landing's sound */
            float pen = K(0x5b4a18) - lowest;
            uint32_t m = 0xbd4ccccdu;
            memcpy(CAR_F(c, 0x424), &m, 4);
            *CAR_F(c, 0x334) = pen + *CAR_F(c, 0x334);
            shift_compression(c, pen);
            if (!(flags & 0x400) && K(0x5b25e8) < *CAR_F(c, 0x3a0)) memset(c->b + 0x418, 0, 2);
            float b = surface_bounce(kind) * K(0x5b261c);
            *CAR_F(c, 0xad0) = rel;
            vel[1] = b * rel + ground_vy;
            clamp_turn(c);
        }
    } else {
        /* Tilted over (or traffic): the body's points against the ground. */
        float fr = surface_friction(kind);
        if (K(0x5b2698) < *CAR_F(c, 0x3a0) && !(flags & 0x80)) fr = 0.1f;
        CarSim_BodyImpulse(c, WSurface_Normal(&c->body), CAR_F(c, 0x400), fr);   /* 0x496580 */
        if (*CAR_F(c, 0x3a0) < K(0x5b23f8)) *CAR_I(c, 0xdb0) = 0;
        if (*CAR_F(c, 0x3a0) < K(0x5b2698) && vy < K(0x5b2628)) {
            vel[1] = vel[1] - surface_bounce(kind) * vy;
            vel[0] *= K(0x5b2610);
            vel[2] *= K(0x5b2610);
            float *av = CAR_F(c, 0x388);
            uint32_t s = 0x3f733333u;
            float k;
            memcpy(&k, &s, 4);
            for (int j = 0; j < 3; j++) av[j] *= k;
        }
        if (g_race_rollover_events && *CAR_F(c, 0x3a0) < K(0x5b4a1c) && *CAR_F(c, 0x504) < K(0x5b2410) && *CAR_I(c, 0xb50) == 0) {
            Car_AddEvent(c, 1, -1, 4, 3, 40.0f, 0);   /* 0x40f080: the rollover's */
            *CAR_F(c, 0x504) = 40.0f;
        }
    }
    *CAR_I(c, 0x88) = g_sim_tick;
    /* Under a roof, going up: bounced down. */
    if (WSurface_FindAbove(&c->body, CAR_F(c, 0x330), NULL) && K(0x5b23f8) < vel[1] &&
        K(0x5b2608) < *CAR_F(c, 0x334) - *CAR_F(c, 0x404))
        vel[1] = -vel[1];
    if (vel[0] != K(0x5b23f8) || vel[1] != K(0x5b23f8) || vel[2] != K(0x5b23f8)) unit3(vel, CAR_F(c, 0x348));
    else memcpy(CAR_F(c, 0x348), CAR_F(c, 0x37c), 12);
}

/* ---- the road tracking task ---- */

/* The distance to the nearest human player's car (+0x78, by the 0.25-weighted ground-plane distance;
   1000 at most; 0 for a human's own car): what the task rates go by. 0x4171b0 */
static void player_distance(Car *c)
{
    c->b[0x7e] = 0;
    if (*CAR_I(c, CAR_FLAGS) & 1) {
        *CAR_F(c, 0x78) = 0;
        return;
    }
    float best = 1000.0f;
    for (int i = 0; i < g_ncars; i++) {
        Car *o = g_cars[i];
        if (!o || !(*CAR_I(o, CAR_FLAGS) & 1)) continue;   /* the human players' list (0x5e8e2c) */
        float dx = fabsf(CAR_F(c, 0x330)[0] - CAR_F(o, 0x330)[0]), dz = fabsf(CAR_F(c, 0x330)[2] - CAR_F(o, 0x330)[2]);
        float d = dx <= dz ? dx * K(0x5b2468) + dz : dz * K(0x5b2468) + dx;
        if (d < best) best = d;
    }
    *CAR_F(c, 0x78) = best;
}

void WSurface_TrackNode(WSurface *s, const float p[3])
{
    int16_t old = s->node;
    if (s->f4 >= 0) PENDING_NOTE(0x481560, "the road node by the junction's two segments");
    if (g_road && g_road->nnodes) s->node = (int16_t)Road_NearestNode(g_road, p, s->node);
    s->changed = s->node != old;
    s->node_changed = s->node != old;
}

static void track_node(Car *c) { WSurface_TrackNode(&c->body, CAR_F(c, 0x330)); }

static const float *node_vec(int node, int off) { return (const float *)(const void *)(g_road->nodes + node * ROAD_NODE_SIZE + off); }

/* The car's own ground plane from the body's polygon (+0x400 its vertices' mean, +0x3e8 its normal,
   +0x3f4 its slope, +0x3dc their cross) and the road's axes at the node (+0x3b8, +0x3c4, +0x3d0).
   0x499490 */
static void body_plane(Car *c)
{
    if (!c->b[0x7f]) return;
    float *at = CAR_F(c, 0x400), old[3];
    memcpy(old, at, sizeof old);
    memset(at, 0, 12);
    WSurface *s = &c->body;
    if (!s->none) {
        for (int i = 0; i < 4; i++) {
            const float *v = WSurface_Vertex(s, i);
            at[0] = v[0] + at[0];
            at[1] = v[1] + at[1];
            at[2] = v[2] + at[2];
        }
        for (int k = 0; k < 3; k++) at[k] *= K(0x5b2468);
    }
    if (at[0] == K(0x5b23f8) && at[1] == K(0x5b23f8) && at[2] == K(0x5b23f8)) {
        memcpy(at, old, sizeof old);
        return;
    }
    if (!s->changed) return;
    memcpy(CAR_F(c, 0x3e8), WSurface_Normal(s), 12);
    memcpy(CAR_F(c, 0x3f4), WSurface_Slope(s), 12);
    cross3(CAR_F(c, 0x3e8), CAR_F(c, 0x3f4), CAR_F(c, 0x3dc));
    if (!g_road || !g_road->nnodes) return;
    memcpy(CAR_F(c, 0x3b8), node_vec(s->node, 0x28), 12);
    memcpy(CAR_F(c, 0x3c4), node_vec(s->node, 0x10), 12);
    memcpy(CAR_F(c, 0x3d0), node_vec(s->node, 0x1c), 12);
}

void CarSim_Track(void *car)
{
    Car *c = car;
    if (!c->b[0x7f] || c->b[0x7d] || !c->b[0x7c]) return;
    player_distance(c);                         /* 0x4171b0 */
    track_node(c);                              /* 0x4743a0 */
    WSurface_Update(&c->body, CAR_F(c, 0x330), true);   /* 0x473fc0 (its +9 replaces the node's) */
    /* The tracker's bytes in the car (+0x8 node, +0x11 changed, +0x12 none, +0x2c surface). */
    memcpy(c->b + 0x8, &c->body.node, 2);
    c->b[0x11] = c->body.changed;
    c->b[0x12] = c->body.none;
    memcpy(c->b + 0x2c, &c->body.surface, 2);
    if (c->body.changed) body_plane(c);         /* 0x499490 */
    *CAR_I(c, 0x460) = c->body.surface;
    int kind = c->body.surface & 0xf;
    *CAR_I(c, 0x464) = kind;
    if (g_race_rally && (kind == 1 || kind == 10)) *CAR_I(c, 0x464) = 2;
    if (!g_road || !g_road->nnodes) return;
    /* The heading against the road: the angle of the car's forward to the node's side axis. */
    float a = (float)asin(dot3(CAR_F(c, 0x37c), node_vec(c->body.node, 0x28)));
    *CAR_F(c, 0x414) = a;
    if (dot3(CAR_F(c, 0x37c), node_vec(c->body.node, 0x1c)) < K(0x5b23f8))
        *CAR_F(c, 0x414) = (K(0x5b23f8) < a ? K(0x5b4a14) : K(0x5b4a10)) - a;
}
