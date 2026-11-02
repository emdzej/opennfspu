/* The race's simulation step (see race.h). */
#include "game/race.h"
#include "game/skid.h"
#include "game/track.h"
#include "render/headlights.h"
#include "render/sky.h"
#include "render/weather.h"
#include "render/particles.h"
#include "render/carparticle.h"
#include "render/r3dcar.h"
#include "game/audio.h"
#include "game/scene.h"
#include "game/sim_rand.h"
#include "game/race_car.h"
#include "game/ai.h"
#include "game/ai_drive.h"
#include "game/ai_police.h"
#include "game/race_progress.h"
#include "exe.h"
#include "game/car.h"
#include "game/car_collide.h"
#include "game/car_drive.h"
#include "pending.h"

int32_t g_race_started;
int32_t g_race_countdown;
int32_t g_race_view;   /* 0x657424: 0 racing, 2 the replay */
int32_t g_race_seed;

/* The cars' per-tick contacts (+0x110c) cleared, the list of the cars checked against each other
   emptied. 0x498190 */
static void Sim_ClearContacts(void)
{
    CarSim_ClearCollided();
    for (int i = 0; i < g_ncars; i++)
        if (g_cars[i] && g_cars[i]->b[0x7f]) *CAR_I(g_cars[i], CAR_CONTACTS) = 0;
    for (int i = 0; i < g_ntraffic; i++)
        if (g_traffic[i]) *CAR_I(g_traffic[i], CAR_CONTACTS) = 0;
}

static int32_t s_fxsaved;   /* 0x657ca4 */

/* Each car renderer's tick (slot 8, R3DCar_v8 0x438350) through their list (0x6115f0, the newest
   first): in the race its particles' emitters set from the car (0x43eeb0 with 1), the viewed car's too
   (which its draw leaves out in the driver's and hood views). 0x443570 */
static void R3DCar_TickAll(void)
{
    if (g_r3d_frontend) return;
    for (int i = g_ntraffic - 1; i >= 0; i--) {
        R3DCar *r = g_traffic[i] ? g_traffic[i]->p[CARP_R3D] : NULL;
        if (r) CarParticles_Update(r->particles, true);
    }
    for (int i = g_ncars - 1; i >= 0; i--) {
        R3DCar *r = g_cars[i] ? g_cars[i]->p[CARP_R3D] : NULL;
        if (r) CarParticles_Update(r->particles, true);
    }
}

void Sim_Step(void)
{
    PENDING_NOTE(0x4b0d70, "the window's deactivation (0x6573e0: the sound paused, the display given back)");
    if (!g_race_started) {
        /* The countdown's steps at the ticks of 0x5cc7c4 (not in the free modes 6, 7, 8). */
        int32_t at = (int32_t)exe_u32(0x5cc7c4 + 4u * (uint32_t)(uint8_t)g_race_countdown);
        if (at <= g_sim_tick && g_race_view < 2 && g_race_mode != 6 && g_race_mode != 7 && g_race_mode != 8) {
            Audio_Countdown(g_race_countdown);   /* 0x4a6010 */
            g_race_countdown = (uint8_t)(g_race_countdown + 1);
        }
        if (g_sim_tick > 0x1bf) g_race_started = 1;
        /* the effects' volume from nothing over ticks 0x11 .. 0x50 */
        if (g_sim_tick == 0) {
            s_fxsaved = g_audio_fx;
            g_audio_fx = 0;
        } else if (0x10 < g_sim_tick && g_sim_tick < 0x51) {
            g_audio_fx = ((g_sim_tick - 0x10) * s_fxsaved) >> 6;
        }
    }
    if (!(g_sim_tick & 1)) {
        Race_ResetRanking();                  /* 0x416160 */
        Sim_ClearContacts();                  /* 0x498190 */
        AI_TickAll();                         /* 0x4067c0 */
        TaskList_Run(&g_tasks_even);
        Race_TickEven();                      /* 0x417820 */
    }
    TaskList_Run(&g_tasks_tick);
    if (g_sim_tick & 1) {
        Race_TickOdd();                       /* 0x417030 */
        Audio_Tick();                         /* 0x4a7ef0 (Race_TickOdd's) */
        Race_UpdatePositions();               /* 0x415fe0 */
        Race_WrongWay();                      /* (Audio_Tick 0x4a7430's part) */
        World_Tick();                         /* 0x479e20: the fading skid marks */
        R3DCar_TickAll();                     /* 0x443570 */
        CarParticles_TickAll();               /* R3DCarParticle_Tick 0x43f590 */
        Scene_Tick();                         /* 0x470b10 */
    }
    PENDING_NOTE(0x48ed20, "Net_Tick");
    g_sim_tick++;
}

void Race_Restart(int32_t start)
{
    g_race_countdown = 0;
    g_race_over = 0;
    g_race_started = 0;
    g_sim_tick = start;
    SimRand_Seed((uint32_t)g_race_seed);   /* 0x4a4560 */
    Render_SeedRand((uint32_t)g_race_seed);   /* 0x44fbd0 */
    Police_ResetScript();                     /* 0x4030b0 */
    g_race_demo = 0;                       /* 0x406a30 */
    for (int i = 0; i < g_ncars; i++) {
        g_ranked.c[i] = g_cars[i];   /* 0x411100 */
        g_ordered.c[i] = g_cars[i];
    }
    for (int i = 0; i < g_ncars; i++) Car_RaceInit(g_cars[i], (uint32_t)i);   /* 0x410790 */
    for (int i = 0; i < g_ntraffic; i++) Car_RaceInit(g_traffic[i], (uint32_t)(g_ncars + i));
    /* 0x406780 frees the controllers, 0x406770 clears the cops' chase slots (0x40b510) and makes them
       again (0x406580). */
    AI_MakeControllers();
    /* 0x406a40: the drivers' moods reset, the leaders the first human and the first rival until the
       ranking's first tick (the lists' first slots, as they are); the lane driver's cars (0x5e6658 ..)
       it clears are cleared at each of its steps here. */
    for (int i = 0; i < g_ncars; i++) AI_MoodsReset(g_cars[i]);   /* 0x40ab60 */
    g_lead_human = g_first_human = g_humans.c[0];
    g_first_rival = g_last_rival = g_rivals.c[0];
    Race_ResetRanking();
    /* 0x468fe0: the render's part: the skid marks cleared (0x479d60), the sky's textures loaded again
       (0x45dbc0), the weather anew (0x459130), the headlights on the local car (0x452550). */
    WLineBundle_Clear();
    if (g_track_name[0]) Sky_LoadFsh(g_track_name, true);
    Weather_Restart();
    if (g_race_local < g_ncars) Headlights_SetCar(g_cars[g_race_local]);
    /* 0x412170: the force feedback's car and its effects restarted (0x533090), only with a
       force-feedback device (0x5e940c): gasm has none, so never. */
}
