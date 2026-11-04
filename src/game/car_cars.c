/* The cars against each other (see car_cars.h). */
#include "game/car_cars.h"
#include "exe.h"
#include "game/car_collide.h"
#include "game/car_damage.h"
#include "game/sim_math.h"
#include <math.h>
#include <string.h>

#define K(va) exe_f32(va)

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float len3(const float v[3]) { return sqrtf(dot3(v, v)); }   /* 0x532890 */
static void unit3(float v[3])
{
    float l = len3(v);
    if (l != 0) v[0] /= l, v[1] /= l, v[2] /= l;
}

/* The pair's shared state (0x628c8c .. 0x628f54): the cars, the products of their axes, each scaled by
   the other's half sizes, their centres' offset in each other's axes, the point tested in the box's axes,
   and where the results go. */
static struct {
    Car *a, *b;               /* 0x628cbc, 0x628cc0 */
    float ab[9];              /* 0x628f08: a's axis i . b's axis j */
    float bh[9];              /* 0x628f2c: ab times b's half sizes (by j) */
    float ah[9];              /* 0x628c8c: ab times a's half sizes (by i) */
    float da[3], db[3];       /* 0x628cc4, 0x628cd0: b's centre less a's in a's and in b's axes */
    float q[3];               /* 0x628f50, 0x628f54, 0x628cb0 */
    float *point, *normal;    /* 0x628f04, 0x628cb4 */
    int fast;                 /* 0x628f00 */
} P;

static const float *axis(Car *c, int i) { return CAR_F(c, 0x364 + 0xc * i); }
static float half(Car *c, int i) { return *CAR_F(c, 0x3a8 + 4 * i); }

static bool inside(Car *box)
{
    for (int i = 0; i < 3; i++)
        if (!(P.q[i] <= half(box, i) && -half(box, i) <= P.q[i])) return false;
    return true;
}

/* The point of `of` at its half sizes times s, in world space. */
static void surface_point(Car *of, const float s[3], float out[3])
{
    for (int k = 0; k < 3; k++)
        out[k] = half(of, 2) * axis(of, 2)[k] * s[2] + half(of, 1) * axis(of, 1)[k] * s[1] + half(of, 0) * axis(of, 0)[k] * s[0] +
                 CAR_F(of, 0x330)[k];
}

/* b's point at s (its half sizes along its axes) inside a? (0x495570 a face's centre, 0x495aa0 a corner;
   the face gives b's axis as the normal.) */
static bool b_in_a(const float s[3], bool face)
{
    for (int i = 0; i < 3; i++) P.q[i] = P.bh[3 * i + 2] * s[2] + P.bh[3 * i + 1] * s[1] + P.bh[3 * i] * s[0] + P.da[i];
    if (!inside(P.a)) return false;
    if (face)
        for (int k = 0; k < 3; k++) P.normal[k] = s[2] * axis(P.b, 2)[k] + s[1] * axis(P.b, 1)[k] + s[0] * axis(P.b, 0)[k];
    surface_point(P.b, s, P.point);
    return true;
}

/* a's point at s inside b? (0x495800 a face, 0x495ca0 a corner; the face's normal a's axis, negated.) */
static bool a_in_b(const float s[3], bool face)
{
    for (int j = 0; j < 3; j++) P.q[j] = (P.ah[6 + j] * s[2] + P.ah[3 + j] * s[1] + P.ah[j] * s[0]) - P.db[j];
    if (!inside(P.b)) return false;
    if (face)
        for (int k = 0; k < 3; k++) P.normal[k] = -(s[2] * axis(P.a, 2)[k] + s[1] * axis(P.a, 1)[k] + s[0] * axis(P.a, 0)[k]);
    surface_point(P.a, s, P.point);
    return true;
}

/* The normal from the box's axes when fast: the axis most along the corner's car's velocity, each turned
   by the point's side (b's corner in a: those it is past on the positive side; a's in b: the negative). */
static void pick_normal(Car *box, const float *vel, bool flip_positive)
{
    float ax[3][3];
    for (int i = 0; i < 3; i++) {
        memcpy(ax[i], axis(box, i), 12);
        if (flip_positive ? K(0x5b23f8) < P.q[i] : P.q[i] < K(0x5b23f8))
            for (int k = 0; k < 3; k++) ax[i][k] *= K(0x5b2624);
    }
    float d0 = fabsf(dot3(ax[0], vel)), d1 = fabsf(dot3(ax[1], vel)), d2 = fabsf(dot3(ax[2], vel));
    const float *n = (d1 < d0 && d2 < d0) ? ax[0] : d2 < d1 ? ax[1] : ax[2];
    memcpy(P.normal, n, 12);
}

/* Whether the boxes touch, the contact point and normal: a face centre, then a corner of either inside
   the other. 0x496f40 */
static bool boxes_touch(float point[3], float normal[3])
{
    P.point = point, P.normal = normal;
    static const float faces[6][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-1, 0, 0}, {0, -1, 0}, {0, 0, -1}};
    for (int f = 0; f < 6; f++)
        if (b_in_a(faces[f], true)) return true;
    for (int f = 0; f < 6; f++)
        if (a_in_b(faces[f], true)) return true;
    static const float corners[8][3] = {{1, 1, 1}, {1, 1, -1}, {1, -1, 1}, {1, -1, -1}, {-1, 1, 1}, {-1, 1, -1}, {-1, -1, 1}, {-1, -1, -1}};
    bool b_corner = false;
    for (int k = 0; k < 8 && !b_corner; k++) b_corner = b_in_a(corners[k], false);
    if (!b_corner) {
        bool a_corner = false;
        for (int k = 0; k < 8 && !a_corner; k++) a_corner = a_in_b(corners[k], false);
        if (!a_corner) return false;
    }
    /* The velocities' largest difference decides fast. */
    P.fast = 0;
    float dv = 0;
    for (int k = 0; k < 3; k++) {
        float d = fabsf(CAR_F(P.b, 0x33c)[k] - CAR_F(P.a, 0x33c)[k]);
        if (dv < d) dv = d;
    }
    Car *box = b_corner ? P.a : P.b;
    if (K(0x5b243c) < dv) {
        P.fast = 1;
        pick_normal(box, CAR_F(b_corner ? P.b : P.a, 0x33c), b_corner);
        return true;
    }
    /* The axis of the face the corner is nearest; its sign by the point's side. */
    float gap[3];
    for (int i = 0; i < 3; i++) gap[i] = K(0x5b23f8) <= P.q[i] ? half(box, i) - P.q[i] : P.q[i] + half(box, i);
    int i = (gap[1] <= gap[0] || gap[2] <= gap[0]) ? (gap[2] <= gap[1] ? 2 : 1) : 0;
    memcpy(normal, axis(box, i), 12);
    if (b_corner ? K(0x5b23f8) < P.q[i] : P.q[i] < K(0x5b23f8))
        for (int k = 0; k < 3; k++) normal[k] *= K(0x5b2624);
    return true;
}

/* Overlapping: pushed apart (up to 32 steps), the first step's push by their masses or their velocities.
   0x497a80 */
static bool separate(Car *a, Car *b, float point[3], float normal[3])
{
    bool hit = false;
    P.a = a, P.b = b;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            P.ab[3 * i + j] = dot3(axis(a, i), axis(b, j));
            P.bh[3 * i + j] = P.ab[3 * i + j] * half(b, j);
            P.ah[3 * i + j] = P.ab[3 * i + j] * half(a, i);
        }
    float pa[3] = {0, 0, 0}, pb[3] = {0, 0, 0};
    for (int step = 0; step < 0x20; step++) {
        float rel[3];
        for (int k = 0; k < 3; k++) rel[k] = CAR_F(b, 0x330)[k] - CAR_F(a, 0x330)[k];
        for (int i = 0; i < 3; i++) P.da[i] = dot3(axis(a, i), rel), P.db[i] = dot3(axis(b, i), rel);
        if (!boxes_touch(point, normal)) return hit;
        if (!hit) {
            float dv[3];
            for (int k = 0; k < 3; k++) dv[k] = CAR_F(a, 0x33c)[k] - CAR_F(b, 0x33c)[k];
            bool by_mass = dv[0] * dv[0] + dv[2] * dv[2] <= K(0x5b49cc);
            float s = 1.0f;
            if (!by_mass) {
                if (K(0x5b26a0) <= rel[2] * rel[2] + rel[0] * rel[0]) {
                    float c[3] = {dv[0], dv[1], dv[2]}, r[3] = {rel[0], rel[1], rel[2]};
                    unit3(c);
                    unit3(r);
                    if (dot3(c, r) <= K(0x5b25e0)) by_mass = true;
                }
                if (!by_mass) {
                    float l = len3(dv) * K(0x5b23f0);
                    s = l == K(0x5b23f8) ? K(0x5b24a8) : K(0x5b23f0) / l;
                }
            }
            if (by_mass) {
                float n[3] = {rel[0], rel[1], rel[2]};
                unit3(n);
                float m = *CAR_F(b, 0x354) + *CAR_F(a, 0x354);
                float fa = *CAR_F(a, 0x354) / m, fb = *CAR_F(b, 0x354) / m;
                for (int k = 0; k < 3; k++) pa[k] = n[k] * fb * K(0x5b49c8), pb[k] = n[k] * fa * K(0x5b24b8);
            } else {
                for (int k = 0; k < 3; k++)
                    pa[k] = CAR_F(a, 0x33c)[k] * -s * K(0x5b24b8), pb[k] = CAR_F(b, 0x33c)[k] * -s * K(0x5b24b8);
            }
            hit = true;
        }
        for (int k = 0; k < 3; k++) CAR_F(a, 0x330)[k] += pa[k], CAR_F(b, 0x330)[k] += pb[k];
    }
    return hit;
}

/* A hit's strength for the hit car (+0x430): 9 against a light car, else the push it takes, at most the
   speed's share (traffic: less for heavier cars, by a's weight, as the original has it). */
static float strength(Car *c, Car *o, Car *a, float dv, float cap)
{
    float s = fabsf(dv * *CAR_F(c, 0x358));
    if (!(*CAR_I(c, CAR_FLAGS) & 0x80) || K(0x5b2600) <= *CAR_F(c, 0x354)) {
        if (*CAR_F(o, 0x354) <= K(0x5b26e4)) return 9.0f;
        return cap <= s ? cap : s;
    }
    float lim = (K(0x5b2404) - *CAR_F(a, 0x354)) * cap;
    return lim <= s ? lim : s;
}

/* The impulse between them at the contact: their velocities and spins, the hits' strengths and kinds,
   the contact for the effects, both cars' damage. False when they part already. 0x4967d0 */
static bool respond(Car *a, Car *b, const float point[3], const float n[3])
{
    float ra[3], rb[3], ca[3], cb[3];
    for (int k = 0; k < 3; k++) ra[k] = point[k] - CAR_F(a, 0x330)[k], rb[k] = point[k] - CAR_F(b, 0x330)[k];
    Vec3_Cross(ra, n, ca);   /* 0x5325f0 */
    Vec3_Cross(rb, n, cb);
    float vb = dot3(CAR_F(b, 0x33c), n), va = dot3(CAR_F(a, 0x33c), n);
    float wa = dot3(CAR_F(a, 0x388), ca), wb = dot3(CAR_F(b, 0x388), cb);
    /* (0x4968af: the denominator in the original's order, b's turning share onto a's stored one, then the
       two masses' halves, b's first) */
    float da = dot3(ca, ca) * *CAR_F(a, 0x398) * K(0x5b23f0);
    float den = ((dot3(cb, cb) * *CAR_F(b, 0x398) * K(0x5b23f0) + da) + *CAR_F(b, 0x358) * K(0x5b23f0)) + *CAR_F(a, 0x358) * K(0x5b23f0);
    float j = (wb + ((vb - va) - wa)) / den;
    Car *both[2] = {a, b};
    for (int i = 0; i < 2; i++)
        if ((*CAR_I(both[i], CAR_FLAGS) & 0x80) && !both[i]->b[0x7c]) {
            j = fabsf(j);
            both[i]->b[0x7c] = 1;
        }
    if (j < K(0x5b23f8)) return false;
    for (int i = 0; i < 2; i++)
        if ((*CAR_I(both[i], CAR_FLAGS) & 0x80) && *CAR_I(both[i], 0x44c) == 0x10) *CAR_I(both[i], 0x42c) = 1;
    float J = j * K(0x5b23f0);
    if (g_race_network && (*CAR_I(a, 0xdac) || *CAR_I(b, 0xdac))) J *= K(0x5b2694);
    float imp[3] = {n[0] * J, n[1] * J, n[2] * J};
    float dv = (*CAR_F(b, 0x354) + *CAR_F(a, 0x354)) * (dot3(CAR_F(a, 0x33c), n) - dot3(CAR_F(b, 0x33c), n));
    float d[3];
    for (int k = 0; k < 3; k++) d[k] = CAR_F(a, 0x33c)[k] - CAR_F(b, 0x33c)[k];
    float cap = len3(d) * K(0x5b2404);
    *CAR_F(a, 0x430) = strength(a, b, a, dv, cap);
    *CAR_F(b, 0x430) = strength(b, a, a, dv, cap);
    float ka = *CAR_F(a, 0x358) * (K(0x5b26d4) <= *CAR_F(a, 0x430) ? K(0x5b3f64) : 1.0f);
    for (int k = 0; k < 3; k++) CAR_F(a, 0x33c)[k] += imp[k] * ka;
    *CAR_I(a, 0x438) = (*CAR_I(b, CAR_FLAGS) & 0x80) ? (int32_t)((uint32_t)*CAR_I(b, 0x44c) | 0x50000u) : 0x50003;
    /* (+0x434 the other car: the port keeps its creation index plus one, a pointer being wider natively) */
    *CAR_I(a, 0x434) = *CAR_I(b, CAR_INDEX) + 1;
    float away[3];
    for (int k = 0; k < 3; k++) away[k] = CAR_F(a, 0x330)[k] - CAR_F(b, 0x330)[k];
    if (n[0] == K(0x5b23f8) && n[1] == K(0x5b23f8) && n[2] == K(0x5b23f8)) away[0] = 0, away[1] = 1.0f, away[2] = 0;
    else unit3(away);
    float kb = *CAR_F(b, 0x358) * (K(0x5b26d4) <= *CAR_F(b, 0x430) ? K(0x5b3f64) : 1.0f);
    for (int k = 0; k < 3; k++) CAR_F(b, 0x33c)[k] -= imp[k] * kb;
    *CAR_I(b, 0x438) = (*CAR_I(a, CAR_FLAGS) & 0x80) ? (int32_t)((uint32_t)*CAR_I(a, 0x44c) | 0x50000u) : 0x50003;
    *CAR_I(b, 0x434) = *CAR_I(a, CAR_INDEX) + 1;
    /* The spins: by the hit's strength, or for light traffic by its weight. */
    float spin[2];
    for (int i = 0; i < 2; i++) {
        Car *c = both[i];
        if (!(*CAR_I(c, CAR_FLAGS) & 0x80) || K(0x5b260c) <= *CAR_F(c, 0x354)) spin[i] = *CAR_F(c, 0x430) <= K(0x5b26d4) ? 1.5f : 3.0f;
        else spin[i] = K(0x5b246c) / *CAR_F(c, 0x354);
    }
    float t[3];
    Vec3_Cross(ra, imp, t);
    for (int k = 0; k < 3; k++) CAR_F(a, 0x388)[k] += t[k] * (spin[0] * *CAR_F(a, 0x398));
    Vec3_Cross(rb, imp, t);
    for (int k = 0; k < 3; k++) CAR_F(b, 0x388)[k] += t[k] * -(spin[1] * *CAR_F(b, 0x398));
    (*(int16_t *)(void *)(a->b + 0x418))++;
    (*(int16_t *)(void *)(b->b + 0x418))++;
    for (int k = 0; k < 3; k++) {
        float m = (CAR_F(b, 0x330)[k] + CAR_F(a, 0x330)[k]) * K(0x5b23f0);
        CAR_F(b, 0x440)[k] = m;
        CAR_F(a, 0x440)[k] = m;
    }
    CarSim_ImpactDamage(a, away, 2);   /* 0x494d60 */
    for (int k = 0; k < 3; k++) away[k] = -away[k];
    CarSim_ImpactDamage(b, away, 2);
    return true;
}

bool CarSim_Cars(Car *a, Car *b)
{
    float point[3], normal[3];
    if (!separate(a, b, point, normal)) return false;
    respond(a, b, point, normal);
    int left = 4;
    CarSim_Edges(a);    /* 0x494f90 */
    CarSim_Objects(a);  /* 0x498d50 */
    CarSim_Edges(b);
    CarSim_Objects(b);
    bool again = separate(a, b, point, normal);
    while (again && 0 < left) {
        left--;
        if (!respond(a, b, point, normal)) return true;
        CarSim_Edges(a);
        CarSim_Objects(a);
        CarSim_Edges(b);
        CarSim_Objects(b);
        if (left == 0) {
            /* Still locked: a heavy knock on both (zone 8). */
            if (*CAR_I(a, 0xd2c) && K(0x5b243c) < *CAR_F(a, 0x35c)) CarSim_ZoneDamage(a, 100.0f, 8, 2);   /* 0x494470 */
            if (*CAR_I(b, 0xd2c) && K(0x5b243c) < *CAR_F(b, 0x35c)) CarSim_ZoneDamage(b, 100.0f, 8, 2);
        }
        again = separate(a, b, point, normal);
    }
    return true;
}
