/* The TCP/IP stream provider, its transport and the stream protocol: see tcp.h. */
#include "real/tcp.h"
#include "exe.h"
#include "platform.h"
#include "real/real.h"
#include "real/systask.h"
#include "real/thread.h"
#include "real/udp.h"
#include "real/wsock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WSAEWOULDBLOCK = 10035, WSAENOBUFS = 10055 };

/* The provider's socket ("socket", 0x54 bytes). */
typedef struct TcpSock {
    uint32_t port;                       /* +0x00 */
    ThreadRec thread;                    /* +0x04 */
    int32_t listener;                    /* +0x20 */
    int32_t accepted;                    /* +0x24: -1 none */
    NetAddr acceptaddr;                  /* +0x28: its address, the family cleared */
    int32_t keep;                        /* +0x3c: the tick it is dropped at, untaken */
    NetConn *owner;                      /* +0x44 */
    NetRecvFn recv;                      /* +0x48: a packet came (before it is handed on) */
    void (*idle)(void *owner);           /* +0x4c: a connection with nothing to send */
    volatile uint8_t stop;               /* +0x52 */
    volatile uint8_t running;            /* +0x53 */
} TcpSock;

/* A packet connection's state ("TCPDATA", the largest packet + 0x10 bytes). */
typedef struct TcpData {
    uint32_t allocated;                  /* +0x00: by init (else the caller's) */
    uint16_t max;                        /* +0x04: the largest packet */
    uint16_t pending;                    /* +0x06: the bytes of the packet still to send */
    uint8_t *next;                       /* +0x08: from */
    uint8_t buf[];                       /* +0x0c: [length 2][data] */
} TcpData;

static int registered;     /* 0x6abf20 */
static int started;        /* 0x6abf18: WSAStartup's count */
static uint16_t maxdgram;  /* 0x6abf10: WSADATA's iMaxUdpDg */
static int maxpacket;      /* 0x6abf1c */

static TcpData *data_of(PacketConn *pc) { return (TcpData *)(void *)pc->st; }

static int max_of(void) { return maxdgram == 0 || 0x400 < maxdgram - 2 ? 0x400 : maxdgram - 2; }

/* Big-endian numbers. 0x56e040, 0x56e070 */
static uint32_t get_be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }
static void put_be16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8), p[1] = (uint8_t)v; }

/* The main thread runs the library's tasks while it waits; a thread lets the others run. */
static bool wait_turn(void)
{
    if (!THREAD_isthread(NULL)) {
        THREAD_sleep(0);
        return true;
    }
    return (int)SYNCTASK_run(0) >= 0;   /* 0x536450 */
}

/* ---------------------------------------------------------------- sockets (tcp.c) */

/* A stream socket bound to the port on any address. 0x581040 */
static int bound_socket(uint32_t port)
{
    int s = WS_socket(WS_AF_INET, WS_SOCK_STREAM, 0);
    if (s == WS_INVALID_SOCKET) return -1;
    WsAddr a;
    memset(&a, 0, sizeof a);
    a.family = WS_AF_INET;
    a.port = WS_htons((uint16_t)port);
    uint32_t any = WS_htonl(0);
    memcpy(a.addr, &any, 4);
    if (WS_bind(s, &a, (int)sizeof a) == 0) return s;
    WS_closesocket(s);
    return -1;
}

/* Non-blocking, SO_REUSEADDR (8) and TCP_NODELAY (6, 1). 0x5810b0 */
static void set_options(int s)
{
    uint32_t on = 1;
    WS_ioctlsocket(s, 0x8004667eu, &on);
    int32_t v = 1;
    WS_setsockopt(s, 0xffff, 8, &v, 4);
    v = 1;
    WS_setsockopt(s, 6, 1, &v, 4);
}

/* A connect in progress: 2 done, 1 still going, 3 refused (or unreachable, timed out), 0 another error.
   0x581110 */
static int poll_connect(int s)
{
    CS_enter(g_net_sendcs);
    CS_leave(g_net_sendcs);
    WsFdSet w = {1, {s}}, e = {1, {s}};
    WsTimeval t = {0, 100};
    if (WS_select(s + 1, NULL, &w, &e, &t) == -1) return 0;
    if (WS_fdisset(s, &e)) {
        int32_t err = 0;
        int len = 4;
        if (WS_getsockopt(s, 0xffff, 0x1007, &err, &len) == -1) return 0;
        return err == 10061 || err == 10051 || err == 10060 ? 3 : 0;
    }
    return WS_fdisset(s, &w) ? 2 : 1;
}

/* ---------------------------------------------------------------- the protocol 0x5e1a10 */

/* Its state (NULL pc: the size it needs), then the socket: with no address the one the provider's thread
   accepted, waited for; else a connect to the address, again until it is made or the connection's idle
   says to stop (one the thread accepted from that address is taken instead, but the accepted address's
   family is cleared, so the comparison never matches). The open ones go into the active list. 0x580d10 */
static int tcp_init(PacketConn *pc, const void *link, int maxdata, int flags)
{
    if (!pc) return maxdata + 0x10;
    TcpData *d = data_of(pc);
    bool mine = d == NULL;
    if (mine) d = calloc(1, (size_t)maxdata + 0x10);
    if (!d) return 0;
    d->allocated = mine;
    d->max = (uint16_t)max_of();
    d->pending = 0;
    d->next = NULL;
    TcpSock *ts = pc->conn->socket;
    pc->alive = 0;
    pc->st = (PktState *)(void *)d;
    uint16_t family;
    memcpy(&family, pc->addr.b, 2);
    bool noaddr = family == 0;
    bool take = false;
    if (noaddr) {
        while (ts->accepted == -1) {
            if (!wait_turn() || pc->idle(pc)) break;
            THREAD_sleep(1000 / TIMER_RATE);
        }
        take = ts->accepted != -1;
    } else if (ts->accepted != -1) {
        take = NETCONN_same(pc->conn, &pc->addr, &ts->acceptaddr);   /* 0x580340 */
    }
    if (take) {
        CS_enter(g_net_sendcs);
        pc->link = ts->accepted;
        pc->addr = ts->acceptaddr;
        pc->alive = 1;
        ts->accepted = -1;
        CS_leave(g_net_sendcs);
    } else if (!noaddr) {
        bool failed = false;
        int state = 1;
        do {
            if (state == 0 || failed) break;
            if (!wait_turn() || pc->idle(pc)) {
                failed = true;
                pc->alive = 0;
                break;
            }
            int s = bound_socket(0);
            if (s == -1) {
                failed = true;
                continue;
            }
            set_options(s);
            WS_connect(s, (const WsAddr *)pc->addr.b, 0x10);
            int err = WS_lasterror();
            if (err == WSAEWOULDBLOCK) {
                pc->link = s;
                state = 1;
                while (state == 1) {
                    state = poll_connect(s);
                    if (state == 2) pc->alive = 1;
                    else if (state == 0) pc->alive = 0;
                    if (!wait_turn() || pc->idle(pc)) {
                        failed = true;
                        pc->alive = 0;
                        break;
                    }
                    THREAD_sleep(1000 / TIMER_RATE);
                }
            } else if (err == 10014 || err == 10022 || err == 10047 || err == 10048 || err == 10049 || err == 10050 ||
                       err == 10051 || err == 10056 || err == 10061 || err == 10064 || err == 10065) {
                failed = true;
                pc->alive = 0;
            }
            if (!pc->alive) WS_closesocket(s);
        } while (state != 2);
    }
    if (!pc->alive && d->allocated) {
        pc->st = NULL;
        free(d);
        return pc->alive;
    }
    if (pc->alive) NetList_Push(PACKET_active(), pc);
    return pc->alive;
}

/* The packet framed and sent at once, what the socket didn't take finished by the thread; 0 while a
   packet is still going (or the socket is full: then the packet is dropped). 0x581210 CONN_send */
static int tcp_send(PacketConn *pc, const void *data, uint32_t lenflags)
{
    TcpData *d = data_of(pc);
    uint32_t len = lenflags & 0xffffff;
    if (d->max < len)
        REAL_abort("\\real\\patch3\\pc\\tcp.c", 0x2e0, "CONN_send - PACKET TOO LONG, MAXIMUM SIZE = %d (TCP).\n", d->max);
    if (d->pending) return 0;
    CS_enter(g_net_sendcs);
    put_be16(d->buf, len);
    memcpy(d->buf + 2, data, len);
    d->pending = (uint16_t)(len + 2);
    d->next = d->buf;
    int r = WS_send(pc->link, d->buf, (int)(len + 2) & 0xffff, 0);
    if (r == -1) {
        int e = WS_lasterror();
        if (e == WSAENOBUFS || e == WSAEWOULDBLOCK) {
            d->pending = 0;
            CS_leave(g_net_sendcs);
            return 0;
        }
    } else if (0 < r) {
        d->pending = (uint16_t)(d->pending - r);
        d->next += r;
    }
    CS_leave(g_net_sendcs);
    return 1;
}

static int tcp_free(PacketConn *pc) { return data_of(pc)->pending == 0; }   /* 0x581300 */

/* Out of the active list: the socket shut down and closed; out of the closed list: the state freed.
   0x581320 */
static int tcp_close(PacketConn *pc)
{
    if (NetList_Remove(PACKET_active(), pc)) {
        WS_shutdown(pc->link, 2);
        THREAD_sleep(1);
        WS_closesocket(pc->link);
        pc->link = -1;
        pc->alive = 0;
        NetList_Push(PACKET_closedlist(), pc);
    }
    if (!NetList_Remove(PACKET_closedlist(), pc)) return 0;
    TcpData *d = data_of(pc);
    if (d->allocated) {
        free(d);
        pc->st = NULL;
    }
    return 1;
}

const PacketProto TCP_proto = {1, tcp_init, tcp_send, tcp_free, NULL, tcp_close};

/* ---------------------------------------------------------------- the provider's thread */

typedef struct Sweep {
    TcpSock *ts;
    WsFdSet r, w, e;
    int n;
    PacketConn *pc[64];
    uint32_t maxfd;
} Sweep;

/* A stream connection into the sets: its reads and errors, its writes while it has some left. 0x581ab0 */
static bool sweep_one(void *node, void *arg)
{
    PacketConn *pc = node;
    Sweep *w = arg;
    if (pc->proto->kind != 1 || pc->link == -1) return true;
    w->pc[w->n++] = pc;
    if (w->maxfd <= (uint32_t)pc->link) w->maxfd = (uint32_t)pc->link;
    if (w->r.count < 64) w->r.fd[w->r.count++] = pc->link;
    if (w->e.count < 64) w->e.fd[w->e.count++] = pc->link;
    if (data_of(pc)->pending && w->w.count < 64) w->w.fd[w->w.count++] = pc->link;
    return true;
}

/* A packet whole in the socket: peeked, handed on (left in the socket if it isn't taken), then read. 1
   the connection is done (closed, an error, too long). 0x581b60 */
static int read_one(TcpSock *ts, PacketConn *pc)
{
    TcpData *d = data_of(pc);
    uint8_t buf[0x404];
    int done = 0;
    CS_enter(g_net_sendcs);
    int r = WS_recv(pc->link, buf, 2, 2);
    if (r == 0) {
        done = 1;
    } else if (r == -1) {
        if (WS_lasterror() != WSAEWOULDBLOCK) done = 1;
    } else if (2 <= r) {
        int len = (int)get_be16(buf);
        if (d->max < len) {
            done = 1;
        } else {
            int whole = len + 2;
            r = WS_recv(pc->link, buf, whole, 2);
            if (r == whole) {
                if (ts->recv) ts->recv(pc->conn, &pc->addr);
                if (PACKET_deliver(pc, buf + 2, len))   /* 0x586c20 */
                    r = WS_recv(pc->link, buf, whole, 0);
            }
            if (r == 0) done = 1;
            else if (r == -1 && WS_lasterror() != WSAEWOULDBLOCK) done = 1;
        }
    }
    CS_leave(g_net_sendcs);
    return done;
}

/* The rest of a packet sent; with nothing left, the provider's idle. 1 on an error. 0x581c80 */
static int write_one(TcpSock *ts, PacketConn *pc)
{
    TcpData *d = data_of(pc);
    uint16_t left = d->pending;
    if (!left) {
        if (ts->idle) ts->idle(ts->owner);
        return 0;
    }
    int done = 0;
    CS_enter(g_net_sendcs);
    uint8_t *p = d->next;
    int r = WS_send(pc->link, p, left, 0);
    if (r == -1) {
        int e = WS_lasterror();
        if (e != WSAENOBUFS && e != WSAEWOULDBLOCK) {
            d->pending = 0;
            done = 1;
        }
    } else if (0 < r) {
        d->next = p + r;
        d->pending = (uint16_t)(left - r);
    }
    CS_leave(g_net_sendcs);
    return done;
}

static int error_one(void) { return 1; }   /* 0x581d30 */

/* A connection lost: its socket closed, out of the active list, closed for its user. 0x581d40 */
static void lose(PacketConn *pc)
{
    WS_closesocket(pc->link);
    pc->link = -1;
    if (NetList_Remove(PACKET_active(), pc)) {
        pc->alive = 0;
        pc->closed(pc);
        NetList_Push(PACKET_closedlist(), pc);
    }
}

/* The socket's thread: an accepted connection nobody took in four seconds (or that the join filter
   turns away) closed; the listener in the read set while none waits; every stream connection's; a 25 ms
   select; a new connection accepted (an error stops the thread); the connections served. 0x581800 */
static void sock_thread(void *arg)
{
    TcpSock *ts = arg;
    ts->running = 1;
    while (!ts->stop) {
        NetConn *o = ts->owner;
        int (*filter)(NetConn *, const NetAddr *) = o ? o->joinfilter : NULL;
        if (ts->accepted != -1 && ((filter && filter(o, &ts->acceptaddr) == 0) || ts->keep < (int32_t)TIMER_count)) {
            WS_closesocket(ts->accepted);
            ts->accepted = -1;
        }
        Sweep w;
        memset(&w, 0, sizeof w);
        w.ts = ts;
        if (ts->accepted == -1) {
            w.r.fd[0] = ts->listener;
            w.r.count = 1;
            w.maxfd = (uint32_t)ts->listener;
        } else {
            THREAD_sleep(1);
        }
        CS_enter(PACKET_active()->cs);   /* 0x580c10: the walk inside the list's section */
        NetList_Walk(PACKET_active(), sweep_one, &w);
        CS_leave(PACKET_active()->cs);
        CS_enter(g_net_sendcs);
        CS_leave(g_net_sendcs);
        WsTimeval t = {0, 25000};
        int ready = WS_select((int)w.maxfd + 1, &w.r, &w.w, &w.e, &t);
        if (ready == -1) continue;
        /* (A packet its connection holds, unread, keeps the socket readable and the select returns at once:
           the original spins and the system's threads run the others meanwhile; the port's threads take
           turns, so the round ends with a millisecond's sleep.) */
        if (0 < ready) plat_thread_sleep_ms(1);
        if (WS_fdisset(ts->listener, &w.r)) {
            WsAddr a;
            int len = 0x10;
            ts->accepted = WS_accept(ts->listener, &a, &len);
            if (ts->accepted == -1) {
                ts->stop = 1;
            } else {
                a.family = 0;
                memset(&ts->acceptaddr, 0, sizeof ts->acceptaddr);
                memcpy(ts->acceptaddr.b, &a, (size_t)len);
                set_options(ts->accepted);
                ts->keep = (int32_t)TIMER_count + TIMER_RATE * 4;
            }
        }
        for (int i = 0; i < w.n; i++) {
            PacketConn *pc = w.pc[i];
            if (pc->link == -1) continue;
            int done = 0;
            if (WS_fdisset(pc->link, &w.r)) done = read_one(ts, pc);
            if (WS_fdisset(pc->link, &w.w)) done |= write_one(ts, pc);
            if (WS_fdisset(pc->link, &w.e)) done |= error_one();
            if (done) lose(pc);
        }
    }
    ts->running = 0;
}

/* ---------------------------------------------------------------- the provider 0x5e1a78 */

/* WSAStartup on the first, the host's address read (0x5834f0) with a stream socket made; the largest
   packet. 0x581630 */
static int tcp_startup(void)
{
    if (started == 0) {
        uint8_t data[400] = {0};
        if (WS_startup(0x101, data) != 0) return 0;
        memcpy(&maxdgram, data + 0x188, 2);
    }
    int s = WS_socket(WS_AF_INET, WS_SOCK_STREAM, 0);
    if (s != -1) {
        NET_hostaddress(g_net_hostaddr);
        WS_closesocket(s);
    }
    started++;
    return max_of();
}

static bool tcp_cleanup(void)   /* 0x5816b0 */
{
    if (!started) return false;
    if (--started == 0) return WS_cleanup() == 0;
    return true;
}

static int tcp_closesock(void *p);

/* A listening socket on the port (0: any, read back), its thread started and waited for. 0x5816e0
   TCP_socket */
static void *tcp_open(int port, void *owner, NetRecvFn recv)
{
    TcpSock *ts = calloc(1, sizeof *ts);
    if (!ts) return NULL;
    ts->accepted = -1;
    ts->listener = bound_socket((uint32_t)port);
    ts->owner = owner;
    ts->recv = recv;
    if (port == 0) {
        WsAddr a;
        int len = 0x10;
        if (WS_getsockname(ts->listener, &a, &len) == 0) ts->port = WS_htons(a.port);
    } else {
        ts->port = (uint32_t)port;
    }
    if (ts->listener != -1 && WS_listen(ts->listener, 5) == 0 && THREAD_createarg(sock_thread, ts, 0, 2, -1, &ts->thread)) {
        while (!ts->running && !ts->stop) {
            wait_turn();
            THREAD_sleep(1);
        }
        if (ts->running) return ts;
    }
    tcp_closesock(ts);
    return NULL;
}

/* The thread stopped and waited for, the listener closed. 0x581d90 */
static int tcp_closesock(void *p)
{
    TcpSock *ts = p;
    if (ts->running) {
        ts->stop = 1;
        while (ts->running) {
            wait_turn();
            THREAD_sleep(1);
        }
    }
    if (ts->listener != -1) WS_closesocket(ts->listener);
    free(ts);
    return 1;
}

static int tcp_port(void *p) { return (int)((TcpSock *)p)->port; }   /* 0x581e00 */
static int tcp_zero(void *p, const char *t) { return 0; }            /* 0x581e10, 0x581e20, 0x581e30 */
static int tcp_zero1(void *p) { return 0; }                          /* 0x581e40 */

/* An address from text: dotted, else a host name; the port given, else the socket's. Not an empty text
   or one ending in a dot. 0x581e50 */
static int tcp_parse(void *p, const char *text, int port, NetAddr *out)
{
    TcpSock *ts = p;
    if (!text || !*text) return 0;
    if (text[strlen(text) - 1] == '.') return 0;
    uint32_t v = WS_inet_addr(text);
    memset(out, 0, sizeof *out);
    WsAddr *a = (WsAddr *)out->b;
    a->family = WS_AF_INET;
    if (v == WS_INADDR_NONE) {
        WsHost *h = WS_gethostbyname(text);
        if (!h) return 0;
        int n = h->length < 4 ? h->length : 4;
        memcpy(a->addr, h->addr_list[0], (size_t)n);
    } else {
        memcpy(a->addr, &v, 4);
    }
    a->port = WS_htons((uint16_t)(port ? (uint32_t)port : ts->port));
    return 1;
}

static int tcp_totext(void *s, const NetAddr *p, char *text, uint32_t *port)   /* 0x581f20 */
{
    const WsAddr *a = (const WsAddr *)p->b;
    strcpy(text, WS_inet_ntoa(a->addr));
    if (port) *port = WS_htons(a->port);
    return 1;
}

/* The first 16 bytes the same (the family too). 0x581f80 */
static bool tcp_same(void *s, const NetAddr *x, const NetAddr *y) { return !memcmp(x->b, y->b, 16); }

static int tcp_localaddr(void *p, NetAddr *out)   /* 0x581fa0 */
{
    int len = 0x14;
    return WS_getsockname(((TcpSock *)p)->listener, (WsAddr *)out->b, &len) != -1;
}

static int tcp_setidle(void *p, void (*idle)(void *owner))   /* 0x581fd0 */
{
    ((TcpSock *)p)->idle = idle;
    return 1;
}

static NetProvider tcp_provider = {
    NULL, {0}, tcp_startup, tcp_cleanup, tcp_open, tcp_closesock, tcp_port, tcp_zero, tcp_zero, tcp_zero,
    tcp_zero1, tcp_parse, tcp_totext, tcp_same, tcp_localaddr, NULL, NULL, NULL, NULL, NULL, tcp_setidle,
};

/* ---------------------------------------------------------------- the transport 0x5e1a28 */

/* For this provider only: the startup, the socket, its local address; the game's GUID and the names.
   0x5813c0 */
static int tr_open(NetConn *c, const uint8_t *game, const uint8_t *provider, int port, const char *name, const char *extra)
{
    const NetProvider *p = c->provider;
    if (memcmp(provider, p->guid, 16)) return 0;   /* 0x581470 */
    PACKET_active();   /* (the packet lists made, 0x5600f0) */
    maxpacket = p->startup();
    if (!maxpacket) return 0;
    c->socket = p->open(port, c, NULL);
    if (!c->socket) return 0;
    p->localaddr(c->socket, &c->self.addr);
    c->opened = 1;
    c->server = NULL;
    memcpy(c->guid, game, 16);
    NET_field(c->self.name, name, 0x33);
    NET_field(c->self.extra, extra, 0x13);
    return maxpacket;
}

static int tr_close(NetConn *c)   /* 0x5814d0 */
{
    if (!c->socket) return 1;
    if (c->keepalive) NET_leave(c);
    const NetProvider *p = c->provider;
    p->close(c->socket);
    c->opened = 0;
    c->socket = NULL;
    return p->cleanup();
}

/* No games here (0x581520 .. 0x5815b0, 0x5815f0 .. 0x581620). */
static int tr_find(NetConn *c, NetGameFn fn, int ticks, int user) { return 0; }
static int tr_join(NetConn *c, const NetAddr *host, const char *password) { return 0; }
static bool tr_query(NetConn *c, const NetAddr *host, NetGameInfo *out) { return false; }
static int tr_host(NetConn *c, NetGameInfo *info, const char *password, NetPlayerFn joinfn) { return 0; }
static int tr_setinfo(NetConn *c, const NetGameInfo *info) { return 0; }
static int tr_hide(NetConn *c, int free_it) { return 0; }
static int tr_zero(NetConn *c) { return 0; }
static unsigned tr_players(NetConn *c, const NetAddr *host, NetPlayerFn fn) { return 0; }
static int tr_kill(NetConn *c, const NetAddr *who) { return 0; }
static int tr_config(NetConn *c, const void *cfg) { return 0; }
static bool tr_receive(NetConn *c, const NetAddr *from, uint8_t type, const uint8_t *data, int len, uint32_t *more) { return false; }
static int tr_setmessage(NetConn *c, void (*fn)(NetConn *, const uint8_t *, int)) { return 0; }
static void tr_message(NetConn *c, const void *data, int len) {}

/* A packet connection on the stream protocol. 0x5815c0 */
static int tr_packets(NetConn *c, void *p, int maxdata, uint32_t flags)
{
    PacketConn *pc = p;
    if (pc) pc->timing[0] = 1;
    return PACKET_open(pc, &TCP_proto, NULL, maxdata, flags);
}

/* (+4 is a function returning 1 there: a non-zero word, so any provider is asked.) */
const NetTransport TCP_transport = {
    1, 1, tr_open, tr_close, tr_find, tr_join, tr_query, tr_host, tr_setinfo, tr_hide, tr_zero, tr_zero,
    tr_players, tr_kill, tr_packets, tr_config, tr_receive, tr_setmessage, tr_message,
};

/* Registered once if the machine has TCP/IP (0x583780, as UDP's), named as given. 0x581ff0 */
const uint8_t *TCP_register(const char *name)
{
    if (!g_net_sendcs) g_net_sendcs = CS_create();
    if (!registered && UDP_probe()) {
        registered = 1;
        tcp_provider.name = exe_str(0x5c10b0);   /* "TCP/IP" */
        memcpy(tcp_provider.guid, exe_data(0x5e1a7c, 16), 16);
        NET_addprovider(&tcp_provider, &TCP_transport);
        if (name) tcp_provider.name = name;
    }
    return registered ? tcp_provider.guid : NULL;
}

/* ---------------------------------------------------------------- self-test */

static PacketConn test_pc;
static int test_got;
static int32_t test_until;

static int test_filter(NetConn *c, const NetAddr *from) { return 1; }
static int test_idle(PacketConn *pc) { return (int32_t)TIMER_count > test_until; }

static int test_receive(PacketConn *pc, const uint8_t *data, int len)
{
    char m[96];
    test_got++;
    snprintf(m, sizeof m, "tcptest: got \"%.*s\"", len, (const char *)data);
    plat_log(m);
    return 1;
}

/* The listening side (--param nethost=1) takes the connection the other makes to 10.0.0.1, then each sends
   five packets. */
static void selftest_thread(void *arg)
{
    static NetConn conn;
    char m[128], v[8], who[16];
    const uint8_t *prov = TCP_register(NULL);
    if (!prov) {
        plat_log("tcptest: no TCP/IP");
        return;
    }
    static const uint8_t game[16] = {'O', 'p', 'e', 'n', 'N', 'F', 'S', 'P', 'U', ' ', 't', 'c', 'p', 0, 0, 1};
    bool hosting = plat_param("nethost", v, sizeof v) && v[0] == '1';
    snprintf(who, sizeof who, "%s", hosting ? "host" : "guest");
    int r = NET_open(&conn, test_filter, game, prov, 0x5003, who, who);
    snprintf(m, sizeof m, "tcptest: %s open %d port %d", who, r, r ? NETCONN_port(&conn) : -1);
    plat_log(m);
    if (!r) return;
    memset(&test_pc, 0, sizeof test_pc);
    test_pc.receive = test_receive;
    test_pc.idle = test_idle;
    test_until = (int32_t)TIMER_count + TIMER_RATE * 20;
    if (!hosting) {
        THREAD_sleep(1000);
        NETCONN_parse(&conn, "10.0.0.1", 0x5003, &test_pc.addr);
    }
    r = PACKET_connect(&test_pc, 0xfa, 0x8004, &conn);
    char peer[32] = "?";
    uint32_t port = 0;
    if (r) NETCONN_totext(&conn, &test_pc.addr, peer, &port);
    snprintf(m, sizeof m, "tcptest: %s connected %d to %s:%u", who, r, peer, port);
    plat_log(m);
    if (!r) return;
    for (int i = 0; i < 5; i++) {
        char t[32];
        snprintf(t, sizeof t, "%s packet %d", who, i);
        while (!PACKET_send(&test_pc, t, (uint32_t)strlen(t))) THREAD_sleep(10);
        THREAD_sleep(20);
    }
    THREAD_sleep(2000);
    snprintf(m, sizeof m, "tcptest: %s received %d", who, test_got);
    plat_log(m);
    PACKET_close(&test_pc);
    NET_close(&conn);
    plat_log("tcptest: done");
}

void TCP_selftest(void)
{
    static ThreadRec rec;
    static bool started_test;
    if (!started_test) {
        started_test = true;
        if (!THREAD_createarg(selftest_thread, NULL, 0x4000, 0, 0, &rec)) plat_log("tcptest: no threads");
    }
}
