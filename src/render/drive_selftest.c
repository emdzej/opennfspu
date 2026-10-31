/* The drive mode (see drive_selftest.h). */
#include "render/drive_selftest.h"
#include "eagl/eagl.h"
#include "game/car.h"
#include "game/car_drive.h"
#include "game/car_physics.h"
#include "game/race_setup.h"
#include "game/race.h"
#include "game/race_car.h"
#include "game/scene.h"
#include "game/track.h"
#include "game/wtree.h"
#include "platform.h"
#include "real/texture.h"
#include "render/r3dcar.h"
#include "render/render.h"
#include "render/render_pending.h"
#include "render/rlayers.h"
#include "render/rlists.h"
#include "render/sky.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static bool ready, failed;
static RaceViews views;
static float env[64];
static double ticks_due;
static float cam_eye[3], cam_look[3];
static bool cam_set;

static void set_env(void)   /* the track viewer's light until the sky's is ported */
{
    float *amb = env + 0x18 / 4, *dif = env + 0x58 / 4, *dir = env + 0x48 / 4, *mul = env + 0x90 / 4;
    amb[0] = 1.0f;
    amb[1] = amb[2] = amb[3] = 0.55f;
    dif[1] = dif[2] = dif[3] = 0.5f;
    float x = -0.35f, y = -0.8f, z = -0.48f, n = sqrtf(x * x + y * y + z * z);
    dir[0] = x / n;
    dir[1] = y / n;
    dir[2] = z / n;
    mul[0] = mul[1] = mul[2] = 1.0f;
    g_rlayer_lights = env;
}

/* The cars through their renderers, their states taken from the simulation. The port's R3DCar_Draw takes
   its flips as the car viewer's y-down camera needs them: the car's right and up rows go to the renderer
   negated (a half turn about its forward axis), and its centre down by the up row times the full height,
   which draws the car upright with the winding the draw's cull flip expects. To fold into R3DCar_Draw
   and the car viewer when the race's car draw (0x43f610) is ported. */
static void draw_cars(RCamera *c)
{
    for (int i = 0; i < g_ncars; i++) {
        Car *car = g_cars[i];
        R3DCar *r = car ? car->p[CARP_R3D] : NULL;
        if (!r || !r->state) continue;
        const float *m = CAR_F(car, 0x364), *p = CAR_F(car, 0x330);
        R3DCarState *st = r->state;
        for (int k = 0; k < 9; k++) st->rot[k] = k < 6 ? -m[k] : m[k];
        float h = st->halfsize[1];
        for (int k = 0; k < 3; k++) st->pos[k] = p[k] - 2.0f * m[3 + k] * h;
        R3DCar_Draw(r, &c->view->model);
    }
}

/* The camera behind the car and above it, looking a little ahead of it; it eases after the car. */
static void chase_camera(Car *car)
{
    const float *p = CAR_F(car, 0x330), *fw = CAR_F(car, 0x37c);
    float fx = fw[0], fz = fw[2], n = sqrtf(fx * fx + fz * fz);
    if (n < 1e-4f) { fx = 0; fz = 1; n = 1; }
    fx /= n;
    fz /= n;
    static float dist = -1;
    if (dist < 0) {   /* --param camdist=<metres> */
        char v[16];
        dist = plat_param("camdist", v, sizeof v) ? (float)atof(v) : 6.5f;
    }
    float eye[3] = {p[0] - fx * dist, p[1] + dist * 0.34f, p[2] - fz * dist};
    float look[3] = {p[0] + fx * 3.0f, p[1] + 0.6f, p[2] + fz * 3.0f};
    if (!cam_set) {
        memcpy(cam_eye, eye, sizeof eye);
        memcpy(cam_look, look, sizeof look);
        cam_set = true;
    }
    for (int k = 0; k < 3; k++) {
        cam_eye[k] += (eye[k] - cam_eye[k]) * 0.18f;
        cam_look[k] += (look[k] - cam_look[k]) * 0.3f;
    }
    float g = WSurface_HeightAt(cam_eye) + 0.8f;   /* not under the ground */
    if (cam_eye[1] < g) cam_eye[1] = g;
    RFrame *f = &views.main->frame;
    memcpy(f->eye, cam_eye, sizeof cam_eye);
    /* back (towards the eye), right = up x back, up' = back x right; rows (-right, up', forward) */
    float bx = cam_eye[0] - cam_look[0], by = cam_eye[1] - cam_look[1], bz = cam_eye[2] - cam_look[2];
    float bn = sqrtf(bx * bx + by * by + bz * bz);
    bx /= bn;
    by /= bn;
    bz /= bn;
    float rx = bz, rz = -bx, rn = sqrtf(rx * rx + rz * rz);
    rx /= rn;
    rz /= rn;
    float ux = by * rz, uy = bz * rx - bx * rz, uz = -by * rx;
    float o[9] = {-rx, 0, -rz, ux, uy, uz, -bx, -by, -bz};
    memcpy(f->orient, o, sizeof o);
}

static bool load(const char *track, const char *carname)
{
    snprintf(g_track_name, sizeof g_track_name, "%s", track);
    Track_Load();
    if (!g_world || !g_world->root) return false;
    Sky_LoadFsh(g_track_name, false);
    RaceViews_ctor(&views);
    if (!RaceSetup_SingleCar(carname)) return false;
    Car_Construct();
    if (g_ncars < 1) return false;
    Car *c = g_cars[0];
    /* The race's set-up: the grid slot behind the start line, on the road (0x410790). */
    Car_RaceInit(c, 0);
    g_sim_tick = 0x1c0 - 0x40;   /* a second before the countdown's end */
    g_render_cars = draw_cars;
    set_env();
    RLists_Init();
    plat_set_frame_rate(60.0);
    return true;
}

/* The driver: the keyboard (DirectInput codes) or the pad into the car's controls. */
static void read_controls(Car *c)
{
    uint8_t keys[KEY_COUNT] = {0};
    bool kb = plat_keys(keys);
    uint32_t pad = plat_pad(0);
    static int autodrive = -1;   /* --param autodrive=1: full throttle, 2: and right, 3: and left (headless runs) */
    if (autodrive < 0) {
        char v[8];
        autodrive = plat_param("autodrive", v, sizeof v) ? v[0] - '0' : 0;
    }
    bool up = autodrive >= 1 || (kb && (keys[0xc8] || keys[0x11])) || (pad & (PAD_A | PAD_UP));
    bool down = (kb && (keys[0xd0] || keys[0x1f])) || (pad & (PAD_B | PAD_DOWN));
    bool left = autodrive == 3 || (kb && (keys[0xcb] || keys[0x1e])) || (pad & PAD_LEFT);
    bool right = autodrive == 2 || (kb && (keys[0xcd] || keys[0x20])) || (pad & PAD_RIGHT);
    bool hand = (kb && keys[0x39]) || (pad & PAD_X);
    c->b[0xd7c] = up ? 0xff : 0;
    c->b[0xd7d] = down ? 0xff : 0;
    c->b[0xd85] = hand ? 1 : 0;
    int target = left == right ? 0 : left ? -0x7f : 0x7f;
    int32_t *s = CAR_I(c, 0xd90);
    int step = 0x10;   /* the keyboard's steering eases over about an eighth of a second */
    if (*s < target) *s = *s + step < target ? *s + step : target;
    else if (*s > target) *s = *s - step > target ? *s - step : target;
}

bool DRIVE_selftest(const char *track, const char *car)
{
    if (!g_render || failed) return false;
    if (!ready) {
        ready = true;
        if (!load(track, car && car[0] ? car : "964")) { failed = true; return false; }
    }
    Car *c = g_cars[0];
    /* The simulation's 64 Hz from the 60 Hz frames. */
    ticks_due += 64.0 / 60.0;
    int n = (int)ticks_due;
    ticks_due -= n;
    for (int i = 0; i < n && i < 4; i++) {
        read_controls(c);
        Sim_Step();   /* 0x415010 */
    }
    chase_camera(c);
    TexManager *tm = g_texgraphics ? g_texgraphics->textures : NULL;
    if (tm) TexManager_begin(tm);
    Render_Frame(&views);
    if (tm) TexManager_end(tm);
    return true;
}
