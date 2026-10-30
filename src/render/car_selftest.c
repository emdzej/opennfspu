/* Port addition: one car of GameData\CarModel drawn through the ported car code, EAGL and THRASH, from
   a fixed three-quarter view in the front end's showroom mode (the integrator calls it, e.g. behind a
   launch parameter). The car draws through its render methods (rmaterial.c, rlayers.c) under a fixed light
   environment of the port's (the game's comes from the track or the showroom scene); textures are pending
   with the texture pages (0x4426c0). */
#include "render/car_selftest.h"
#include "eagl/eagl.h"
#include "real/set3d.h"
#include "render/r3dcar.h"
#include "render/rlayers.h"
#include "render/rmaterial.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool ready;
static char loaded[64];
static EaglView view;
static R3DCarState state;
static R3DCar car;
static int frame;

/* The light environment (rlayers.h layout): ambient 0.4, diffuse 0.6 from above and in front. */
static float env[64];

static void set_env(void)
{
    float *amb = env + 0x18 / 4, *dif = env + 0x58 / 4, *dir = env + 0x48 / 4, *mul = env + 0x90 / 4;
    amb[0] = 1.0f;
    amb[1] = amb[2] = amb[3] = 0.4f;
    dif[1] = dif[2] = dif[3] = 0.6f;
    float x = -0.35f, y = -0.8f, z = -0.48f, n = sqrtf(x * x + y * y + z * z);
    dir[0] = x / n;
    dir[1] = y / n;
    dir[2] = z / n;
    mul[0] = mul[1] = mul[2] = 1.0f;
    g_rlayer_lights = env;
}

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
    set_env();
    /* The method key of the render quality setup (0x44ebe0) without reflections: 1 (environment maps 3,
       with texture flags 4). Each material's base layer group needs it. */
    g_rmethod_key = 1;
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
    /* The eye in world space, for the back-face culling of the coordinate layers. */
    EaglMatrix inv = v;
    EAGL_MatInvertOrtho(&inv);
    g_rlayer_eye[0] = inv.m[12];
    g_rlayer_eye[1] = inv.m[13];
    g_rlayer_eye[2] = inv.m[14];
    EAGL_ViewClear(&view);
    /* What the game's View_Begin (0x467170) does around EAGL's: the vertex cache emptied, and the
       context's viewport set (layers that find everything inside skip it and use the view's). */
    RLayers_ResetCache();
    EAGL_ViewBegin(&view);
    EAGL_ContextViewport(g_eagl);
    R3DCar_Draw(&car, &view.model);
    EAGL_ViewEnd(&view);
    THRASH.fn.pageflip();
    frame++;
    return true;
}
