/* The drive mode (see drive_selftest.h). */
#include "render/drive_selftest.h"
#include "render/particles.h"
#include "render/carparticle.h"
#include "render/weather.h"
#include "real/mouse.h"
#include "eagl/eagl.h"
#include "render/headlights.h"
#include "game/ai.h"
#include "game/ai_police.h"
#include "game/ai_data.h"
#include "game/car.h"
#include "game/car_damage.h"
#include "game/car_drive.h"
#include "game/car_physics.h"
#include "game/race_setup.h"
#include "game/mission.h"
#include "game/race.h"
#include "game/hud.h"
#include "game/loadscreen.h"
#include "game/input.h"
#include "game/controls.h"
#include "game/music.h"
#include "game/replay_file.h"
#include "game/sim_rand.h"
#include "game/race_progress.h"
#include "pending.h"
#include "render/carpages.h"
#include "game/race_car.h"
#include "game/traffic.h"
#include "game/scene.h"
#include "game/track.h"
#include "game/wtree.h"
#include "platform.h"
#include "real/texture.h"
#include "real/depthconv.h"
#include "real/mem.h"
#include "real/window.h"
#include "render/r3dcar.h"
#include "render/render.h"
#include "render/rtex.h"
#include "render/sprite.h"
#include "render/dynlight.h"
#include "render/rworld.h"
#include "render/rlayers.h"
#include "render/rlists.h"
#include "exe.h"
#include "game/audio.h"
#include "game/camera.h"
#include "render/noise.h"
#include "render/sky.h"
#include "render/sky_sun.h"
#include "render/carlight.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static bool ready, failed;
static RaceViews views;
static float env[64];
static double ticks_due;
static int s_start;   /* the race's first tick (--param start) */
static bool s_replay_again;   /* 0x657428: the replay's restart */
static int s_slot;    /* 0x606aa0: the race loop's tick slots (the replay's speeds skip or double them) */

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

/* The cars through their renderers, their states taken from the simulation. Port deviation: the original's
   matrix (R3DObject_Draw 0x4433c0: scale (-1, 1, -1), the car's rows, scale (1, -1, 1), the place less the
   up row times the half height) draws the models upside down here and mirrored (determinant -1, which
   0x467ed0 flips the cull for); where the original makes up for its y is not found yet. The models' own
   data says what the result must be: the driver sits at the model's x +0.4 and the eye (drivercampos) at
   the car's x -0.38, z likewise, y up in both, so a model point (x, y, z) goes to the car's (-x, y, -z),
   a proper turn. The rows go to R3DCar_Draw with their y column negated (its last scale puts it back),
   the place so that its centre comes out as the original's, and the cull flipped back around the draw.
   To fold into R3DCar_Draw and the front end's objects (fe3d.c's half turn) when the race's car draw
   (0x43f610) is ported. */
/* A car's draw object (its renderer's +0xc object, vtable slot 2 R3DObject_Draw 0x4433c0): the entry
   the queue puts into the opaque list draws the car at the lists' flush. */
typedef struct { RDrawObject base; R3DCar *r; } CarDrawObject;
static CarDrawObject s_cardraw[64];
static RCamera *s_carcam;   /* (the view the cars were queued for) */

static void car_entry_draw(RDrawObject *o, int index, const RListEntry *e)
{
    (void)index, (void)e;
    CarDrawObject *d = (CarDrawObject *)o;
    Render_SetCull(g_render);
    R3DCar_Draw(d->r, &s_carcam->view->model);
    Render_SetCull(g_render);
}

static const RDrawVtbl car_draw_vtbl = {NULL, NULL, car_entry_draw, NULL, NULL, NULL, NULL};

/* R3DCar_v13 (0x439ec0): whether the car is drawn and its level of detail (+0x2c0). Not drawn: a car
   without its +0x7f flag, the viewed car in camera mode 0; a view inside the viewed car keeps the level.
   Else from the detail's table (0x5cd5b8: one less in a replay, one more for a human car at detail 0;
   0 for flag 0x10's cars), the LODs that have parts (the first empty one's number, else 7) and the size on
   screen of the car's box: three corners (x, y, z), (-2x, y, z), (x, y, -z) of its half size (+0x3a8)
   about its centre (R3DCar_v5 0x43a340) through its rotation, projected; the two edges' lengths
   (the halved-exponent square root) times 640 / the screen's width. 3 or less: not drawn. Port: the
   machine's memory isn't low (MEM_info 0x53c140 under 40 MB adds one); the transform is EAGL_MatTransform4's
   (the original's 0x539e60 sums in its own order). */
static float exe_f32(uint32_t va) { union { uint32_t u; float f; } c = {exe_u32(va)}; return c.f; }

static float sqrt_bits(float v)
{
    int32_t i;
    memcpy(&i, &v, 4);
    i = (i >> 1) + 0x1fc00000;
    memcpy(&v, &i, 4);
    return v;
}

static bool car_lod(R3DCar *r, Car *car, EaglView *view)
{
    if (!car->b[0x7f]) return false;
    if (*CAR_I(car, CAR_INDEX) == g_camera_car) {
        if (g_racecam[0].mode == 0) return false;   /* (0x465630) */
        if (g_racecam[0].flags2 & 8) goto lower;
    }
    {
        int q = g_r3d_quality, low = 0;
        int base = (int)exe_u32(0x5cd5b8 + 4 * (uint32_t)q) + low;
        uint32_t fl = (uint32_t)*CAR_I(car, CAR_FLAGS);
        if (g_race_view < 2 || q == 0 || low) {
            if (q == 0 && (fl & 8) && !low) base++;
        } else {
            base--;
        }
        if (fl & 0x10) base = (!low || q != 0) ? 0 : 1;
        int levels = 7;
        for (uint32_t k = 0; k < r->nlods; k++)
            if (r->lods[k]->nparts == 0) { levels = (int)k + 1; break; }
        levels -= base;
        const float *pos = CAR_F(car, 0x330), *up = CAR_F(car, 0x370), *hs = CAR_F(car, 0x3a8);
        float t[3] = {pos[0] - up[0] * hs[1], pos[1] - up[1] * hs[1], pos[2] - up[2] * hs[1]};
        EaglMatrix m;
        EAGL_MatFromRotTrans(&m, CAR_F(car, 0x364), t);
        float pts[12] = {hs[0], hs[1], hs[2], 1, hs[0] * exe_f32(0x5b2628), hs[1], hs[2], 1, hs[0], hs[1], -hs[2], 1};
        float w[12], sc[3][4];
        EAGL_MatTransform4(&m, 3, pts, 16, w, 16);
        for (int k = 0; k < 3; k++) EAGL_ViewProject(view, 1, w + 4 * k, sc[k]);
        float d1 = (sc[1][0] - sc[0][0]) * (sc[1][0] - sc[0][0]) + (sc[1][1] - sc[0][1]) * (sc[1][1] - sc[0][1]);
        float d2 = (sc[2][0] - sc[0][0]) * (sc[2][0] - sc[0][0]) + (sc[2][1] - sc[0][1]) * (sc[2][1] - sc[0][1]);
        float size = exe_f32(0x5b2a8c) / (float)g_renderopts.width * (sqrt_bits(d1) + sqrt_bits(d2));
        if (size <= exe_f32(0x5b2600)) return false;
        float f = exe_f32(0x5b24a8) - sqrt_bits(size) * 0.09765625f;
        if (f < exe_f32(0x5b23f8)) f = exe_f32(0x5b23f8);
        f *= (float)(levels - 1);
        if (!(fl & 0x10)) f = (f + exe_f32(0x5b24a8)) * exe_f32(0x5b23f0) * f;
        f -= 0.5f;   /* (the double at 0x5b2778) */
        int lod = base + (int)rintf(f);
        int cap = 7 + (q < 2 ? -1 : -2);   /* (R3DCar_LODCount 0x43acd0: 7) */
        if (lod < cap) cap = lod < 0 ? 0 : lod;
        r->curlod = cap;
    }
lower:
    /* 0x43e5a0 for each level below the drawn one: for its wheel slots that have an article (+0x14), the
       slot's offset height (+0x6d0, R3DCar_WheelPos 0x438140) from the car's wheel record (+0x820, 0xc4
       apart; the wheels in the order 0x5b36f0, mirrored 0x5b3700). A car drawn at level 0 keeps its. */
    for (int k = 0; k < r->curlod && k < (int)r->nlods; k++)
        for (int w = 0; w < 4; w++) {
            if (!r->lods[k]->slot[w]) continue;
            uint32_t wheel = exe_u32((g_r3d_mirror ? 0x5b3700u : 0x5b36f0u) + 4u * (uint32_t)w);
            r->wheelpos[w][1] = *CAR_F(car, 0x820 + 0xc4 * (int)wheel);
        }
    return true;
}

/* 0x443540 (the view's +0x11b: alpha reference 10, then 0x442fd0 for each car): its render ticks; the
   viewed car not in its own mirror (the camera's flag 0x10) and, unless the camera's flag 8 says so, the
   car's sphere (+0x330, radius +0x3b4) in the view; then queued into the opaque list (0x466950) by its
   squared distance to the eye, rounded, to be drawn there among the world's objects. */
static void draw_cars(RCamera *c)
{
    THRASH.fn.setstate(0x24, 10);
    s_carcam = c;
    for (int i = 0; i < g_ncars && i < 64; i++) {
        Car *car = g_cars[i];
        R3DCar *r = car ? car->p[CARP_R3D] : NULL;
        if (!r || !r->state) continue;
        const float *m = CAR_F(car, 0x364), *p = CAR_F(car, 0x330);
        R3DCarState *st = r->state;
        for (int k = 0; k < 9; k++) st->rot[k] = k % 3 == 1 ? -m[k] : m[k];
        float h = st->halfsize[1];
        for (int k = 0; k < 3; k++) st->pos[k] = p[k] - m[3 + k] * h + st->rot[3 + k] * h;
        CarAnim_Update(r, car);            /* R3DCar_Animate 0x438370 */
        R3DCar_Dents(r, false);            /* R3DCar_v7 0x4382f0: 0x438160 */
        R3DCar_ApplyMorphs(r);             /* 0x437f60 */
        CarLight_Update(&r->light, car, c->update[0]);   /* R3DCar_v15 0x43a490 (the cars' render ticks) */
        r->lit = true;
        R3DCar_SetLights(r, car);          /* R3DCar_v6 0x439290 */
        bool viewed = *CAR_I(car, CAR_INDEX) == g_camera_car, nocull = false;
        if (viewed) {
            if (g_racecam[0].flags2 & 0x10) continue;   /* (0x442fd0: not in its own mirror) */
            nocull = (g_racecam[0].flags2 & 8) != 0;
        }
        if (!nocull && !EAGL_ViewSphereVisible(c->view, p, *CAR_F(car, 0x3b4))) continue;
        if (!car_lod(r, car, c->view)) continue;   /* R3DCar_v13 0x439ec0 */
        const float *eye = c->frame.eye;
        float dx = p[0] - eye[0], dy = p[1] - eye[1], dz = p[2] - eye[2];
        s_cardraw[i] = (CarDrawObject){{&car_draw_vtbl}, r};
        RListEntry q = {NULL, &s_cardraw[i].base, 0, 0, (uint32_t)(int64_t)rintf(dx * dx + dy * dy + dz * dz)};
        RLists_Insert(RLIST_OPAQUE, &q);   /* 0x466950 */
        /* R3DCar_v7's particles: not the viewed car's in the in-car and hood views. */
        if (!viewed || (g_racecam[0].mode != 0 && g_racecam[0].mode != 1))
            CarParticles_Update(r->particles, true);   /* 0x43eeb0 */
    }
}

/* The race's load, a step a frame: the original's loads at once between the loading screen's steps
   (LoadingScreen 0x4a4850, each with a flip), which a frame of the port's shows one by one. The steps
   follow GameMain 0x4b6840 (the race's set-up, Race_Configure 0x4b4b70, then step 0) and GameStartup
   0x4b65a0: the texture quality, 1 and 2, Render_RaceInit 0x468ef0 (3 after the common art), 4 and 5,
   Track_Load, 6, (the collision sounds) 7, Race_SelectTrackDefaults 0x414dd0 (8 before the cars, 9
   after them), and the race's start; 10 ends the screen. 0 for another frame, 1 done, -1 failed. */
static int s_loadstep;
static int load(const char *track, const char *carname)
{
    char v[8];
    switch (s_loadstep++) {
    case 0:
        snprintf(g_track_name, sizeof g_track_name, "%s", track);
        g_race_type = g_drive_race_type;    /* (its option 0x10, RACE_TYPE: the world's scenes follow it) */
        Replay_Apply();   /* (a replay file's race: GameStartup's 0x413830, the track and the race type its) */
        {
            char st[8];
            if (plat_param("skilltest", st, sizeof st)) {   /* --param skilltest=<n>: <track>_st<n>.scn as a race type 4 */
                g_race_type = 4;
                g_skill_test = atoi(st);
            }
        }
        plat_param("driver", g_race_driver_name, sizeof g_race_driver_name);   /* --param driver=<name>: the player's name */
        if (!RaceSetup_SingleCar(carname)) return -1;
        {
            int opponents = plat_param("opponents", v, sizeof v) ? atoi(v) : g_drive_opponents;   /* --param opponents=<n> */
            g_ai_drive = plat_param("aidrive", v, sizeof v) && v[0] == '1';       /* --param aidrive=1 */
            if (0 < opponents) RaceSetup_Opponents(opponents);
            int cops = plat_param("cops", v, sizeof v) ? atoi(v) : g_drive_cops;   /* --param cops=<n> */
            if (0 < cops) RaceSetup_Cops(cops, g_track_name);   /* (0x4ec590: before the traffic) */
            int traffic = plat_param("traffic", v, sizeof v) ? atoi(v) : g_drive_traffic;   /* --param traffic=<n> */
            if (0 < traffic) RaceSetup_Traffic(traffic, g_track_name);   /* (0x4ec590) */
        }
        if (g_drive_laps > 0) g_race_laps = g_drive_laps;                    /* (the front end's choice) */
        if (plat_param("laps", v, sizeof v)) g_race_laps = atoi(v);          /* --param laps=<n> */
        Replay_Apply();   /* (the file's descriptions and settings over the set-up's) */
        LoadingScreen(0);
        return 0;
    case 1:
        /* GameStartup 0x4b65a0: the texture quality for the machine (0x44de80) before the track's textures.
           The original measures the machine; the port's is one above every threshold (1 GHz, 256 MB, 64 MB
           of video memory): level 3. Until the first race the front end runs at
           the level's initial 0, as the original's does. */
        RTex_detectquality(&RTex_machine);
        LoadingScreen(1);
        return 0;
    case 2:
        LoadingScreen(2);
        return 0;
    case 3: {
        /* Render_RaceInit 0x468ef0: the device's render states kept (0x62814c, 0x38 bytes) for the race's end */
        RenderState *drs = g_render && g_render->device ? g_render->device->rs : NULL;
        free(g_racerstate);
        g_racerstate = drs ? malloc(sizeof *g_racerstate) : NULL;
        if (g_racerstate) *g_racerstate = *drs;
        Render_LoadViewKeys(g_track_name);  /* 0x44f9b0 */
        Render_LoadCommonFsh(g_render ? g_render->textures : NULL);   /* 0x448a40 */
        LoadingScreen(3);
        return 0;
    }
    case 4:
        Noise_Init();                       /* 0x44f110 (Render_RaceInit 0x468ef0) */
        Sprites_Init();                     /* 0x451360 */
        Particles_Init();                   /* 0x455180 */
        DynLights_Init();                   /* 0x455e30 */
        Sky_LoadFsh(g_track_name, false);
        Weather_Init();                     /* 0x458a50 */
        SpriteBank_Load();                  /* 0x452090 */
        LoadingScreen(4);
        return 0;
    case 5:
        LoadingScreen(5);
        return 0;
    case 6:
        Track_Load();
        if (!g_world || !g_world->root) return -1;
        LoadingScreen(6);
        return 0;
    case 7:
        LoadingScreen(7);   /* (after the collision sounds, which the port loads with the race's sounds below) */
        return 0;
    case 8:
        CarEnv_Load(g_track_name);          /* 0x440fa0 (R3DCar_LoadEnvMap) */
        Headlights_Load();                  /* 0x452460 */
        RaceViews_ctor(&views);
        Traffic_SmackDescs();   /* 0x4a15d0 (Race_SelectTrackDefaults 0x414dd0) */
        LoadingScreen(8);
        return 0;
    case 9:
        Car_Construct();
        if (g_ncars < 1) return -1;
        LoadingScreen(9);
        return 0;
    default:
        break;
    }
    Headlights_SetCar(g_cars[0]);   /* 0x452550 (the viewed car's) */
    /* The race's set-up: the grid slots behind the start line, on the road (0x410790). */
    /* The race from its first tick (0x414ed0) with the fly-in; --param start=<tick> (a port test aid)
       starts it later, the camera straight in its mode (the hash tests use 384, a second before the
       countdown's end). */
    s_start = plat_param("start", v, sizeof v) ? atoi(v) : 0;
    if (s_start < 0) s_start = 0;
    g_sim_tick = s_start;
    s_slot = s_start;   /* (0x414ed0: the slots from the race's start) */
    SimRand_Seed((uint32_t)g_race_seed);   /* 0x414ed0 (0x4a4560): before the cars' set-up (0x411100) */
    Render_SeedRand((uint32_t)g_race_seed);   /* (0x44fbd0) */
    for (int i = 0; i < g_ncars; i++) Car_RaceInit(g_cars[i], i);
    for (int i = 0; i < g_ntraffic; i++) Car_RaceInit(g_traffic[i], (uint32_t)(g_ncars + i));
    AI_Load();             /* the racing line (0x4031f0) */
    Input_Reset(Replay_Loaded());   /* (a file's replay kept) */
    Controls_Load();   /* RaceLoop: 0x412e20, the options' bindings, 0x4117c0 */
    g_input_cursor = g_input_samples;   /* (0x606ac8) */
    if (g_ai_drive) *CAR_I(g_cars[0], 0xda8) = 1;   /* (the AIDRIVE key) */
    Police_LoadScript();   /* AI_LoadCopScript 0x403040 */
    AI_MakeControllers();  /* 0x406580 */
    AI_LoadData();         /* 0x406950: the personalities, the traffic's configuration */
    {
        /* The race camera on the viewed car (--param follow=<car>), in its list's first mode or
           --param cam=<mode>. */
        int follow = plat_param("follow", v, sizeof v) ? atoi(v) : 0;
        if (follow < 0 || follow >= g_ncars) follow = 0;
        g_camera_car = follow;
        int mode = plat_param("cam", v, sizeof v) ? atoi(v) : DESC_I(g_cars[follow], 0x208);
        Camera_Init(0, g_cars[follow], s_start == 0 ? 0xe : mode);   /* (0x463f70: mode 14, the fly-in) */
        int look = plat_param("camlook", v, sizeof v) ? atoi(v) : -1;   /* --param camlook=<car>: the car looked at */
        if (look >= 0 && look < g_ncars) g_racecam[0].base = g_cars[look];
    }
    /* The race's sounds (GameStartup 0x4b65a0: Audio_Init 0x4a6610, the player's engine 0x4a8aa0 ->
       0x4ac9e0 from its description's SOUNDNAME). */
    if (Audio_Init()) {
        Audio_LoadCollisionSounds((const char *)CAR_DESCB(g_cars[0]) + 0x2e8);
        Audio_LoadTrackSounds();   /* 0x4afda0 */
        Audio_StartRace();         /* 0x4a8670 */
    }
    g_render_cars = draw_cars;
    HUD_Init();   /* 0x424420 */
    if (g_race_view == 2) HUD_RaceOver();   /* (RaceLoop's 0x426a40 with the view 2: a file's replay) */
    else HUD_RaceStart();   /* (RaceLoop's 0x426a40 with 0) */
    set_env();
    plat_set_frame_rate(60.0);
    LoadingScreen(10);
    return 1;
}

/* The controls sampled (Input_Sample 0x412e90: fe.txt's bindings, controls.c) into the local
   player's packet. --param autodrive=1 (a test aid for headless runs) holds the gas, 2 and right, 3 and
   left: what the keys' readers give (full keys: the steering a quarter of 0xff, the gas 0x7e). */
static void read_controls(void)
{
    Input_Sample();
    static int autodrive = -1;
    if (autodrive < 0) {
        char v[8];
        autodrive = plat_param("autodrive", v, sizeof v) ? v[0] - '0' : 0;
    }
    if (autodrive >= 1) g_input_packet[2] = 0x7e;
    if (autodrive == 2) g_input_packet[1] = 0xff / 4;
    if (autodrive == 3) g_input_packet[1] = (uint8_t)(-0xff / 4);
}

int g_drive_opponents, g_drive_laps, g_drive_race_type, g_drive_traffic, g_drive_cops;
int g_drive_result;

int DRIVE_Results(int places[8], int times[8], bool clean[8])
{
    int n = 0;
    for (int i = 0; ready && i < g_racers.n && n < 8; i++, n++) {
        Car *c = g_racers.c[i];
        places[n] = *CAR_I(c, 0xcd0) ? *CAR_I(c, 0xcd4) : 0;   /* (the results' copies) */
        times[n] = *CAR_I(c, 0xcd8);
        clean[n] = c->b[0xcc0] == 0;   /* (the career's safe driving: no more damage than at the start) */
    }
    return n;
}

bool DRIVE_PlayerResult(char name[32], char car[32], int32_t *best, int32_t *time)
{
    Car *c = ready && g_racers.n ? g_racers.c[0] : NULL;
    if (!c) return false;
    /* (the results' stats of racer 0, 0x5e7644 the driver, 0x5e7684 the car, 0x5e77c8 the best lap,
       0x5e77a4 the time: the description's and the results' copies) */
    snprintf(name, 32, "%.31s", (const char *)CAR_DESCB(c) + 0x17c);
    snprintf(car, 32, "%.31s", (const char *)CAR_DESCB(c) + 0x534);
    *best = *CAR_I(c, 0xcfc);
    *time = *CAR_I(c, 0xcd8);
    return true;
}

bool DRIVE_MissionStats(int32_t *time, int32_t *damage, int32_t *cones)
{
    Car *c = ready && g_racers.n ? g_racers.c[0] : NULL;
    if (!c) return false;
    *time = *CAR_I(c, 0xcd8);
    int32_t sum = 0;
    for (int off = 0xc98; off < 0xcc0; off += 4) sum = (int32_t)((float)sum + *CAR_F(c, off));
    *damage = sum / 10;
    *cones = *CAR_I(c, 0xb60);
    return true;
}

static int32_t s_wear_ids[20];
static float s_wear[20];
static bool s_wear_kept;

/* The parts' wear as the race-over's results left it (+0xbf8 the ids, +0xc48 the wear, 0x493c60), kept
   before the replay starts the race again. */
static void keep_wear(void)
{
    Car *c = g_cars[0];
    if (!c) return;
    for (int i = 0; i < 20; i++) {
        s_wear_ids[i] = *CAR_I(c, 0xbf8 + 4 * i);
        memcpy(&s_wear[i], c->b + 0xc48 + 4 * i, 4);
    }
    s_wear_kept = true;
}

int DRIVE_PartsWear(int32_t ids[20], float wear[20])
{
    if (!ready || !s_wear_kept) return 0;
    memcpy(ids, s_wear_ids, sizeof s_wear_ids);
    memcpy(wear, s_wear, sizeof s_wear);
    return 20;
}

int DRIVE_PlayerPlace(void)
{
    Car *c = g_cars[0];
    /* the results' copies the race-over keeps through the replay (+0xcd0 the finish, +0xcd4 the place) */
    if (!ready || !c || *CAR_I(c, 0xcd0) == 0) return 0;
    return *CAR_I(c, 0xcd4);
}
bool g_drive_paused;

/* --param hudscript=<frame>@<x>:<y>,<frame>=<text>,...: the race's frames (from its start) at which the
   mouse goes to (x, y) in the display mode's pixels, the left button down that frame and up the next,
   or the text's keys are typed one a frame (tests of the HUD's menus); <frame>!<x>:<y> the mouse there
   with the button held, <frame>^<x>:<y> there with it let go (drags). */
static void hud_script(void)
{
    static char sc[256];
    static int state = -1, frame;
    if (state < 0) state = plat_param("hudscript", sc, sizeof sc) ? 1 : 0;
    if (!state) return;
    frame++;
    for (const char *p = sc; *p;) {
        int f = atoi(p);
        const char *q = p;
        while (*q >= '0' && *q <= '9') q++;
        const char *end = strchr(q, ',');
        if (!end) end = q + strlen(q);
        if (*q == '@' || *q == '!' || *q == '^') {
            int x = atoi(q + 1);
            const char *c = strchr(q, ':');
            int y = c && c < end ? atoi(c + 1) : 0;
            if (*q != '@') {
                if (frame == f) MOUSE_Override(true, x, y, *q == '!');
            } else if (frame == f) MOUSE_Override(true, x, y, 1);
            else if (frame == f + 1) MOUSE_Override(true, x, y, 0);
        } else if (*q == '=') {
            int i = frame - f;
            if (0 <= i && i < end - q - 1) {
                static const char keys[] = "abcdefghijklmnopqrstuvwxyz0123456789";
                static const uint8_t dik[] = {0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26,
                                              0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d,
                                              0x15, 0x2c, 0x0b, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a};
                const char *k = strchr(keys, q[1 + i]);
                if (k) HUD_TypeKey(dik[k - keys], false);
            }
        }
        p = *end ? end + 1 : end;
    }
}

/* The pause key (0x415130 with 1: I_PAUSE pressed): in the race it pauses when no modal screen is up;
   in the race-over (the replay) it ends the race. */
static void pause_key(void)
{
    if (!Input_Action(ACT_PAUSE, true) || g_sim_tick <= 0x20) return;
    if (g_replay_mode < 2) {
        if (!HUD_ModalShown()) {
            Audio_Pause();           /* 0x4aaf20 */
            g_drive_paused = true;   /* 0x606a90 */
            HUD_Pause();             /* 0x4267c0 */
        }
    } else {
        if (g_sim_tick < 0x21) g_race_quit = 1;   /* (0x415130: so early it's a quit) */
        g_drive_result = 1;          /* Race_End(1) */
    }
}

/* RaceLoop's walk over the interface keys' samples since the last frame (0x606ac8 up to 0x5e99b8, only
   the last when more than 16 behind): the orbit keys while a screen is up, the HUD's keys, the camera's
   and the look keys while not paused, then the pause key. */
/* RaceLoop's screenshot key (0x415300): the display copied into a window of its size and depth, that
   converted to 15 bits (0x7e), written as Unleashed<nn>.tga in the save directory; nn 0 .. 98, then 0
   again (0x606adc, from 0 each run). */
static void race_screenshot(void)
{
    static int shot;   /* 0x606adc */
    char f[40];
    snprintf(f, sizeof f, exe_str(0x5cc7d4), "", shot);
    Window *w = WINDOW_new(WINDOW_display.width, WINDOW_display.height, WINDOW_display.depth, 0);
    if (!w) return;   /* (the original reads through it) */
    WINDOW_selectdisplay();          /* 0x5342a0 */
    WINDOW_readshape(w->shape);      /* 0x535390 */
    WINDOW_deselect();
    uint8_t *c = SHAPE_converttype(w->shape, DEPTH_bpptotype(15), 0, 0);   /* 0x5350f0, 0x534ab0 */
    Window *cw = c ? WINDOW_fromshape(c, 0) : NULL;
    if (cw) {
        WINDOW_select(cw);
        WINDOW_savetga(1, f);        /* 0x534080 */
        WINDOW_deselect();
        WINDOW_destroy(cw);
    }
    WINDOW_destroy(w);
    MEM_purge(c);
    if (++shot > 0x62) shot = 0;
}

static void race_actions(void)
{
    if (g_input_samples - g_input_cursor > 0x10) g_input_cursor = g_input_samples;
    for (; g_input_cursor <= g_input_samples; g_input_cursor++) {
        if (g_drive_result) break;
        if (HUD_ModalShown() && g_replay_mode < 2) {
            if (Input_Action(ACT_ROTCAMCCLOCK, true)) g_racecam[0].lookspeed = 3;    /* 0x461650 */
            if (Input_Action(ACT_ROTCAMCLOCK, true)) g_racecam[0].lookspeed = -3;   /* 0x461660 */
            if (Input_Action(ACT_ROTCAMSTOP, true)) g_racecam[0].lookspeed = 0;     /* 0x461670 */
        }
        if (Input_Action(ACT_SCREENSHOT, true)) race_screenshot();
        if (Input_Action(ACT_TOGGLEPAUSEMENU, true)) HUD_TogglePauseMenu();   /* 0x425710 */
        if (!g_drive_paused) {
            if (Input_Action(ACT_VIEWDISTANCE, true))   /* 0x44f0f0 */
                g_renderopts.adjustableviewdistance = (g_renderopts.adjustableviewdistance + 1) % 4;
            if (Input_Action(ACT_HORIZON, true)) Sky_ToggleHorizon();   /* 0x45dc40's +0x60 */
            if (Input_Action(ACT_TOGGLEHUD, true)) HUD_ToggleHud();             /* 0x427370 */
            if (Input_Action(ACT_CYCLEMAP, true)) HUD_CycleMap();               /* 0x426700 */
            if (Input_Action(ACT_ZOOMINMAP, true)) HUD_ZoomMap(true);           /* 0x4275c0 */
            /* (I_CYCLEHUD's pause: its test, 0x427a80, is never true) */
            if (Input_Action(ACT_TOGGLEREARVIEW, true)) HUD_ToggleRearView();   /* 0x427510 */
            if (Input_Action(ACT_TOGGLEMAP, true)) HUD_ToggleMap();             /* 0x427320 */
            if (Input_Action(ACT_ZOOMOUTMAP, true)) HUD_ZoomMap(false);         /* 0x4275f0 */
            if (Input_Action(ACT_HUDTRANSPARENCY, true)) HUD_ToggleTrays();     /* 0x427590 */
            if (Input_Action(ACT_SFX, true)) Audio_ToggleSfx();   /* 0x4ab2b0 */
            if (Input_Action(ACT_MUSIC, true)) Music_Next();      /* 0x4ad560 */
            if (Input_Action(ACT_CYCLECAMERA, true) && g_replay_mode < 2) Camera_NextMode(0);   /* 0x465920 */
            g_look[0] = Input_Action(ACT_LOOKFORWARD, false);
            g_look[1] = Input_Action(ACT_LOOKRIGHT, false);
            g_look[2] = Input_Action(ACT_LOOKBEHIND, false);
            g_look[3] = Input_Action(ACT_LOOKLEFT, false);
        }
        pause_key();
    }
}

/* The typed keys and the menus' mouse (HUD_HandleInput 0x425eb0 in RaceLoop 0x415300). */
static void race_menus(void)
{
    uint8_t k[16];
    int nk = plat_key_presses(k, 16);
    uint8_t held[KEY_COUNT] = {0};
    bool shift = plat_keys(held) && (held[0x2a] || held[0x36]);
    for (int i = 0; i < nk; i++) HUD_TypeKey(k[i], shift);   /* (the window's WM_CHAR, 0x610a80) */
    hud_script();
    int32_t mx, my;
    uint32_t b = 0;
    static uint32_t was;
    int event = 0;   /* (the window's mouse message: 2 the left button down, 1 up) */
    if (MOUSE_state(&mx, &my, &b)) {
        if ((b & 1) && !(was & 1)) event = 2;
        else if (!(b & 1) && (was & 1)) event = 1;
        was = b;
    }
    int act = HUD_HandleInput(0, 0, event);
    /* The replay panel's: RaceLoop's map onto the replay's controls (0x414870). */
    static const int8_t replay_map[0x18] = {[6] = 8, [7] = 1, [8] = 3, [9] = 2, [10] = 4, [0xb] = 5, [0xc] = 6,
                                            [0xd] = 7, [0x16] = 9, [0x17] = 10};
    if (6 <= act && act < 0x18 && (replay_map[act] || act == 7)) {
        if (Replay_Control(replay_map[act]) == 2) s_replay_again = true;   /* restart (0x657428) */
        return;
    }
    switch (act) {
    case 2:
        if (!Replay_Paused()) Audio_Resume();   /* 0x4aafd0 (a paused replay's stay off) */
        g_drive_paused = false;
        break;
    case 3:
        g_drive_result = 2;
        break;
    case 4:   /* exit to windows (after the HUD's question) */
        g_drive_result = 3;
        break;
    case 5:   /* exit to game menu */
        g_drive_result = 1;
        break;
    case 0x15:
        /* view replay: the race loop ends and GameMain's second loop replays it (0x60ca10). The port's
           race-over path replays it the same way, behind the results. */
        g_drive_paused = false;
        g_race_over = 1;
        break;
    default:
        break;
    }
}

bool DRIVE_selftest(const char *track, const char *car)
{
    if (!g_render || failed) return false;
    if (!ready) {
        int r = load(track, car && car[0] ? car : "964");
        if (r < 0) { failed = true; return false; }
        if (r == 0) return true;
        ready = true;
    }
    Car *c = g_cars[0];
    App_WaitActive();   /* 0x4b0d70 (RaceLoop) */
    race_menus();
    if (g_replay_mode == 2 && !g_drive_paused) {
        /* RaceLoop's replay part: its quit ends the race; its pause stops it (the cameras updated once,
           the sounds stopped) */
        if (s_replay_again) {
            /* (the sounds' stop, 0x4ab130, with the restart below) */
        } else if (Replay_ExitWanted()) {
            Audio_Stop();    /* 0x4ab040 */
            if (!g_drive_result) g_drive_result = 1;
        } else if (Replay_Paused() != Replay_PauseWanted()) {
            Replay_SetPaused(Replay_PauseWanted());
            if (Replay_Paused()) {
                Camera_Update();
                Audio_Pause();    /* 0x4aaf20 */
            } else {
                Audio_Resume();   /* 0x4aafd0 */
            }
        }
    }
    /* The simulation's 64 Hz from the 60 Hz frames (none while paused). */
    bool held = g_drive_paused || (g_replay_mode == 2 && Replay_Paused());
    ticks_due += held ? 0.0 : 64.0 / 60.0;
    if (held) {
        /* (the timer's samples at 32 Hz go on while the simulation waits; nothing is queued) */
        static double due;
        due += 32.0 / 60.0;
        for (; due >= 1.0; due -= 1.0) Input_Sample();
    }
    int n = (int)ticks_due;
    ticks_due -= n;
    for (int i = 0; i < n && i < 4; i++) {
        if (g_replay_mode == 2) {
            /* The replay's slots (0x606aa0): at a quarter (speed 1) or half (0) speed the simulation
               steps on every fourth or second, the others hold the cameras; at double (3) two steps. */
            if (!(s_slot & 1)) {
                read_controls();
                Sim_HalfStep(s_slot >> 1);   /* 0x415f10 */
            }
            Camera_Hold(false);
            int sp = Replay_Speed();
            if ((sp == 1 && (s_slot & 3)) || (sp == 0 && (s_slot & 1))) {
                Camera_Hold(true);   /* 0x606ad4 */
                Camera_Update();
            } else {
                if (sp == 3) Sim_Step();
                Sim_Step();   /* 0x415010 */
            }
            s_slot++;
            continue;
        }
        if (!(g_sim_tick & 1)) {
            read_controls();
            if (g_replay_mode < 2) Input_QueuePacket();   /* 0x413820 */
            Sim_HalfStep(g_sim_tick >> 1);   /* 0x415f10 */
        }
        Sim_Step();   /* 0x415010 */
    }
    race_actions();
    {
        /* --param finish=<tick>: the player finished there (a test aid; the race's own finish is the
           lap count's, 0x417460). */
        static int finish = -2;
        char fv[12];
        if (finish == -2) finish = plat_param("finish", fv, sizeof fv) ? atoi(fv) : -1;
        if (0 < finish && finish <= g_sim_tick && *CAR_I(c, 0xb50) == 0) {
            *CAR_I(c, 0xb50) = 2;
            *CAR_I(c, 0xb14) = g_sim_tick;
            *CAR_I(c, 0xb24) = g_sim_tick - 0x1c0;
        }
    }
    /* The race over (all the humans finished): the race-over screens (the original's second race loop,
       race view 2, replays the race behind them). */
    {
        char ev[12];   /* --param endrace=<tick>: the race over at that tick (port addition, tests) */
        if (!g_race_over && plat_param("endrace", ev, sizeof ev) && atoi(ev) <= g_sim_tick) {
            g_race_over = 1;
            Race_End(2);   /* (as Race_TickOdd's end does: the results' copies the screens read) */
        }
        /* --param shot=<tick>: the screenshot key at that tick, once (tests) */
        static bool shot_done;
        if (!shot_done && plat_param("shot", ev, sizeof ev) && atoi(ev) <= g_sim_tick) {
            shot_done = true;
            race_screenshot();
        }
        /* --param raceexit=<tick>: the race-over's Escape at that tick of its replay (tests) */
        if (g_replay_mode == 2 && plat_param("raceexit", ev, sizeof ev) && atoi(ev) <= g_sim_tick) g_drive_result = 1;
        /* --param replayagain=<tick>: the replay panel's restart at that tick of the replay, once (tests) */
        static bool again_done;
        if (g_replay_mode == 2 && !again_done && plat_param("replayagain", ev, sizeof ev) && atoi(ev) <= g_sim_tick) {
            again_done = true;
            HUD_ReplayRestart();
            if (Replay_Control(1) == 2) s_replay_again = true;
        }
    }
    if ((g_race_over && g_race_view < 2) || s_replay_again) {
        /* GameMain's second race loop: the race again from its start, replaying the packets
           (0x414ed0, 0x413ec0), the HUD's visuals hidden (0x425740), then the race-over screens. The
           replay panel's restart (0x657428) ends the loop and GameMain runs it again: the results'
           screens then stay hidden (0x60ca10). */
        if (s_replay_again) Audio_Flush();   /* 0x4ab130 */
        else {
            keep_wear();
            char rf[40];   /* --param replaysave=<file>: the race's replay written as the race-over starts (tests) */
            if (plat_param("replaysave", rf, sizeof rf)) Replay_Write(rf);
        }
        s_replay_again = false;
        g_race_view = 2;
        g_replay_mode = 2;
        World_Restart();   /* 0x471ac0 */
        Race_Restart(s_start);
        Input_Reset(true);
        Camera_ReplayStart();   /* 0x413ec0 */
        s_slot = g_sim_tick;    /* (0x414ed0 restarts the slots with the ticks) */
        HUD_ReplayPlaying();
        if (g_ai_drive) *CAR_I(g_cars[0], 0xda8) = 1;
        HUD_HideAll();      /* 0x425740 */
        HUD_RaceOver();     /* 0x426a40 */
    }
    Camera_Update();   /* 0x463af0 (the race loop's, after the ticks) */
    char lv[4];
    if (plat_param("ailog", lv, sizeof lv) && g_sim_tick % 128 < 2)
        for (int i = 0; i < g_ncars; i++) {
            Car *o = g_cars[i];
            char b[160];
            int16_t node;
            memcpy(&node, o->b + 8, 2);
            snprintf(b, sizeof b, "view %d tick %d car %d node %d speed %.1f target %.1f gear %d thr %d brk %d steer %d", g_race_view, g_sim_tick, i, node, *CAR_F(o, 0x35c) * 3.6f,
                     *CAR_F(o, 0xeb8) * 3.6f, (int8_t)o->b[0xd82], o->b[0xd7c], o->b[0xd7d], *CAR_I(o, 0xd90));
            plat_log(b);
        }
    TexManager *tm = g_texgraphics ? g_texgraphics->textures : NULL;
    if (tm) TexManager_begin(tm);
    Render_Frame(&views);
    if (tm) TexManager_end(tm);
    return true;
}

/* The race's end (GameShutdown 0x4b66e0 with Render_RaceShutdown, as far as the port has them): the HUD,
   the AI, the sounds, the cars' renderers and tasks, the render module's art and lights, the world; a
   later DRIVE_selftest loads a race again. (The sky object and the cars' memory stay: their ends aren't
   ported.) */
void DRIVE_Shutdown(void)
{
    if (!ready) return;
    Replay_Write("replay.rpl");   /* Replay_Save 0x413ff0: the default replay */
    HUD_Free();        /* 0x4252f0 */
    AI_Free();
    Police_FreeScript();    /* 0x4030d0 */
    Audio_Shutdown();  /* 0x4a8850 */
    for (int i = 0; i < g_ncars; i++) {
        R3DCar *r = g_cars[i] ? g_cars[i]->p[CARP_R3D] : NULL;
        if (r) {
            CarParticles_Free(r->particles);   /* (R3DCar's destructor: 0x43ee50) */
            r->particles = NULL;
            R3DCar_Free(r);
            g_cars[i]->p[CARP_R3D] = NULL;
        }
        for (int w = 0; g_cars[i] && w < 4; w++) {   /* (the car's destructor: its wheels' trails) */
            free(g_cars[i]->p[CARP_WHEELS + w]);
            g_cars[i]->p[CARP_WHEELS + w] = NULL;
        }
    }
    g_ncars = 0;
    g_tasks_tick.count = g_tasks_even.count = 0;   /* (the lists are the race's) */
    g_render_cars = NULL;
    Headlights_Free();      /* 0x452530 */
    CarEnv_Free();          /* 0x440fd0 */
    SpriteBank_Free();      /* 0x4520c0 */
    Weather_Free();         /* 0x458a80 */
    Sky_Free();             /* 0x45dbd0 (the sun with it) */
    DynLights_Free();       /* 0x455ec0 */
    Sprites_Free();         /* 0x451480 */
    Render_FreeViewKeys();  /* 0x44f9e0 */
    CarParticles_Clear();   /* 0x43ecd0 */
    Particles_Free();       /* 0x4552c0 */
    R3DCar_FreeCommon();    /* 0x443a80 (0x414fa0: the car commons go with the race) */
    Render_FreeCommonFsh(); /* 0x448af0 */
    /* (Render_RaceShutdown's end) the render states kept at the race's start back on the device, the fog
       off in the record and sent (0x538890 -> 0x5374d0), then off at the driver */
    RenderState *drs = g_render && g_render->device ? g_render->device->rs : NULL;
    if (drs && g_racerstate) *drs = *g_racerstate;
    free(g_racerstate);
    g_racerstate = NULL;
    if (drs) {
        RenderState_SetFogMode(drs, 0);
        RenderState_Send(drs);
    }
    THRASH.fn.setstate(0x15, 0);
    Scene_Clear();          /* 0x46cf60 */
    BWorld_DeInit();        /* 0x471ba0 */
    g_race_over = 0;
    g_race_view = 0;
    g_replay_mode = 0;
    ticks_due = 0;
    ready = failed = false;
    s_loadstep = 0;
    g_drive_paused = false;
    g_drive_result = 0;
    s_replay_again = false;
    s_wear_kept = false;
    Replay_Unload();
}
