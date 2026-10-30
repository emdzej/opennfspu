/* The track viewer (see track_selftest.h). */
#include "render/track_selftest.h"
#include "eagl/eagl.h"
#include "game/scene.h"
#include "game/track.h"
#include "game/world.h"
#include "real/texture.h"
#include "render/rdraw.h"
#include "render/render.h"
#include "render/rlayers.h"
#include "render/rlists.h"
#include "platform.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static bool ready, failed;
static RCamera *camera;
static float env[64];

/* A light environment until the sky's is ported (the race's comes from 0x459940). */
static void set_env(void)
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

/* The camera a few metres behind and above the start line, looking along it and slightly down. The start
   line's matrix rows are its axes: the first is the race's direction (the road runs along it), the second
   up; the scene and the world have up = +y (the second row is (0, 1, 0)). */
static void place_camera(const float pos[3], const float m[9])
{
    RFrame *f = &camera->frame;
    float fx = m[0], fz = m[2];
    {   /* --param yaw=<degrees> turns the heading */
        char v[16];
        if (plat_param("yaw", v, sizeof v)) {
            float a = (float)atof(v) * 3.14159265f / 180.0f, c = cosf(a), s2 = sinf(a);
            float nx = fx * c - fz * s2, nz = fx * s2 + fz * c;
            fx = nx;
            fz = nz;
        }
    }
    float n = sqrtf(fx * fx + fz * fz);
    if (n < 1e-6f) { fx = 0; fz = 1; n = 1; }
    fx /= n;
    fz /= n;
    f->eye[0] = pos[0] - fx * 8.0f;
    f->eye[1] = pos[1] + 2.5f;
    f->eye[2] = pos[2] - fz * 8.0f;
    /* back (towards the eye), up a little; right = up x back (up (0, 1, 0)); up' = back x right; the rows
       (-right, up', forward) as the car viewer's (a proper rotation) */
    float bx = -fx, by = 0.12f, bz = -fz, bn = sqrtf(bx * bx + by * by + bz * bz);
    bx /= bn; by /= bn; bz /= bn;
    float rx = bz, rz = -bx, rn = sqrtf(rx * rx + rz * rz);
    rx /= rn;
    rz /= rn;
    float ux = by * rz, uy = bz * rx - bx * rz, uz = -by * rx;
    float o[9] = {-rx, 0, -rz, ux, uy, uz, -bx, -by, -bz};
    memcpy(f->orient, o, sizeof o);
}

static RaceViews views;

static bool load(const char *name)
{
    snprintf(g_track_name, sizeof g_track_name, "%s", name);
    Track_Load();
    if (!g_world || !g_world->root) return false;
    RaceViews_ctor(&views);
    camera = views.main;
    float pos[3] = {0, 0, 0}, m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    if (!Scene_StartLine(pos, m)) {
        SceneElem *e = Scene_Camera(Scene_FirstCamera(false));
        if (e) {
            scene_pos(e, pos);
            scene_mat(e, m);
        }
    }
    /* The heading (a viewer's choice until the race's car placement is ported): the start line's second
       direction vector (+0xc8) when its flag (+0xe8) is set, which runs along the road on the tracks that
       have one; else the first row, which does on the others. */
    SceneElem *t = Scene_FindTriggerByType(2);
    if (t && scene_i32(t, 0xe8)) {
        m[0] = scene_f32(t, 0xc8);
        m[1] = scene_f32(t, 0xcc);
        m[2] = scene_f32(t, 0xd0);
    }
    place_camera(pos, m);
    set_env();
    RLists_Init();
    return true;
}

bool TRACK_selftest(const char *name)
{
    if (!g_render || failed) return false;
    if (!ready) {
        ready = true;
        if (!load(name)) { failed = true; return false; }
    }
    /* The race frame (0x468900); the camera stays where it was put (its update, 0x461060, is pending). */
    TexManager *tm = g_texgraphics ? g_texgraphics->textures : NULL;
    if (tm) TexManager_begin(tm);
    Render_Frame(&views);
    if (tm) TexManager_end(tm);
    return true;
}
