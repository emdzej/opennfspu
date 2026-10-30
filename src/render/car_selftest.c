/* Port addition: one car of GameData\CarModel drawn through the ported car code, EAGL and THRASH, from
   an orbiting camera in the front end's showroom mode (the integrator calls it, e.g. behind a launch
   parameter). The frame goes the race's way: the device's begin, the main view's cull (Render_MainViews
   0x468ae0), a render-system camera (Render_CreateCamera 0x467230) through View_Begin / View_End, the page
   flip. The car draws through its render methods (rmaterial.c, rlayers.c) under a fixed light environment of
   the port's (the game's comes from the track or the showroom scene), with the shared art loaded and a
   specification of four colours. */
#include "render/car_selftest.h"
#include "real/texture.h"
#include "render/carpages.h"
#include "eagl/eagl.h"
#include "real/set3d.h"
#include "render/r3dcar.h"
#include "render/render.h"
#include "render/rlayers.h"
#include "render/rmaterial.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool ready;
static char loaded[64];
static RCamera *camera;
static R3DCarState state;
/* The car's specification as the front end would fill it: four colours (body, stripe, interior, wheels:
   the port's choice of a silver car with a dark interior; the original's come from the player's pick),
   no licence text, no race number, colour package 0. */
static R3DCarSpec spec;
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
    camera = Render_CreateCamera(g_render, 0, 0, 0, 1, 1, 0xff202830u, g_render->znear, g_render->zfar);
    set_env();
    R3DCar_LoadCommon();   /* carcmn.fsh, the shared pages and the colour packages (0x443690), once */
    ready = true;
}

static bool load(const char *name)
{
    if (car.loaded || car.state) R3DCar_Free(&car);
    memset(&state, 0, sizeof state);
    state.rot[0] = state.rot[4] = state.rot[8] = 1;
    memset(&spec, 0, sizeof spec);
    spec.colours[0] = 0xffb8bcc0u;
    spec.colours[1] = 0xff202020u;
    spec.colours[2] = 0xff303030u;
    spec.colours[3] = 0xffc0c0c0u;
    spec.racenumber = -1;
    for (int i = 0; i < CAR_NOPTIONS; i++) spec.options[i] = -1;
    state.spec = &spec;
    R3DCar_Init(&car, &state);
    char path[96];
    snprintf(path, sizeof path, "GameData\\CarModel\\%s", name);
    g_r3d_frontend = 1;
    /* The colours before the load, as CarSpecs_Load does (0x410fcf, then the load through the vtable):
       the pages are tinted with them while they are composed. */
    R3DCar_SetColours(&car, 0);
    R3DCar_LoadCrp(&car, path);
    snprintf(loaded, sizeof loaded, "%s", name);
    return car.loaded;
}

/* The camera's frame: the eye on a circle around the car, looking at a point above its centre. The
   game's world has y down (R3DCar_Draw's last scale (1, -1, 1)); the orientation's rows are right, up and
   forward (render.h RFrame). */
static void place_camera(float yaw, float pitch, float dist)
{
    float tx = 0, ty = -0.55f, tz = 0;
    /* back: from the target to the eye (above the car: towards -y) */
    float bx = sinf(yaw) * cosf(pitch), by = -sinf(pitch), bz = cosf(yaw) * cosf(pitch);
    RFrame *f = &camera->frame;
    f->eye[0] = tx + bx * dist;
    f->eye[1] = ty + by * dist;
    f->eye[2] = tz + bz * dist;
    /* right = up x back with the world's up (0, -1, 0), then up = back x right */
    float rx = -bz, ry = 0, rz = bx, rn = sqrtf(rx * rx + rz * rz);
    rx /= rn;
    rz /= rn;
    float ux = by * rz - bz * ry, uy = bz * rx - bx * rz, uz = bx * ry - by * rx;
    /* A proper rotation (determinant +1): with (right, up, back) right-handed as built above, the rows
       (-right, up, forward) keep the handedness the game's cameras have; the rows (right, up, forward)
       are a mirror, which flips every triangle's screen winding against the race view's cull (2). */
    float o[9] = {-rx, -ry, -rz, ux, uy, uz, -bx, -by, -bz};
    memcpy(f->orient, o, sizeof o);
}

bool CAR_selftest(const char *name)
{
    if (!g_render) return false;
    if (!ready) setup();
    if (strcmp(loaded, name) && !load(name)) return false;
    place_camera(0.6f + (float)frame * 0.01f, 0.28f, 4.2f);
    TexManager *tm = g_texgraphics ? g_texgraphics->textures : NULL;
    Render_BeginFrame(g_render);
    if (tm) TexManager_begin(tm);   /* textures to the card for the frame, as the race frame does */
    RenderState_SetCull(g_render->device->rs, g_r3d_mirror ? 1 : 2);
    View_Begin(g_render, camera);
    R3DCar_Draw(&car, &camera->view->model);
    View_End(g_render);
    if (tm) TexManager_end(tm);
    Render_EndFrame(g_render);
    frame++;
    return true;
}
