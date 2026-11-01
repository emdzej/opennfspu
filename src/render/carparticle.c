/* A car's particles: see carparticle.h. */
#include "render/carparticle.h"
#include "exe.h"
#include "game/camera.h"
#include "render/render.h"
#include "render/sky.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static float f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static double f64(uint32_t va)
{
    uint64_t u = (uint64_t)exe_u32(va + 4) << 32 | exe_u32(va);
    double d;
    memcpy(&d, &u, 8);
    return d;
}

/* The queue of the cars drawn (a set by address, 0x6115c0). */
static CarParticles **s_queue;
static int s_nqueue, s_capqueue;

CarParticles *CarParticles_New(Car *car)
{
    if (!car) return NULL;
    CarParticles *cp = calloc(1, sizeof *cp);
    if (!cp) return NULL;
    cp->car = car;
    for (int i = 0; i < 11; i++) {
        PartEmitter *e = &cp->rec[i];
        e->tag = exe_u32(0x5b3724 + 4u * (uint32_t)i);   /* "SMRTSMRTSMRTSMRTCSPKWTSPWTSPRDDTRDDTRDDTRDDT" */
        e->axis1[0] = 1.0f;
        e->axis2[1] = 1.0f;
        cp->sys[i] = PartSystem_New(0, e, false);
        if (cp->sys[i]) cp->sys[i]->active = false;
    }
    if (cp->sys[4]) {
        cp->sys[4]->flags |= 1 | 4;
        cp->sys[4]->origin = (const PartEmitter *)(void *)CAR_F(car, 0x330);   /* (its place is read) */
    }
    return cp;
}

void CarParticles_Free(CarParticles *cp)
{
    if (!cp) return;
    for (int i = 0; i < s_nqueue; i++)
        if (s_queue[i] == cp) {
            memmove(s_queue + i, s_queue + i + 1, (size_t)(s_nqueue - i - 1) * sizeof *s_queue);
            s_nqueue--;
            break;
        }
    for (int i = 0; i < 11; i++)
        if (cp->sys[i]) PartSystem_Free(cp->sys[i]);
    free(cp);
}

static void queue(CarParticles *cp)
{
    int i = 0;
    while (i < s_nqueue && (uintptr_t)s_queue[i] < (uintptr_t)cp) i++;
    if (i < s_nqueue && s_queue[i] == cp) return;
    if (s_nqueue == s_capqueue) {
        int cap = s_capqueue ? s_capqueue * 2 : 16;
        CarParticles **q = realloc(s_queue, (size_t)cap * sizeof *q);
        if (!q) return;
        s_queue = q;
        s_capqueue = cap;
    }
    memmove(s_queue + i + 1, s_queue + i, (size_t)(s_nqueue - i) * sizeof *s_queue);
    s_queue[i] = cp;
    s_nqueue++;
}

static void on(PartSystem *s, float k, float d2)
{
    s->k44 = k;
    s->active = true;
    s->life = 100;
    s->dist2 = d2;
}

/* Per wheel (its record 0xc4 bytes from +0x7f8: place, +0x4c the surface, +0x78 its slip), on a dry road
   (the sky's wet level 0x45cec0 at most 0.1): skid smoke on the surfaces of kind 1 (0x5cd964) slipping
   by half, thrown up with the car's speed and 0.8; on wet asphalt the rear wheels' spray above 7.5 (its
   share of the next 15) behind the wheel, thrown back and up (4.35 and a twentieth of the speed), where
   the driver modulates alpha; and the surface's dirt (0x5b3750: dirt, grass, snow) slipping by 0.3 when
   going, grass and snow a car's forward length behind and higher. */
void CarParticles_Update(CarParticles *cp, bool shown)
{
    if (!cp || !g_render_particles) return;
    Car *c = cp->car;
    const float *eye = g_racecam[0].eye, *p = CAR_F(c, 0x330), *v = CAR_F(c, 0x33c), *fwd = CAR_F(c, 0x37c);
    float dx = p[0] - eye[0], dy = p[1] - eye[1], dz = p[2] - eye[2];
    float d2 = dx * dx + dy * dy + dz * dz;
    bool wet = f32(0x5b2604) < Sky_GlowLevel();   /* 0x45dc40, 0x45cec0 */
    if (!shown) {
        for (int i = 0; i < 11; i++)
            if (cp->sys[i]) cp->sys[i]->active = false;
    } else {
        for (int w = 0; w < 4; w++) {
            const uint32_t o = 0xc4u * (uint32_t)w;
            uint32_t surf = *(const uint32_t *)(const void *)(c->b + 0x844 + o) & 0xf;
            int32_t kind = (int32_t)exe_u32(0x5cd964 + 4u * surf);
            float slip = *CAR_F(c, 0x870 + o);
            const float *wp = CAR_F(c, 0x7f8 + o);
            PartSystem *s = cp->sys[w];
            if (s) {
                if (wet || kind != 1 || slip < f32(0x5b23f0)) {
                    s->active = false;
                } else {
                    PartEmitter *e = &cp->rec[w];
                    memcpy(e->pos, wp, 12);
                    on(s, slip, d2);
                    e->axis2[0] = v[0];
                    e->axis2[1] = (float)((double)v[1] + f64(0x5b37b8));
                    e->axis2[2] = v[2];
                }
            }
            uint32_t tag = exe_u32(0x5b3750 + 4u * surf);
            float speed = *CAR_F(c, 0xd60);
            s = cp->sys[7 + w];
            if (s) {
                if (tag == 0 || slip < f32(0x5b25f0) || speed <= 0) {
                    s->active = false;
                } else {
                    PartEmitter *e = &cp->rec[7 + w];
                    memcpy(e->pos, wp, 12);
                    s->k44 = slip;
                    PartSystem_SetTag(s, tag);
                    s->active = true;
                    s->life = 100;
                    s->dist2 = d2;
                    float up = f32(0x5b24e8);
                    if (tag != 0x54444452u) {   /* 'RDDT' */
                        e->pos[0] -= fwd[0];
                        e->pos[2] -= fwd[2];
                        up = f32(0x5b37b0);
                    }
                    e->axis2[0] = v[0] - fwd[0] * f32(0x5b37ac);
                    e->axis2[1] = up + v[1];
                    e->axis2[2] = v[2] - fwd[2] * f32(0x5b37ac);
                }
            }
            if (2 <= w && (s = cp->sys[3 + w])) {
                float lo = f32(0x5cd9b0);
                if (!wet || kind != 1 || speed <= lo || !g_render_modalpha) {
                    s->active = false;
                } else {
                    float x = speed - lo, m = lo + lo;
                    if (m < x) x = m;
                    PartEmitter *e = &cp->rec[3 + w];
                    memcpy(e->pos, wp, 12);
                    e->pos[0] -= fwd[0] * f32(0x5b37a8);
                    e->pos[2] -= fwd[2] * f32(0x5b37a8);
                    on(s, x / m, d2);
                    float f = fwd[0];
                    e->axis2[0] = v[0] - (f + f);
                    e->axis2[1] = (float)((double)(v[1] + f32(0x5b3798)) + (double)speed * f64(0x5b37a0));
                    f = fwd[2];
                    e->axis2[2] = v[2] - (f + f);
                }
            }
        }
    }
    queue(cp);
}

static void normcross(const float *a, const float *b, float *out)   /* 0x532610 */
{
    float t[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    float n = 1.0f / sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);   /* 0x532640 */
    out[0] = n * t[0];
    out[1] = t[1] * n;
    out[2] = t[2] * n;
}

/* Its place relative to the car with the car's ground height below it as the bounce's (+0x404), thrown
   back along a tenth of the velocity, its sideways axes across the velocity (about up, or a little off
   it when the velocity is nearly vertical); restarted. */
void CarParticles_Sparks(CarParticles *cp, const float p[3], const float v[3])
{
    if (!cp || !g_render_particles || !cp->sys[4]) return;
    Car *c = cp->car;
    const float *eye = g_racecam[0].eye, *cpos = CAR_F(c, 0x330);
    float dx = p[0] - eye[0], dy = p[1] - eye[1], dz = p[2] - eye[2];
    PartEmitter *e = &cp->rec[4];
    e->pos[0] = p[0] - cpos[0];
    e->pos[1] = p[1] - cpos[1];
    e->pos[2] = p[2] - cpos[2];
    e->pos[3] = *CAR_F(c, 0x404) - p[1];
    for (int k = 0; k < 3; k++) e->axis2[k] = v[k] * f32(0x5b2444);
    float a[3] = {0, 1.0f, 0};
    if (fabs(v[0]) < f64(0x5b2bc0) && fabs(v[2]) < f64(0x5b2bc0)) a[0] = 0.015f;
    float t[3] = {a[1] * v[2] - a[2] * v[1], a[2] * v[0] - a[0] * v[2], a[0] * v[1] - a[1] * v[0]};   /* 0x5325f0 */
    normcross(t, v, e->axis1);
    normcross(t, v, cp->sys[4]->side);
    PartSystem_Reset(cp->sys[4]);
    cp->sys[4]->life = 100;
    cp->sys[4]->dist2 = dx * dx + dy * dy + dz * dz;
}

void CarParticles_DrawAll(void)
{
    for (int i = 0; i < s_nqueue; i++)
        for (int k = 0; k < 11; k++)
            if (s_queue[i]->sys[k]) PartSystem_Draw(s_queue[i]->sys[k]);
}

void CarParticles_TickAll(void)
{
    for (int i = 0; i < s_nqueue; i++)
        for (int k = 0; k < 11; k++)
            if (s_queue[i]->sys[k]) PartSystem_Step(s_queue[i]->sys[k]);
    s_nqueue = 0;
}

void CarParticles_Clear(void)
{
    free(s_queue);
    s_queue = NULL;
    s_nqueue = s_capqueue = 0;
}
