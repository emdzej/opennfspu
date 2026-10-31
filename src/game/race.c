/* The race's simulation step (see race.h). */
#include "game/race.h"
#include "game/audio.h"
#include "game/scene.h"
#include "game/sim_rand.h"
#include "game/race_car.h"
#include "game/ai.h"
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

void Sim_Step(void)
{
    PENDING_NOTE(0x4b0d70, "the replay's tick (0x4b0d70)");
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
        PENDING_NOTE(0x417030, "the odd tick's World_Tick, the cars' renderers' and particles' ticks");
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
    PENDING_NOTE(0x414ed0, "the race restart's other parts (0x44fbd0, the cops' script, the cameras 0x468fe0, 0x412170)");
    for (int i = 0; i < g_ncars; i++) {
        g_ranked.c[i] = g_cars[i];   /* 0x411100 */
        g_ordered.c[i] = g_cars[i];
    }
    for (int i = 0; i < g_ncars; i++) Car_RaceInit(g_cars[i], (uint32_t)i);   /* 0x410790 */
    for (int i = 0; i < g_ntraffic; i++) Car_RaceInit(g_traffic[i], (uint32_t)(g_ncars + i));
    AI_MakeControllers();                  /* 0x406780, 0x406580 */
    Race_ResetRanking();
}
