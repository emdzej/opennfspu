/* EA real library synchronous tasks (\real\cmn\systask.c): up to 16 callbacks run by SYNCTASK_run when
   the timer has ticked since the last run and their time has come, then rescheduled by their period.
   Long loads call SYNCTASK_run between pieces of work (the chunk loader every 16 pages), so periodic work
   (a loading screen) keeps going. The port's timer advances per frame, never during a load, so those
   calls find no new tick, as the original's do between two ticks. */
#pragma once
#include <stdint.h>

typedef unsigned (*SyncTaskFn)(int arg, int late);   /* late: ticks past its time */

/* The timer's tick count (0x6b7b80, 128 Hz in the original's timer thread; the timer port drives it). */
extern volatile uint32_t TIMER_count;

void SYNCTASK_add(SyncTaskFn fn, int period, int delay);   /* 0x536350 */
unsigned SYNCTASK_run(int arg);                             /* 0x536450: the tasks' results or'ed */
/* The tasks run for that many ticks or until one returns non-zero (its result). The port's timer moves
   between frames only, so the wait sleeps the thread a frame at a time (the original spins). 0x5364e0 */
int SYNCTASK_wait(int ticks);
