/* The cars' task functions (the pointers at +0xe0c of a car, set in 0x410dc0, registered by
   Car_Construct 0x411190): the simulation's per-car work. Each is pending until its port lands; the
   registration (lists and priorities) is the original's. */
#include "game/car.h"
#include "game/car_physics.h"
#include "pending.h"

#define TASK(addr, what)                                       \
    static void task_##addr(void *car)                         \
    {                                                          \
        (void)car;                                             \
        PENDING_NOTE(0x##addr, what);                          \
    }
TASK(49aee0, "car task: 0x49aee0 (even, 6)")
TASK(414830, "car task: 0x414830 (unregistered)")
TASK(499150, "car task: 0x499150 (even, 0x15)")
TASK(49fb20, "car task: 0x49fb20 (even, 0x1e)")
TASK(49bcb0, "car task: 0x49bcb0 (even, 0x1e)")
TASK(498f60, "car task: 0x498f60 (even, 0x28)")
TASK(4102e0, "car task: 0x4102e0 (even, 0x32)")
TASK(4169d0, "car task: 0x4169d0 (every tick, 0x19)")
TASK(4a0e10, "car task: 0x4a0e10 (traffic, +0xe1c)")
TASK(4083c0, "car task: 0x4083c0 (replay)")
TASK(49b6b0, "car task: 0x49b6b0 (+0xe24 variant)")
static void task_516210(void *car) { (void)car; }   /* 0x516210: a function that returns at once */

enum { T_E0C, T_E10, T_E14, T_E18, T_E1C, T_E20, T_E24, T_E28, T_E2C, T_E30 };
static TaskFn *slot(Car *c, int k) { return (TaskFn *)(void *)&c->p[CARP_TASKS + k]; }   /* +0xe0c + 4 k */

void Car_SetTasks(Car *c)
{
    uint32_t f = (uint32_t)*CAR_I(c, CAR_FLAGS);
    *slot(c, T_E0C) = task_49aee0;
    *slot(c, T_E10) = task_414830;
    *slot(c, T_E14) = task_499150;
    *slot(c, T_E1C) = task_49fb20;
    *slot(c, T_E20) = task_49bcb0;
    *slot(c, T_E24) = CarSim_Move;   /* 0x49b710 */
    *slot(c, T_E28) = CarSim_Turn;   /* 0x49b850 */
    *slot(c, T_E2C) = task_498f60;
    *slot(c, T_E30) = task_4102e0;
    *slot(c, T_E18) = task_4169d0;
    if (f & 0x80) {
        *slot(c, T_E10) = NULL;
        *slot(c, T_E14) = NULL;
        *slot(c, T_E1C) = task_4a0e10;
    }
    if (f & 2) {
        if (f & 1) *slot(c, T_E0C) = task_4083c0;
        *slot(c, T_E10) = NULL;
        *slot(c, T_E14) = task_516210;
        *slot(c, T_E1C) = task_4083c0;
    }
    /* (f & 4) with 0x657420 set and 0x657404 > 0 takes 0x49b6b0 for +0xe24: those race settings come
       with the race set-up's port. */
    (void)task_49b6b0;
    PENDING_NOTE(0x657420, "the +0xe24 variant's race condition (0x657420, 0x657404)");
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

float Track_TableFactor(const char *aispec)
{
    (void)aispec;
    PENDING_NOTE(0x4a2ba0, "Track_Table (the track's AI tuning)");
    return 1.0f;
}

float Track_CarFactor(const char *aispec)
{
    (void)aispec;
    PENDING_NOTE(0x4a2730, "the track's factor for a car (0x4a2730)");
    return 1.0f;
}
