/* The cars' task functions (the pointers at +0xe0c of a car, set in 0x410dc0, registered by
   Car_Construct 0x411190): the simulation's per-car work, registered (lists and priorities) as the
   original's. */
#include "game/car.h"
#include "game/ai_drive.h"
#include "game/race_progress.h"
#include "game/car_collide.h"
#include "game/car_drive.h"
#include "game/car_ground.h"
#include "game/car_physics.h"
#include "game/car_respawn.h"
#include "game/input.h"
#include "game/traffic.h"
/* 0x414830: the car's packet recorded or played back (0x414410, 0x4144d0 by the replay's mode 0x5e99f4); set
   in +0xe10 but never registered, so nothing runs it. */
static void task_414830(void *car) { (void)car; }
static void task_516210(void *car) { (void)car; }   /* 0x516210: a function that returns at once */

enum { T_E0C, T_E10, T_E14, T_E18, T_E1C, T_E20, T_E24, T_E28, T_E2C, T_E30 };
static TaskFn *slot(Car *c, int k) { return (TaskFn *)(void *)&c->p[CARP_TASKS + k]; }   /* +0xe0c + 4 k */

void Car_SetTasks(Car *c)
{
    uint32_t f = (uint32_t)*CAR_I(c, CAR_FLAGS);
    *slot(c, T_E0C) = CarSim_Track;    /* 0x49aee0 */
    *slot(c, T_E10) = task_414830;
    *slot(c, T_E14) = CarSim_PacketTask;   /* 0x499150 */
    *slot(c, T_E1C) = CarSim_Drive;    /* 0x49fb20 */
    *slot(c, T_E20) = CarSim_Forces;   /* 0x49bcb0 */
    *slot(c, T_E24) = CarSim_Move;   /* 0x49b710 */
    *slot(c, T_E28) = CarSim_Turn;   /* 0x49b850 */
    *slot(c, T_E2C) = CarSim_Collide;  /* 0x498f60 */
    *slot(c, T_E30) = CarSim_HitTask;   /* 0x4102e0 */
    *slot(c, T_E18) = CarSim_RaceTask;   /* 0x4169d0 */
    if (f & 0x80) {
        *slot(c, T_E10) = NULL;
        *slot(c, T_E14) = NULL;
        *slot(c, T_E1C) = Traffic_Drive;   /* 0x4a0e10 */
    }
    if (f & 2) {
        if (f & 1) *slot(c, T_E0C) = AI_DriveTask;   /* 0x4083c0 (a racer returns at once) */
        *slot(c, T_E10) = NULL;
        *slot(c, T_E14) = task_516210;
        *slot(c, T_E1C) = AI_DriveTask;   /* 0x4083c0 */
    }
    /* (f & 4) in a multiplayer race (0x657420 set, the players 0x657404 above 0) takes 0x49b6b0 for
       +0xe24; the port's races are single-player, where 0x657420 is 0. */
}

void Car_RegisterTasks(Car *c)
{
    TaskList_Add(&g_tasks_even, *slot(c, T_E0C), c, 6);
    TaskList_Add(&g_tasks_even, *slot(c, T_E14), c, 0x15);
    TaskList_Add(&g_tasks_even, *slot(c, T_E1C), c, 0x1e);
    TaskList_Add(&g_tasks_even, *slot(c, T_E20), c, 0x1e);
    TaskList_Add(&g_tasks_even, *slot(c, T_E2C), c, 0x28);
    TaskList_Add(&g_tasks_even, *slot(c, T_E30), c, 0x32);
    TaskList_Add(&g_tasks_tick, *slot(c, T_E18), c, 0x19);
    TaskList_Add(&g_tasks_tick, *slot(c, T_E24), c, 0x1e);
    TaskList_Add(&g_tasks_tick, *slot(c, T_E28), c, 0x1e);
}

