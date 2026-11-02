/* The traffic objects: see traffic.h. */
#include "game/traffic.h"
#include "exe.h"
#include "game/car_ground.h"
#include "game/car_physics.h"
#include "game/sim_math.h"
#include "game/smack.h"
#include "game/track.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
#define K(va) exe_f32(va)

uint8_t g_trafficdesc[TRAFFIC_MAX][TRAFFICDESC_SIZE];
static float s_ground_n[3];   /* 0x6559c8 */

static void put_i(uint8_t *d, int off, int32_t v) { memcpy(d + off, &v, 4); }

/* The world's bodies (+0x34: the scene bundle's, then the instance bundle's). */
static WSmackBody *body(int i)
{
    return g_world && 0 <= i && (uint32_t)i < g_world->nbodies ? g_world->bodies[i] : NULL;
}

void Traffic_SmackDescs(void)   /* 0x4a15d0 */
{
    g_ntrafficcars = g_world ? (int)g_world->nbodies : 0;   /* (at most 125, under the descriptions' room) */
    for (int i = 0; i < g_ntrafficcars; i++) {
        uint8_t *d = g_trafficdesc[i];
        put_i(d, 0x580, i);
        put_i(d, 0x590, 0);
        snprintf((char *)d + 0x228, 0x20, "%s", exe_str(0x5d0d3c));
        put_i(d, 0x4, 1);
        put_i(d, 0x174, 2);
        put_i(d, 0x0, 0xb);
        put_i(d, 0xc, 1);
        put_i(d, 0x10, -1);
        put_i(d, 0x24, 0);
        put_i(d, 0x20, 0);
        put_i(d, 0x204, 0);
        put_i(d, 0x8, 0x80);
        put_i(d, 0x14, 0);
        put_i(d, 0x18, 0);
        put_i(d, 0x1c, 0);
    }
}

static int body_index(Car *c, uint32_t id) { return (int)(id & 0x7f) - g_nracecars; }

void Traffic_Shape(Car *c, uint32_t id)   /* 0x4a1060 */
{
    if (DESC_I(c, 4) != 1) {
        *CAR_F(c, 0x354) = *CAR_F(c, 0x3b0) * *CAR_F(c, 0x3ac) * *CAR_F(c, 0x3a8) * K(0x5b2404);
        return;
    }
    const WSmackBody *b = body(body_index(c, id));
    if (!b) return;
    *CAR_F(c, 0x354) = b->mass * K(0x5b273c);
    static const int8_t kinds[13] = {0x13, 0x12, 0x11, 0x14, 0x10, 0xe, 0xf, 3, 9, 10, 0xb, 0xc, 0xd};
    *CAR_I(c, 0x44c) = 0 < b->audio && b->audio < 13 ? kinds[b->audio] : 0x13;
    /* The points and their box (from 0 each way but the far z's start, 0x5b23f8). */
    float mn[3] = {0, 0, 0}, mx[3] = {0, 0, K(0x5b23f8)};
    *CAR_I(c, 0x32c) = b->npts;
    float *p = CAR_F(c, 0x8c);
    for (int i = 0; i < b->npts; i++, p += 3) {
        memcpy(p, b->pts[i], 12);
        for (int k = 0; k < 3; k++) {
            if (p[k] < mn[k]) mn[k] = p[k];
            if (mx[k] < p[k]) mx[k] = p[k];
        }
    }
    for (int k = 0; k < 3; k++) *CAR_F(c, 0x3a8 + 4 * k) = (mx[k] - mn[k]) * K(0x5b23f0);
}

void Traffic_Place(Car *c)   /* 0x4a12f0 */
{
    if (DESC_I(c, 4) == 1) {
        WSmackBody *b = body(body_index(c, (uint32_t)*CAR_I(c, 0)));
        if (b) {
            float *rot = CAR_F(c, 0x364);
            memcpy(rot, b->rot0, 36);
            for (int r = 0; r < 3; r++) Vec3_Unit(rot + 3 * r, rot + 3 * r);   /* 0x532640 */
            memcpy(CAR_F(c, 0x490), rot, 36);
            memcpy(CAR_F(c, 0x330), b->pos0, 12);
            WSmackBody_Follow(b, rot, CAR_F(c, 0x330));
        }
    }
    /* (an object whose description's +4 isn't 1 goes on its road node, 0x629540, at the node's point with
       no turn: Traffic_SmackDescs gives every one 1) */
    c->b[0x7e] = 0;
    c->b[0x7f] = 1;
    c->b[0x7d] = 1;
    c->b[0x7c] = 0;
    CarSim_BodyPoints(c);    /* 0x49ada0 */
    CarSim_TrackBody(c);     /* 0x499460 */
    CarSim_Track(c);         /* 0x49aee0 (it returns at once: +0x7c is clear) */
    CarSim_BodyPlane(c);     /* 0x499490 */
    *CAR_F(c, 0x41c) = CarSim_TrafficGround(c, s_ground_n);   /* 0x499830 */
    CarSim_RideHeights(c, CAR_F(c, 0x3e8), CAR_F(c, 0x400));   /* 0x49a870 */
    uint16_t surf;
    memcpy(&surf, c->b + 0x2c, 2);
    *CAR_I(c, 0x460) = surf;
    *CAR_I(c, 0x464) = surf & 0xf;
    CarSim_TrafficCorners(c, K(0x5b24e8));
    CarSim_BodyPoints(c);    /* 0x49ada0 */
}

/* A race car on the ground (+0x7f) within 0x5b241c of the object (the octagonal distance). 0x4a0d60 */
static bool car_near(Car *c)
{
    for (int i = 0; i < g_ncars; i++) {
        Car *o = g_cars[i];
        if (!o->b[0x7f]) continue;
        float dx = fabsf(CAR_F(c, 0x330)[0] - CAR_F(o, 0x330)[0]), dz = fabsf(CAR_F(c, 0x330)[2] - CAR_F(o, 0x330)[2]);
        float d = dx <= dz ? dx * K(0x5b2468) + dz : dz * K(0x5b2468) + dx;
        if (d < K(0x5b241c)) return true;
    }
    return false;
}

static float abs3(const float *v) { return fabsf(v[2]) + fabsf(v[1]) + fabsf(v[0]); }

void Traffic_Drive(void *car)   /* 0x4a0e10 */
{
    Car *c = car;
    float *vel = CAR_F(c, 0x33c), *spin = CAR_F(c, 0x388);
    if (c->b[0x7d]) {
        if (c->b[0x7c]) {
            memset(vel, 0, 12);
            memset(spin, 0, 12);
        }
        if (car_near(c)) c->b[0x7d] = 0;
        return;
    }
    if (K(0x5b2738) <= *CAR_F(c, 0x424)) {
        /* in the air: only the fast turns slowed */
        if (K(0x5b2404) < fabsf(spin[0])) spin[0] *= K(0x5b4a94);
        if (K(0x5b2404) < fabsf(spin[1])) spin[1] *= K(0x5b4a94);
        if (K(0x5b2404) < fabsf(spin[2])) spin[2] *= K(0x5b4a94);
    } else {
        for (int k = 0; k < 3; k++) vel[k] *= 0.925f;   /* 0x3f6ccccd */
        spin[0] *= K(0x5b4a94);
        spin[1] *= K(0x5b25f4);
        spin[2] *= K(0x5b4a94);
    }
    if (abs3(vel) < K(0x5b2604) && abs3(spin) < K(0x5b26a0) && *CAR_F(c, 0x428) < K(0x5b27a8) && !car_near(c))
        c->b[0x7d] = 1;
}
