/* The sky object (0x45d6c0, 100 bytes; the global at 0x627e48) and its passes: the update before the world
   (0x45cef0) and the draw (0x45d3b0) of the world draw's first sky call (0x45d690), the after-the-world pass
   (0x45d060 via 0x45d6b0). See sky.h. */
#include "render/sky.h"
#include "eagl/eagl.h"
#include "exe.h"
#include "game/track.h"
#include "pending.h"
#include "render/render.h"
#include "render/sky_levels.h"
#include "render/sky_tex.h"
#include "render/sky_text.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    SkyLevel *layer[10];      /* +0x00 */
    int32_t nlayers;          /* +0x28 */
    int32_t f2c;              /* +0x2c */
    float f30;                /* +0x30: 0.001 */
    SkyConfig *config;        /* +0x34 */
    float eye[3];             /* +0x38: the camera's eye of the last draw */
    void *tracks[3];          /* +0x44: the configuration's keyframe tracks (+0xddc, +0x1a5c, +0x655c) */
    int32_t region;           /* +0x50: the eye's place along the track (0x4812a0) */
    uint8_t fogset;           /* +0x54 */
    uint8_t pad55[3];
    int32_t f58;
    int32_t lasttick;         /* +0x5c */
    uint8_t f60;              /* +0x60: 1 */
} Sky;

static Sky *g_sky;            /* 0x627e48 */

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static float cfg_f(const SkyConfig *c, int off) { float f; memcpy(&f, c->data + off, 4); return f; }
/* 2 pi as the double at 0x5bb2c0 (fmul qword). */
static double two_pi(void)
{
    uint32_t lo = exe_u32(0x5bb2c0), hi = exe_u32(0x5bb2c4);
    uint64_t u = (uint64_t)hi << 32 | lo;
    double d;
    memcpy(&d, &u, 8);
    return d;
}
static double sin_turns(float t) { return sin((double)t * two_pi()); }
static double cos_turns(float t) { return cos((double)t * two_pi()); }

/* 0x45d6c0 */
static Sky *Sky_ctor(Sky *s, const char *name)
{
    memset(s, 0, sizeof *s);
    s->f30 = 0.001f;   /* 0x3a83126f */
    s->f60 = 1;
    Sky_CreateConfig(name);                                  /* 0x4594d0 */
    s->config = Sky_Config();
    PENDING_NOTE(0x45cb00, "the sky's colour objects (0x627e3c, 0x627e38)");
    PENDING_NOTE(0x4600b0, "the sun and its lens flare (0x627e5c)");
    Sky_CreateLight(g_renderopts.skyrendering);             /* 0x459820 */
    PENDING_NOTE(0x45a180, "the sky's keyframe tracks (0x45a180, 0x45a350, 0x45a830)");
    SkyText text;
    Sky_LoadText(&text, name, g_renderopts.skyrendering);
    for (int i = 0; i < text.count; i++) {
        uint32_t f = SkyText_record(&text, i)->flags;
        /* The record's type picks the level (0x45acb0 basic 0x5c bytes; 0x45b460 multi 0x88 with mode 0..3
           for 4, 8, 0x10, 0x20; 0x45be40 free clouds 0xfc). Not ported yet: the original adds a slot for
           each (NULL when the allocation fails); the port adds nothing until they are. */
        if (f & 2) PENDING_NOTE(0x45acb0, "SkyLevel_BasicSky");
        else if (f & 0x3c) PENDING_NOTE(0x45b460, "SkyLevel_MultiSky");
        else if (f & 0x40) PENDING_NOTE(0x45be40, "SkyLevel_FreeClouds");
    }
    SkyHorizon *h = malloc(sizeof *h);
    if (h) SkyHorizon_ctor(h, g_skytex, s->config);
    s->layer[s->nlayers++] = h ? &h->base : NULL;
    SkyText_free(&text);                                     /* 0x460030 */
    return s;
}

void Sky_Create(const char *name, bool reload)
{
    if (!reload || !g_skytex) {
        /* A new object for each race (the original doesn't free the last one). */
        g_skytex = malloc(sizeof *g_skytex);
        if (g_skytex) SkyTextures_ctor(g_skytex, Sky_Fsh());   /* 0x460910 */
    } else {
        SkyTextures_reload(g_skytex, Sky_Fsh());               /* 0x460b90 */
        PENDING_NOTE(0x45d9d0, "the old sky object's destructor on a reload");
    }
    Sky *s = malloc(sizeof *s);
    g_sky = s ? Sky_ctor(s, name) : NULL;
}

/* 0x45cef0: the elapsed ticks (half the simulation ticks since the last update, at least 1, at most 64),
   the eye's ground position turned by the two weather regions' wind angles (+0x3c, +0xb3c of the
   configuration), every layer's update, then the sky colour's (0x45a420, 0x45a7d0, 0x45cbb0). */
static void sky_update(Sky *s)
{
    int now = 0;   /* the simulation's tick counter (0x606ac4): the race loop isn't ported yet */
    int ticks = 1;
    if (s->lasttick) {
        int d = now - s->lasttick;
        ticks = d == 1 ? 1 : d / 2;
        if (ticks > 0x40) ticks = 0x40;
    }
    s->lasttick = now;
    PENDING_NOTE(0x45a210, "the weather regions' blend along the track (keyframe track 0)");
    float blend = 0;
    float a = -cfg_f(s->config, 0x3c), b = -cfg_f(s->config, 0xb3c);
    float pa[2], pb[2];
    pa[0] = (float)(sin_turns(a) * (double)s->eye[2] + (double)(float)(cos_turns(a) * (double)s->eye[0]));
    pa[1] = (float)((double)(float)(cos_turns(a) * (double)s->eye[2]) - sin_turns(a) * (double)s->eye[0]);
    pb[0] = (float)(sin_turns(b) * (double)s->eye[2] + (double)(float)(cos_turns(b) * (double)s->eye[0]));
    pb[1] = (float)((double)(float)(cos_turns(b) * (double)s->eye[2]) - sin_turns(b) * (double)s->eye[0]);
    for (int i = 0; i < s->nlayers; i++)
        if (s->layer[i]) s->layer[i]->vt->update(s->layer[i], pa, pb, blend, ticks);
    PENDING_NOTE(0x45cbb0, "the sky colour (0x45a420, 0x45a7d0, 0x45cbb0)");
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
        if (exe_f32(0x5b3ed8) <= elevation) PENDING_NOTE(0x45a8a0, "the sky regions' cover test");
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
    PENDING_NOTE(0x4812a0, "the eye's place along the track (the sky's region)");
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
    PENDING_NOTE(0x45d1a0, "the sky's fog (0x45d1a0)");
    for (int i = 0; i < s->nlayers; i++)
        if (s->layer[i] && (s->layer[i]->flags & 0x400)) draw_layer(s, c, i, heading, fwd[1]);
    PENDING_NOTE(0x460150, "the sun and the lens flare");
    for (int i = 0; i < s->nlayers; i++)
        if (s->layer[i] && !(s->layer[i]->flags & 0x400)) draw_layer(s, c, i, heading, fwd[1]);
    PENDING_NOTE(0x516210, "0x516210 (twice) after the sky");
    rs->state39 = s39;
    EAGL_RSSet(0x39, (uint32_t)s39);
    rs->state28 = s28;
    EAGL_RSSet(0x28, (uint32_t)s28);
    memcpy(c->frame.eye, eye, sizeof eye);
    RCamera_Setup(c);
    PENDING_NOTE(0x45d270, "the world's fog after the sky (0x45d270)");
    set_nearfar(c, znear, zfar);
}

void Sky_DrawBefore(void)   /* 0x45d690 */
{
    if (!g_sky) return;
    sky_update(g_sky);
    sky_draw(g_sky);
}

void Sky_DrawAfter(void)    /* 0x45d6b0 -> 0x45d060 */
{
    if (g_sky) PENDING_NOTE(0x45d060, "the sky's after-the-world pass (0x460320)");
}
