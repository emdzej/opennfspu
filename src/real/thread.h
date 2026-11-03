/* EA real library threads, critical sections and events (\real\pc, 0x531f60..0x532040, 0x55ef70..0x55fa30)
   on the platform's cooperative threads (platform.h plat_thread_*: gasm's, in the LOOP THREADS build).
   The original's are Windows threads; here a thread runs until it sleeps, waits or yields, so the
   critical sections are recursive locks that only matter across those points. Priorities (0x55f660's
   SetThreadPriority) have no meaning on one thread and are kept only as values. Times in ticks are the
   real library's (TIMER_RATE a second, 0x5deb48). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define TIMER_RATE 128   /* 0x5deb48 */

/* A critical section (0x531f60 a new one, 0x532040 its end). */
typedef struct PlatMutex CritSec;
CritSec *CS_create(void);             /* 0x531f60 */
void CS_enter(CritSec *cs);           /* 0x532020 */
void CS_leave(CritSec *cs);           /* 0x532030 */
void CS_destroy(CritSec *cs);         /* 0x532040 */

/* A thread's record, the caller's (+0 the handle, +4 the stack size, +8 the priority, +0x10 the thread's
   id, +0x14 its serial, +0x18 its slot in the table 0x6a572c). */
typedef struct ThreadRec {
    struct ThreadSlot *handle;
    uint32_t stack;
    int priority;
    int unused0c;
    uint32_t id;
    uint32_t serial;
    int slot;
} ThreadRec;

/* THREAD_create 0x55f1d0: fn() on a new thread; 0x55f3a0: fn(arg). NULL if it can't be made. */
void *THREAD_create(void (*fn)(void), uint32_t stack, int priority, int unused, ThreadRec *rec);
void *THREAD_createarg(void (*fn)(void *), void *arg, uint32_t stack, int priority, int unused, ThreadRec *rec);
bool THREAD_ended(ThreadRec *rec);                 /* 0x55f730 */
bool THREAD_wait(ThreadRec *rec, int ticks);       /* 0x55f7c0: 0 ticks until it ends */
bool THREAD_isthread(ThreadRec *rec);              /* 0x55f530: NULL the main thread */
void THREAD_sleep(uint32_t ms);                    /* 0x55f4f0 (SleepEx) */
void THREAD_sleepticks(int ticks);                 /* 0x55f500 */

/* Events (0x55f8d0 an automatic one, 0x55f9d0 a manual one). */
typedef struct PlatEvent Event;
Event *EVENT_create(void);                         /* 0x55f8d0 */
Event *EVENT_createmanual(void);                   /* 0x55f9d0 */
void EVENT_set(Event *e);                          /* 0x55f8e0, 0x55f9e0 */
void EVENT_reset(Event *e);                        /* 0x55f9f0 */
bool EVENT_poll(Event *e);                         /* 0x55f8f0: set now (taking it) */
void EVENT_wait(Event *e);                         /* 0x55f940, 0x55fa10 */
bool EVENT_waitticks(Event *e, int ticks);         /* 0x55f960 */
void EVENT_close(Event *e);                        /* 0x55f9c0 */
void EVENT_setclose(Event *e);                     /* 0x55fa30 */

/* The timer's callbacks: TIMER_addcallback's eight (0x6b7b60, run by the timer thread 0x5646f0 at each
   tick) and the secondary timer's (0x53bd50, 0x69e5b4: run by its thread 0x53be60 on the same tick).
   TIMER_run runs them for the ticks the frame's time has reached and advances TIMER_count. */
void TIMER_addcallback(void (*fn)(void));          /* 0x55fef0 */
void TIMER_removecallback(void (*fn)(void));       /* 0x55ff30 */
void TIMER_addsecondary(void (*fn)(void));         /* 0x53bd50 */
void TIMER_removesecondary(void (*fn)(void));      /* 0x53bf40 */
void TIMER_run(uint32_t ms);
