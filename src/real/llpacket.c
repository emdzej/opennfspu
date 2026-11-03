/* The packet connections: see llpacket.h. */
#include "real/llpacket.h"
#include "exe.h"
#include "pending.h"
#include "platform.h"
#include "real/netgathr.h"
#include "real/real.h"
#include "real/systask.h"
#include "real/thread.h"
#include "real/udp.h"
#include "real/wsock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- the generic layer */

static NetList active;    /* 0x6b7c60 */
static NetList closed;    /* 0x6b7c40 */
static bool lists_ready;  /* 0x6a5a70 */
static int nticking;      /* 0x6a5a74 */

static int ret0(PacketConn *pc) { return 0; }   /* 0x55ff60, 0x55ff70 */
static void ret0v(PacketConn *pc) {}
static int ret0r(PacketConn *pc, const uint8_t *d, int n) { return 0; }

static void closeall_atexit(void)   /* 0x560130 */
{
    if (!lists_ready) return;
    PACKET_closeall();
    NetList_Free(&active);
    NetList_Free(&closed);
}

static void lists_init(void)   /* 0x5600f0 */
{
    if (lists_ready) return;
    NetList_Init(&active, NULL, NULL);
    NetList_Init(&closed, NULL, NULL);
    atexit(closeall_atexit);
    lists_ready = true;
}

/* The timer's tick on each open connection with a protocol tick. 0x5600a0, 0x5600c0 */
static bool tick_one(void *node, void *arg)
{
    PacketConn *pc = node;
    if (pc->proto->tick && pc->alive) pc->proto->tick(pc);
    return true;
}

static void tick(void)
{
    CS_enter(active.cs);
    NetList_Walk(&active, tick_one, NULL);
    CS_leave(active.cs);
}

int PACKET_open(PacketConn *pc, const PacketProto *proto, const void *link, int maxdata, uint32_t flags)
{
    if (!lists_ready) lists_init();
    if (pc) {
        if (!pc->timing[1]) pc->timing[1] = 2;
        if (!pc->timing[2]) pc->timing[2] = 4;
        if (!pc->timing[3]) pc->timing[3] = 6;
        if (!pc->idle) pc->idle = ret0;
        if (!pc->timeout) pc->timeout = ret0;
        if (!pc->closed) pc->closed = ret0v;
        if (!pc->receive) pc->receive = ret0r;
        pc->proto = proto;
        if (proto->tick) {
            if (nticking == 0) {
                CS_enter(g_net_sendcs);
                TIMER_addsecondary(tick);
                CS_leave(g_net_sendcs);
            }
            nticking++;
        }
        pc->filters = NULL;
    }
    int r = proto->init(pc, link, maxdata, (int)flags);
    if (pc && proto->tick && !r && --nticking == 0) {
        CS_enter(g_net_sendcs);
        TIMER_removesecondary(tick);
        CS_leave(g_net_sendcs);
    }
    return r;
}

int PACKET_close(PacketConn *pc)
{
    int r = 0;
    if (pc && pc->st && pc->proto) {
        r = pc->proto->close(pc);
        if (r && pc->proto->tick && nticking && --nticking == 0) {
            CS_enter(g_net_sendcs);
            TIMER_removesecondary(tick);
            CS_leave(g_net_sendcs);
        }
        if (pc->filters) PENDING_NOTE(0x586bf0, "the packet filters");
    }
    if (THREAD_isthread(NULL)) SYNCTASK_run(0);
    return r;
}

void PACKET_closeall(void)
{
    if (!lists_ready) return;
    for (PacketConn *pc; (pc = NetList_At(&active, 0));) PACKET_close(pc);
    for (PacketConn *pc; (pc = NetList_At(&closed, 0));) PACKET_close(pc);
}

/* (0x586d60 / 0x586db0: one piece, no filters.) */
int PACKET_send(PacketConn *pc, const void *data, uint32_t lenflags)
{
    if (!pc || !pc->alive) return 0;
    if (pc->filters) PENDING_NOTE(0x586e60, "the packet filters");
    return pc->proto->send(pc, data, (lenflags & 0xff000000u) | (lenflags & 0xffffff));
}

/* The data to the connection's owner (0x586c20; no filters). */
static int deliver(PacketConn *pc, const uint8_t *data, int len) { return pc->receive(pc, data, len); }
int PACKET_deliver(PacketConn *pc, const uint8_t *data, int len) { return deliver(pc, data, len); }
NetList *PACKET_active(void) { return &active; }
NetList *PACKET_closedlist(void) { return &closed; }

/* ---------------------------------------------------------------- the protocol 0x5e2e58 */

#define SEQ_LAST 0xfffbu
#define SEQ_ACK 0xfffdu
#define SEQ_UNRELIABLE 0xfffeu
#define SEQ_END 0xffffu

typedef struct PktSlot {
    struct PktSlot *next;  /* +0x00 */
    int32_t sent;          /* +0x04: the tick sent, -1 not yet */
    int32_t len;           /* +0x08: on the wire */
    uint8_t resent;        /* +0x0c */
    uint8_t pad;
    uint8_t wire[];        /* +0x0e: ack, seq, id, len, data */
} PktSlot;

struct PktState {
    PacketConn *pc;          /* +0x00 */
    const PacketLink *link;  /* +0x04 */
    CritSec *cs;             /* +0x08 */
    int maxdata;             /* +0x0c */
    bool allocated;          /* +0x10 */
    uint8_t id;              /* +0x11 */
    uint8_t heard;           /* +0x12: anything from the other end */
    PktSlot *ctrl;           /* +0x14: a slot for the lone acknowledgements */
    uint8_t *rbuf;           /* +0x18 */
    int rbufsize;            /* +0x1c */
    int32_t rttdev;          /* +0x20 */
    int32_t sinceack;        /* +0x28: in order since the last acknowledgement */
    int32_t acktimer;        /* +0x2c: -1 now */
    int32_t rexmit;          /* +0x30: -1 nothing out, -2 unsent ones */
    int32_t srtt, rttvar, rto;   /* +0x34 (x8), +0x38 (x4), +0x3c */
    int32_t extras, dups, resends, delivered, bytesin;   /* +0x40 .. +0x50 */
    uint32_t start;          /* +0x54 */
    uint8_t hdr;             /* +0x58 */
    uint16_t nextseq;        /* +0x5a */
    uint16_t acked;          /* +0x5c */
    uint16_t lastin;         /* +0x5e */
    NetList freel;           /* +0x60 */
    NetList sent;            /* +0x7c */
    uint16_t nextslot;       /* +0x98 */
    NetList window;          /* +0x9c: by the sequence number each slot waits for */
    NetList ooo;             /* +0xb8: arrived ahead */
    NetList ready;           /* +0xd4 */
    uint8_t *block;
};

static uint32_t ticks(void) { return TIMER_count; }   /* TIMER_ticks 0x53c050 */
static uint16_t next_seq(uint16_t s) { return s == SEQ_LAST ? 0 : (uint16_t)(s + 1); }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static uint16_t slot_seq(const PktSlot *s) { return be16(s->wire + 2); }
static uint32_t seq_key(void *node, void *arg) { return slot_seq(node); }   /* 0x595330 */

/* When the oldest unacknowledged packet is due again (-2 kept: unsent ones wait). 0x595850 */
static void set_rexmit(PktState *st, const PktSlot *s)
{
    if (s && s->sent == -1) {
        st->rexmit = -2;
        return;
    }
    if (st->rexmit == -2) return;
    PktSlot *first = NetList_Find(&st->sent, NULL, NULL);
    if (!first) {
        st->rexmit = -1;
        return;
    }
    st->rexmit = st->rto + first->sent - (int32_t)ticks();
    if (st->rexmit < 0) st->rexmit = 0;
}

/* The packets acknowledged freed; the round trip measured on the latest that wasn't resent. 0x595700 */
static void acknowledge(PktState *st, uint16_t ack)
{
    if ((uint16_t)(ack - st->acked - 1) >= (uint16_t)(st->nextseq - st->acked - 1)) return;
    st->acked = ack;
    int32_t best = 0x7ffffff;
    PktSlot *sample = NULL;
    for (PktSlot *s; (s = NetList_Pop(&st->sent));) {
        int32_t rtt = (int32_t)ticks() - s->sent;
        if (rtt < best && !s->resent) {
            best = rtt;
            sample = s;
        }
        NetList_Push(&st->freel, s);
        if (slot_seq(s) == ack) break;
    }
    if (sample && !sample->resent) {
        int32_t var;
        if (st->srtt == 0) {
            var = best * 2;
            st->srtt = best * 8;
        } else {
            if (best < 1) best = 1;
            int32_t err = best - (st->srtt >> 3);
            st->rttdev = (st->rttdev >> 1) + err;
            st->srtt += err;
            if (err < 0) err = -err;
            var = st->rttvar - (st->rttvar >> 2) + err;
        }
        int32_t rto = var + (st->srtt >> 3);
        st->rttvar = var;
        rto += rto >> 2;
        rto += rto >> 1;
        st->rto = rto;
        if (rto < (TIMER_RATE + 19) / 20) st->rto = (TIMER_RATE + 19) / 20;
        if (st->rto > TIMER_RATE * 60) st->rto = TIMER_RATE * 60;
    }
    set_rexmit(st, NULL);
}

/* A packet in its slot: in order, it and the ones it lets follow go to be delivered; else it waits.
   0x595900 */
static void arrived(PktState *st, PktSlot *s)
{
    uint16_t want = next_seq(st->lastin);
    if (slot_seq(s) != want) {
        NetList_Insert(&st->ooo, s);
        return;
    }
    int32_t k = st->sinceack++;
    st->acktimer = k < 2 ? TIMER_RATE / 2 : -1;
    st->lastin = want;
    NetList_Append(&st->ready, s);
    st->delivered++;
    want = next_seq(want);
    for (PktSlot *o; (o = NetList_TakeKey(&st->ooo, want));) {
        st->lastin = want;
        NetList_Append(&st->ready, o);
        st->delivered++;
        want = next_seq(want);
    }
}

static void remote_closed(PktState *st);

/* One datagram: the acknowledgement, then its packet and the ones it carries. 0x5954f0 */
static int parse(PktState *st, int *count, const uint8_t *p, int n)
{
    PacketConn *pc = st->pc;
    if (n < 6) return 0;
    int len = p[5];
    int end = len + 6;
    if (end > n) return 0;
    if (pc->alive) pc->alive = (int32_t)exe_u32(0x5df6a8);
    acknowledge(st, be16(p));
    st->heard = 1;
    uint16_t seq = be16(p + 2);
    if (len == 0) {
        if (seq == SEQ_END) remote_closed(st);
        return end;
    }
    if (seq > SEQ_LAST) {
        if (seq == SEQ_UNRELIABLE) deliver(pc, p + 6, len);
        return end;
    }
    PktSlot *s = NetList_TakeKey(&st->window, seq);
    if (s) {
        memcpy(s->wire, p, (size_t)end);
        arrived(st, s);
        (*count)++;
    } else {
        st->dups++;
    }
    int pos = end;
    while (pos + 2 < n) {
        uint8_t xl = p[pos], dist = p[pos + 1];
        pos += 2 + xl;
        if (pos > n) continue;
        uint16_t xs = (uint16_t)(seq - dist);
        if (xs > SEQ_LAST) xs = (uint16_t)(xs + 0xfffc);
        PktSlot *o = NetList_TakeKey(&st->window, xs);
        if (o) {
            st->extras++;
            put16(o->wire + 2, xs);
            o->wire[5] = xl;
            memcpy(o->wire + 6, p + pos - xl, xl);
            arrived(st, o);
            (*count)++;
        }
    }
    return pos;
}

/* A datagram from the link: true if it brought packets to deliver. 0x595460 */
static bool read_one(PktState *st)
{
    int count = 0;
    if (!st->link->decodes) {
        REAL_abort("\\real\\cmn\\llpacket.c", 0x2a5, "packet -- attempt to read without Link Layer decoding. Implement a link layer for serial!\n");
        return false;
    }
    int n = st->link->recv(st->pc, st->rbuf, st->rbufsize);
    st->bytesin += n + 0x1c;
    if (n > 0) parse(st, &count, st->rbuf, n);
    return count != 0;
}

/* The link has data: read it, deliver what is in order (a slot given back waits for the window's next
   number). 0x595350 */
static void on_data(PacketConn *pc)
{
    if (!pc->alive) return;
    PktState *st = pc->st;
    bool more;
    do {
        more = st->link->pending(pc) ? read_one(st) : false;
        for (PktSlot *s; (s = NetList_Pop(&st->ready));) {
            if (!deliver(pc, s->wire + 6, s->wire[5])) {
                NetList_Push(&st->ready, s);
                return;
            }
            CS_enter(st->sent.cs);
            put16(s->wire + 2, st->nextslot);
            st->nextslot = next_seq(st->nextslot);
            CS_leave(st->sent.cs);
            NetList_Insert(&st->window, s);
        }
    } while (more);
}

/* The other end closed (or went silent): the link down, the owner told. 0x5958c0 */
static void remote_closed(PktState *st)
{
    PacketConn *pc = st->pc;
    if (NetList_Remove(&active, pc)) {
        pc->alive = 0;
        st->link->close(pc);
        pc->closed(pc);
        NetList_Push(&closed, pc);
    }
}

/* Out with the last packet received acknowledged; the acknowledgement timer restarted. 0x595c60 */
static bool transmit(PktSlot *s, PktState *st)
{
    put16(s->wire, st->lastin);
    int len = s->len;
    int r = st->link->send(st->pc, s->wire, len);
    s->sent = r != len ? -1 : (int32_t)ticks();
    st->sinceack = 0;
    st->acktimer = TIMER_RATE;
    return r == len;
}

/* A packet into the slot: a reliable one numbered, with up to two of the newest unacknowledged after it
   while they fit. 0x595ac0 */
static void build(PktSlot *s, PktState *st, const void *data, int len, bool reliable)
{
    s->sent = -1;
    uint16_t seq = SEQ_UNRELIABLE;
    if (reliable) {
        CS_enter(st->sent.cs);
        seq = st->nextseq;
        st->nextseq = next_seq(seq);
        CS_leave(st->sent.cs);
    }
    put16(s->wire + 2, seq);
    s->wire[5] = (uint8_t)len;
    s->wire[4] = st->hdr;
    s->resent = 0;
    s->len = (len & 0xff) + 6;
    memcpy(s->wire + 6, data, (size_t)len);
    if (!reliable) return;
    int n = NetList_Count(&st->sent);
    uint8_t *at = s->wire + 6 + len;
    for (int k = 0; k < 2; k++) {
        PktSlot *o = NetList_At(&st->sent, --n);
        if (!o) continue;
        int sz = o->wire[5] + 2;
        if (st->maxdata < s->len + sz) return;
        at[0] = o->wire[5];
        at[1] = (uint8_t)(seq - slot_seq(o));
        memcpy(at + 2, o->wire + 6, o->wire[5]);
        at += sz;
        s->len += sz;
    }
}

/* A control packet alone (the acknowledgement, the end). 0x595e30 */
static bool control(PktSlot *s, PktState *st, uint16_t seq)
{
    s->wire[5] = 0;
    s->wire[4] = st->hdr;
    put16(s->wire + 2, seq);
    put16(s->wire, st->lastin);
    return st->link->send(st->pc, s->wire, 6) == 6;
}

/* 0x5959f0: a reliable packet waits in the sent list for its acknowledgement; an unreliable one is
   sent from the stack. */
static int pk_send(PacketConn *pc, const void *data, uint32_t lenflags)
{
    bool reliable = !(lenflags & PACKET_UNRELIABLE);
    if (!pc || !pc->alive) return 0;
    PktState *st = pc->st;
    uint8_t local[sizeof(PktSlot) + 0x200];
    PktSlot *s = reliable ? NetList_Pop(&st->freel) : (PktSlot *)(void *)local;
    if (!s) return 0;
    build(s, st, data, (int)(lenflags & 0xffffff), reliable);
    if (!transmit(s, st) && !reliable) return 0;
    if (reliable) {
        NetList_Append(&st->sent, s);
        set_rexmit(st, s);
    }
    return 1;
}

static int pk_freeslots(PacketConn *pc)   /* 0x595cf0 */
{
    NetList *l = &pc->st->freel;
    return NetList_Count(l) ? NetList_Count(l) : 0;
}

typedef struct Budget { PktState *st; int n; } Budget;

/* 0x595ee0: the unsent ones out, while the budget lasts. */
static bool resend_unsent(void *node, void *arg)
{
    PktSlot *s = node;
    Budget *b = arg;
    if (b->n > 0 && s->sent == -1) {
        b->n--;
        if (!transmit(s, b->st)) b->n = 0;
    }
    set_rexmit(b->st, s);
    return true;
}

/* 0x595e90: due again, resent (and so no round trip measured on it). */
static bool resend(void *node, void *arg)
{
    PktSlot *s = node;
    Budget *b = arg;
    if (b->n > 0) {
        b->n--;
        s->resent = 1;
        b->st->resends++;
        if (!transmit(s, b->st)) b->n = 0;
        set_rexmit(b->st, s);
    }
    return true;
}

/* A tick: the silence time-out, the resends (unsent ones first, then the oldest on its time-out, the
   time-out doubled), and an acknowledgement alone when one is due. 0x595d20 */
static int pk_tick(PacketConn *pc)
{
    PktState *st = pc->st;
    if (--pc->alive == 0) {
        pc->alive = pc->timeout(pc);
        if (pc->alive == 0) {
            remote_closed(st);
            return 1;
        }
    }
    if (!st->link->isup(pc)) {
        remote_closed(st);
        return 1;
    }
    if (st->rexmit == -2) {
        Budget b = {st, 4};
        st->rexmit = 0;
        CS_enter(st->sent.cs);
        NetList_Walk(&st->sent, resend_unsent, &b);
        CS_leave(st->sent.cs);
    }
    if (st->rexmit >= 0 && --st->rexmit == -1) {
        st->rto *= 2;
        if (st->rto > TIMER_RATE * 60) st->rto = TIMER_RATE * 60;
        Budget b = {st, 1};
        CS_enter(st->sent.cs);
        NetList_Walk(&st->sent, resend, &b);
        CS_leave(st->sent.cs);
    }
    if (st->acktimer-- > 0) return 1;
    control(st->ctrl, st, SEQ_ACK);
    st->acktimer = TIMER_RATE;
    return 1;
}

/* The end sent (three tries), the link closed, the state freed. 0x595f30 */
static int pk_close(PacketConn *pc)
{
    if (!pc || !pc->st) return 0;
    PktState *st = pc->st;
    const PacketLink *link = st->link;
    for (PktSlot *s; (s = NetList_Pop(&st->sent));) NetList_Push(&st->freel, s);
    if (NetList_Remove(&active, pc)) {
        if (pc->alive) {
            pc->alive = 0;
            for (int tries = 3; tries;) {
                if (!link->isup(pc)) break;
                if (control(st->ctrl, st, SEQ_END)) break;
                if (!THREAD_isthread(NULL)) THREAD_sleepticks(1);
                else SYNCTASK_wait(1);   /* (0x53c0f0) */
                tries--;
            }
            if (!THREAD_isthread(NULL)) THREAD_sleepticks(3);
            else SYNCTASK_wait(3);
            link->close(pc);
        }
        NetList_Push(&closed, pc);
    }
    if (NetList_Remove(&closed, pc)) {
        NetList_Free(&st->freel);
        NetList_Free(&st->sent);
        NetList_Free(&st->window);
        NetList_Free(&st->ooo);
        NetList_Free(&st->ready);
        CS_destroy(st->cs);
        if (st->allocated) {
            free(st->block);
            free(st);
            pc->st = NULL;
        }
        return 1;
    }
    return 0;
}

/* The state and its slots (half the window's sequence numbers to send from, as many less one to receive
   into, a control slot and a receive buffer), the link opened, then waiting until the other end is
   heard (the idle callback can give up). 0x594f50 */
static uint8_t next_id = 1;   /* 0x5e2e50 */

static int pk_init(PacketConn *pc, const void *linkp, int maxdata, int flags)
{
    const PacketLink *link = linkp;
    if (maxdata < 6) maxdata = 6;
    if (maxdata < link->mindata) maxdata = link->mindata;
    int half = (1 << (flags & 0x1f)) / 2, n = half - 1;
    int bufsize = link->decodes ? 0x100 : 0x200;
    size_t slotsize = (size_t)(maxdata + 0x17) & ~(size_t)3;
    if (!pc) return (int)((size_t)(half + n) * slotsize + 0xf0 + (size_t)bufsize);
    bool allocated = pc->st == NULL;
    PktState *st = allocated ? calloc(1, sizeof *st) : pc->st;
    if (!st) return 0;
    if (allocated) {
        st->block = calloc((size_t)(n * 2 + 1) * slotsize + (size_t)bufsize, 1);   /* "PKTBUF" */
        if (!st->block) {
            free(st);
            return 0;
        }
    }
    pc->next = NULL;
    pc->alive = 0;
    void (*saved_closed)(PacketConn *) = pc->closed;
    st->maxdata = maxdata;
    pc->st = st;
    st->rexmit = -1;
    st->acktimer = -1;
    pc->closed = ret0v;
    st->pc = pc;
    st->sinceack = 0;
    st->srtt = st->rttvar = 0;
    st->rto = TIMER_RATE * 3;
    st->extras = st->resends = st->dups = st->delivered = st->bytesin = 0;
    st->rttdev = 0;
    st->hdr = 0;
    st->start = ticks();
    st->allocated = allocated;
    st->link = link;
    st->id = next_id++;
    st->heard = 0;
    st->cs = CS_create();
    st->nextseq = 1;
    st->acked = 0;
    st->lastin = 0;
    NetList_Init(&st->freel, NULL, NULL);
    NetList_Init(&st->sent, NULL, NULL);
    st->nextslot = 1;
    NetList_Init(&st->window, seq_key, st);
    NetList_Init(&st->ooo, seq_key, st);
    NetList_Init(&st->ready, NULL, NULL);
    uint8_t *at = st->block;
    for (int i = 0; i < n; i++, at += slotsize) {
        PktSlot *s = (PktSlot *)(void *)at;
        memset(s, 0, sizeof *s);
        NetList_Push(&st->freel, s);
    }
    for (int i = 0; i < n; i++, at += slotsize) {
        PktSlot *s = (PktSlot *)(void *)at;
        memset(s, 0, sizeof *s);
        put16(s->wire + 2, st->nextslot);
        st->nextslot = next_seq(st->nextslot);
        NetList_Insert(&st->window, s);
    }
    st->ctrl = (PktSlot *)(void *)at;
    st->rbuf = at + slotsize;
    st->rbufsize = bufsize;
    int32_t timeout = (int32_t)exe_u32(0x5df6a8);
    if (!timeout) REAL_abort("\\real\\cmn\\llpacket.c", 0x39e, "CONNpackettimeout - MUST BE NONZERO\n");
    bool gaveup = false;
    if (link->open(pc, on_data)) {
        pc->alive = 1000000000;
        NetList_Push(&active, pc);
        do {
            if (!THREAD_isthread(NULL)) {
                THREAD_sleepticks(1);
                gaveup = pc->idle(pc) != 0;
            } else {
                gaveup = SYNCTASK_wait(1) < 0 || pc->idle(pc) != 0;
            }
            if (pc->alive == 0) break;
            pc->alive = 1000000000;
        } while (!st->heard && !gaveup);
        if (pc->alive == 0 || gaveup) {
            pc->alive = 0;
            link->close(pc);
        }
    }
    if (pc->alive && !gaveup) {
        pc->alive = timeout;
        pc->closed = saved_closed;
        return 1;
    }
    NetList_Remove(&active, pc);
    NetList_Remove(&closed, pc);
    if (allocated) {
        free(st->block);
        free(st);
        pc->st = NULL;
    }
    return 0;
}

const PacketProto LLPACKET_proto = {0, pk_init, pk_send, pk_freeslots, pk_tick, pk_close};

/* ---------------------------------------------------------------- the gathering's link layer 0x5e1ad8 */

typedef struct LinkEntry { PacketConn *pc; NetConn *conn; NetAddr addr; uint32_t pad[1]; } LinkEntry;
static LinkEntry links[128];   /* 0x6abf28 */
static int nlinks;             /* 0x6acf2c */
static CritSec *links_cs;      /* 0x6acf28 */

/* Registered for the socket callback, the gathering's '=' datagrams from the address dropped: -1, asking
   (0 without a socket). 0x582220 */
static int link_open(PacketConn *pc, void (*notify)(PacketConn *))
{
    NetConn *c = pc->conn;
    pc->notify = notify;
    if (!c->socket) {
        pc->link = 0;
        return 0;
    }
    c->provider->discard(c->socket, &pc->addr, '=');
    if (!links_cs) links_cs = CS_create();
    CS_enter(links_cs);
    if (nlinks < (int)(sizeof links / sizeof *links)) {
        links[nlinks].pc = pc;
        links[nlinks].conn = c;
        links[nlinks].addr = pc->addr;
        nlinks++;
    }
    c->packets++;
    pc->link = -1;
    CS_leave(links_cs);
    return pc->link;
}

static int link_close(PacketConn *pc)   /* 0x5822d0 */
{
    NetConn *c = pc->conn;
    void *s = c->socket;
    if (s && c->packets) {
        CS_enter(links_cs);
        int i = 0;
        while (i < nlinks && links[i].pc != pc) i++;
        nlinks--;
        c->packets--;
        if (i < nlinks) memmove(&links[i], &links[i + 1], (size_t)(nlinks - i) * sizeof *links);
        CS_leave(links_cs);
        c->provider->discard(s, &pc->addr, '=');
    }
    pc->link = 0;
    return 1;
}

static int link_isup(PacketConn *pc) { return pc->link && pc->conn->opened; }   /* 0x582380 */

static int link_cansend(PacketConn *pc)   /* 0x5823b0 */
{
    NetConn *c = pc->conn;
    return c->provider->sendempty(c->socket) ? GATHR_link.maxpacket : 0;
}

static int link_pending(PacketConn *pc)   /* 0x5823e0 */
{
    NetConn *c = pc->conn;
    return c->provider->received(c->socket) * GATHR_link.maxpacket;
}

/* The next '=' datagram from the other end; the others through the gathering on the way (a '!' takes the
   connection down; the first '=' marks it up). 0x582410 */
static int link_recv(PacketConn *pc, uint8_t *buf, int max)
{
    NetConn *c = pc->conn;
    for (;;) {
        NetAddr a = pc->addr;
        uint8_t type = '=';
        int n = c->provider->recv(c->socket, &a, &type, buf, max);
        if (n == 0) return 0;
        uint32_t more;
        bool data = c->transport->receive(c, &a, type, buf, n, &more);
        if (!more) {
            pc->alive = 0;
        } else {
            if (!data) continue;
            if (pc->link) pc->link = 1;
        }
        if (data) return n;
    }
}

/* As '=' to the other end (asking with a 'q' first while not up). 0x5824b0 */
static int link_send(PacketConn *pc, const uint8_t *data, int len)
{
    NetConn *c = pc->conn;
    if (pc->link < 0) c->provider->send(c->socket, &pc->addr, 'q', NULL, 0);
    return c->provider->send(c->socket, &pc->addr, '=', data, len) ? len : 0;
}

const PacketLink GATHR_link = {link_open, link_close, link_isup, link_cansend, link_pending, link_recv, link_send,
                               0x102, 0xad, 1};

bool PACKET_dispatch(NetConn *c, const NetAddr *from)
{
    if (!links_cs) return false;
    bool got = false;
    CS_enter(links_cs);
    for (int i = 0; i < nlinks; i++)
        if (links[i].conn == c && NETCONN_same(c, from, &links[i].pc->addr)) {
            if (links[i].pc->notify) links[i].pc->notify(links[i].pc);
            got = true;
            break;
        }
    CS_leave(links_cs);
    return got;
}

int PACKET_connect(PacketConn *pc, int maxdata, uint32_t flags, NetConn *conn)
{
    if (pc) {
        pc->link = 0;
        pc->conn = conn;
        if (!pc->timing[1]) pc->timing[1] = (int16_t)((TIMER_RATE + 0x3b) / 0x3c);
        if (!pc->timing[2]) pc->timing[2] = (int16_t)((TIMER_RATE + 0x3b) / 0x3c);
        if (!pc->timing[3]) pc->timing[3] = (int16_t)((TIMER_RATE + 0x31) / 0x32);
    }
    return conn->transport->packets(conn, pc, maxdata, flags);
}

/* ---------------------------------------------------------------- self-test */

static PacketConn test_pc;
static NetAddr test_peer;
static volatile bool test_joined;
static int test_got;

static int test_on_join(NetConn *c, const NetPlayerInfo *p, int number, int ishost)
{
    test_peer = p->addr;
    test_joined = true;
    return 1;
}

static int test_on_game(NetConn *c, const NetGameInfo *g, int players, const NetAddr *from)
{
    test_peer = *from;
    return 0;
}

static int test_receive(PacketConn *pc, const uint8_t *data, int len)
{
    char m[96];
    test_got++;
    snprintf(m, sizeof m, "pktest: got \"%.*s\"", len, (const char *)data);
    plat_log(m);
    return 1;
}

/* The gathering's host and a guest (--param nethost=1 on one) open a packet connection to each other and
   send ten reliable packets and three unreliable ones. */
static void selftest_thread(void *arg)
{
    static NetConn conn;
    char m[128], v[8], who[16];
    const uint8_t *prov = UDP_register(NULL);
    if (!prov) {
        plat_log("pktest: no UDP");
        return;
    }
    static const uint8_t game[16] = {'O', 'p', 'e', 'n', 'N', 'F', 'S', 'P', 'U', ' ', 'p', 'k', 't', 0, 0, 1};
    bool hosting = plat_param("nethost", v, sizeof v) && v[0] == '1';
    snprintf(who, sizeof who, "%s", hosting ? "host" : "guest");
    if (!NET_open(&conn, NULL, game, prov, 0x5002, who, who)) return;
    if (hosting) {
        NetGameInfo gi;
        memset(&gi, 0, sizeof gi);
        gi.maxplayers = 4;
        conn.transport->host(&conn, &gi, NULL, test_on_join);
        while (!test_joined) THREAD_sleep(50);
    } else {
        THREAD_sleep(500);
        conn.transport->find(&conn, test_on_game, TIMER_RATE * 2, 0);
        if (conn.transport->join(&conn, &test_peer, NULL) != 1) {
            plat_log("pktest: join failed");
            return;
        }
    }
    memset(&test_pc, 0, sizeof test_pc);
    test_pc.addr = test_peer;
    test_pc.receive = test_receive;
    int r = PACKET_connect(&test_pc, 0xfa, 0x8004, &conn);
    snprintf(m, sizeof m, "pktest: %s connected %d", who, r);
    plat_log(m);
    if (!r) return;
    for (int i = 0; i < 10; i++) {
        char t[32];
        snprintf(t, sizeof t, "%s reliable %d", who, i);
        while (!PACKET_send(&test_pc, t, (uint32_t)strlen(t))) THREAD_sleep(10);
        THREAD_sleep(20);
    }
    for (int i = 0; i < 3; i++) {
        char t[32];
        snprintf(t, sizeof t, "%s unreliable %d", who, i);
        PACKET_send(&test_pc, t, (uint32_t)strlen(t) | PACKET_UNRELIABLE);
    }
    THREAD_sleep(3000);
    snprintf(m, sizeof m, "pktest: %s received %d, resends %d dups %d rto %d", who, test_got, test_pc.st ? test_pc.st->resends : -1,
             test_pc.st ? test_pc.st->dups : -1, test_pc.st ? test_pc.st->rto : -1);
    plat_log(m);
    PACKET_close(&test_pc);
    NET_close(&conn);
    plat_log("pktest: done");
}

void PACKET_selftest(void)
{
    static ThreadRec rec;
    static bool started_test;
    if (!started_test) {
        started_test = true;
        if (!THREAD_createarg(selftest_thread, NULL, 0x4000, 0, 0, &rec)) plat_log("pktest: no threads");
    }
}
