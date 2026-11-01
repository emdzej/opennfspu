/* The front end's 3D objects and rooms (fe3d.h). */
#include "front/fe3d.h"
#include "app.h"
#include "game/sim_math.h"
#include "pending.h"
#include "render/camera_math.h"
#include "render/carpages.h"
#include "render/render.h"
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

/* Port deviation: with the front end's identity camera and its objects' identity orientations, the port
   draws the models upside down (the positions are right): the race's cars carry a half turn about their
   forward axis in their orientation (up row 0, -1, 0) that the front end's don't, and where the original
   makes up for it is not found yet. Until it is, each object is drawn with that half turn added, about the
   point the original's draw puts at its position (R3DObject_Draw 0x4433c0: pos - up * half height). */
void FE3DObject_Draw(FE3DObject *o, EaglMatStack *model)
{
    R3DCarState *st = &o->state;
    float rot[9], pos[3];
    memcpy(rot, st->rot, sizeof rot);
    memcpy(pos, st->pos, sizeof pos);
    static const float half[9] = {-1, 0, 0, 0, -1, 0, 0, 0, 1};
    Mat3_Mul(half, rot, st->rot);
    float h = st->halfsize[1];
    for (int i = 0; i < 3; i++) st->pos[i] = pos[i] - rot[3 + i] * h + st->rot[3 + i] * h;
    R3DCar_Draw(&o->car, model);
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
    /* the dealership's (FEDealership3DRoom 0x4e8090) */
    bool dealer;
    FE3DObject *seg[3];       /* +0x2ec .. +0x2f4: dealer2a, dealer2b, dealer2c */
    float step[2];            /* +0x2d4, +0x2d8: one car's place to the next (x, z) */
    int index;                /* +0x30c: the dealer car shown */
    /* the track select's (FETrackSelect3DRoom 0x4e9a30) */
    bool track;
    FE3DObject *wall;         /* +0x318: trackwall (the model +0x314 is "track") */
    float from[4], to[4];     /* +0x324, +0x330: the place (x y z) and x turn it zooms between */
    int32_t zoom_at;          /* +0x320: the tick the zoom started (-1 not yet) */
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
    r->model = FE3DObject_New(2, "garage", none, 0, -1, 0, NULL);   /* 0x4e9570 */
    FE3DObject_SetPos(r->model, r->pos[0], r->pos[1], r->pos[2]);
    PENDING_NOTE(0x4e9380, "FEGarage3DRoom_v15: the car's floor shadow ('FESH' and its matrix)");
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
    PENDING_NOTE(0x4e8310, "the dealership's slide between cars and its reflection pass");
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
    }
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
    float *f = l->f;
    memset(f, 0, sizeof l->f);
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
/* FEDealership3DRoom_v15 0x4e8310: the segments a half turn round, at the car's place and a step to
   either side (those of a car the list has), lit by the room; the car turning in the middle. */
static void dealer_draw(FE3DRoom *r, EaglMatStack *model)
{
    int32_t t = ticks();
    if (r->spin != 0) r->rot[1] = (float)t / ((float)TICKS_HZ * r->spin);
    float x = r->pos[0], y = r->pos[1], z = r->pos[2];
    for (int i = -1; i <= 1; i++) {
        if (r->index + i < 0) continue;
        FE3DObject *o = r->seg[(r->index + i) % 3];
        if (!o) continue;
        light(r, o, false);
        FE3DObject_RotY(o, 0.5f);
        FE3DObject_RotX(o, r->rot[0]);
        FE3DObject_SetPos(o, (float)i * r->step[0] + x, y, (float)i * r->step[1] + z);
        FE3DObject_Draw(o, model);
    }
    if (r->car) {
        light(r, r->car, true);
        FE3DObject_RotY(r->car, r->rot[1]);
        FE3DObject_RotX(r->car, r->rot[0]);
        FE3DObject_SetPos(r->car, x, y, z);
        hinge(r->car, 1, 0, r->rot[1], 0);
        FE3DObject_Draw(r->car, model);
    }
}

static void room_draw(FE3DRoom *r, EaglMatStack *model)
{
    if (r->track) {
        track_draw(r, model);
        return;
    }
    if (r->dealer) {
        dealer_draw(r, model);
        return;
    }
    int32_t t = ticks();
    if (r->car) {
        hinge(r->car, 1, 0, 0, (float)t * 0.0078125f);       /* 0x5b273c */
        hinge(r->car, 2, 0, 0, (float)t * 0.0052083335f);    /* 0x5b71ac */
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
    else if (*id) PENDING_NOTE(0x4e6f10, "the dealership, factory, track select and victory rooms");
}

void FE3D_SetIndex(int index)
{
    if (s_room) s_room->index = index;
}

void FE3D_SetCar(const char *file)
{
    if (!s_room) return;
    if (!file) file = "";
    if (!strcmp(s_carfile, file)) return;
    snprintf(s_carfile, sizeof s_carfile, "%s", file);
    FE3DObject_Free(s_room->car);
    s_room->car = NULL;
    if (!*file) return;
    /* (the accessor's colours, 0x4bfbb0, pend: the race's silver) */
    static const uint32_t colours[4] = {0xffb8bcc0u, 0xffc0c0c0u, 0xff202020u, 0xff303030u};
    s_room->car = FE3DObject_New(0, file, colours, 0, -1, 0, NULL);   /* FECar3DControl_v21 0x4bf9a0 */
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
