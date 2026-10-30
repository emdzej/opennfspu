/* \real\cmn\systask.c, see systask.h. */
#include "real/systask.h"
#include "real/real.h"
#include <stddef.h>

volatile uint32_t TIMER_count;   /* 0x6b7b80 */

/* 0x69dd20: 16 entries of (function, period, next tick, running). */
static struct {
    SyncTaskFn fn;
    int period;
    uint32_t next;
    int running;
} tasks[16];
static int adding;           /* 0x69de20: adds in progress */
static uint32_t last_run;    /* 0x69de24: the tick of the last run */

/* The slot: the last one already holding fn, else the first free one, skipping as many free slots as
   there are adds in progress (an add from inside another one doesn't take the same slot). */
void SYNCTASK_add(SyncTaskFn fn, int period, int delay)
{
    int skip = adding++, slot = -1;
    for (int i = 0; i < 16; i++) {
        if (tasks[i].fn == fn) slot = i;
        else if (!tasks[i].fn && slot == -1) {
            if (skip) skip--;
            else slot = i;
        }
    }
    if (slot == -1) {
        REAL_abort("\\real\\cmn\\systask.c", 99, "SYNCTASK_add - LIST FULL\n");
        adding--;
        return;
    }
    tasks[slot].fn = fn;
    tasks[slot].period = period;
    tasks[slot].next = TIMER_count + (uint32_t)delay;
    tasks[slot].running = 0;
    adding--;
}

unsigned SYNCTASK_run(int arg)
{
    uint32_t now = TIMER_count;
    if (last_run == now) return 0;
    last_run = now;
    unsigned r = 0;
    for (int i = 0; i < 16; i++) {
        if (tasks[i].fn && (int32_t)tasks[i].next <= (int32_t)now && !tasks[i].running) {
            tasks[i].running = 1;
            r |= tasks[i].fn(arg, (int)(now - tasks[i].next));
            tasks[i].next = now + (uint32_t)tasks[i].period;
            tasks[i].running = 0;
        }
    }
    return r;
}
