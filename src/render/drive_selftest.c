/* The drive mode (see drive_selftest.h). */
#include "render/drive_selftest.h"
#include "real/mouse.h"
#include "eagl/eagl.h"
#include "render/headlights.h"
#include "game/ai.h"
#include "game/car.h"
#include "game/car_damage.h"
#include "game/car_drive.h"
#include "game/car_physics.h"
#include "game/race_setup.h"
#include "game/mission.h"
#include "game/race.h"
#include "game/hud.h"
#include "game/input.h"
#include "game/sim_rand.h"
#include "game/race_progress.h"
#include "pending.h"
#include "render/carpages.h"
#include "game/race_car.h"
#include "game/scene.h"
#include "game/track.h"
#include "game/wtree.h"
#include "platform.h"
#include "real/texture.h"
#include "render/r3dcar.h"
#include "render/render.h"
#include "render/rtex.h"
#include "render/sprite.h"
#include "render/dynlight.h"
#include "render/render_pending.h"
#include "render/rlayers.h"
#include "render/rlists.h"
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
        CarAnim_Update(r, car);            /* R3DCar_Animate 0x438370 */
        CarLight_Update(&r->light, car);   /* R3DCar_v15 0x43a490 (the cars' render ticks) */
        r->lit = true;
        R3DCar_SetLights(r, car);          /* R3DCar_v6 0x439290 */
        R3DCar_Draw(r, &c->view->model);
    }
}

static bool load(const char *track, const char *carname)
{
    snprintf(g_track_name, sizeof g_track_name, "%s", track);
    g_race_type = g_drive_race_type;    /* (its option 0x10, RACE_TYPE: the world's scenes follow it) */
    {
        char st[8];
        if (plat_param("skilltest", st, sizeof st)) {   /* --param skilltest=<n>: <track>_st<n>.scn as a race type 4 */
            g_race_type = 4;
            g_skill_test = atoi(st);
        }
    }
    /* GameStartup 0x4b65a0: the texture quality for the machine (0x44de80) before the track's textures.
       The original measures the machine; the port's is one above every threshold (1 GHz, 256 MB, 64 MB
       of video memory) on THRASH's 32-bit display: level 3. Until the first race the front end runs at
       the level's initial 0, as the original's does. */
    RTex_detectquality(&(RTexMachine){1000000000, 256ll << 20, 64u << 20, 32});
    Track_Load();
    if (!g_world || !g_world->root) return false;
    Render_LoadCommonFsh(g_render ? g_render->textures : NULL);   /* 0x448a40 */
    Noise_Init();                       /* 0x44f110 (Render_RaceInit 0x468ef0) */
    Sprites_Init();                     /* 0x451360 */
    DynLights_Init();                   /* 0x455e30 */
    Sky_LoadFsh(g_track_name, false);
    SpriteBank_Load();                  /* 0x452090 */
    CarEnv_Load(g_track_name);          /* 0x440fa0 (R3DCar_LoadEnvMap) */
    Headlights_Load();                  /* 0x452460 */
    RaceViews_ctor(&views);
    if (!RaceSetup_SingleCar(carname)) return false;
    char v[8];
    int opponents = plat_param("opponents", v, sizeof v) ? atoi(v) : g_drive_opponents;   /* --param opponents=<n> */
    g_ai_drive = plat_param("aidrive", v, sizeof v) && v[0] == '1';       /* --param aidrive=1 */
    if (0 < opponents) RaceSetup_Opponents(opponents);
    if (g_drive_laps > 0) g_race_laps = g_drive_laps;                    /* (the front end's choice) */
    if (plat_param("laps", v, sizeof v)) g_race_laps = atoi(v);          /* --param laps=<n> */
    Car_Construct();
    if (g_ncars < 1) return false;
    Headlights_SetCar(g_cars[0]);   /* 0x452550 (the viewed car's) */
    /* The race's set-up: the grid slots behind the start line, on the road (0x410790). */
    g_sim_tick = 0x1c0 - 0x40;   /* a second before the countdown's end */
    SimRand_Seed((uint32_t)g_race_seed);   /* 0x414ed0 (0x4a4560): before the cars' set-up (0x411100) */
    for (int i = 0; i < g_ncars; i++) Car_RaceInit(g_cars[i], i);
    AI_Load();             /* the racing line (0x4031f0) */
    Input_Reset(false);
    if (g_ai_drive) *CAR_I(g_cars[0], 0xda8) = 1;   /* (the AIDRIVE key) */
    AI_MakeControllers();  /* 0x406580 */
    {
        /* The race camera on the viewed car (--param follow=<car>), in its list's first mode or
           --param cam=<mode>. */
        int follow = plat_param("follow", v, sizeof v) ? atoi(v) : 0;
        if (follow < 0 || follow >= g_ncars) follow = 0;
        g_camera_car = follow;
        int mode = plat_param("cam", v, sizeof v) ? atoi(v) : DESC_I(g_cars[follow], 0x208);
        Camera_Init(0, g_cars[follow], mode);
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
    set_env();
    RLists_Init();
    plat_set_frame_rate(60.0);
    return true;
}

/* The driver: the keyboard (DirectInput codes) or the pad into the car's controls. */
/* The driver: the keyboard (DirectInput codes) or the pad into the local player's packet (port: the
   original samples its configured controls, Input_Sample 0x412e90; the keyboard's steering eases in). */
static void read_controls(void)
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
    bool reset = (kb && keys[0x13]) || (pad & PAD_Y);   /* R: C1_COMMAND2_RESET */
    static int steer;
    int target = left == right ? 0 : left ? -0x3f : 0x3f;
    int step = 8;   /* the keyboard's steering eases over about an eighth of a second */
    if (steer < target) steer = steer + step < target ? steer + step : target;
    else if (steer > target) steer = steer - step > target ? steer - step : target;
    g_input_packet[0] = (uint8_t)((hand ? 1 : 0) | (reset ? 5 << 3 : 0));
    /* The cameras (the original's configured view keys): C the next of the car's list, B looking back. */
    static bool was_c;
    bool cam_key = kb && keys[0x2e];
    if (cam_key && !was_c) Camera_NextMode(0);
    was_c = cam_key;
    g_look[2] = kb && keys[0x30];
    g_input_packet[1] = (uint8_t)(int8_t)steer;
    g_input_packet[2] = up ? 0x7f : 0;
    g_input_packet[3] = down ? 0x7f : 0;
}

int g_drive_opponents, g_drive_laps, g_drive_race_type;
int g_drive_result;

int DRIVE_Results(int places[8], int times[8])
{
    int n = 0;
    for (int i = 0; ready && i < g_racers.n && n < 8; i++, n++) {
        Car *c = g_racers.c[i];
        places[n] = *CAR_I(c, 0xcd0) ? *CAR_I(c, 0xcd4) : 0;   /* (the results' copies) */
        times[n] = *CAR_I(c, 0xcd8);
    }
    return n;
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

/* The pause key (Input_Action 8: Escape) and the menus' mouse (0x415130, HUD_HandleInput 0x425eb0 in
   RaceLoop 0x415300). In the race Escape pauses when no modal screen is up; in the race-over (the
   replay) it ends the race. */
static void race_menus(void)
{
    uint8_t k[16];
    int nk = plat_key_presses(k, 16);
    bool esc = false;
    for (int i = 0; i < nk; i++) esc |= k[i] == 0x01;
    if (esc && 0x20 < g_sim_tick) {
        if (g_replay_mode < 2) {
            if (!HUD_ModalShown()) {
                PENDING_NOTE(0x4aaf20, "the sounds paused with the race");
                g_drive_paused = true;   /* 0x606a90 */
                HUD_Pause();             /* 0x4267c0 */
            }
        } else {
            g_drive_result = 1;          /* Race_End(1) */
        }
    }
    int32_t mx, my;
    uint32_t b = 0;
    static uint32_t was;
    bool click = false;
    if (MOUSE_state(&mx, &my, &b)) {
        click = (b & 1) && !(was & 1);
        was = b;
    }
    switch (HUD_HandleInput(0, 0, click)) {
    case 2:
        g_drive_paused = false;
        break;
    case 3:
        g_drive_result = 2;
        break;
    case 4:   /* exit to windows (the original's confirmation message first: pending, hud.c) */
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
        ready = true;
        if (!load(track, car && car[0] ? car : "964")) { failed = true; return false; }
    }
    Car *c = g_cars[0];
    race_menus();
    /* The simulation's 64 Hz from the 60 Hz frames (none while paused). */
    ticks_due += g_drive_paused ? 0.0 : 64.0 / 60.0;
    int n = (int)ticks_due;
    ticks_due -= n;
    for (int i = 0; i < n && i < 4; i++) {
        if (!(g_sim_tick & 1)) {
            read_controls();
            if (g_replay_mode < 2) Input_QueuePacket();   /* 0x413820 */
            Sim_HalfStep(g_sim_tick >> 1);   /* 0x415f10 */
        }
        Sim_Step();   /* 0x415010 */
    }
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
       race view 2, replays the race behind them: the replay isn't ported, the race goes on). */
    {
        char ev[12];   /* --param endrace=<tick>: the race over at that tick (port addition, tests) */
        if (!g_race_over && plat_param("endrace", ev, sizeof ev) && atoi(ev) <= g_sim_tick) g_race_over = 1;
    }
    if (g_race_over && g_race_view < 2) {
        /* GameMain's second race loop: the race again from its start, replaying the packets
           (0x414ed0, 0x413ec0), the HUD's visuals hidden (0x425740), then the race-over screens. */
        keep_wear();
        g_race_view = 2;
        g_replay_mode = 2;
        Race_Restart(0x1c0 - 0x40);
        Input_Reset(true);
        if (g_ai_drive) *CAR_I(g_cars[0], 0xda8) = 1;
        HUD_HideAll();      /* 0x425740 */
        HUD_RaceOver();     /* 0x426a40 */
        PENDING_NOTE(0x415300, "the replay's speeds and cameras");
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
    Headlights_Update();   /* 0x452590 */
    Render_Frame(&views);
    if (tm) TexManager_end(tm);
    return true;
}

/* The race's end (GameShutdown 0x4b66e0 with Render_RaceShutdown, as far as the port has them): the HUD,
   the AI, the sounds, the cars' renderers and tasks, the render module's art and lights, the world; a
   later DRIVE_selftest loads a race again. (The replay's save, the sky object and the cars' memory stay:
   their ends aren't ported.) */
void DRIVE_Shutdown(void)
{
    if (!ready) return;
    HUD_Free();        /* 0x4252f0 */
    AI_Free();
    Audio_Shutdown();  /* 0x4a8850 */
    for (int i = 0; i < g_ncars; i++) {
        R3DCar *r = g_cars[i] ? g_cars[i]->p[CARP_R3D] : NULL;
        if (r) {
            R3DCar_Free(r);
            g_cars[i]->p[CARP_R3D] = NULL;
        }
    }
    g_ncars = 0;
    g_tasks_tick.count = g_tasks_even.count = 0;   /* (the lists are the race's) */
    g_render_cars = NULL;
    Headlights_Free();      /* 0x452530 */
    CarEnv_Free();          /* 0x440fd0 */
    SpriteBank_Free();      /* 0x4520c0 */
    Sun_Free();             /* 0x4600e0 */
    DynLights_Free();       /* 0x455ec0 */
    Sprites_Free();         /* 0x451480 */
    R3DCar_FreeCommon();    /* 0x443a80 (0x414fa0: the car commons go with the race) */
    Render_FreeCommonFsh(); /* 0x448af0 */
    Scene_Clear();          /* 0x46cf60 */
    BWorld_DeInit();        /* 0x471ba0 */
    g_race_over = 0;
    g_race_view = 0;
    g_replay_mode = 0;
    ticks_due = 0;
    ready = failed = false;
    g_drive_paused = false;
    g_drive_result = 0;
    s_wear_kept = false;
}
