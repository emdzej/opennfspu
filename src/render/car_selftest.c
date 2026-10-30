/* Port addition: one car of GameData\CarModel drawn through the ported car code, EAGL and THRASH, from
   a fixed three-quarter view in the front end's showroom mode (the integrator calls it, e.g. behind a
   launch parameter). Until render methods are ported the car shows in the stand-in material
   (car_pending.c): grey, shaded from its normals, untextured. */
#include "render/car_selftest.h"
#include "eagl/eagl.h"
#include "real/set3d.h"
#include "render/r3dcar.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool ready;
static char loaded[64];
static EaglView view;
static R3DCarState state;
static R3DCar car;
static int frame;

static void setup(void)
{
    EAGL_ContextCreate();
    EAGL_ContextInit(g_eagl, 1);
    EAGL_ContextResetStages(g_eagl);
    EAGL_SetDisplaySize(640, 480);
    EAGL_ViewInit(&view, NULL);
    EAGL_ViewSetViewport(&view, 640, 480, 0, 0, 1, 1000);
    view.fov = 50;
    view.aspect = 0.75f;
    view.znear = 0.1f;
    view.zfar = 1000;
    view.background = 0xff202830;
    ready = true;
}

static bool load(const char *name)
{
    if (car.loaded || car.state) R3DCar_Free(&car);
    memset(&state, 0, sizeof state);
    state.rot[0] = state.rot[4] = state.rot[8] = 1;
    R3DCar_Init(&car, &state);
    char path[96];
    snprintf(path, sizeof path, "GameData\\CarModel\\%s", name);
    g_r3d_frontend = 1;
    R3DCar_LoadCrp(&car, path);
    snprintf(loaded, sizeof loaded, "%s", name);
    return car.loaded;
}

bool CAR_selftest(const char *name)
{
    if (!ready) setup();
    if (strcmp(loaded, name) && !load(name)) return false;
    THRASH.fn.setstate(THRASH_STATE_SHADE, 1);
    THRASH.fn.setstate(THRASH_STATE_ZBUFFER, 1);
    THRASH.fn.setstate(THRASH_STATE_CULL, 0);
    THRASH.fn.setstate(THRASH_STATE_BLEND, 0);
    /* The camera: R3DCar_Draw's model matrix negates the car's axes (0x4433c0: scale (-1, 1, -1), the
       rotation, scale (1, -1, 1)), the game's cameras undo that; here a scale by -1, a turn about the
       vertical axis, a tilt and the distance. */
    float yaw = 0.6f + (float)frame * 0.01f, pitch = 0.28f, dist = 6.5f;
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    EaglMatrix v, t;
    EAGL_MatScale(&v, -1, -1, -1, 1);
    float ry[9] = {cy, 0, -sy, 0, 1, 0, sy, 0, cy}, rx[9] = {1, 0, 0, 0, cp, sp, 0, -sp, cp};
    static const float zero[3] = {0, 0, 0};
    EAGL_MatFromRotTrans(&t, ry, zero);
    EAGL_MatPostMul(&v, &t);
    EAGL_MatFromRotTrans(&t, rx, zero);
    EAGL_MatPostMul(&v, &t);
    EAGL_MatTranslation(&t, 0, -0.55f, -dist);
    EAGL_MatPostMul(&v, &t);
    *EAGL_StackTopForWrite(&view.view) = v;
    EAGL_ViewClear(&view);
    EAGL_ViewBegin(&view);
    R3DCar_Draw(&car, &view.model);
    EAGL_ViewEnd(&view);
    THRASH.fn.pageflip();
    frame++;
    return true;
}
