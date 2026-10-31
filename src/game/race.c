/* The race's simulation step (see race.h). */
#include "game/race.h"
#include "game/race_progress.h"
#include "exe.h"
#include "game/car.h"
#include "game/car_collide.h"
#include "game/car_drive.h"
#include "pending.h"

int32_t g_race_started;
int32_t g_race_countdown;
int32_t g_race_laps_done;   /* 0x657424 */

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

void Sim_Step(void)
{
    PENDING_NOTE(0x4b0d70, "the replay's tick (0x4b0d70)");
    if (!g_race_started) {
        /* The countdown's steps at the ticks of 0x5cc7c4 (not in the free modes 6, 7, 8). */
        int32_t at = (int32_t)exe_u32(0x5cc7c4 + 4u * (uint32_t)(uint8_t)g_race_countdown);
        if (at <= g_sim_tick && g_race_laps_done < 2 && g_race_mode != 6 && g_race_mode != 7 && g_race_mode != 8) {
            PENDING_NOTE(0x4a6010, "Audio_Countdown");
            g_race_countdown = (uint8_t)(g_race_countdown + 1);
        }
        if (g_sim_tick > 0x1bf) g_race_started = 1;
        PENDING_NOTE(0x5d1744, "the start's volume ramp (0x5d1744, 0x657ca4)");
    }
    if (!(g_sim_tick & 1)) {
        PENDING_NOTE(0x416160, "Sim_ResetCollisionSlots (the collision events)");
        Sim_ClearContacts();                  /* 0x498190 */
        PENDING_NOTE(0x4067c0, "AI_TickAll");
        TaskList_Run(&g_tasks_even);
        Race_TickEven();                      /* 0x417820 */
    }
    TaskList_Run(&g_tasks_tick);
    if (g_sim_tick & 1) {
        Race_TickOdd();                       /* 0x417030 */
        PENDING_NOTE(0x417030, "the odd tick's Race_UpdatePositions, Audio_Tick, World_Tick, the cars' renderers' and particles' ticks, Scene_Tick");
    }
    PENDING_NOTE(0x48ed20, "Net_Tick");
    g_sim_tick++;
}
