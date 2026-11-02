/* The front end's 3D objects and rooms (fe3d.h). */
#include "front/fe3d.h"
#include "render/carlight.h"
#include "front/fe_parts.h"
#include "front/femovie.h"
#include "app.h"
#include "exe.h"
#include "real/window.h"
#include "game/sim_math.h"
#include "render/camera_math.h"
#include "render/carpages.h"
#include "render/render.h"
#include "render/rtex.h"
#include "render/r3dcar.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TIMER_ticks 0x53c050: the real library's 128 Hz count (0x5deb48 the rate). */
enum { TICKS_HZ = 128 };
static int32_t ticks(void) { return (int32_t)((uint64_t)app_ticks_ms() * TICKS_HZ / 1000u); }

/* ---------------------------------------------------------------- FE3DObject */

FE3DObject *FE3DObject_New(int kind, const char *name, const uint32_t colours[4], uint32_t scheme,
                           int racenumber, int style, const int *options)
{
    FE3DObject *o = calloc(1, sizeof *o);
    if (!o) return NULL;
    o->kind = kind;
    snprintf(o->name, sizeof o->name, "%s", name);
    R3DCar_LoadCommon();
    static const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};   /* 0x5bb2d0 */
    memcpy(o->state.rot, identity, sizeof identity);
    o->state.style = style;                                         /* +0x568 */
    for (int i = 0; i < CAR_NOPTIONS; i++)                          /* +0x32c: option 10 always the style's */
        o->spec.options[i] = options && i != 10 ? options[i] : -1;
    o->spec.colours[0] = colours[0];   /* +0x218, +0x224, +0x21c, +0x220 as 0x4d0bb0 stores its arguments */
    o->spec.colours[3] = colours[1];
    o->spec.colours[1] = colours[2];
    o->spec.colours[2] = colours[3];
    o->spec.racenumber = racenumber;   /* +0x510 */
    o->spec.scheme = scheme;
    o->state.spec = &o->spec;
    o->state.damage = o->damage;
    R3DCar_Init(&o->car, &o->state);   /* R3DCar_ctor(state, 0x32, 0x14, 3) */
    /* (a room's 20 flags at +0x2d4 set, a car's +0x700: not read by the ported draw) */
    /* The colours before the load, as the race's CarSpecs_Load does: the port composes the pages with them
       while loading (the original sets them after, 0x443ce0, and recomposes, 0x442c80). */
    R3DCar_SetColours(&o->car, scheme);
    char path[128];
    snprintf(path, sizeof path, "%s%s", kind == 0 ? "GameData\\CarModel\\" : "FEData\\Models\\", name);   /* "%s%s" 0x5cc588 */
    R3DCar_LoadCrp(&o->car, path);
    o->car.curlod = 0;   /* +0x2c0 */
    o->state.pos[0] = 0;
    o->state.pos[1] = o->state.halfsize[1];
    o->state.pos[2] = 0;
    memcpy(o->state.rot, identity, sizeof identity);
    return o;
}

void FE3DObject_Free(FE3DObject *o)
{
    if (!o) return;
    R3DCar_Free(&o->car);
    free(o);
}

void FE3DObject_SetPos(FE3DObject *o, float x, float y, float z)
{
    o->state.pos[0] = x;
    o->state.pos[1] = y + o->state.halfsize[1];
    o->state.pos[2] = z;
}

void FE3DObject_RotY(FE3DObject *o, float turns) { Mat3_RotY(o->state.rot, turns); }

void FE3DObject_RotX(FE3DObject *o, float turns)
{
    if (turns == 0) return;
    float r[9], a[9];
    memcpy(a, o->state.rot, sizeof a);
    Mat3_RotX(r, turns);
    Mat3_Mul(a, r, o->state.rot);
}

void FE3DObject_RotZ(FE3DObject *o, float turns)
{
    if (turns == 0) return;
    float r[9], a[9];
    memcpy(a, o->state.rot, sizeof a);
    Mat3_RotZ(r, turns);
    Mat3_Mul(a, r, o->state.rot);
}

/* Port deviation: with the original's matrix (R3DObject_Draw 0x4433c0) the port draws the models upside
   down and mirrored, and where the original makes up for its y is not found yet. As the race's cars
   (drive_selftest.c draw_cars): a model point (x, y, z) goes to the object's (-x, y, -z), which is where
   the models' own data puts the driver against the driver's eye; the rows with their y column negated
   (R3DCar_Draw's last scale puts it back), the place so that the centre comes out as the original's
   (pos - up * half height), and the cull flipped back around the draw (the result is no mirror). */
void FE3DObject_Draw(FE3DObject *o, EaglMatStack *model)
{
    R3DCarState *st = &o->state;
    float rot[9], pos[3];
    memcpy(rot, st->rot, sizeof rot);
    memcpy(pos, st->pos, sizeof pos);
    for (int k = 0; k < 9; k++) st->rot[k] = k % 3 == 1 ? -rot[k] : rot[k];
    float h = st->halfsize[1];
    for (int i = 0; i < 3; i++) st->pos[i] = pos[i] - rot[3 + i] * h + st->rot[3 + i] * h;
    Render_SetCull(g_render);
    CarAnim_Update(&o->car, NULL);   /* (0x4d1030's 0x442fd0: slot 9, R3DCar_Animate) */
    R3DCar_Dents(&o->car, false);    /* (and after its draw slot 7, R3DCar_v7 0x4382f0: here before, as */
    R3DCar_ApplyMorphs(&o->car);     /*  the race's cars) */
    R3DCar_Draw(&o->car, model);
    Render_SetCull(g_render);
    memcpy(st->rot, rot, sizeof rot);
    memcpy(st->pos, pos, sizeof pos);
}

static void hinge(FE3DObject *o, int kind, float x, float y, float z)   /* 0x443e60 */
{
    if (!o->car.hinge) return;
    o->car.hinge[kind][0] = x;
    o->car.hinge[kind][1] = y;
    o->car.hinge[kind][2] = z;
}

/* ---------------------------------------------------------------- FE3DRoom */

typedef struct {
    char id[32];
    FE3DObject *model;        /* +0x2ec (FESpinning3DRoom) */
    FE3DObject *car;          /* +0x26c */
    float spin;               /* +0x270: seconds a turn (0: still) */
    float pos[3];             /* +0x278 */
    float rot[3];             /* +0x284: x, y, z (turns) */
    float fov;                /* +0x290: the camera's half angle */
    float ambient[3];         /* +0x294 (0..255) */
    float diffuse[3];         /* +0x2a0 */
    float dir[3];             /* +0x2b8 */
    float zoom;               /* +0x2e8: towards the close-up place */
    int hinge_axis;           /* the tick's hinges 1 and 2 (_v1): the garage's third component, the factory's
                                 first, -1 none (the victory room's) */
    /* the dealership's (FEDealership3DRoom 0x4e8090) */
    bool dealer;
    FE3DObject *seg[3];       /* +0x2ec .. +0x2f4: dealer2a, dealer2b, dealer2c */
    float step[2];            /* +0x2d4, +0x2d8: one car's place to the next (x, z) */
    int index;                /* +0x30c: the dealer car shown */
    int want, count;          /* the dealer accessor's (+0x2fc) position (v0x28) and its cars (v0x24) */
    FE3DObject *oldcar;       /* +0x2f8: the car slid away from */
    bool sliding;             /* +0x308 */
    int prev;                 /* +0x310: the car slid from */
    int32_t slide_start;      /* +0x314 */
    char shown[64];           /* +0x32c: the car shown at the last tick, its colours (+0x324, +0x320 ..) */
    uint32_t showncol[4];
    bool havecol;
    int slide_len;            /* +0x318: 256 ticks */
    float slide;              /* +0x31c: 0 .. 1 */
    /* the track select's (FETrackSelect3DRoom 0x4e9a30) */
    bool track;
    FE3DObject *wall;         /* +0x318: trackwall (the model +0x314 is "track") */
    float from[4], to[4];     /* +0x324, +0x330: the place (x y z) and x turn it zooms between */
    int32_t zoom_at;          /* +0x320: the tick the zoom started (-1 not yet) */
    /* the victory room's */
    FEMovie *movie;           /* +0x2f0 */
    bool garage;              /* FEGarage3DRoom (its v15's shadow) */
} FE3DRoom;

static FE3DRoom *s_room;
static RCamera *s_camera;     /* the application's +0xfc */
static char s_carfile[64];

/* The base room (0x4e6d60) and the garage's own values (0x4e91d0). */
static FE3DRoom *garage_new(void)
{
    FE3DRoom *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    snprintf(r->id, sizeof r->id, "ID_GARAGE");
    r->spin = 30.0f;
    r->pos[0] = 0;
    r->pos[1] = -1.1f;
    r->pos[2] = 4.8f;
    r->fov = 37.5f;
    r->ambient[0] = 55.0f;
    r->ambient[1] = 65.0f;
    r->ambient[2] = 55.0f;
    r->diffuse[0] = r->diffuse[1] = r->diffuse[2] = 120.0f;
    r->dir[0] = -0.14f;
    r->dir[1] = -0.98f;
    r->dir[2] = 0.114f;
    static const uint32_t none[4] = {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};
    r->hinge_axis = 2;
    r->model = FE3DObject_New(2, "garage", none, 0, -1, 0, NULL);   /* 0x4e9570 */
    FE3DObject_SetPos(r->model, r->pos[0], r->pos[1], r->pos[2]);
    r->garage = true;
    return r;
}

/* The factory (0x4e8f20) and the victory room (FEVictory3DRoom_Load 0x4ea860): spinning rooms as the
   garage, their own places and light. */
static FE3DRoom *spinning_new(const char *id, const char *name, float z, float fov, const float amb[3],
                              const float dif[3], float dy, float dz, int axis)
{
    FE3DRoom *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    snprintf(r->id, sizeof r->id, "%s", id);
    r->spin = 30.0f;
    r->pos[1] = -1.1f;
    r->pos[2] = z;
    r->fov = fov;
    memcpy(r->ambient, amb, sizeof r->ambient);
    memcpy(r->diffuse, dif, sizeof r->diffuse);
    r->dir[1] = dy;
    r->dir[2] = dz;
    r->hinge_axis = axis;
    static const uint32_t none[4] = {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};
    r->model = FE3DObject_New(2, name, none, 0, -1, 0, NULL);   /* 0x4e9570 */
    FE3DObject_SetPos(r->model, r->pos[0], r->pos[1], r->pos[2]);
    return r;
}

static FE3DRoom *factory_new(void)
{
    static const float amb[3] = {110.0f, 120.0f, 110.0f}, dif[3] = {50.0f, 50.0f, 50.0f};
    return spinning_new("ID_FACTORY", "factory", 4.8f, 37.5f, amb, dif, -0.99f, 0.1f, 0);
}

static FE3DRoom *victory_new(void)
{
    static const float amb[3] = {75.0f, 75.0f, 75.0f}, dif[3] = {110.0f, 110.0f, 110.0f};
    FE3DRoom *r = spinning_new("ID_VICTORY", "victory", 5.2f, 32.0f, amb, dif, -0.98f, 0.2f, -1);
    if (!r) return NULL;
    /* Its movie, without sound or a texture of its own, played onto the model's screen. The original picks
       the era's film by the career's year (0x502870) against the eras' (0x501f40 of 0x20b, 0x20a, 0x209:
       er4pc-av, er3pc-av, er2pc-av), then always writes "%ser1pc-av.mad" over the name: every victory
       shows the classic era's film. Kept. */
    char path[100];
    snprintf(path, sizeof path, exe_str(0x5da670), FE_DIR_MOVIES);
    r->movie = FEMovie_Load(path, 0, 0, 0x100, 0x100, false, true);
    return r;
}

/* The dealership (FEDealership3DRoom_Load 0x4e8090): three segments of showroom, one per car of the
   dealer's list, side by side a step (10.53, -2.1) apart, the chosen car in the middle one turning. */
static FE3DRoom *dealer_new(void)
{
    FE3DRoom *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    snprintf(r->id, sizeof r->id, "ID_DEALER");
    r->dealer = true;
    r->spin = 30.0f;
    r->pos[1] = -1.1f;
    r->pos[2] = 4.8f;
    r->fov = 37.5f;
    for (int i = 0; i < 3; i++) r->ambient[i] = r->diffuse[i] = 110.0f;
    r->dir[1] = -0.99f;
    r->dir[2] = 0.15f;
    r->step[0] = 10.53f;
    r->step[1] = -2.1f;
    static const uint32_t none[4] = {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};
    static const char *const names[3] = {"dealer2a", "dealer2b", "dealer2c"};
    for (int i = 0; i < 3; i++) r->seg[i] = FE3DObject_New(2, names[i], none, 0, -1, 0, NULL);
    r->model = r->seg[0];
    return r;
}

static void light(FE3DRoom *r, FE3DObject *o, bool car);

/* The track select (FETrackSelect3DRoom_Load 0x4e9a30): the models "track" and "trackwall", the place
   zooming in from (0, -4.8, 6.2) to (0, -3.2, 1.9) over 300 ticks once the screen is up (0x4e9d00). */
static FE3DRoom *track_new(void)
{
    FE3DRoom *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    snprintf(r->id, sizeof r->id, "ID_TRACK");
    r->track = true;
    r->spin = 20.0f;
    r->fov = 25.0f;
    r->ambient[0] = 55.0f, r->ambient[1] = 65.0f, r->ambient[2] = 76.0f;
    r->diffuse[0] = r->diffuse[1] = r->diffuse[2] = 115.0f;
    r->dir[0] = -0.29f, r->dir[1] = -1.5f, r->dir[2] = 0.29f;
    const float from[4] = {0, -4.8f, 6.2f, -0.04f}, to[4] = {0, -3.2f, 1.9f, 0};
    memcpy(r->from, from, sizeof from);
    memcpy(r->to, to, sizeof to);
    memcpy(r->pos, from, 12);
    r->rot[0] = from[3];
    r->zoom_at = -1;
    static const uint32_t none[4] = {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};
    r->model = FE3DObject_New(2, "track", none, 0, -1, 0, NULL);
    r->wall = FE3DObject_New(2, "trackwall", none, 0, -1, 0, NULL);
    return r;
}

/* 0x4e9d00: the zoom in; the track model and the wall four times a quarter turn apart, hinges 13 and 14
   turning (a 128th of a turn a tick); both lit by the room. */
static void track_draw(FE3DRoom *r, EaglMatStack *model)
{
    int32_t t = ticks();
    if (r->zoom_at < 0) r->zoom_at = t;   /* (0x4e09c0: the screen up) */
    int dt = t - r->zoom_at;
    if (dt > 300) dt = 300;
    float k = (float)dt * 0.0033333334f, j = 1.0f - k;
    for (int i = 0; i < 3; i++) r->pos[i] = k * r->to[i] + j * r->from[i];
    r->rot[0] = k * r->to[3] + j * r->from[3];
    light(r, r->model, false);
    light(r, r->wall, false);
    hinge(r->model, 13, (float)t * 0.0078125f, 0, 0);
    hinge(r->model, 14, (float)t * 0.0078125f, 0, 0);
    FE3DObject *objs[5] = {r->model, r->wall, r->wall, r->wall, r->wall};
    static const float turn[5] = {0, 0, 0.25f, 0.5f, 0.75f};
    for (int i = 0; i < 5; i++) {
        FE3DObject *o = objs[i];
        if (!o) continue;
        FE3DObject_SetPos(o, r->pos[0], r->pos[1], r->pos[2]);
        FE3DObject_RotY(o, r->rot[1] + turn[i]);
        FE3DObject_RotX(o, r->rot[0]);
        FE3DObject_RotZ(o, r->rot[2]);
        FE3DObject_Draw(o, model);
    }
}

static void room_free(FE3DRoom *r)
{
    if (!r) return;
    if (r->track) FE3DObject_Free(r->wall);
    if (r->dealer) {
        FE3DObject_Free(r->seg[1]);
        FE3DObject_Free(r->seg[2]);
        FE3DObject_Free(r->oldcar);
    }
    FEMovie_Free(r->movie);   /* FEVictory3DRoom_v0 0x4eaa30 */
    FE3DObject_Free(r->model);
    FE3DObject_Free(r->car);
    free(r);
}

/* An object's light block from the room's colours, or the car's fixed light (0x4e7b60). */
static void light(FE3DRoom *r, FE3DObject *o, bool car)
{
    if (!o) return;
    float fade = 1.0f;   /* 1 - the room's fade (0x4e0930) / 0x200 */
    CarLight *l = &o->car.light;
    float *f = l->f;   /* (the rest as it was: the garage's shadow record, +0x14c, stays) */
    f[0x8c / 4] = f[0x90 / 4] = f[0x94 / 4] = 1.0f;
    f[0xcc / 4] = 10.0f;
    f[0x98 / 4] = f[0x9c / 4] = 1.0f;
    f[0xa0 / 4] = f[0xa4 / 4] = f[0xa8 / 4] = 0.2f;
    f[0xac / 4] = 1.0f;
    f[0xb0 / 4] = f[0xb4 / 4] = f[0xb8 / 4] = 0.6f;
    f[0xbc / 4] = f[0xc0 / 4] = f[0xc4 / 4] = f[0xc8 / 4] = 1.0f;
    f[0x18 / 4] = 1.0f;
    f[0x58 / 4] = 1.0f;
    if (!car) {
        for (int i = 0; i < 3; i++) {
            f[0x1c / 4 + i] = fade * r->ambient[i] * (1.0f / 255.0f);   /* 0x5b3888 */
            f[0x5c / 4 + i] = fade * r->diffuse[i] * (1.0f / 255.0f);
        }
        float n = (float)(1.0 / sqrt(r->dir[2] * r->dir[2] + r->dir[1] * r->dir[1] + r->dir[0] * r->dir[0]));
        for (int i = 0; i < 3; i++) {
            r->dir[i] *= n;   /* (kept: the room's direction is normalised in place) */
            f[0x48 / 4 + i] = r->dir[i];
        }
    } else {
        for (int i = 0; i < 3; i++) {
            f[0x1c / 4 + i] = fade * 0.3f;   /* 0x5b25f0 */
            f[0x5c / 4 + i] = fade * 0.7f;   /* 0x5b2618 */
        }
        f[0x48 / 4] = 0;
        f[0x4c / 4] = -0.70700002f;   /* 0xbf34fdf4 */
        f[0x50 / 4] = 0.70700002f;
    }
    f[0x54 / 4] = 0;
    ((uint8_t *)f)[0x89] = 0;
    ((uint8_t *)f)[0x88] = 1;
    memcpy(f + 0x28 / 4, f + 0x18 / 4, 16);
    memcpy(f + 0x38 / 4, f + 0x18 / 4, 16);
    memcpy(f + 0x68 / 4, f + 0x58 / 4, 16);
    memcpy(f + 0x78 / 4, f + 0x58 / 4, 16);
    o->car.lit = true;
}

static void place(FE3DRoom *r, FE3DObject *o)
{
    if (!o) return;
    FE3DObject_RotY(o, r->rot[1]);
    FE3DObject_RotX(o, r->rot[0]);
    FE3DObject_RotZ(o, r->rot[2]);
}

/* FEGarage3DRoom_v1 0x4e92e0 (the tick) and FEGarage3DRoom_v15 0x4e9380 -> FESpinning3DRoom's draw
   0x4e9690. */
static uint32_t s_carcol[4];
static bool s_carcol_set;
static FE3DObject *car_new(const char *file, const uint32_t *colours);

/* FEDealership3DRoom_v1 0x4e8a50: a slide between cars when the dealer's list moves (not during one):
   256 ticks, the car slid from made of the car shown at the last tick (0x4d0bb0, its hinge 1 the car's)
   and drawn a step away; the slide's end; then the car shown kept for the next. */
static void dealer_tick_slide(FE3DRoom *r);
static void dealer_tick(FE3DRoom *r)
{
    dealer_tick_slide(r);
    snprintf(r->shown, sizeof r->shown, "%s", s_carfile);
    memcpy(r->showncol, s_carcol, sizeof r->showncol);
    r->havecol = s_carcol_set;
}

static void dealer_tick_slide(FE3DRoom *r)
{
    int32_t t = ticks();
    if (r->sliding && r->slide_len < t - r->slide_start) {
        r->sliding = false;
        r->prev = r->index;
    }
    if (!r->sliding) {
        r->index = r->want;
        if (r->index != r->prev) {
            FE3DObject_Free(r->oldcar);
            r->oldcar = r->shown[0] ? car_new(r->shown, r->havecol ? r->showncol : NULL) : NULL;
            if (r->oldcar && r->car && r->car->car.hinge && r->oldcar->car.hinge)
                memcpy(r->oldcar->car.hinge[1], r->car->car.hinge[1], sizeof r->car->car.hinge[1]);   /* 0x4d1430, 0x4d13e0 */
            r->sliding = true;
            r->slide_start = t;
            r->slide_len = 0x100;
        }
        if (!r->sliding) return;
    }
    float f = (float)(t - r->slide_start) / (float)r->slide_len;
    r->slide = f < 0 ? 0 : f > 1.0f ? 1.0f : f;
}

static float minf(float a, float b) { return b <= a ? b : a; }
static float clamp01(float v) { return v < 0 ? 0 : v > 1.0f ? 1.0f : v; }

/* The segment of the list's place k: dealer2a for the first car, dealer2c from the last on, dealer2b
   between. */
static FE3DObject *dealer_seg(FE3DRoom *r, int k) { return r->seg[k == 0 ? 0 : k >= r->count - 1 ? 2 : 1]; }

/* One object reflected (0x4d1040: its +0x471 set, the LOD list's +0x2c8 drawn with 0x30) or plainly
   (0x4d1030); each drawn at once (0x4671d0). */
static void draw_reflected(FE3DObject *o, EaglMatStack *model, int lod)
{
    o->car.cockpitlod = lod;
    o->car.interior = true;
    FE3DObject_Draw(o, model);
    o->car.interior = false;
}

/* FEDealership3DRoom_v15 0x4e8310: lit by the room, the car turning; the segments a half turn round, at
   the car's place and a step to either side (none before the first car). Not sliding: the room's zoom.
   Sliding to the next car (the previous one +1): the place eases a step back across (0.6 of the slide,
   at 4/3 the rate, 0x5b5fbc) and along (after 0.6, at 2.5, 0x5b7028), and a segment more on that side;
   to the one before, the other way (0.4 the split). Then (the rooms' detail on, 0x4e2da0) the
   reflections: each segment's LOD 1, the car's 3 (its LOD set to 1, kept for its own draw), the car
   slid from at its place; then the same plainly, the car slid from with the car's hinges 6 and 7. */
static void dealer_draw(FE3DRoom *r, EaglMatStack *model)
{
    dealer_tick(r);
    for (int i = 0; i < 3; i++) light(r, r->seg[i], false);
    light(r, r->car, true);
    light(r, r->oldcar, true);
    int32_t t = ticks();
    if (r->spin != 0) r->rot[1] = (float)t / ((float)TICKS_HZ * r->spin);   /* +0x300 */
    for (int i = 0; i < 3; i++) {
        if (!r->seg[i]) continue;
        FE3DObject_RotY(r->seg[i], 0.5f);
        FE3DObject_RotX(r->seg[i], r->rot[0]);
    }
    FE3DObject *cars[2] = {r->car, r->oldcar};
    for (int i = 0; i < 2; i++) {
        if (!cars[i]) continue;
        FE3DObject_RotY(cars[i], r->rot[1]);
        FE3DObject_RotX(cars[i], r->rot[0]);
    }
    if (r->car) hinge(r->car, 1, 0, r->rot[1], 0);
    float x = r->pos[0], y = r->pos[1], z = r->pos[2];
    int lo = -1, hi = 1;
    if (!r->sliding) {
        float k = 1.0f - r->zoom;
        x = k * x;
        y = k * y - r->zoom * 0.8f;     /* 0x5b24e8 */
        z = r->zoom * 0.3f + k * z;     /* 0x5b25f0 */
    } else {
        int d = r->prev - r->index;
        if (d == 1) {
            float a = minf(r->slide, 0.6f);                         /* 0x5b2678 */
            float c = clamp01((r->slide - 0.6f) * 2.5f);
            x -= (1.0f - a * 1.6666666f) * r->step[0];              /* 0x5b5fbc */
            hi = 2;
            z -= (1.0f - c) * r->step[1];
        } else if (d == -1) {
            float a = minf(r->slide, 0.4f);                         /* 0x5b261c */
            float c = clamp01((r->slide - 0.4f) * 1.6666666f);
            lo = -2;
            x += (1.0f - c) * r->step[0];
            z += (1.0f - a * 2.5f) * r->step[1];
        } else {
            r->prev = r->index;
            r->sliding = false;
        }
    }
    for (int pass = 0; pass < 2; pass++) {
        bool refl = pass == 0;   /* (the reflections with the rooms' detail on, 0x4e2da0: the port's is) */
        for (int i = lo; i <= hi; i++) {
            if (r->index + i < 0) continue;
            FE3DObject *o = dealer_seg(r, r->index + i);
            if (!o) continue;
            FE3DObject_SetPos(o, (float)i * r->step[0] + x, y, (float)i * r->step[1] + z);
            if (refl) draw_reflected(o, model, 1);
            else FE3DObject_Draw(o, model);
        }
        if (!r->car) continue;
        FE3DObject_SetPos(r->car, x, y, z);
        if (refl) {
            r->car->car.curlod = 1;
            draw_reflected(r->car, model, 3);
        } else {
            FE3DObject_Draw(r->car, model);
        }
        if (r->sliding && r->oldcar) {
            float d = (float)(r->prev - r->index);
            if (refl) {
                r->oldcar->car.curlod = 1;
            } else if (r->car->car.hinge && r->oldcar->car.hinge) {
                memcpy(r->oldcar->car.hinge[6], r->car->car.hinge[6], sizeof r->car->car.hinge[6]);   /* R3DCar_Hinge, 0x443e60 */
                memcpy(r->oldcar->car.hinge[7], r->car->car.hinge[7], sizeof r->car->car.hinge[7]);
            }
            FE3DObject_SetPos(r->oldcar, d * r->step[0] + x, y, d * r->step[1] + z);
            if (refl) draw_reflected(r->oldcar, model, 3);
            else FE3DObject_Draw(r->oldcar, model);
        }
    }
}

/* FEGarage3DRoom_v15 0x4e9380: with a car, its floor shadow on the room model: the shadow-map layer's
   rows (the model's light block +0x164 .., a record at 0x690a18) projecting the floor onto common.fsh's
   'FESH' (0x613e04) from the car's place (last frame's), turned with the room's spin (-360 degrees a
   turn, 0x5b45b4) and scaled to the car's box (its full width and length), centred; without one no
   texture. (The car's +0x700 is cleared too: not read by the ported draw.) */
static void garage_shadow(FE3DRoom *r)
{
    if (!r->model) return;
    if (!r->car) {
        r->model->car.light.lighttex = NULL;   /* 0x440840(0) */
        return;
    }
    const R3DCarState *st = &r->car->state;
    EaglMatrix m, n;
    EAGL_MatIdentity(&m);
    EAGL_MatTranslation(&n, -st->pos[0], 0, -st->pos[2]);
    EAGL_MatPostMul(&m, &n);
    EAGL_MatAxisAngle(&n, r->rot[1] * -360.0f, 0, 1.0f, 0);
    EAGL_MatPostMul(&m, &n);
    EAGL_MatScale(&n, 1.0f / (st->halfsize[0] + st->halfsize[0]), 1.0f, 1.0f / (st->halfsize[2] + st->halfsize[2]), 1.0f);
    EAGL_MatPostMul(&m, &n);
    EAGL_MatTranslation(&n, 0.5f, 0, 0.5f);
    EAGL_MatPostMul(&m, &n);
    /* the record's rows (+0x18 ..): u by the matrix's first column, v by its third (0x440850 copies
       the record, the rest of it as it was, zero) */
    static float rec[16];   /* 0x690a18 */
    rec[6] = m.m[0], rec[7] = m.m[4], rec[8] = m.m[8], rec[10] = m.m[12];
    rec[11] = m.m[2], rec[12] = m.m[6], rec[13] = m.m[10], rec[15] = m.m[14];
    memcpy(&r->model->car.light.f[0x14c / 4], rec, sizeof rec);
    r->model->car.light.lighttex = g_rtex_common[0] ? RTexBank_find(g_rtex_common[0], 0, 0x48534546) : NULL;   /* 'FESH' (0x44d1f0) */
}

static void room_draw(FE3DRoom *r, EaglMatStack *model)
{
    if (r->garage) garage_shadow(r);
    if (r->track) {
        track_draw(r, model);
        return;
    }
    if (r->dealer) {
        dealer_draw(r, model);
        return;
    }
    if (r->movie && r->model && 5 < r->model->car.npages) {
        /* FEVictory3DRoom_v1 0x4eaa80: the movie's frame into the model's texture page 5, its screen
           (0x4479c0, 0x443bb0) */
        CarPage *p = &r->model->car.pages[5];
        FEMovie_Update(r->movie, CarPage_Lock(p));
        CarPage_Unlock(p);
    }
    int32_t t = ticks();
    if (r->model && r->hinge_axis >= 0) {
        /* FEGarage3DRoom_v1 0x4e92e0: the room model's (+0x2ec) hinges 1 and 2 turn with the clock (their
           third component; FEFactory3DRoom_v1 0x4e9030 the first) */
        float a[3] = {0, 0, 0}, b[3] = {0, 0, 0};
        a[r->hinge_axis] = (float)t * 0.0078125f;      /* 0x5b273c */
        b[r->hinge_axis] = (float)t * 0.0052083335f;   /* 0x5b71ac */
        hinge(r->model, 1, a[0], a[1], a[2]);
        hinge(r->model, 2, b[0], b[1], b[2]);
    }
    float k = 1.0f - r->zoom;
    float x = k * r->pos[0], y = k * r->pos[1] - r->zoom * 0.8f, z = r->zoom * 0.3f + k * r->pos[2];   /* 0x5b24e8, 0x5b25f0 */
    FE3DObject_SetPos(r->model, x, y, z);
    if (r->car) FE3DObject_SetPos(r->car, x, y, z);
    light(r, r->model, false);
    light(r, r->car, true);
    if (r->spin != 0) r->rot[1] = (float)t / ((float)TICKS_HZ * r->spin);
    place(r, r->model);
    place(r, r->car);
    hinge(r->model, 8, 0, 0, (float)t / (float)(TICKS_HZ + TICKS_HZ));
    hinge(r->model, 9, 0, 0, (float)t / (float)TICKS_HZ);
    FE3DObject_Draw(r->model, model);
    if (r->car) FE3DObject_Draw(r->car, model);
}

void FE3D_SetRoom(const char *id)
{
    if (!id || !*id || !strcmp(id, "ID_NONE")) id = "";
    if (s_room && !strcmp(s_room->id, id)) return;
    if (!s_room && !*id) return;
    room_free(s_room);
    s_room = NULL;
    s_carfile[0] = 0;
    if (!strcmp(id, "ID_GARAGE")) s_room = garage_new();
    else if (!strcmp(id, "ID_DEALER")) s_room = dealer_new();
    else if (!strcmp(id, "ID_TRACK")) s_room = track_new();
    else if (!strcmp(id, "ID_FACTORY")) s_room = factory_new();
    else if (!strcmp(id, "ID_VICTORY")) s_room = victory_new();
    /* (0x4e6f10 finds the id's maker in its map; for another id it calls a null one, a crash: no layout
       names one, the port makes none) */
}

void FE3D_SetIndex(int index, int count)
{
    if (!s_room) return;
    s_room->want = index;
    s_room->count = count;
    if (!s_room->dealer) s_room->index = index;
}

static const uint32_t s_silver[4] = {0xffb8bcc0u, 0xffc0c0c0u, 0xff202020u, 0xff303030u};

/* A car of a file in its colours (the description's order; NULL the port's silver). */
static FE3DObject *car_new(const char *file, const uint32_t *colours)
{
    /* the accessor's colours (0x4bfbb0) in 0x4d0bb0's argument order: +0x218, +0x224, +0x21c, +0x220 */
    const uint32_t args[4] = {colours ? colours[0] : 0, colours ? colours[3] : 0, colours ? colours[1] : 0, colours ? colours[2] : 0};
    return FE3DObject_New(0, file, colours ? args : s_silver, 0, -1, 0, NULL);
}

void FE3D_SetCar(const char *file, const uint32_t *colours)
{
    if (!s_room) return;
    if (!file) file = "";
    const uint32_t *col = colours ? colours : s_silver;
    if (!strcmp(s_carfile, file) && !memcmp(s_carcol, col, sizeof s_carcol)) return;
    memcpy(s_carcol, col, sizeof s_carcol);
    s_carcol_set = colours != NULL;
    snprintf(s_carfile, sizeof s_carfile, "%s", file);
    FE3DObject_Free(s_room->car);
    s_room->car = NULL;
    if (!*file) return;
    s_room->car = car_new(file, colours);   /* FECar3DControl_v21 0x4bf9a0 */
    FE3DObject_SetPos(s_room->car, s_room->pos[0], s_room->pos[1], s_room->pos[2]);   /* 0x4e98a0 */
}

bool FE3D_Draw(void)
{
    if (!s_room || !s_room->model || !g_render) return false;
    if (!s_camera) s_camera = Render_CreateCamera(g_render, 6, 0, 0, 1, 1, 0, g_render->znear, g_render->zfar);
    if (!s_camera) return false;
    s_camera->halffov = s_room->fov;   /* 0x4e7b40 -> 0x4d5d10 */
    s_camera->clear = 0xff000000u;     /* (the frame's clear, 0x538a00) */
    View_Begin(g_render, s_camera);
    RenderState_SetCull(g_render->device->rs, 2);
    room_draw(s_room, &s_camera->view->model);
    View_End(g_render);
    return true;
}

void FE3D_Free(void)
{
    room_free(s_room);
    s_room = NULL;
    s_carfile[0] = 0;
    RCamera_Free(s_camera);
    s_camera = NULL;
}

float FE3D_CarYaw(void) { return s_room ? s_room->rot[1] : 0; }
void FE3D_SetZoom(float f)
{
    if (s_room) s_room->zoom = f;
}
R3DCar *FE3D_Car(void) { return s_room && s_room->car ? &s_room->car->car : NULL; }
void FE3D_SetCarDamage(const float d[10])
{
    if (s_room && s_room->car) memcpy(s_room->car->damage, d, sizeof s_room->car->damage);
}
void FE3D_CarHinge(int kind, float x, float y, float z)
{
    if (s_room && s_room->car) hinge(s_room->car, kind, x, y, z);
}
