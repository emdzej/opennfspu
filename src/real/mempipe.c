/* The memory link: see mempipe.h. */
#include "real/mempipe.h"
#include "exe.h"
#include "real/mem.h"
#include "real/systask.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- named objects (CreateFileMappingA, CreateSemaphoreA, CreateMutexA and their Open.. calls) ---- */

typedef enum { OBJ_MAPPING, OBJ_SEMAPHORE, OBJ_MUTEX } ObjType;

typedef struct NamedObj {
    struct NamedObj *next;
    ObjType type;
    char name[256];
    int refs;              /* handles and views */
    uint8_t *mem;          /* a mapping's */
    int count, max;        /* a semaphore's */
    CritSec *cs;           /* a mutex's */
} NamedObj;

static NamedObj *objects;

static NamedObj *obj_find(ObjType t, const char *name)
{
    for (NamedObj *o = objects; o; o = o->next)
        if (o->type == t && !strcmp(o->name, name)) return o;
    return NULL;
}

static NamedObj *obj_new(ObjType t, const char *name)
{
    NamedObj *o = calloc(1, sizeof *o);
    if (!o) return NULL;
    o->type = t;
    snprintf(o->name, sizeof o->name, "%s", name);
    o->refs = 1;
    o->next = objects;
    objects = o;
    return o;
}

/* CloseHandle / UnmapViewOfFile: the last one frees the object (its name free again). */
static void obj_release(NamedObj *o)
{
    if (!o || --o->refs > 0) return;
    for (NamedObj **p = &objects; *p; p = &(*p)->next)
        if (*p == o) {
            *p = o->next;
            break;
        }
    free(o->mem);
    if (o->cs) CS_destroy(o->cs);
    free(o);
}

static NamedObj *sem_create(const char *name, int max)   /* 0x582aa0 (an existing one opened) */
{
    NamedObj *o = obj_find(OBJ_SEMAPHORE, name);
    if (o) {
        o->refs++;
        return o;
    }
    o = obj_new(OBJ_SEMAPHORE, name);
    if (o) o->max = max;
    return o;
}

static NamedObj *sem_open(const char *name)   /* 0x582d50 */
{
    NamedObj *o = obj_find(OBJ_SEMAPHORE, name);
    if (o) o->refs++;
    return o;
}

static void sem_release(NamedObj *o)   /* 0x582f50 */
{
    if (o->count < o->max) o->count++;
}

/* A wait of ms (0 a look, under 1 for ever in 0x582f60's terms is the caller's). 0x582f60, 0x583030 */
static bool sem_wait(NamedObj *o, int ms)
{
    for (int waited = 0; o->count == 0; waited++) {
        if (0 <= ms && ms <= waited) return false;
        THREAD_sleep(1);
    }
    o->count--;
    return true;
}

static NamedObj *mutex_create(const char *name)   /* 0x582ac0 */
{
    NamedObj *o = obj_find(OBJ_MUTEX, name);
    if (o) {
        o->refs++;
        return o;
    }
    o = obj_new(OBJ_MUTEX, name);
    if (o) o->cs = CS_create();
    return o;
}

static NamedObj *mutex_open(const char *name)   /* 0x582d80 */
{
    NamedObj *o = obj_find(OBJ_MUTEX, name);
    if (o) o->refs++;
    return o;
}

static void mutex_lock(NamedObj *o) { CS_enter(o->cs); }     /* 0x582da0 */
static void mutex_unlock(NamedObj *o) { CS_leave(o->cs); }   /* 0x582dc0 */

/* A new mapping of size bytes, NULL if one of the name is there (ERROR_ALREADY_EXISTS); *handle its
   handle, the view returned. 0x582ad0 */
static uint8_t *mapping_create(const char *name, int size, NamedObj **handle)
{
    *handle = NULL;
    if (obj_find(OBJ_MAPPING, name)) return NULL;
    NamedObj *o = obj_new(OBJ_MAPPING, name);
    if (!o) return NULL;
    o->mem = calloc(1, (size_t)size);
    if (!o->mem) {
        obj_release(o);
        return NULL;
    }
    o->refs++;   /* the view */
    *handle = o;
    return o->mem;
}

/* An existing mapping's view (its handle closed at once). 0x582dd0 */
static uint8_t *mapping_open(const char *name, NamedObj **view)
{
    NamedObj *o = obj_find(OBJ_MAPPING, name);
    *view = o;
    if (!o) return NULL;
    o->refs++;
    return o->mem;
}

/* ---- a pipe (0x14 bytes: the role, the header, the semaphore, the mutex, the mapping's handle) ---- */

/* The mapping: [reader in][writer in][reader's count][writer's count][read at][write at][size], then the
   ring (from 0x1c). */
enum { H_READER, H_WRITER, H_RCOUNT, H_WCOUNT, H_READ, H_WRITE, H_SIZE, H_DATA = 0x1c };

typedef struct Pipe {
    int32_t role;          /* 0 reads, 1 writes */
    uint8_t *hdr;
    NamedObj *sem;
    NamedObj *mutex;
    NamedObj *map;         /* the mapping's handle (its maker's), then its view */
} Pipe;

static uint32_t *H(Pipe *p) { return (uint32_t *)p->hdr; }
static uint32_t end_of(Pipe *p) { return H_DATA + H(p)[H_SIZE]; }

/* This end's pipe, made: it writes (role 1) or reads. 0x5828d0 */
static bool pipe_create(Pipe *p, int size, const char *name, int role)
{
    char map[256], sem[256], mutex[256];
    snprintf(map, sizeof map, "%sPipe", name);
    snprintf(sem, sizeof sem, "%sSemaphore", name);
    snprintf(mutex, sizeof mutex, "%sMutex", name);
    p->role = role;
    p->sem = sem_create(sem, size / 4);
    p->mutex = mutex_create(mutex);
    uint8_t *h = mapping_create(map, size + 0x1f, &p->map);
    if (!h) return false;
    p->hdr = h;
    H(p)[H_SIZE] = (uint32_t)size;
    H(p)[H_READER] = role == 0;
    H(p)[H_WRITER] = role != 0;
    H(p)[H_READ] = H(p)[H_WRITE] = H_DATA;
    H(p)[H_RCOUNT] = H(p)[H_WCOUNT] = 0;
    return true;
}

/* The maker's handle closed once connected (its view stays). 0x5828a0 */
static void pipe_closemap(Pipe *p)
{
    if (p->map) {
        obj_release(p->map);
        p->map = NULL;
    }
}

/* The other end's pipe, opened as its reader (role 0) or writer when that place is free. 0x582b40 */
static bool pipe_open(Pipe *p, const char *name, int role)
{
    if (!p) return true;
    char map[256], sem[256], mutex[256];
    snprintf(map, sizeof map, "%sPipe", name);
    snprintf(sem, sizeof sem, "%sSemaphore", name);
    snprintf(mutex, sizeof mutex, "%sMutex", name);
    NamedObj *view;
    p->hdr = NULL;
    uint8_t *h = mapping_open(map, &view);
    if (h) {
        p->sem = sem_open(sem);
        if (p->sem) {
            p->mutex = mutex_open(mutex);
            if (!p->mutex) {
                obj_release(p->sem);
            } else {
                uint32_t *w = (uint32_t *)h;
                mutex_lock(p->mutex);
                if (role == 0) {
                    if (!w[H_READER]) {
                        p->role = 0;
                        p->hdr = h;
                        w[H_READER] = 1;
                    }
                } else if (!w[H_WRITER]) {
                    p->role = 1;
                    p->hdr = h;
                    w[H_WRITER] = 1;
                }
                mutex_unlock(p->mutex);
            }
        }
    }
    if (p->hdr) {
        p->map = view;
        return true;
    }
    if (h) obj_release(view);
    return false;
}

static bool pipe_both(Pipe *p)   /* 0x582e20 */
{
    return p->hdr && H(p)[H_READER] && H(p)[H_WRITER];
}

static bool pipe_close(Pipe *p)   /* 0x582e40 */
{
    if (!p) return false;
    if (p->hdr) {
        NamedObj *m = p->mutex;
        mutex_lock(m);
        if (p->role == 0) H(p)[H_READER] = 0;
        else if (p->role == 1) H(p)[H_WRITER] = 0;
        mutex_unlock(m);
        obj_release(m);
        obj_release(p->sem);
        obj_release(p->map);   /* (the view) */
    }
    memset(p, 0, sizeof *p);
    return true;
}

/* The semaphore looked at for ms: a packet waiting (the count put back). 0x582f20 */
static bool pipe_waiting(Pipe *p, int ms)
{
    if (sem_wait(p->sem, ms < 1 ? -1 : ms)) {
        sem_release(p->sem);
        return true;
    }
    return false;
}

/* The next packet: its length (0 none) and *data, or with take it is passed. A ring end with less than
   a length word, or a 0 length, goes back to the start. 0x582fa0 */
static int pipe_read(Pipe *p, const uint8_t **data, bool take)
{
    if (!p || !p->hdr || p->role != 0) return 0;
    uint32_t *h = H(p);
    if (h[H_READ] == h[H_WRITE]) return 0;
    uint32_t end = end_of(p);
    uint32_t len;
    if (end <= h[H_READ] + 4 || (memcpy(&len, p->hdr + h[H_READ], 4), len == 0)) {
        h[H_READ] = H_DATA;
        if (h[H_READ] == h[H_WRITE]) return 0;
        memcpy(&len, p->hdr + h[H_READ], 4);
        if (len == 0) return 0;
    }
    if (!take) {
        *data = p->hdr + h[H_READ] + 4;
    } else {
        h[H_READ] += len + 4;
        sem_wait(p->sem, 0);
    }
    if (end <= h[H_READ]) h[H_READ] = H_DATA;
    return (int)len;
}

/* Room for n bytes (an empty ring starts over; one with no room before its end goes on at the start,
   a 0 word marking the jump). 0x583140 */
static bool pipe_room(Pipe *p, int n)
{
    if (!p || !p->hdr || p->role != 1) return false;
    uint32_t *h = H(p);
    if (h[H_WRITE] == h[H_READ]) {
        mutex_lock(p->mutex);
        h[H_READ] = h[H_WRITE] = H_DATA;
        mutex_unlock(p->mutex);
    }
    uint32_t rd = h[H_READ], wr = h[H_WRITE], need = wr + 4 + (uint32_t)n;
    if (wr < rd) return need < rd;
    uint32_t end = end_of(p);
    if (end < need) {
        if (rd <= H_DATA + 4 + (uint32_t)n) return false;
        if (wr + 4 <= end) memset(p->hdr + wr, 0, 4);
        h[H_WRITE] = H_DATA;
    }
    return true;
}

static bool pipe_write(Pipe *p, const void *data, int n)   /* 0x5830b0 */
{
    if (n < 0) return false;
    uint32_t *h = H(p);
    bool ok = pipe_room(p, n);
    if (ok) {
        uint32_t len = (uint32_t)n;
        memcpy(p->hdr + h[H_WRITE], &len, 4);
        memcpy(p->hdr + h[H_WRITE] + 4, data, (size_t)n);
        h[H_WRITE] += len + 4;
        sem_release(p->sem);
    }
    if (end_of(p) <= h[H_WRITE] && h[H_READ] != H_DATA) h[H_WRITE] = H_DATA;
    return ok;
}

/* This end's count into the pipe and the other end's back (reader: [2] in, [3] out; writer the other
   way round). 0x5832b0 */
static uint32_t pipe_count(Pipe *p, uint32_t v)
{
    if (!p || !p->hdr) return 0;
    if (p->role == 0) {
        H(p)[H_RCOUNT] = v;
        return H(p)[H_WCOUNT];
    }
    uint32_t r = H(p)[H_RCOUNT];
    H(p)[H_WCOUNT] = v;
    return r;
}

/* ---- the protocol ---- */

/* A link's state ("MEMDATA", 0x50 bytes). */
typedef struct MemLink {
    Pipe in;               /* +0x00: the other end's, read */
    Pipe out;              /* +0x14: this end's, written */
    int32_t allocated;     /* +0x28 */
    uint32_t count;        /* +0x2c */
    uint32_t seen;         /* +0x30: the other end's count last read */
    ThreadRec reader;      /* +0x34 */
} MemLink;

static MemLink *link_of(PacketConn *pc) { return (MemLink *)pc->st; }

static int32_t timeout_ticks(void) { return (int32_t)exe_u32(0x5df6a8); }

static int noop_idle(PacketConn *pc) { return 0; }   /* 0x55ff60 (as the closed call while connecting) */
static void noop_closed(PacketConn *pc) { (void)noop_idle(pc); }

/* The other end's packets to the owner as they come (one it won't take yet waits 10 ms). 0x582ec0 */
static void reader_thread(void *arg)
{
    PacketConn *pc = arg;
    MemLink *l = link_of(pc);
    while (pc->alive) {
        const uint8_t *data;
        int len;
        if (pipe_waiting(&l->in, 100) && (len = pipe_read(&l->in, &data, false))) {
            if (!PACKET_deliver(pc, data, len)) THREAD_sleep(10);
            else pipe_read(&l->in, NULL, true);
        }
    }
}

/* Both pipes made and opened until the other end is in (or the idle call gives up), then the reader.
   0x5825a0 */
static int mem_init(PacketConn *pc, const void *namep, int size, int which)
{
    const char *name = namep;
    if (!pc) return 0x50;
    bool allocated = false, opened = false;
    MemLink *l = link_of(pc);
    if (!l) {
        l = MEM_alloc("MEMDATA", (int)sizeof *l, exe_u32(0x5deb60));
        allocated = true;
        if (!l) return pc->alive;
        memset(l, 0, sizeof *l);
    }
    void (*closed)(PacketConn *) = pc->closed;
    pc->closed = noop_closed;
    pc->alive = 0;
    char mine[256], theirs[256];
    snprintf(mine, sizeof mine, "%s%s", name, which == 0 ? "_SERVER" : "_CLIENT");
    snprintf(theirs, sizeof theirs, "%s%s", name, which == 0 ? "_CLIENT" : "_SERVER");
    bool made = pipe_create(&l->out, size, mine, 1);
    if (made) {
        while (!pc->alive && !pc->idle(pc)) {
            if (!THREAD_isthread(NULL)) THREAD_sleep(0);
            else if ((int)SYNCTASK_run(0) < 0) break;
            THREAD_sleep(10);
            if (!opened) opened = pipe_open(&l->in, theirs, 0);
            else pc->alive = pipe_both(&l->out);
        }
        pipe_closemap(&l->out);
    }
    if (pc->alive) {
        NetList_Push(PACKET_active(), pc);
        l->allocated = allocated;
        l->count = l->seen = 0;
        pc->st = (PktState *)l;
        pc->alive = timeout_ticks();
        pc->closed = closed;
        THREAD_createarg(reader_thread, pc, 0, 2, -1, &l->reader);
        return pc->alive;
    }
    if (made) pipe_close(&l->out);
    if (opened) pipe_close(&l->in);
    if (allocated) MEM_purge(l);
    return pc->alive;
}

static int mem_send(PacketConn *pc, const void *data, uint32_t lenflags)   /* 0x583070 */
{
    if (!pc || !pc->alive || !pc->st) return 0;
    return pipe_write(&link_of(pc)->out, data, (int)(lenflags & 0xffffff));
}

static int mem_freeslots(PacketConn *pc) { return pipe_room(&link_of(pc)->out, 1); }   /* 0x5831f0 */

/* Gone from the other end: off the open connections, closed. 0x5832e0 */
static void lost(PacketConn *pc)
{
    NetList *a = PACKET_active();
    CS_enter(a->cs);
    if (NetList_Remove(a, pc)) {
        pc->alive = 0;
        pc->closed(pc);
        NetList_Push(PACKET_closedlist(), pc);
    }
    CS_leave(a->cs);
}

/* Each tick: the counts traded; the other's standing still for the time-out ticks asks the owner (its
   answer new ticks, or 0 closes). 0x583210 */
static int mem_tick(PacketConn *pc)
{
    MemLink *l = link_of(pc);
    if (l) {
        if (pipe_both(&l->in) && pipe_both(&l->out)) {
            pipe_count(&l->out, l->count);
            uint32_t other = pipe_count(&l->in, l->count);
            l->count++;
            if (other == l->seen) {
                if (--pc->alive != 0) return 1;
                pc->alive = pc->timeout(pc);
            } else {
                l->seen = other;
                pc->alive = timeout_ticks();
            }
        } else {
            pc->alive = 0;
        }
    }
    if (pc->alive == 0) lost(pc);
    return 1;
}

static int mem_close(PacketConn *pc)   /* 0x583330 */
{
    MemLink *l;
    if (!pc || !(l = link_of(pc))) return 0;
    NetList *a = PACKET_active();
    CS_enter(a->cs);
    if (NetList_Remove(a, pc)) {
        pc->alive = 0;
        NetList_Push(PACKET_closedlist(), pc);
    }
    CS_leave(a->cs);
    if (!NetList_Remove(PACKET_closedlist(), pc)) return 0;
    THREAD_wait(&l->reader, 0);
    int r = pipe_close(&l->out) && pipe_close(&l->in);
    if (l->allocated) MEM_purge(l);
    return r;
}

const PacketProto MEMPIPE_proto = {3, mem_init, mem_send, mem_freeslots, mem_tick, mem_close};

int MEMPIPE_connect(PacketConn *pc, const char *name, int size, int which)
{
    if (pc) {
        if (!pc->timing[1]) pc->timing[1] = 1;
        if (!pc->timing[2]) pc->timing[2] = 1;
        if (!pc->timing[3]) pc->timing[3] = 1;
        pc->timing[0] = 5;
    }
    return PACKET_open(pc, &MEMPIPE_proto, name, size, (uint32_t)which);
}
