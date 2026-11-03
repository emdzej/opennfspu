/* The port's WinSock on a gasm-relay room: see wsock.h. */
#include "real/wsock.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RELAY_WELCOME = 1, RELAY_JOIN = 2, RELAY_LEAVE = 3, RELAY_FULL = 4, RELAY_DATA = 0x10 };
enum { WSAEWOULDBLOCK = 10035, WSAENOTSOCK = 10038, WSAEADDRINUSE = 10048, WSAENETDOWN = 10050,
       WSANOTINITIALISED = 10093, FIONBIO = 0x8004667e };
enum { NSOCK = 32, QUEUE = 256, HEADER = 8, MAXMSG = 0x10000 };

typedef struct Datagram {
    WsAddr from;
    int len;
    uint8_t *data;
} Datagram;

typedef struct Sock {
    bool used, nonblocking;
    int type;
    uint16_t port;          /* host order; 0 unbound */
    Datagram q[QUEUE];
    int head, count;
} Sock;

static int started;            /* WSAStartup's count */
static int32_t conn = -1;      /* the relay */
static int me = -1;            /* our relay index */
static bool full;
static Sock socks[NSOCK];
static uint16_t next_port = 49152;
static int last_error;
static uint8_t rx[MAXMSG];

uint16_t WS_htons(uint16_t v) { return (uint16_t)(v << 8 | v >> 8); }
uint32_t WS_htonl(uint32_t v) { return v << 24 | (v & 0xff00) << 8 | (v >> 8 & 0xff00) | v >> 24; }
int WS_lasterror(void) { return last_error; }

static int fail(int e)
{
    last_error = e;
    return WS_SOCKET_ERROR;
}

static Sock *sock(int s) { return s >= 1 && s <= NSOCK && socks[s - 1].used ? &socks[s - 1] : NULL; }

static void own_addr(uint8_t a[4])
{
    a[0] = 10, a[1] = 0, a[2] = 0, a[3] = (uint8_t)(me + 1);
}

static bool is_broadcast(const uint8_t a[4]) { return a[0] == 255 && a[1] == 255 && a[2] == 255 && a[3] == 255; }

/* A datagram into the queue of each socket of ours bound to its port (a broadcast reaches them all,
   the sender's own too, as Windows loops broadcasts back). */
static void deliver(const uint8_t from[4], uint16_t from_port, uint16_t to_port, const uint8_t *d, int n)
{
    for (int i = 0; i < NSOCK; i++) {
        Sock *k = &socks[i];
        if (!k->used || k->type != WS_SOCK_DGRAM || k->port != to_port || k->count == QUEUE) continue;
        Datagram *g = &k->q[(k->head + k->count) % QUEUE];
        memset(&g->from, 0, sizeof g->from);
        g->from.family = WS_AF_INET;
        g->from.port = WS_htons(from_port);
        memcpy(g->from.addr, from, 4);
        g->len = n;
        g->data = n ? malloc((size_t)n) : NULL;
        if (n && !g->data) continue;
        if (n) memcpy(g->data, d, (size_t)n);
        k->count++;
    }
}

void WS_pump(void)
{
    if (conn <= 0) return;
    for (;;) {
        int32_t n = plat_net_recv(conn, rx, sizeof rx);
        if (n <= 0) {
            if (n < 0) {
                plat_net_close(conn);
                conn = -1;
                me = -1;
            }
            return;
        }
        if (n > (int32_t)sizeof rx) return;   /* (too long for any datagram: left queued, never read) */
        switch (rx[0]) {
        case RELAY_WELCOME:
            if (n >= 3) me = rx[1];
            break;
        case RELAY_FULL:
            full = true;
            break;
        case RELAY_DATA:
            if (n >= 2 + HEADER) {
                const uint8_t *p = rx + 2;
                uint8_t mine[4], from[4] = {10, 0, 0, (uint8_t)(rx[1] + 1)};
                own_addr(mine);
                if (is_broadcast(p) || !memcmp(p, mine, 4))
                    deliver(from, (uint16_t)(p[6] << 8 | p[7]), (uint16_t)(p[4] << 8 | p[5]), p + HEADER,
                            (int)n - 2 - HEADER);
            }
            break;
        default:   /* JOIN, LEAVE: datagrams know no peers */
            break;
        }
    }
}

/* The relay opened on the first WSAStartup and waited for (a frame at a time, ten seconds at most) until
   it has given us our index: the host's address. */
static void relay_open(void)
{
    char url[256], room[64];
    if (!plat_param("relay", url, sizeof url) || !url[0]) return;
    if (!plat_param("room", room, sizeof room) || !room[0]) snprintf(room, sizeof room, "nfs5");
    char full_url[340];
    size_t l = strlen(url);
    snprintf(full_url, sizeof full_url, "%s%s%s", url, l && url[l - 1] == '/' ? "" : "/", room);
    conn = plat_net_open(full_url);
    if (conn <= 0) {
        conn = -1;
        return;
    }
    double until = plat_time_ms() + 10000;
    while (me < 0 && conn > 0 && !full && plat_time_ms() < until) {
        WS_pump();
        if (me >= 0) break;
        int st = plat_net_state(conn);
        if (st == PLAT_NET_CLOSED || st == PLAT_NET_ERROR) break;
        plat_thread_sleep_ms(1);
    }
    if (me < 0 && conn > 0) {
        plat_net_close(conn);
        conn = -1;
    }
}

int WS_startup(uint16_t version, void *data)
{
    if (started++ == 0) relay_open();
    return 0;
}

int WS_cleanup(void)
{
    if (started <= 0) return fail(WSANOTINITIALISED);
    if (--started == 0) {
        for (int i = 0; i < NSOCK; i++)
            if (socks[i].used) WS_closesocket(i + 1);
        if (conn > 0) plat_net_close(conn);
        conn = -1;
        me = -1;
        full = false;
    }
    return 0;
}

/* Datagram sockets of the Internet family only: there is no IPX here. */
int WS_socket(int af, int type, int protocol)
{
    if (af != WS_AF_INET || type != WS_SOCK_DGRAM) return fail(10047 /* WSAEAFNOSUPPORT */);
    for (int i = 0; i < NSOCK; i++)
        if (!socks[i].used) {
            memset(&socks[i], 0, sizeof socks[i]);
            socks[i].used = true;
            socks[i].type = type;
            return i + 1;
        }
    return fail(10024 /* WSAEMFILE */);   /* (WS_INVALID_SOCKET) */
}

int WS_closesocket(int s)
{
    Sock *k = sock(s);
    if (!k) return fail(WSAENOTSOCK);
    for (int i = 0; i < k->count; i++) free(k->q[(k->head + i) % QUEUE].data);
    memset(k, 0, sizeof *k);
    return 0;
}

static bool port_taken(uint16_t p)
{
    for (int i = 0; i < NSOCK; i++)
        if (socks[i].used && socks[i].port == p) return true;
    return false;
}

int WS_bind(int s, const WsAddr *a, int len)
{
    Sock *k = sock(s);
    if (!k) return fail(WSAENOTSOCK);
    uint16_t p = a ? WS_htons(a->port) : 0;
    if (p && port_taken(p)) return fail(WSAEADDRINUSE);
    while (!p || port_taken(p)) {   /* (an ephemeral port: 49152 and up) */
        p = next_port++;
        if (!next_port) next_port = 49152;
    }
    k->port = p;
    return 0;
}

int WS_getsockname(int s, WsAddr *a, int *len)
{
    Sock *k = sock(s);
    if (!k) return fail(WSAENOTSOCK);
    memset(a, 0, sizeof *a);
    a->family = WS_AF_INET;
    a->port = WS_htons(k->port);
    if (len) *len = (int)sizeof *a;
    return 0;
}

int WS_setsockopt(int s, int level, int name, const void *v, int len) { return sock(s) ? 0 : fail(WSAENOTSOCK); }

int WS_ioctlsocket(int s, uint32_t cmd, uint32_t *arg)
{
    Sock *k = sock(s);
    if (!k) return fail(WSAENOTSOCK);
    if (cmd == FIONBIO && arg) k->nonblocking = *arg != 0;
    return 0;
}

int WS_sendto(int s, const void *buf, int len, int flags, const WsAddr *to, int tolen)
{
    Sock *k = sock(s);
    if (!k) return fail(WSAENOTSOCK);
    if (!k->port) WS_bind(s, NULL, 0);
    if (conn <= 0 || me < 0) return fail(WSAENETDOWN);
    static uint8_t tx[2 + HEADER + MAXMSG];
    if (len < 0 || len > MAXMSG) return fail(10040 /* WSAEMSGSIZE */);
    tx[0] = RELAY_DATA;
    memcpy(tx + 1, to->addr, 4);
    memcpy(tx + 5, &to->port, 2);
    tx[7] = (uint8_t)(k->port >> 8), tx[8] = (uint8_t)k->port;
    memcpy(tx + 1 + HEADER, buf, (size_t)len);
    uint8_t mine[4];
    own_addr(mine);
    bool local = !memcmp(to->addr, mine, 4) || (to->addr[0] == 127);
    if (!local && !plat_net_send(conn, tx, (uint32_t)(1 + HEADER + len))) return fail(WSAENETDOWN);
    if (local || is_broadcast(to->addr)) deliver(mine, k->port, WS_htons(to->port), buf, len);
    return len;
}

int WS_recvfrom(int s, void *buf, int len, int flags, WsAddr *from, int *fromlen)
{
    Sock *k = sock(s);
    if (!k) return fail(WSAENOTSOCK);
    WS_pump();
    while (!k->count) {
        if (k->nonblocking) return fail(WSAEWOULDBLOCK);
        plat_thread_sleep_ms(1);
        WS_pump();
        if (!k->used) return fail(WSAENOTSOCK);
    }
    Datagram *g = &k->q[k->head];
    int n = g->len < len ? g->len : len;
    if (n > 0) memcpy(buf, g->data, (size_t)n);
    if (from) *from = g->from;
    if (fromlen) *fromlen = (int)sizeof *from;
    free(g->data);
    k->head = (k->head + 1) % QUEUE;
    k->count--;
    return n;   /* (a longer one is cut, WSAEMSGSIZE in Windows; the game reads short ones) */
}

static bool readable(const WsFdSet *r)
{
    for (uint32_t i = 0; r && i < r->count; i++) {
        Sock *k = sock(r->fd[i]);
        if (k && k->count) return true;
    }
    return false;
}

/* Only readability is waited for (the library's sockets are always writable here). */
int WS_select(int n, WsFdSet *r, WsFdSet *w, WsFdSet *e, const WsTimeval *t)
{
    double until = t ? plat_time_ms() + t->sec * 1000.0 + t->usec / 1000.0 : -1;
    WS_pump();
    while (!readable(r)) {
        if (t && plat_time_ms() >= until) break;
        plat_thread_sleep_ms(1);
        WS_pump();
    }
    int got = 0;
    if (r) {
        uint32_t keep = 0;
        for (uint32_t i = 0; i < r->count; i++) {
            Sock *k = sock(r->fd[i]);
            if (k && k->count) r->fd[keep++] = r->fd[i];
        }
        r->count = keep;
        got += (int)keep;
    }
    if (w) got += (int)w->count;
    if (e) e->count = 0;
    return got;
}

int WS_fdisset(int s, const WsFdSet *set)
{
    for (uint32_t i = 0; set && i < set->count; i++)
        if (set->fd[i] == s) return 1;
    return 0;
}

/* The host's name: "nfs5-<index>" in a room, "localhost" without one. */
int WS_gethostname(char *name, int len)
{
    if (me >= 0) snprintf(name, (size_t)len, "nfs5-%d", me);
    else snprintf(name, (size_t)len, "localhost");
    return 0;
}

uint32_t WS_inet_addr(const char *s)
{
    unsigned a, b, c, d;
    char tail;
    if (!s || sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
        return WS_INADDR_NONE;
    uint8_t v[4] = {(uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d};
    uint32_t r;
    memcpy(&r, v, 4);
    return r;
}

char *WS_inet_ntoa(const uint8_t addr[4])
{
    static char b[16];
    snprintf(b, sizeof b, "%u.%u.%u.%u", addr[0], addr[1], addr[2], addr[3]);
    return b;
}

/* Names: our own (the host's address, or 127.0.0.1 without a room), "nfs5-<i>" a room's peer i, and
   dotted addresses. */
WsHost *WS_gethostbyname(const char *name)
{
    static uint8_t addr[4];
    static char *list[2] = {(char *)addr, NULL};
    static char *noalias[1] = {NULL};
    static char hname[64];
    static WsHost h;
    char own[32];
    WS_gethostname(own, sizeof own);
    int peer;
    uint32_t dotted = WS_inet_addr(name);
    if (dotted != WS_INADDR_NONE) memcpy(addr, &dotted, 4);
    else if (!strcmp(name, own) || !strcmp(name, "localhost")) {
        if (me >= 0 && strcmp(name, "localhost")) own_addr(addr);
        else addr[0] = 127, addr[1] = 0, addr[2] = 0, addr[3] = 1;
    } else if (sscanf(name, "nfs5-%d", &peer) == 1 && peer >= 0 && peer < 255)
        addr[0] = 10, addr[1] = 0, addr[2] = 0, addr[3] = (uint8_t)(peer + 1);
    else
        return NULL;
    snprintf(hname, sizeof hname, "%s", name);
    h.name = hname;
    h.aliases = noalias;
    h.addrtype = WS_AF_INET;
    h.length = 4;
    h.addr_list = list;
    return &h;
}

/* --param thrash_test=3: two or more players in a room broadcast their name on port 0x5000 from a thread
   of their own, once a second, and log what they hear (a check of the relay, the sockets and the threads). */
#include "real/thread.h"
static void selftest_thread(void *arg)
{
    WS_startup(0x101, NULL);
    int s = WS_socket(WS_AF_INET, WS_SOCK_DGRAM, 0);
    WsAddr a = {WS_AF_INET, WS_htons(0x5000), {0}, {0}};
    WS_bind(s, &a, sizeof a);
    char host[32], m[128];
    WS_gethostname(host, sizeof host);
    snprintf(m, sizeof m, "wstest: %s", host);
    plat_log(m);
    WsAddr all = {WS_AF_INET, WS_htons(0x5000), {255, 255, 255, 255}, {0}};
    for (int round = 0; round < 6; round++) {
        WS_sendto(s, host, (int)strlen(host) + 1, 0, &all, sizeof all);
        WsFdSet r = {1, {s}};
        WsTimeval t = {1, 0};
        while (WS_select(0, &r, NULL, NULL, &t) > 0) {
            char b[64];
            WsAddr from;
            int n = WS_recvfrom(s, b, sizeof b - 1, 0, &from, NULL);
            if (n <= 0) break;
            b[n] = 0;
            snprintf(m, sizeof m, "wstest: %s heard \"%s\" from %s:%u", host, b, WS_inet_ntoa(from.addr), WS_htons(from.port));
            plat_log(m);
            r = (WsFdSet){1, {s}};
        }
    }
    WS_closesocket(s);
    WS_cleanup();
    plat_log("wstest: done");
}

void WS_selftest(void)
{
    static ThreadRec rec;
    static bool started_test;
    if (!started_test) {
        started_test = true;
        if (!THREAD_createarg(selftest_thread, NULL, 0x4000, 0, 0, &rec)) plat_log("wstest: no threads");
    }
}
