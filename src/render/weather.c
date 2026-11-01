/* The weather's snow and rain: see weather.h. */
#include "render/weather.h"
#include "exe.h"
#include "game/car.h"
#include "game/hud.h"
#include "game/input.h"
#include "pending.h"
#include "real/random.h"
#include "render/particles.h"
#include "render/render.h"
#include "render/rmesh.h"
#include "render/rtex.h"
#include "render/sky.h"
#include "render/sky_levels.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static float f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static float f32u(uint32_t u)
{
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

#define MAXDROPS 401   /* three vertices each */

/* The configuration (0x54 bytes, the constructor's argument): the box in camera space (x, y and z, each
   least, most, range; z's most twice), the most drops at level 1, the drops' sizes across and along,
   the wind, the gusts' direction, the texture and whether the drops wobble. */
typedef struct {
    float x0, x1, xr;       /* [0x2181] */
    float y0, y1, yr;
    float z0, z1, z1b, zr;
    float count;            /* [0x218b] */
    float across, along;    /* [0x218c], [0x218d] */
    float wind[3];          /* [0x218e] */
    float gustdir[3];       /* [0x2191] */
    Texture *tex;           /* [0x2194] */
    bool wobble;            /* [0x2195] */
} PrecipCfg;

/* A volume (0x8858 bytes; 0x456da0). */
typedef struct {
    float level;                    /* [0]: the sky's level it was made for */
    int32_t n, target;              /* [1], [2]: the drops in use, the drops wanted */
    float verts[MAXDROPS * 3][4];   /* +0x0c: a drop's place is its first vertex */
    float uv[MAXDROPS * 3][2];      /* +0x4b3c */
    uint32_t col[MAXDROPS * 3];     /* +0x70d4 */
    uint8_t active[MAXDROPS];       /* +0x83a0 */
    int32_t phase[8];               /* [0x214d] */
    float wob[8][3];                /* [0x2155] */
    float lastsin;                  /* [0x216d] */
    SkyBlend blend;                 /* [0x216e] */
    RMesh mesh;                     /* [0x2170] */
    float gust, gusttarget;         /* [0x2171], [0x2172] */
    float lastorient[9];            /* [0x2173] */
    float lasteye[3];               /* [0x217c] */
    bool started;                   /* [0x217f] */
    float speed2;                   /* [0x2180]: the eye's ground speed squared, per tick */
    PrecipCfg c;                    /* [0x2181] */
    float sin[128];                 /* [0x2196] */
} Precip;

static struct {
    int32_t tick;     /* +0x00 */
    Precip *snow;     /* +0x04 */
    Precip *rain;     /* +0x08 */
} *s_weather;         /* 0x627e08 */

float g_weather_time, g_weather_steps;   /* 0x627e10, 0x627e14 */

/* The render's random numbers (0x5ceae0) as a share of 1 (2^-16, 0x5b253c) of r, from lo. */
static float rnd(float r, float lo) { return (float)Render_Rand() * r * f32(0x5b253c) + lo; }

/* The camera's frame as the original keeps it (0x466140): world to camera, the orientation transposed. */
static void frame_rot(const RCamera *cam, float m[9])
{
    const float *o = cam->frame.orient;
    m[0] = o[0]; m[1] = o[3]; m[2] = o[6];
    m[3] = o[1]; m[4] = o[4]; m[5] = o[7];
    m[6] = o[2]; m[7] = o[5]; m[8] = o[8];
}

static void xform(const float v[3], const float m[9], float out[3])   /* 0x5327b0: row vectors */
{
    float t[3];
    for (int j = 0; j < 3; j++) t[j] = v[0] * m[j] + v[1] * m[3 + j] + v[2] * m[6 + j];
    memcpy(out, t, sizeof t);
}

static void target_of(Precip *p, float level)
{
    p->level = level;
    p->target = (int32_t)lrintf(level * p->c.count - f32(0x5b23f0));
    if (p->target < 3) p->target = 0;
}

static void park(Precip *p, int i)
{
    p->active[i] = 0;
    float *v = p->verts[3 * i];
    v[0] = p->c.x0 - p->c.xr;
    v[1] = p->c.y0 - p->c.yr;
    v[2] = p->c.z0 - p->c.zr;
}

static void wobble_reset(Precip *p)
{
    for (int i = 0; i < 8; i++) {
        p->phase[i] = (int32_t)(REAL_randf() * f32(0x5b2428));   /* 1000; _ftol */
        p->wob[i][0] = p->wob[i][1] = p->wob[i][2] = 0;
    }
    p->lastsin = 0;
}

/* Every drop at once, at random in the box (0x4569d0's and 0x456f10's): the level checked against
   itself first, which changes nothing. */
static void reset(Precip *p)
{
    float level = p->level;
    memset(p->active, 0, sizeof p->active);
    wobble_reset(p);
    if (p->level != level) target_of(p, level);
    p->speed2 = 0;
    p->n = p->target;
    for (int i = 0; i < p->target; i++) {
        p->active[i] = 1;
        float *v = p->verts[3 * i];
        v[0] = rnd(p->c.xr, p->c.x0);
        v[1] = rnd(p->c.yr, p->c.y0);
        v[2] = rnd(p->c.zr, p->c.z0);
    }
}

/* A new drop (0x456c40): moving fast (0x5b3e3c), at the box's far end; else along its top. */
static void spawn(Precip *p, int i)
{
    p->active[i] = 1;
    float *v = p->verts[3 * i];
    if (f32(0x5b3e3c) < p->speed2) {
        v[0] = rnd(p->c.xr, p->c.x0);
        v[1] = rnd(p->c.yr, p->c.y0);
        v[2] = p->c.z1;
    } else {
        v[0] = rnd(p->c.xr, p->c.x0);
        v[1] = p->c.y1;
        v[2] = rnd(p->c.zr, p->c.z0);
    }
    p->n++;
}

/* Toward the drops wanted (0x4569d0): twenty more a step; fewer by the tail's spent drops, all parked
   at once when none are wanted at speed; all anew when four times as many are wanted at speed (10000,
   0x5b2764). */
static void adjust(Precip *p)
{
    int n = p->n, target = p->target;
    if (target <= n * 4 || p->speed2 <= f32(0x5b2764)) {
        if (n < target) {
            int end = n + 20 < target ? n + 20 : target;
            for (int i = n; i < end; i++) spawn(p, i);
        } else if (target < n) {
            if (target == 0 && f32(0x5b3e3c) < p->speed2)
                for (int i = 0; i < p->n; i++) park(p, i);
            while (p->target <= p->n - 1 && !p->active[p->n - 1]) p->n--;
        }
    } else {
        reset(p);
    }
}

/* A drop out of the box re-enters at the opposite side elsewhere (0.01 inside on y, 0x5b2bc0). */
static bool wrap(Precip *p, float *v)
{
    const PrecipCfg *c = &p->c;
    const float eps = (float)f64(0x5b2bc0);
    bool w = false;
    if (c->z1 < v[2]) {
        v[2] = c->z0;
        v[0] = rnd(c->xr, c->x0);
        v[1] = rnd(c->yr, c->y0);
        w = true;
    } else if (v[2] < c->z0) {
        v[2] = c->z1;
        v[0] = rnd(c->xr, c->x0);
        v[1] = rnd(c->yr, c->y0);
        w = true;
    }
    if (c->x1 < v[0]) {
        v[0] = c->x0;
        v[1] = rnd(c->yr, c->y0);
        v[2] = rnd(c->zr, c->z0);
        w = true;
    } else if (v[0] < c->x0) {
        v[0] = c->x1;
        v[1] = rnd(c->yr, c->y0);
        v[2] = rnd(c->zr, c->z0);
        w = true;
    }
    if (c->y1 < v[1]) {
        v[1] = c->y0 + eps;
        v[0] = rnd(c->xr, c->x0);
        v[2] = rnd(c->zr, c->z0);
        w = true;
    } else if (v[1] < c->y0) {
        v[1] = c->y1 - eps;
        v[0] = rnd(c->xr, c->x0);
        v[2] = rnd(c->zr, c->z0);
        w = true;
    }
    return w;
}

/* The drops moved (0x456f10): the wobbles' steps along the sine table (the y offset the sine's change,
   x and z the wind's by the gust, 9 times, 0x5b368c); each drop turned and moved with the camera and
   blown (t), wobbling by its eighth unless a race screen holds the game; wrapped round the box, the
   wrapped ones past the wanted count parked. Over twenty drops with half wrapped at once: all anew. */
static void move(Precip *p, const float R[9], const float t[3], int32_t dt)
{
    if (p->n <= 0) return;
    if (p->c.wobble) {
        for (int i = 0; i < 8; i++) {
            p->phase[i] = (int32_t)((uint32_t)p->phase[i] + (uint32_t)dt * 0x80u);
            int32_t k = (int32_t)((uint32_t)p->phase[i] * 0x80u) / 0x10000 % 128;
            if (k < 0) k += 128;   /* (past 2^24 the original's index goes negative, reading the fields before the
                                      table; the port folds it into the table) */
            float s = p->sin[k], d = s - p->lastsin;
            p->wob[i][0] = d * p->c.wind[0] * p->gust * f32(0x5b368c);
            p->wob[i][1] = d;
            p->wob[i][2] = d * p->c.wind[2] * p->gust * f32(0x5b368c);
            p->lastsin = s;
        }
    }
    for (int i = 0; i < p->n; i++) {
        if (!p->active[i] && p->n < p->target) p->active[i] = 1;
        if (p->active[i] == 1) {
            float *v = p->verts[3 * i];
            xform(v, R, v);
            v[0] += t[0];
            v[1] += t[1];
            v[2] += t[2];
            if (p->c.wobble && (!HUD_ModalShown() || 1 < g_replay_mode)) {
                v[0] += p->wob[i & 7][0];
                v[1] += p->wob[i & 7][1];
                v[2] += p->wob[i & 7][2];
            }
        }
    }
    int n = p->n;
    float wrapped = 0;
    for (int i = 0; i < p->n; i++) {
        if (p->active[i] != 1) continue;
        if (!wrap(p, p->verts[3 * i])) continue;
        wrapped += 1.0f;
        if (p->target <= i) park(p, i);
    }
    if (20 < p->n && (float)n * f32(0x5b23f0) < wrapped) reset(p);
}

/* The drops' triangles (0x4564c0): down (0, -1, 0) in camera space and across it from the view's
   (0, 0, -1), scaled in x and y only; the even drop's vertices its place, there across and along, there
   along; the odd one's its place, across, across and along. */
static void build(Precip *p, const float rot[9])
{
    if (p->n <= 0) return;
    float u[3] = {0, -1.0f, 0}, b[3] = {0, 0, -1.0f}, r[3];
    xform(u, rot, u);
    r[0] = b[1] * u[2] - b[2] * u[1];   /* 0x5325f0 */
    r[1] = b[2] * u[0] - b[0] * u[2];
    r[2] = b[0] * u[1] - b[1] * u[0];
    const float A[3] = {r[0] * p->c.across, r[1] * p->c.across, r[2]};
    const float U[3] = {u[0] * p->c.along, u[1] * p->c.along, u[2]};
    for (int k = 0; k < p->n * 3; k += 6) {   /* 0x53c200: the w's equal, the first's kept */
        float (*v)[4] = &p->verts[k];
        for (int j = 0; j < 3; j++) {
            v[1][j] = v[0][j] + A[j] + U[j];
            v[2][j] = v[0][j] + U[j];
            v[4][j] = v[3][j] + A[j];
            v[5][j] = v[3][j] + A[j] + U[j];
        }
    }
}

/* A step (0x4566a0): the camera's turn and move since the last (none the first time), the ground speed
   by the ticks (0x5cee14, ten and above as ten), the drops wanted when the level changed, the count
   adjusted; the gusts (up by 0.004 a tick, 0x5b3e38, to the target 1 that a chance of 0.0015 a tick sets,
   0x5b2634, then down by 0.0015 to 0) and the wind by them (none while a race screen holds the game);
   the drops moved and built. */
static void update(Precip *p, int32_t dt, float level)
{
    RCamera *cam = g_render->camera;
    float rot[9], R[9], d[3], t[3] = {0, 0, 0};
    float fdt = (float)(uint32_t)dt;
    frame_rot(cam, rot);
    if (!p->started) {
        p->started = true;
        memcpy(p->lastorient, cam->frame.orient, sizeof p->lastorient);
        memcpy(p->lasteye, cam->frame.eye, sizeof p->lasteye);
        static const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};   /* 0x5bb2d0 */
        memcpy(R, identity, sizeof R);
        d[0] = d[1] = d[2] = 0;
    } else {
        const float *a = p->lastorient;   /* 0x5321e0 */
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                R[3 * i + j] = a[3 * i] * rot[j] + a[3 * i + 1] * rot[3 + j] + a[3 * i + 2] * rot[6 + j];
        for (int j = 0; j < 3; j++) d[j] = p->lasteye[j] - cam->frame.eye[j];
        memcpy(p->lastorient, cam->frame.orient, sizeof p->lastorient);
        memcpy(p->lasteye, cam->frame.eye, sizeof p->lasteye);
    }
    int k = (uint32_t)dt < 11 ? dt - 1 : 9;
    p->speed2 = (d[2] * d[2] + d[0] * d[0]) * f32(0x5cee14 + 4u * (uint32_t)k);
    if (p->level != level) target_of(p, level);
    adjust(p);
    if (0 < p->n) {
        if (p->gust < p->gusttarget) {
            p->gust += fdt * f32(0x5b3e38);
            if (p->gusttarget < p->gust) p->gusttarget = 0;
        } else if (p->gusttarget < p->gust) {
            p->gust -= fdt * f32(0x5b2634);
            if (p->gust < p->gusttarget) p->gust = p->gusttarget = 0;
        } else if (REAL_randf() < fdt * f32(0x5b2634)) {
            p->gusttarget = 1.0f;
        }
        float w[3] = {p->c.wind[0], p->c.wind[1], p->c.wind[2]};
        if (!HUD_ModalShown() || 1 < g_replay_mode) {
            float s = (1.0f - p->gust) * fdt;   /* Vec3_ScaleArray 0x5320d0 */
            for (int j = 0; j < 3; j++) w[j] *= s;
            if (0 < p->gust)
                for (int j = 0; j < 3; j++) w[j] += p->c.gustdir[j] * (fdt * p->gust);
        } else {
            w[0] = w[1] = w[2] = 0;
        }
        for (int j = 0; j < 3; j++) t[j] = d[j] + w[j];
        xform(t, rot, t);
    }
    move(p, R, t, dt);
    build(p, rot);
}

/* The draw (0x4563b0): 249 vertices at a time. */
static void draw(Precip *p)
{
    int total = p->n * 3;
    for (int k = 0; k < total; k += 249) {
        int m = total - k < 249 ? total - k : 249;
        RMesh_setUVs(&p->mesh, 0, p->uv[k]);
        RMesh_setColours(&p->mesh, 0, &p->col[k]);
        RMesh_setPositions(&p->mesh, p->verts[k], m);
        RMesh_draw(&p->mesh, -1);
    }
}

/* 0x44d1f0 on particle.fsh. The original swaps the name's bytes into its bank's big-endian keys; the
   port's bank keys the names as read (SHAPE_entryname), so the constant goes in as it is. */
static Texture *find(uint32_t name)
{
    return g_rtex_common[1] ? RTexBank_find(g_rtex_common[1], 0, name) : NULL;
}

/* 0x456da0: the sine table (a turn in 128, 0x5b273c), the drops off, the wobbles, the triangles' texture
   coordinates two drops at a time (0x456660), white, w 1; the additive blend (0x5377c0(1)) and the mesh
   (type 9, triangles, a stage). */
static Precip *precip_new(const PrecipCfg *c)
{
    Precip *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    p->c = *c;
    for (int i = 0; i < 128; i++) p->sin[i] = (float)sin((double)((float)i * f32(0x5b273c)) * f64(0x5bb2c0));
    wobble_reset(p);
    static const float uv[6][2] = {{0, 0}, {1, 1}, {0, 1}, {0, 0}, {1, 0}, {1, 1}};
    for (int i = 0; i < MAXDROPS * 3; i++) {
        memcpy(p->uv[i], uv[i % 6], sizeof uv[0]);
        p->col[i] = 0xffffffffu;
        p->verts[i][3] = 1.0f;
    }
    SkyBlend_ctor(&p->blend);
    p->blend.v[2] = 1;
    RMesh_ctor(&p->mesh, 9, 3, 1, NULL, 0);
    RMesh_stage(&p->mesh, 0, c->tex, &p->blend, p->uv, p->col, 0);
    return p;
}

static void precip_free(Precip *p)
{
    if (!p) return;
    RMesh_dtor(&p->mesh);
    free(p);
}

/* 0x458b60: snow ("0ons" + the special effects' level, 0x5ceeb8, swapped: sno0..sno3, else snow) in a box
   30 deep from 1, drifting and wobbling; rain (rai0..rai3, else rain) in a box 12.5 deep from 2.5, falling
   at 0.8 a tick, its drops longer. */
void Weather_Init(void)
{
    s_weather = calloc(1, sizeof *s_weather);
    if (!s_weather) return;
    uint32_t lv = (uint32_t)g_render_specials;
    PrecipCfg snow = {-15, 15, 30, -3, 10, 13, 1, 30, 30, 29, (float)g_render_precip, 0.2f, 0.2f,
                      {0.01f, -0.01f, 0}, {0.02f, -0.02f, 0.15f}, NULL, true};
    snow.tex = find(exe_u32(0x5ceeb8 + 4 * lv));
    if (!snow.tex) snow.tex = find(0x776f6e73);   /* 'snow' */
    s_weather->snow = precip_new(&snow);
    PrecipCfg rain = {-15, 15, 30, -3, 10, 13, 2.5f, 15, 15, 12.5f, (float)g_render_precip, 0.2f, 0.5f,
                      {0, -0.8f, 0}, {0, 0, 0}, NULL, false};
    rain.gustdir[1] = f32u(0xbea3d70bu);   /* (an ulp past -0.32) */
    rain.tex = find(exe_u32(0x5ceeb8 + 0x10 + 4 * lv));
    if (!rain.tex) rain.tex = find(0x6e696172);   /* 'rain' */
    s_weather->rain = precip_new(&rain);
    PENDING_NOTE(0x457670, "the lens drops (SPLT, 0x67e4 bytes)");
}

void Weather_Free(void)   /* 0x458a80 */
{
    if (!s_weather) return;
    precip_free(s_weather->rain);
    precip_free(s_weather->snow);
    free(s_weather);
    s_weather = NULL;
}

void Weather_Frame(void)
{
    if (!s_weather || !g_render || !g_render->camera) return;
    Precip *snow = s_weather->snow, *rain = s_weather->rain;
    if (!HUD_ModalShown() || 1 < g_replay_mode) {
        int32_t dt = g_sim_tick - s_weather->tick;
        s_weather->tick = g_sim_tick;
        if (dt) {
            g_weather_time += (float)(uint32_t)dt;
            g_weather_steps += 1.0f;
            float sl = Sky_SnowLevel(), rl = Sky_GlowLevel();
            if (snow) update(snow, dt, sl);
            if (rain) update(rain, dt, rl);
            PENDING_NOTE(0x458190, "the lens drops' step");
        }
    } else {
        g_weather_time += 1.0f;
        if (snow) update(snow, 1, Sky_SnowLevel());
        if (rain) update(rain, 1, Sky_GlowLevel());
    }
    RCamera *cam = g_render->camera;
    if (!((rain && rain->n) || (snow && snow->n)) || cam->v12c != 2) return;
    /* In camera space: the frame at the origin unturned (0x4660e0, 0x466140), no depth writes. */
    RFrame saved = cam->frame;
    memset(cam->frame.eye, 0, sizeof cam->frame.eye);
    memset(cam->frame.trans, 0, sizeof cam->frame.trans);
    static const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};   /* 0x5bb2d0 */
    memcpy(cam->frame.orient, identity, sizeof identity);
    memcpy(cam->frame.rot, identity, sizeof identity);
    RenderState *rs = g_render->device->rs;
    int zwrite = rs->state39, zfunc = rs->state28;
    RenderState_SetZWrite(rs, 0);
    RCamera_Setup(cam);
    if (snow) draw(snow);
    if (rain) draw(rain);
    RenderState_SetZWrite(rs, zwrite);
    RenderState_SetZFunc(rs, zfunc);
    cam->frame = saved;
    RCamera_Setup(cam);
}
