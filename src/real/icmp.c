/* The pings: see icmp.h. */
#include "real/icmp.h"
#include "platform.h"
#include "real/netlist.h"
#include "real/thread.h"
#include "real/wsock.h"
#include <stdio.h>
#include <string.h>

enum { NMSG = 20, PING = 8, QUIT = 6 };

typedef struct IcmpMsg {
    struct IcmpMsg *next;
    int32_t type;          /* +0x04: 8 a ping, 6 the end */
    uint32_t addr;         /* +0x08: network order */
    void *arg;             /* +0x0c */
    IcmpAnswerFn fn;       /* +0x10 */
} IcmpMsg;

static volatile int running;   /* 0x6a45a8 */
static ThreadRec rec;          /* 0x6a3c78 */
static NetList freemsgs, queue;   /* (the message queue 0x6a3b60, 0x57fd70 .. 0x57fed0) */
static IcmpMsg msgs[NMSG];     /* 0x6a3c98 */
static Event *wake;

static IcmpMsg *msg_get(void)   /* 0x57fe60: waited for */
{
    IcmpMsg *m;
    while (!(m = NetList_Pull(&queue))) EVENT_wait(wake);
    return m;
}

static void msg_post(IcmpMsg *m)   /* 0x57fe30 */
{
    NetList_Push(&queue, m);
    EVENT_set(wake);
}

/* The thread: each ping four echoes of 32 bytes (' ' .. '}'), TTL 64, 2.5 s each, 100 ms apart; the
   answer the last one's round trip (-1 for a reply that wasn't a success; the first echo unanswered ends
   the tries, -1 if it was the first). (ICMP.DLL absent or its handle refused: the thread ends at once.)
   0x55d580 ICMP_init */
static void icmp_thread(void)
{
    uint8_t data[0x20];
    uint8_t c = 0x20;
    for (int i = 0; i < 0x20; i++, c++) {
        if (0x7d < c) c = 0x20;
        data[i] = c;
    }
    while (running) {
        IcmpMsg *m = msg_get();
        if (m->type == PING) {
            int32_t ms = -1;
            uint8_t a[4];
            memcpy(a, &m->addr, 4);
            for (int i = 0; i < 4; i++) {
                if (i) THREAD_sleep(100);
                int r = WS_echo(a, data, 0x20, 0x9c4);
                if (r < 0) break;   /* (IcmpSendEcho 0: no reply in the time) */
                ms = r;
            }
            m->fn(m->arg, ms);
        }
        NetList_Push(&freemsgs, m);   /* 0x57fed0 */
    }
}

bool ICMP_start(void)
{
    if (!running) {
        NetList_Init(&freemsgs, NULL, NULL);
        NetList_Init(&queue, NULL, NULL);
        for (int i = 0; i < NMSG; i++) NetList_Push(&freemsgs, &msgs[i]);
        if (!wake) wake = EVENT_create();
        running = 1;   /* (set by the thread there, before ICMP.DLL is loaded) */
        if (!THREAD_create(icmp_thread, 0, 1, -1, &rec)) running = 0;
        /* (0x5570e0: ICMP_stop at exit) */
    }
    return running;
}

void ICMP_stop(void)
{
    if (!running) return;
    running = 0;
    IcmpMsg *m;
    while (!(m = NetList_Pop(&freemsgs))) THREAD_sleep(1);   /* 0x57fe00 */
    m->type = QUIT;
    msg_post(m);
    THREAD_wait(&rec, 0);   /* 0x55f7c0 */
}

bool ICMP_ping(const char *addr, IcmpAnswerFn fn, void *arg)
{
    if (!running) return false;
    IcmpMsg *m = NetList_Pop(&freemsgs);
    if (!m) return false;
    m->type = PING;
    m->addr = WS_inet_addr(addr);
    m->arg = arg;
    m->fn = fn;
    msg_post(m);
    return true;
}

/* --param thrash_test=11: each player pings 10.0.0.1 and 10.0.0.2 (itself among them) and an absent
   10.0.0.9 through the thread, and logs the answers. */
static volatile int test_answers;

static void test_answer(void *arg, int32_t ms)
{
    char m[64];
    snprintf(m, sizeof m, "icmptest: %s answered %d", (const char *)arg, (int)ms);
    plat_log(m);
    test_answers++;
}

static void test_thread(void *arg)
{
    uint8_t data[400];
    WS_startup(0x101, data);
    THREAD_sleep(1500);
    if (!ICMP_start()) {
        plat_log("icmptest: no pinger");
        return;
    }
    static const char *const to[3] = {"10.0.0.1", "10.0.0.2", "10.0.0.9"};
    for (int i = 0; i < 3; i++) ICMP_ping(to[i], test_answer, (void *)to[i]);
    while (test_answers < 3) THREAD_sleep(50);
    ICMP_stop();
    plat_log("icmptest: done");
}

void ICMP_selftest(void)
{
    static ThreadRec r;
    static bool started_test;
    if (!started_test) {
        started_test = true;
        THREAD_createarg(test_thread, NULL, 0x4000, 0, 0, &r);
    }
}
