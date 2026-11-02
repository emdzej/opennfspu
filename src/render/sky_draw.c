/* The sky object (0x45d6c0, 100 bytes; the global at 0x627e48) and its passes: the update before the world
   (0x45cef0) and the draw (0x45d3b0) of the world draw's first sky call (0x45d690), the after-the-world pass
   (0x45d060 via 0x45d6b0). See sky.h. */
#include "render/sky.h"
#include "render/sky_sun.h"
#include "eagl/eagl.h"
#include "exe.h"
#include "game/track.h"
#include "pending.h"
#include "render/render.h"
#include "render/sky_levels.h"
#include "render/sky_tex.h"
#include "render/sky_text.h"
#include "game/car.h"
#include "game/audio.h"
#include "game/road.h"
#include "real/random.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The configuration's keyframe tracks along the road: keys {int32 node, ...} ending at the first negative
   node (at most 400), the current key and the next (the last key's next is the first: the track wraps
   round the road). Fewer than two keys: the first two set to defaults. */
typedef struct {
    uint8_t *keys;            /* +0x00 */
    SkyConfig *config;        /* +0x04 */
    int32_t n;                /* +0x08 */
    int32_t cur, next;        /* +0x0c, +0x10 */
    int32_t f14;
    uint8_t f18;
} SkyTrack;                   /* 0x45a180 (the weather's blend: {node, float}), 0x45a830 (the cover: {node, byte}) */

typedef struct {
    uint8_t *keys;            /* +0x00: 0x30 bytes a key */
    int32_t n;                /* +0x04 */
    int32_t cur, next;        /* +0x08, +0x0c */
    int32_t f10;
    SkyConfig *config;        /* +0x14 */
    uint8_t f18;
} SkyColourTrack;             /* 0x45a350: +4 the sky's fog density, +8 the world's, +0x10 the sky's fog
                                 colour, +0x14 the world's, +0x18, +0x1c the lightning, +0x20 */

typedef struct {
    SkyLevel *layer[10];      /* +0x00 */
    int32_t nlayers;          /* +0x28 */
    int32_t f2c;              /* +0x2c */
    float f30;                /* +0x30: 0.001 */
    SkyConfig *config;        /* +0x34 */
    float eye[3];             /* +0x38: the camera's eye of the last draw */
    SkyTrack *blend;          /* +0x44: the configuration's +0xddc */
    SkyColourTrack *colour;   /* +0x48: +0x1a5c */
    SkyTrack *cover;          /* +0x4c: +0x655c */
    int32_t region;           /* +0x50: the eye's node along the road (0x4812a0) */
    uint8_t fogset;           /* +0x54 */
    uint8_t pad55[3];
    int32_t f58;
    int32_t lasttick;         /* +0x5c */
    uint8_t f60;              /* +0x60: 1 */
} Sky;

/* The lightning (0x627e38, 0x24 bytes; 0x45cb00). */
typedef struct {
    uint8_t active;           /* +0x00 */
    int32_t ticks;            /* +0x04: the sky's ticks so far */
    int32_t start;            /* +0x08 */
    uint32_t end;             /* +0x0c */
    int32_t level;            /* +0x10: 0, 1, 2 by the storm's strength */
    int32_t f14;
    SkyConfig *config;        /* +0x18 */
    float x, y;               /* +0x1c, +0x20: the flash on the sky's 15 x 15 grid */
} SkyLightning;

static Sky *g_sky;            /* 0x627e48 */
static SkyLightning *g_lightning;   /* 0x627e38 */

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static float cfg_f(const SkyConfig *c, int off) { float f; memcpy(&f, c->data + off, 4); return f; }
static double exe_f64(uint32_t va)
{
    uint64_t u = (uint64_t)exe_u32(va + 4) << 32 | exe_u32(va);
    double d;
    memcpy(&d, &u, 8);
    return d;
}
/* 2 pi as the double at 0x5bb2c0 (fmul qword). */
static double two_pi(void) { return exe_f64(0x5bb2c0); }
static double sin_turns(float t) { return sin((double)t * two_pi()); }
static double cos_turns(float t) { return cos((double)t * two_pi()); }

static int32_t key_node(const uint8_t *keys, int stride, int k)
{
    int32_t v;
    memcpy(&v, keys + k * stride, 4);
    return v;
}
static float key_f(const uint8_t *keys, int stride, int k, int off)
{
    float v;
    memcpy(&v, keys + k * stride + off, 4);
    return v;
}
static uint32_t key_u(const uint8_t *keys, int stride, int k, int off)
{
    uint32_t v;
    memcpy(&v, keys + k * stride + off, 4);
    return v;
}
static void put_u(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

static int count_keys(const uint8_t *keys, int stride)
{
    int n = 0;
    while (key_node(keys, stride, n) >= 0 && ++n < 400) {}
    return n;
}

/* 0x45a180 */
static void SkyTrack_init(SkyTrack *t, uint8_t *keys, SkyConfig *c)
{
    t->keys = keys;
    t->config = c;
    t->f18 = 0;
    t->n = count_keys(keys, 8);
    if (t->n < 2) {
        put_u(keys, 0);
        put_u(keys + 8, 4);
        put_u(keys + 4, 0);
        put_u(keys + 0xc, 0x3f800000u);   /* 1.0 */
        t->n = 2;
    }
    t->f14 = 0;
    t->cur = 0;
    t->next = 1;
}

/* 0x45a830: as 0x45a180 with byte values. */
static void SkyTrack_initBool(SkyTrack *t, uint8_t *keys, SkyConfig *c)
{
    t->keys = keys;
    t->config = c;
    t->f18 = 0;
    t->n = count_keys(keys, 8);
    if (t->n < 2) {
        put_u(keys, 0);
        put_u(keys + 8, 4);
        keys[4] = 0;
        keys[0xc] = 0;
        t->n = 2;
    }
    t->f14 = 0;
    t->cur = 0;
    t->next = 1;
}

/* 0x45a350: twelve words a key; the defaults all zero but the second key's node, 4. */
static void SkyColourTrack_init(SkyColourTrack *t, uint8_t *keys, SkyConfig *c)
{
    t->keys = keys;
    t->config = c;
    t->f18 = 0;
    t->n = count_keys(keys, 0x30);
    if (t->n < 2) {
        memset(keys, 0, 0x60);
        put_u(keys + 0x30, 4);
        t->n = 2;
    }
    t->f10 = 0;
    t->cur = 0;
    t->next = 1;
}

/* The keys around node p: the current pair kept while p is in [cur, next), else searched from the
   first (0x45a1f0 / 0x45a400 step it). */
static void track_find(const uint8_t *keys, int stride, int n, int32_t *cur, int32_t *next, int p)
{
    if (p >= key_node(keys, stride, *cur) && p < key_node(keys, stride, *next)) return;
    *cur = 0;
    *next = n < 2 ? 0 : 1;
    if (n == 1 || n - 1 < 0) return;
    do {
        if (key_node(keys, stride, *cur) <= p && p < key_node(keys, stride, *next)) break;
        int k = *cur + 1;
        *cur = k;
        *next = k + 1 < n ? k + 1 : 0;
    } while (*cur < n - 1);
}

/* A node difference brought round the road: into half the road's length either way, then 0 .. length. */
static int wrap_nodes(int d, int len)
{
    if (d >= 1 && d > len / 2) d -= len;
    else if (d < 0 && d < -(len / 2)) d += len;
    if (d < 0) d += len;
    return d;
}

/* How far p is from the current key to the next, 0 .. 1; -1 when they are one key or at one node. */
static double track_frac(const uint8_t *keys, int stride, int cur, int next, int p)
{
    int a = key_node(keys, stride, cur), b = key_node(keys, stride, next);
    if (cur == next || a == b) return -1;
    int len = g_road ? g_road->nnodes : 1;
    return (double)wrap_nodes(p - a, len) / (double)wrap_nodes(b - a, len);
}

/* The blend's value at node p, interpolated between the keys. 0x45a210 */
static float SkyTrack_value(SkyTrack *t, int p)
{
    track_find(t->keys, 8, t->n, &t->cur, &t->next, p);
    double f = track_frac(t->keys, 8, t->cur, t->next, p);
    if (f < 0) return key_f(t->keys, 8, t->cur, 4);
    return (float)(f * (double)key_f(t->keys, 8, t->next, 4) + (1.0 - f) * (double)key_f(t->keys, 8, t->cur, 4));
}

/* Whether the current key at node p is set (the sky's layers covered). 0x45a8a0 */
static bool SkyTrack_flag(SkyTrack *t, int p)
{
    track_find(t->keys, 8, t->n, &t->cur, &t->next, p);
    return t->keys[t->cur * 8 + 4] != 0;
}

/* The current key's weight at node p (1 at the key, falling to 0 at the next); the accessors below mix
   the two keys' fields by it. 0x45a420 */
static float SkyColour_weight(SkyColourTrack *t, int p)
{
    track_find(t->keys, 0x30, t->n, &t->cur, &t->next, p);
    double f = track_frac(t->keys, 0x30, t->cur, t->next, p);
    return f < 0 ? 1.0f : (float)(1.0 - f);
}

static float colour_mix(const SkyColourTrack *t, float w, int off)
{
    return (float)((double)key_f(t->keys, 0x30, t->cur, off) * w + (1.0 - (double)w) * (double)key_f(t->keys, 0x30, t->next, off));
}
static float SkyColour_skyFog(const SkyColourTrack *t, float w)     /* 0x45a560: cubed */
{
    double v = (double)key_f(t->keys, 0x30, t->cur, 4) * w + (1.0 - (double)w) * (double)key_f(t->keys, 0x30, t->next, 4);
    return (float)(v * v * v);
}
static float SkyColour_worldFog(const SkyColourTrack *t, float w)   /* 0x45a5a0: cubed */
{
    double v = (double)key_f(t->keys, 0x30, t->cur, 8) * w + (1.0 - (double)w) * (double)key_f(t->keys, 0x30, t->next, 8);
    return (float)(v * v * v);
}
static float SkyColour_lightning(const SkyColourTrack *t, float w) { return colour_mix(t, w, 0x1c); }   /* 0x45a7d0 */

/* Two colours mixed by 16.16 weights, opaque. */
static uint32_t argb_mix(uint32_t a, uint32_t b, float w)
{
    int ia = (int)lrintf(w * 65536.0f), ib = 0x10000 - ia;   /* 0x5b3e68 */
    uint32_t r = 0xff000000u;
    for (int sh = 0; sh < 24; sh += 8)
        r |= ((((a >> sh & 0xff) * (uint32_t)ia) >> 16) + (((b >> sh & 0xff) * (uint32_t)ib) >> 16)) << sh;
    return r;
}

/* The sky's fog colour: the current key's alone when the next key has no fog (unless forced). 0x45a5e0 */
static uint32_t SkyColour_skyFogColour(const SkyColourTrack *t, float w, bool force)
{
    uint32_t a = key_u(t->keys, 0x30, t->cur, 0x10);
    if (key_f(t->keys, 0x30, t->next, 4) <= exe_f32(0x5b3e64) && !force) return a;
    return argb_mix(a, key_u(t->keys, 0x30, t->next, 0x10), w);
}

/* The world's fog colour, the same way by the world's density (never forced). 0x45a6c0 */
static uint32_t SkyColour_worldFogColour(const SkyColourTrack *t, float w)
{
    uint32_t a = key_u(t->keys, 0x30, t->cur, 0x14);
    if (exe_f32(0x5b3e64) < key_f(t->keys, 0x30, t->next, 8)) return argb_mix(a, key_u(t->keys, 0x30, t->next, 0x14), w);
    return a;
}

/* ---- the lightning (0x45cb00 .. 0x45cdb0) ---- */

static int32_t g_colourgrid[49][4];   /* 0x627e3c (0x310 bytes) */

/* The lightning's colours (0x45ca40): the sky file's 7 x 7 shape 'ltmp' as (a, r, g, b) cells, else all
   0xff. */
static void colourgrid_create(SkyTextures *t)
{
    int slot = t ? SkyTextures_find(t, exe_str(0x5cf110)) : -1;
    const uint8_t *shape = slot >= 0 ? SkyTextures_shape(t, slot) : NULL;
    if (!shape) {
        for (int i = 0; i < 49; i++) g_colourgrid[i][0] = g_colourgrid[i][1] = g_colourgrid[i][2] = g_colourgrid[i][3] = 0xff;
        return;
    }
    const uint8_t *p = shape + 0x10;
    for (int i = 0; i < 49; i++, p += 4) {
        g_colourgrid[i][0] = p[3];
        g_colourgrid[i][1] = p[2];
        g_colourgrid[i][2] = p[1];
        g_colourgrid[i][3] = p[0];
    }
}

/* The sky's (0x45dc40) glow level at the eye's node (0x45cec0): the colour keys' +0x18 mixed. 1 without
   a sky (the original has one in every race). */
float Sky_GlowLevel(void)
{
    if (!g_sky || !g_sky->colour) return 1.0f;
    float w = SkyColour_weight(g_sky->colour, g_sky->region);   /* 0x45a420 */
    return colour_mix(g_sky->colour, w, 0x18);                  /* 0x45a7a0 */
}

/* 0 without a sky. */
float Sky_SnowLevel(void)
{
    if (!g_sky || !g_sky->colour) return 0;
    float w = SkyColour_weight(g_sky->colour, g_sky->region);   /* 0x45a420 */
    return colour_mix(g_sky->colour, w, 0x20);                  /* 0x45a800 */
}

const int32_t *Sky_ColourCell(int a, int b) { return g_colourgrid[a * 7 + 0x18 + b]; }   /* 0x45cae0 */

bool Sky_LightningActive(void) { return g_lightning && g_lightning->active; }

/* 0x45cdb0: the record's index from the level's list (0x5cf0a0) at round(trunc(r * 3) - 0.5), which
   rounds to even: 0, 0 or 2. */
void Sky_LightningFlash(float pos[2], int32_t rec[5], int *level)
{
    SkyLightning *l = g_lightning;
    pos[0] = l->x;
    pos[1] = l->y;
    int k = (int)(REAL_randf() * 3.0f);   /* 0x5b2600; _ftol */
    int i = (int)lrintf((float)k - 0.5f);
    uint32_t list = exe_u32(0x5cf0a0 + 4 * (uint32_t)l->level);
    uint32_t r = exe_u32(list + 4 * (uint32_t)i);
    for (int w = 0; w < 5; w++) rec[w] = (int32_t)exe_u32(0x5cf018 + r * 0x14 + 4 * (uint32_t)w);
    *level = l->level;
}

static void lightning_create(SkyConfig *c)   /* 0x45cb00 */
{
    colourgrid_create(g_skytex);
    free(g_lightning);
    g_lightning = calloc(1, sizeof *g_lightning);
    if (!g_lightning) return;
    g_lightning->start = -1;
    g_lightning->end = 0xffffffffu;
    g_lightning->config = c;
}

static void lightning_free(void)   /* 0x45cb70 (the colour grid is static here) */
{
    free(g_lightning);
    g_lightning = NULL;
}

/* A strike (0x45cc50): it lasts 60 to 120 ticks; the flash at a random angle and distance by the level
   (0x5cf0c8 + 0x5cf0d4 * r, times half the configuration's +0x40), on the sky's grid (x 15 / +0x40),
   with a chance of one in ten of its thunder (0x4a8110). */
static void lightning_strike(SkyLightning *l)
{
    float size = cfg_f(l->config, 0x40), half = size * 0.5f;   /* 0x5b23f0 */
    float r = REAL_randf();
    l->start = l->ticks;
    l->end = (uint32_t)((int)lrintf((float)(((double)r + 1.0) * 60.0)) + l->ticks);   /* 0x5b26f0 */
    float span = exe_f32(0x5cf0d4 + 4 * (uint32_t)l->level), base = exe_f32(0x5cf0c8 + 4 * (uint32_t)l->level);
    float dist = (float)(((double)REAL_randf() * span + base) * half);
    float ang = REAL_randf();
    double c = cos_turns(ang), sn = sin_turns(ang);
    float vol = ang * 65535.0f;   /* 0x5b3ed0 */
    if (REAL_randf() < 0.1f)      /* 0x5b2604 */
        Audio_Thunder((int32_t)exe_u32(0x5cf104 + 4 * (uint32_t)l->level), (int)lrintf(vol));
    float x = ((half - (float)(sn * dist)) / size) * 15.0f;   /* 0x5b243c */
    float y = (((float)c * dist + half) / size) * 15.0f;
    l->x = x >= 15.0f ? 15.0f : x;
    if (l->x < 0) l->x = 0;
    l->y = y >= 15.0f ? 15.0f : y;
    if (l->y < 0) l->y = 0;
    l->active = 1;
}

/* Each sky update (0x45cbb0): the ticks counted; idle, a storm's strength from 0.04 up picks the level
   (0.33, 0.66) and a strike comes with a chance of ticks times the level's (0x5cf0b0); striking, it ends
   after its end tick. */
static void lightning_tick(SkyLightning *l, float strength, int ticks)
{
    int was = l->ticks;
    l->ticks = was + ticks;
    if (l->active) {
        if (l->end < (uint32_t)(was + ticks)) l->active = 0;
        return;
    }
    if (strength < 0.04f) return;   /* 0x5b2418 */
    l->level = strength < 0.33f ? 0 : strength < 0.66f ? 1 : 2;   /* 0x5b3ecc, 0x5b2520 */
    if ((double)REAL_randf() < (double)ticks * (double)exe_f32(0x5cf0b0 + 4 * (uint32_t)l->level)) lightning_strike(l);
}

/* 0x45d6c0 */
static Sky *Sky_ctor(Sky *s, const char *name)
{
    memset(s, 0, sizeof *s);
    s->f30 = 0.001f;   /* 0x3a83126f */
    s->f60 = 1;
    Sky_CreateConfig(name);                                  /* 0x4594d0 */
    s->config = Sky_Config();
    lightning_create(s->config);                             /* 0x45cb00 */
    Sun_Create(s->config);                                   /* 0x4600b0 */
    Sky_CreateLight(g_renderopts.skyrendering);             /* 0x459820 */
    if (s->config && s->config->data) {
        s->blend = malloc(sizeof *s->blend);
        if (s->blend) SkyTrack_init(s->blend, s->config->data + 0xddc, s->config);
        s->colour = malloc(sizeof *s->colour);
        if (s->colour) SkyColourTrack_init(s->colour, s->config->data + 0x1a5c, s->config);
        s->cover = malloc(sizeof *s->cover);
        if (s->cover) SkyTrack_initBool(s->cover, s->config->data + 0x655c, s->config);
    }
    SkyText text;
    Sky_LoadText(&text, name, g_renderopts.skyrendering);
    for (int i = 0; i < text.count; i++) {
        uint32_t f = SkyText_record(&text, i)->flags;
        /* The record's type picks the level (0x45acb0 basic 0x5c bytes; 0x45b460 multi 0x88 with mode 0..3
           for 4, 8, 0x10, 0x20; 0x45be40 free clouds 0xfc), a slot each (NULL when the allocation fails).
           The free clouds (type "freecloud": noise-textured cloud billboards) aren't ported: no sky text
           the game ships has one, so the port adds no slot for them. */
        if (f & 2) {
            SkyBasic *b = malloc(sizeof *b);
            if (b) SkyBasic_ctor(b, SkyText_record(&text, i), g_skytex, s->config, (f & 0x200) == 0x200);
            if (s->nlayers < 9) s->layer[s->nlayers++] = b ? &b->base : NULL;
        } else if (f & 0x3c) {
            int mode = f & 4 ? 0 : f & 8 ? 1 : f & 0x10 ? 2 : 3;
            SkyMulti *m = malloc(sizeof *m);
            if (m) SkyMulti_ctor(m, SkyText_record(&text, i), g_skytex, s->config, mode, (f & 0x200) == 0x200);
            if (s->nlayers < 9) s->layer[s->nlayers++] = m ? &m->base : NULL;
        }
    }
    SkyHorizon *h = malloc(sizeof *h);
    if (h) SkyHorizon_ctor(h, g_skytex, s->config);
    s->layer[s->nlayers++] = h ? &h->base : NULL;
    SkyText_free(&text);                                     /* 0x460030 */
    return s;
}

/* 0x45d9d0: the layers (their destructor slot), the three tracks (an empty destructor, 0x516210), then
   the light slot, the sun, the lightning and the configuration. */
static void Sky_dtor(Sky *s)
{
    for (int i = 0; i < 10; i++)
        if (s->layer[i]) s->layer[i]->vt->dtor(s->layer[i], 1);
    free(s->cover);
    free(s->colour);
    free(s->blend);
    Sky_FreeLight();    /* 0x459890 */
    Sun_Free();         /* 0x4600e0 */
    lightning_free();   /* 0x45cb70 */
    Sky_FreeConfig();   /* 0x459500 */
}

void Sky_Create(const char *name, bool reload)
{
    if (!reload || !g_skytex) {
        /* A new object for each race (the original doesn't free the last one). */
        g_skytex = malloc(sizeof *g_skytex);
        if (g_skytex) SkyTextures_ctor(g_skytex, Sky_Fsh());   /* 0x460910 */
    } else {
        SkyTextures_reload(g_skytex, Sky_Fsh());               /* 0x460b90 */
        if (g_sky) {
            Sky_dtor(g_sky);                                   /* 0x45d9d0 */
            free(g_sky);
        }
        g_sky = NULL;
    }
    Sky *s = malloc(sizeof *s);
    g_sky = s ? Sky_ctor(s, name) : NULL;
}

void Sky_Free(void)
{
    if (g_skytex) {
        SkyTextures_dtor(g_skytex);   /* 0x460970 */
        free(g_skytex);
    }
    g_skytex = NULL;
    if (g_sky) {
        Sky_dtor(g_sky);              /* 0x45d9d0 */
        free(g_sky);
    }
    g_sky = NULL;
    Sky_FreeFsh();
}

/* 0x45cef0: the elapsed ticks (half the simulation ticks since the last update, at least 1, at most 64),
   the eye's ground position turned by the two weather regions' wind angles (+0x3c, +0xb3c of the
   configuration), every layer's update, then the sky colour's (0x45a420, 0x45a7d0, 0x45cbb0). */
static void sky_update(Sky *s)
{
    int now = g_sim_tick;   /* 0x606ac4 */
    int ticks = 1;
    if (s->lasttick) {
        int d = now - s->lasttick;
        ticks = d == 1 ? 1 : d / 2;
        if (ticks > 0x40) ticks = 0x40;
    }
    s->lasttick = now;
    float blend = s->blend ? SkyTrack_value(s->blend, s->region) : 0;
    float a = -cfg_f(s->config, 0x3c), b = -cfg_f(s->config, 0xb3c);
    /* x, -, z (the original's [1] is a stack word never set) */
    float pa[3] = {0}, pb[3] = {0};
    pa[0] = (float)(sin_turns(a) * (double)s->eye[2] + (double)(float)(cos_turns(a) * (double)s->eye[0]));
    pa[2] = (float)((double)(float)(cos_turns(a) * (double)s->eye[2]) - sin_turns(a) * (double)s->eye[0]);
    pb[0] = (float)(sin_turns(b) * (double)s->eye[2] + (double)(float)(cos_turns(b) * (double)s->eye[0]));
    pb[2] = (float)((double)(float)(cos_turns(b) * (double)s->eye[2]) - sin_turns(b) * (double)s->eye[0]);
    for (int i = 0; i < s->nlayers; i++)
        if (s->layer[i]) s->layer[i]->vt->update(s->layer[i], pa, pb, blend, ticks);
    if (s->colour && g_lightning)
        lightning_tick(g_lightning, SkyColour_lightning(s->colour, SkyColour_weight(s->colour, s->region)), ticks);
}

/* The fog passes (0x45d1a0 the sky's, 0x45d270 the world's): with fog at this node, exponential fog of
   the density and colour; else off if it was on. */
static void sky_fog(Sky *s, bool world)
{
    if (!s->colour) return;
    RenderState *rs = g_render->device->rs;
    float w = SkyColour_weight(s->colour, s->region);
    float d = world ? SkyColour_worldFog(s->colour, w) : SkyColour_skyFog(s->colour, w);
    if (exe_f32(0x5b3e64) < d) {
        RenderState_SetFogMode(rs, 4);
        RenderState_SetFogMode(rs, 1);
        s->fogset = 1;
        RenderState_SetFogDensity(rs, d);
        RenderState_SetFogColour(rs, world ? SkyColour_worldFogColour(s->colour, w) : SkyColour_skyFogColour(s->colour, w, false));
    } else if (s->fogset) {
        RenderState_SetFogMode(rs, 0);
        s->fogset = 0;
    }
}

uint32_t Sky_ClearColour(bool mirror)
{
    /* 0x459940: the light slot's two colours (+8 .. +0x10 and +0x14 .. +0x1c floats; the mirror's
       record 0x1c on) mixed by the weather's blend; a record with +4 set takes the sky's fog colour
       instead (0x45d170). */
    const uint8_t *slot = Sky_LightSlot();
    Sky *s = g_sky;
    if (!slot || !s) return 0xff000000u;
    const uint8_t *r = slot + (mirror ? 0x1c : 0);
    int32_t own;
    memcpy(&own, r + 4, 4);
    if (own) return s->colour ? SkyColour_skyFogColour(s->colour, SkyColour_weight(s->colour, s->region), true) : 0xff000000u;
    float f = s->blend ? SkyTrack_value(s->blend, s->region) : 0, g = 1.0f - f;
    float a[6];
    memcpy(a, r + 8, sizeof a);
    uint32_t out = 0xff000000u;
    for (int k = 0; k < 3; k++) out |= ((uint32_t)lrintf(f * a[3 + k] + g * a[k]) & 0xff) << (16 - 8 * k);
    return out;
}

/* A layer's draw (0x45d330): the layers flagged 0x80 when the view draws them (+0x11c), the others in the
   main view (+0x12c == 2) unless the region hides them. */
static void draw_layer(Sky *s, RCamera *c, int i, float heading, float elevation)
{
    SkyLevel *l = s->layer[i];
    if (!l) return;
    if ((l->flags & 0x80) && c->flags[4] == 1) {
        l->vt->draw(l, heading);
        return;
    }
    if (c->v12c == 2 && s->f60 == 1) {
        if (exe_f64(0x5b3ed8) <= (double)elevation && s->cover && SkyTrack_flag(s->cover, s->region)) return;
        l->vt->draw(l, heading);
    }
}

/* The angle between two vectors, in turns (0x532850). */
static float angle_turns(const float a[3], const float b[3])
{
    float la = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]), lb = sqrtf(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    float d = (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / (la * lb);
    return (float)(acos((double)d) * (double)exe_f32(0x5bb2f4));
}

static void set_nearfar(RCamera *c, float znear, float zfar)   /* 0x467060 */
{
    c->zfar = zfar;
    c->znear = znear;
    EAGL_ViewSetPerspective(c->view, c->halffov + c->halffov,
                            ((c->x1 - c->x0) * 640.0f) / ((c->y1 - c->y0) * 480.0f), znear, zfar);
    if (c->active) EAGL_ContextViewport(g_eagl);
}

/* 0x45d3b0 */
static void sky_draw(Sky *s)
{
    RCamera *c = g_render->camera;
    float znear = c->znear, zfar = c->zfar;
    set_nearfar(c, 0.15f, 700.0f);   /* 0x3e19999a, 0x442f0000 */
    RenderState *rs = g_render->device->rs;
    int s39 = rs->state39, s28 = rs->state28;
    rs->state39 = 0;
    EAGL_RSSet(0x39, 0);
    rs->state28 = 7;
    EAGL_RSSet(0x28, 7);
    memcpy(s->eye, c->frame.eye, sizeof s->eye);
    if (g_road) s->region = Road_NearestNode(g_road, s->eye, s->region);   /* 0x4812a0 */
    /* The heading: the forward row's angle to +z, in turns, the other way round for negative x. */
    float fwd[3] = {c->frame.orient[6], c->frame.orient[7], c->frame.orient[8]};
    float f2[3] = {fwd[0], fwd[1], fwd[2]};
    if (exe_f32(0x5b27b4) < fwd[2] || fwd[2] < exe_f32(0x5b3ee0) || exe_f32(0x5b27b4) < fwd[0]) f2[1] = 0;
    static const float z[3] = {0, 0, 1};
    float heading = angle_turns(z, f2);
    if (f2[0] < exe_f32(0x5b23f8)) heading = exe_f32(0x5b24a8) - heading;
    /* The sky around the eye: the frame's eye at the origin for its draw. */
    float eye[3];
    memcpy(eye, c->frame.eye, sizeof eye);
    memset(c->frame.eye, 0, sizeof c->frame.eye);
    RCamera_Setup(c);
    THRASH.fn.setstate(0x24, 10);
    sky_fog(s, false);   /* 0x45d1a0 */
    for (int i = 0; i < s->nlayers; i++)
        if (s->layer[i] && (s->layer[i]->flags & 0x400)) draw_layer(s, c, i, heading, fwd[1]);
    if (g_sun) Sun_Draw(g_sun);                              /* 0x460150 */
    for (int i = 0; i < s->nlayers; i++)
        if (s->layer[i] && !(s->layer[i]->flags & 0x400)) draw_layer(s, c, i, heading, fwd[1]);
    /* (0x516210 twice: an empty function) */
    rs->state39 = s39;
    EAGL_RSSet(0x39, (uint32_t)s39);
    rs->state28 = s28;
    EAGL_RSSet(0x28, (uint32_t)s28);
    memcpy(c->frame.eye, eye, sizeof eye);
    RCamera_Setup(c);
    sky_fog(s, true);    /* 0x45d270 */
    set_nearfar(c, znear, zfar);
}

void Sky_ToggleHorizon(void)
{
    Sky *s = g_sky;   /* 0x45dc40 */
    if (!s) return;
    if (s->f60 == 0) {
        s->f60 = 1;          /* 0x45da50 */
        Sky_SetLight(true);
    } else if (s->f60 == 1) {
        s->f60 = 0;          /* 0x45da70 */
        Sky_SetLight(false);
    }
}

void Sky_DrawBefore(void)   /* 0x45d690 */
{
    if (!g_sky) return;
    sky_update(g_sky);
    sky_draw(g_sky);
}

/* 0x45d060: the lens flare around the eye, as the sky was drawn (the render states of sky_draw). */
static void sky_after(Sky *s)
{
    if (!g_sun || !g_sun->enabled) return;
    RCamera *c = g_render->camera;
    RenderState *rs = g_render->device->rs;
    int s39 = rs->state39, s28 = rs->state28;
    rs->state39 = 0;
    EAGL_RSSet(0x39, 0);
    rs->state28 = 7;
    EAGL_RSSet(0x28, 7);
    RFrame f = c->frame;
    memcpy(s->eye, c->frame.eye, sizeof s->eye);
    memset(c->frame.eye, 0, sizeof c->frame.eye);    /* 0x4660e0 */
    memset(c->frame.trans, 0, sizeof c->frame.trans);
    THRASH.fn.setstate(0x24, 10);
    RCamera_Setup(c);
    Sun_DrawFlare(g_sun);                             /* 0x460320 */
    rs->state39 = s39;
    EAGL_RSSet(0x39, (uint32_t)s39);
    rs->state28 = s28;
    EAGL_RSSet(0x28, (uint32_t)s28);
    c->frame = f;
    RCamera_Setup(c);
}

void Sky_DrawAfter(void)    /* 0x45d6b0 -> 0x45d060 */
{
    if (g_sky) sky_after(g_sky);
}
