/* \real\pc's threads, critical sections, events and timer callbacks: see thread.h. */
#include "real/thread.h"
#include "exe.h"
#include "platform.h"
#include "real/systask.h"
#include <stdlib.h>

/* ---- critical sections ---- */

CritSec *CS_create(void) { return plat_mutex_create(); }
void CS_enter(CritSec *cs) { plat_mutex_lock(cs); }
void CS_leave(CritSec *cs) { plat_mutex_unlock(cs); }
void CS_destroy(CritSec *cs) { plat_mutex_destroy(cs); }

/* ---- the thread table ---- */

/* 0x6a572c: the threads' slots { serial, handle, thread id }, 0x6a5730 their count (100), under 0x6a5734.
   The handle here is the slot's own: the platform's thread ends by itself (detached), and its end is
   what a wait on the original's thread handle sees. */
typedef struct ThreadSlot {
    uint32_t serial;
    PlatThread *thread;
    uint32_t id;
} ThreadSlot;
static ThreadSlot *slots;      /* 0x6a572c */
static int nslots;             /* 0x6a5730 */
static CritSec *slots_cs;      /* 0x6a5734 */
static uint32_t next_serial;   /* 0x5df6a0 */
static bool started;           /* 0x6a5724 */
static uint32_t main_id;       /* 0x6a571c */
static CritSec *threads_cs;    /* 0x6a5720 */

/* 0x55f160: the table, zeroed. */
static void table_init(int n)
{
    if (!slots_cs) slots_cs = CS_create();
    if (!slots) {
        slots = calloc((size_t)n, sizeof *slots);
        if (slots) nslots = n;
    }
}

/* 0x55f0d0: the module's start (100 slots by default), the main thread's id kept; its exit hook
   (0x5570e0 with 0x55ef70) ends nothing the port keeps. */
static void threads_init(int n)
{
    if (started) return;
    started = true;
    next_serial = exe_u32(0x5df6a0);
    table_init(n ? n : 100);
    main_id = plat_thread_id();
    threads_cs = CS_create();
}

/* 0x55f310: a free slot for the thread, its serial and slot in the record. */
static bool slot_add(ThreadRec *rec, PlatThread *t, uint32_t id)
{
    bool ok = false;
    CS_enter(slots_cs);
    for (int i = 0; i < nslots; i++) {
        if (slots[i].serial) continue;
        rec->serial = next_serial;
        rec->slot = i;
        rec->id = id;
        slots[i].serial = next_serial++;
        slots[i].thread = t;
        slots[i].id = id;
        rec->handle = &slots[i];
        ok = true;
        break;
    }
    CS_leave(slots_cs);
    return ok;
}

/* 0x55f060: the slot freed when its serial is still the thread's. */
static void slot_free(int slot, uint32_t serial)
{
    CS_enter(slots_cs);
    if (slot >= 0 && slot < nslots && slots[slot].serial == serial) {
        slots[slot].serial = 0;
        slots[slot].thread = NULL;
        slots[slot].id = 0;
    }
    CS_leave(slots_cs);
}

/* 0x55f760: the record's slot while it is still the thread's. */
static ThreadSlot *slot_of(ThreadRec *rec)
{
    ThreadSlot *s = NULL;
    if (rec->slot >= 0 && rec->slot < nslots) {
        CS_enter(slots_cs);
        if (slots[rec->slot].serial == rec->serial) s = &slots[rec->slot];
        CS_leave(slots_cs);
    }
    return s;
}

/* The start block the creator waits on (its first word cleared once the thread has copied it). */
typedef struct {
    volatile int pending;
    ThreadRec *rec;
    void (*fn0)(void);
    void (*fn1)(void *);
    void *arg;
} Start;

/* 0x55f2a0: the new thread copies its start, lets the creator go on, runs, then frees its slot. */
static int trampoline(void *p)
{
    Start s = *(Start *)p;
    /* The thread runs first when its creator sleeps: the record is set by then (slot_add). */
    int slot = s.rec->slot;
    uint32_t serial = s.rec->serial;
    s.rec->id = plat_thread_id();
    ((Start *)p)->pending = 0;
    if (s.fn0) s.fn0();
    else s.fn1(s.arg);
    slot_free(slot, serial);
    return 0;
}

/* THREAD_create 0x55f1d0 and 0x55f3a0: the thread made suspended, recorded, its priority set (0x55f660),
   resumed, and the creator sleeping 1 ms at a time until the thread has taken its start. */
static void *create(void (*fn0)(void), void (*fn1)(void *), void *arg, uint32_t stack, int priority,
                    ThreadRec *rec)
{
    if (!started) threads_init(0);
    void *h = NULL;
    CS_enter(threads_cs);
    Start s = {1, rec, fn0, fn1, arg};
    PlatThread *t = plat_thread_create(trampoline, &s);
    if (t) {
        rec->stack = stack;
        rec->priority = priority;
        rec->unused0c = 0;
        slot_add(rec, t, 0);
        h = rec->handle;
        plat_thread_detach(t);
        while (s.pending) THREAD_sleep(1);
    }
    CS_leave(threads_cs);
    return h;
}

void *THREAD_create(void (*fn)(void), uint32_t stack, int priority, int unused, ThreadRec *rec)
{
    return create(fn, NULL, NULL, stack, priority, rec);
}

void *THREAD_createarg(void (*fn)(void *), void *arg, uint32_t stack, int priority, int unused, ThreadRec *rec)
{
    return create(NULL, fn, arg, stack, priority, rec);
}

/* 0x55f730: ended unless its slot is still its own. */
bool THREAD_ended(ThreadRec *rec)
{
    ThreadSlot *s = slot_of(rec);
    return !(s && s->serial == rec->serial);
}

/* 0x55f7c0: a wait for the thread's end, in rate / 100 tick steps (0 ticks: until it ends); true if it
   has. The original waits on the thread's handle. */
bool THREAD_wait(ThreadRec *rec, int ticks)
{
    if (!rec->handle) return false;
    if (ticks == 0) {
        while (!THREAD_ended(rec)) THREAD_sleepticks(TIMER_RATE / 100);
        return true;
    }
    THREAD_sleepticks(ticks);
    return THREAD_ended(rec);
}

/* 0x55f530: the calling thread is the record's (NULL: the main thread's; -1 any). */
bool THREAD_isthread(ThreadRec *rec)
{
    uint32_t id = plat_thread_id();
    if (!started) threads_init(0);
    if (!rec) return id == main_id;
    if (rec == (ThreadRec *)-1) return true;
    return id == rec->id;   /* (0x55f590) */
}

static void main_slept(double ms);

void THREAD_sleep(uint32_t ms)   /* 0x55f4f0 */
{
    plat_thread_sleep_ms((double)ms);
    main_slept((double)ms);
}

void THREAD_sleepticks(int ticks)   /* 0x55f500 */
{
    plat_thread_sleep_ms((double)(ticks * 1000 / TIMER_RATE));
    main_slept((double)(ticks * 1000 / TIMER_RATE));
}

/* ---- events ---- */

Event *EVENT_create(void) { return plat_event_create(false); }
Event *EVENT_createmanual(void) { return plat_event_create(true); }
void EVENT_set(Event *e) { plat_event_set(e); }
void EVENT_reset(Event *e) { plat_event_reset(e); }
bool EVENT_poll(Event *e) { return plat_event_wait(e, 0); }
void EVENT_wait(Event *e) { plat_event_wait(e, -1); }
bool EVENT_waitticks(Event *e, int ticks) { return plat_event_wait(e, (double)(ticks * 1000 / TIMER_RATE)); }
void EVENT_close(Event *e) { plat_event_destroy(e); }

void EVENT_setclose(Event *e)   /* 0x55fa30 */
{
    plat_event_set(e);
    plat_event_destroy(e);
}

/* ---- the timer's callbacks ---- */

static void (*callbacks[8])(void);    /* 0x6b7b60 */
static void (*secondary[8])(void);    /* 0x69e5b4 */

void TIMER_addcallback(void (*fn)(void))
{
    for (int i = 0; i < 8; i++)
        if (callbacks[i] == fn) return;
    for (int i = 0; i < 8; i++)
        if (!callbacks[i]) {
            callbacks[i] = fn;
            return;
        }
}

void TIMER_removecallback(void (*fn)(void))
{
    for (int i = 0; i < 8; i++)
        if (callbacks[i] == fn) {
            callbacks[i] = NULL;
            return;
        }
}

/* 0x53bd50: into the first free slot unless it is there already (the secondary thread's start,
   wininittimer 0x53bdd0, is the port's TIMER_run). */
void TIMER_addsecondary(void (*fn)(void))
{
    int free_at = -1;
    for (int i = 0; i < 8; i++) {
        if (secondary[i] == fn) return;
        if (!secondary[i] && free_at < 0) free_at = i;
    }
    if (free_at >= 0) secondary[free_at] = fn;
}

/* 0x53bf40: taken out, the ones after it moved up. */
void TIMER_removesecondary(void (*fn)(void))
{
    for (int i = 0; i < 8; i++)
        if (secondary[i] == fn) {
            for (; i < 7; i++) secondary[i] = secondary[i + 1];
            secondary[7] = NULL;
            return;
        }
}

/* The timer thread (0x5646f0: each tick TIMER_count advanced, the callbacks run) and the secondary one
   (0x53be60: the tick's event, its list up to the first empty slot), for every tick up to ms. */
static uint32_t frame_ms;   /* the last frame's time */
static double slept_ms;     /* the main thread's sleeps inside frames, all told */

static void advance(uint32_t ms)
{
    uint32_t to = (uint32_t)((uint64_t)ms * TIMER_RATE / 1000u);
    while ((int32_t)(to - TIMER_count) > 0) {
        TIMER_count++;
        for (int i = 0; i < 8; i++)
            if (callbacks[i]) callbacks[i]();
        for (int i = 0; i < 8 && secondary[i]; i++) secondary[i]();
    }
}

void TIMER_run(uint32_t ms)
{
    frame_ms = ms;
    advance(ms + (uint32_t)slept_ms);
}

/* The original's timer runs on while the main thread waits (the network's requests wait for answers by
   ticks); the port's moves at frames, which don't come while the main thread sleeps inside one: its
   sleeps move the timer on by their time, and the frames' time after them. */
static void main_slept(double ms)
{
    if (!THREAD_isthread(NULL)) return;
    slept_ms += ms;
    advance(frame_ms + (uint32_t)slept_ms);
}
