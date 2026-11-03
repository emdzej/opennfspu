/* The port's WinSock on a gasm-relay room: see wsock.h. */
#include "real/wsock.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RELAY_WELCOME = 1, RELAY_JOIN = 2, RELAY_LEAVE = 3, RELAY_FULL = 4, RELAY_DATA = 0x10 };
enum { WSAEWOULDBLOCK = 10035, WSAEALREADY = 10037, WSAENOTSOCK = 10038, WSAEINVAL = 10022,
       WSAEADDRINUSE = 10048, WSAENETDOWN = 10050, WSAECONNRESET = 10054, WSAEISCONN = 10056,
       WSAENOTCONN = 10057, WSAETIMEDOUT = 10060, WSAECONNREFUSED = 10061, WSANOTINITIALISED = 10093,
       FIONBIO = 0x8004667e };
enum { NSOCK = 32, QUEUE = 256, HEADER = 9, MAXMSG = 0x10000, BACKLOG = 5 };
/* A message's kind (its header's last byte): a datagram, or a stream's handshake, data and end. */
enum { K_DGRAM = 0, K_SYN = 1, K_SYNACK = 2, K_DATA = 3, K_FIN = 4, K_RST = 5, K_IPX = 6 };
#define CONNECT_MS 21000.0   /* (Windows' connect gives up after about 21 seconds without an answer) */

typedef struct Datagram {
    WsAddr from;
    int len;
    uint8_t *data;
} Datagram;

typedef struct Sock {
    bool used, nonblocking;
    int af, type;
    uint16_t port;          /* host order; 0 unbound */
    Datagram q[QUEUE];
    int head, count;
    /* a stream: listening (the connections arrived and not yet accepted), or connecting or connected
       to its peer; what has come and not been read; the peer's end; the pending error (SO_ERROR) */
    bool listening, connecting, connected, fin, failed;
    int pend[BACKLOG], npend;
    uint8_t peer[4];
    uint16_t peerport;
    double until;           /* the connect's give-up time */
    uint8_t *rbuf;
    int rlen, rcap;
    int error;
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

/* An address of the family: sockaddr_in, or SOCKADDR_IPX (14 bytes: the family, the network (0 here),
   the node, the socket) whose node is 00:00 and the host's four address bytes. */
static void make_addr(WsAddr *a, int af, const uint8_t host[4], uint16_t port)
{
    memset(a, 0, sizeof *a);
    a->family = (uint16_t)af;
    uint8_t *p = (uint8_t *)a;
    uint16_t v = WS_htons(port);
    if (af == WS_AF_IPX) {
        memcpy(p + 8, host, 4);
        memcpy(p + 12, &v, 2);
    } else {
        a->port = v;
        memcpy(a->addr, host, 4);
    }
}

static int addr_len(int af) { return af == WS_AF_IPX ? 14 : (int)sizeof(WsAddr); }

/* An address's host (255.255.255.255 for IPX's broadcast node) and port. */
static uint16_t addr_parts(const WsAddr *a, int af, uint8_t host[4])
{
    const uint8_t *p = (const uint8_t *)a;
    uint16_t v;
    if (af == WS_AF_IPX) {
        static const uint8_t all[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        if (!memcmp(p + 6, all, 6)) memset(host, 0xff, 4);
        else memcpy(host, p + 8, 4);
        memcpy(&v, p + 12, 2);
    } else {
        memcpy(host, a->addr, 4);
        v = a->port;
    }
    return WS_htons(v);
}

/* A datagram into the queue of each socket of ours of the family bound to its port (a broadcast reaches
   them all, the sender's own too, as Windows loops broadcasts back). */
static void deliver(int af, const uint8_t from[4], uint16_t from_port, uint16_t to_port, const uint8_t *d, int n)
{
    for (int i = 0; i < NSOCK; i++) {
        Sock *k = &socks[i];
        if (!k->used || k->af != af || k->type != WS_SOCK_DGRAM || k->port != to_port || k->count == QUEUE) continue;
        Datagram *g = &k->q[(k->head + k->count) % QUEUE];
        make_addr(&g->from, af, from, from_port);
        g->len = n;
        g->data = n ? malloc((size_t)n) : NULL;
        if (n && !g->data) continue;
        if (n) memcpy(g->data, d, (size_t)n);
        k->count++;
    }
}

static bool emit(const uint8_t to[4], uint16_t to_port, uint16_t from_port, uint8_t kind, const void *d, int n);

static Sock *stream_of(const uint8_t from[4], uint16_t from_port, uint16_t to_port)
{
    for (int i = 0; i < NSOCK; i++) {
        Sock *k = &socks[i];
        if (k->used && k->type == WS_SOCK_STREAM && !k->listening && k->port == to_port && (k->connecting || k->connected) &&
            k->peerport == from_port && !memcmp(k->peer, from, 4))
            return k;
    }
    return NULL;
}

static void reset(Sock *k, int error)
{
    if (k->connecting) k->failed = true;
    k->connecting = k->connected = false;
    k->fin = true;
    k->error = error;
}

/* A stream's message: a connection asked for on a listening port is made at once (as the system's TCP
   completes the handshake before accept) and waits for accept; refused (RST) with no listener or a full
   backlog. */
static void deliver_stream(const uint8_t from[4], uint16_t from_port, uint16_t to_port, uint8_t kind, const uint8_t *d, int n)
{
    if (kind == K_SYN) {
        Sock *l = NULL;
        for (int i = 0; i < NSOCK && !l; i++)
            if (socks[i].used && socks[i].type == WS_SOCK_STREAM && socks[i].listening && socks[i].port == to_port) l = &socks[i];
        if (!l || l->npend == BACKLOG || stream_of(from, from_port, to_port)) {
            if (!l || l->npend == BACKLOG) emit(from, from_port, to_port, K_RST, NULL, 0);
            return;
        }
        int s = WS_socket(WS_AF_INET, WS_SOCK_STREAM, 0);
        Sock *k = s > 0 ? &socks[s - 1] : NULL;
        if (!k) {
            emit(from, from_port, to_port, K_RST, NULL, 0);
            return;
        }
        k->port = to_port;
        k->connected = true;
        memcpy(k->peer, from, 4);
        k->peerport = from_port;
        l->pend[l->npend++] = s;
        emit(from, from_port, to_port, K_SYNACK, NULL, 0);
        return;
    }
    Sock *k = stream_of(from, from_port, to_port);
    if (!k) {
        if (kind == K_DATA) emit(from, from_port, to_port, K_RST, NULL, 0);
        return;
    }
    switch (kind) {
    case K_SYNACK:
        if (k->connecting) k->connecting = false, k->connected = true;
        break;
    case K_RST:
        reset(k, k->connecting ? WSAECONNREFUSED : WSAECONNRESET);
        break;
    case K_FIN:
        k->fin = true;
        break;
    case K_DATA:
        if (n <= 0 || k->fin) break;
        if (k->rcap < k->rlen + n) {
            int cap = k->rcap ? k->rcap : 0x1000;
            while (cap < k->rlen + n) cap *= 2;
            uint8_t *b = realloc(k->rbuf, (size_t)cap);
            if (!b) break;
            k->rbuf = b, k->rcap = cap;
        }
        memcpy(k->rbuf + k->rlen, d, (size_t)n);
        k->rlen += n;
        break;
    }
}

/* --param netdrop=1 (a test): the other hosts' datagrams to us are lost, broadcasts aside, as behind a
   firewall that lets no UDP in (a join over UDP then fails, and CONMAN tries TCP/IP). */
static bool drop_unicast(void)
{
    static int drop = -1;
    if (drop < 0) {
        char v[8];
        drop = plat_param("netdrop", v, sizeof v) && v[0] == '1';
    }
    return drop;
}

/* A peer gone from the room: its streams reset. */
static void peer_left(int index)
{
    uint8_t a[4] = {10, 0, 0, (uint8_t)(index + 1)};
    for (int i = 0; i < NSOCK; i++) {
        Sock *k = &socks[i];
        if (k->used && k->type == WS_SOCK_STREAM && (k->connecting || k->connected) && !memcmp(k->peer, a, 4))
            reset(k, WSAECONNRESET);
    }
}

/* Connects that had no answer in time give up. */
static void timeouts(void)
{
    double now = plat_time_ms();
    for (int i = 0; i < NSOCK; i++)
        if (socks[i].used && socks[i].connecting && socks[i].until <= now) reset(&socks[i], WSAETIMEDOUT);
}

void WS_pump(void)
{
    if (conn <= 0) return;
    timeouts();
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
                uint16_t fp = (uint16_t)(p[6] << 8 | p[7]), tp = (uint16_t)(p[4] << 8 | p[5]);
                if (p[8] == K_DGRAM || p[8] == K_IPX) {
                    if (is_broadcast(p) || (!memcmp(p, mine, 4) && !drop_unicast()))
                        deliver(p[8] == K_IPX ? WS_AF_IPX : WS_AF_INET, from, fp, tp, p + HEADER, (int)n - 2 - HEADER);
                } else if (!memcmp(p, mine, 4)) {
                    deliver_stream(from, fp, tp, p[8], p + HEADER, (int)n - 2 - HEADER);
                }
            }
            break;
        case RELAY_LEAVE:
            if (n >= 2) peer_left(rx[1]);
            break;
        default:   /* JOIN */
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

/* The relay joined on the first WSAStartup and kept from then on: the room's index is the host's address,
   which stays the machine's whatever WinSock's count does (leaving and joining again could come back
   with another). */
int WS_startup(uint16_t version, void *data)
{
    if (started++ == 0 && conn <= 0) relay_open();
    return 0;
}

int WS_cleanup(void)
{
    if (started <= 0) return fail(WSANOTINITIALISED);
    if (--started == 0)
        for (int i = 0; i < NSOCK; i++)
            if (socks[i].used) WS_closesocket(i + 1);
    return 0;
}

/* Internet datagrams and streams, IPX datagrams. */
int WS_socket(int af, int type, int protocol)
{
    if (!(af == WS_AF_INET && (type == WS_SOCK_DGRAM || type == WS_SOCK_STREAM)) && !(af == WS_AF_IPX && type == WS_SOCK_DGRAM))
        return fail(10047 /* WSAEAFNOSUPPORT */);
    for (int i = 0; i < NSOCK; i++)
        if (!socks[i].used) {
            memset(&socks[i], 0, sizeof socks[i]);
            socks[i].used = true;
            socks[i].af = af;
            socks[i].type = type;
            return i + 1;
        }
    return fail(10024 /* WSAEMFILE */);   /* (WS_INVALID_SOCKET) */
}

/* (A connected stream tells its peer it has ended; the connections a listener hadn't accepted are
   refused.) */
int WS_closesocket(int s)
{
    Sock *k = sock(s);
    if (!k) return fail(WSAENOTSOCK);
    for (int i = 0; i < k->count; i++) free(k->q[(k->head + i) % QUEUE].data);
    if (k->type == WS_SOCK_STREAM) {
        if (k->connected || k->connecting) emit(k->peer, k->peerport, k->port, K_FIN, NULL, 0);
        for (int i = 0; i < k->npend; i++) {
            Sock *a = sock(k->pend[i]);
            if (a) emit(a->peer, a->peerport, a->port, K_RST, NULL, 0);
            if (a) free(a->rbuf), memset(a, 0, sizeof *a);
        }
        free(k->rbuf);
    }
    memset(k, 0, sizeof *k);
    return 0;
}

/* (Streams, datagrams and IPX have ports of their own, as TCP's, UDP's and IPX's sockets.) */
static bool port_taken(uint16_t p, const Sock *k)
{
    for (int i = 0; i < NSOCK; i++)
        if (socks[i].used && socks[i].af == k->af && socks[i].type == k->type && socks[i].port == p) return true;
    return false;
}

int WS_bind(int s, const WsAddr *a, int len)
{
    Sock *k = sock(s);
    if (!k) return fail(WSAENOTSOCK);
    uint8_t host[4];
    uint16_t p = a ? addr_parts(a, k->af, host) : 0;
    if (p && port_taken(p, k)) return fail(WSAEADDRINUSE);
    while (!p || port_taken(p, k)) {   /* (an ephemeral port: 49152 and up) */
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
    uint8_t host[4] = {0};
    if (k->af == WS_AF_IPX && me >= 0) own_addr(host);   /* (an IPX node is the host's; INADDR_ANY otherwise) */
    make_addr(a, k->af, host, k->port);
    if (len) *len = addr_len(k->af);
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

/* A message to (to, to_port) from our from_port: through the relay, and to our own sockets when it is
   ours (or 127.*) or a datagram broadcast. */
static bool emit(const uint8_t to[4], uint16_t to_port, uint16_t from_port, uint8_t kind, const void *d, int n)
{
    if (conn <= 0 || me < 0) return false;
    static uint8_t tx[2 + HEADER + MAXMSG];
    tx[0] = RELAY_DATA;
    memcpy(tx + 1, to, 4);
    tx[5] = (uint8_t)(to_port >> 8), tx[6] = (uint8_t)to_port;
    tx[7] = (uint8_t)(from_port >> 8), tx[8] = (uint8_t)from_port;
    tx[9] = kind;
    if (n > 0) memcpy(tx + 1 + HEADER, d, (size_t)n);
    uint8_t mine[4];
    own_addr(mine);
    bool local = !memcmp(to, mine, 4) || to[0] == 127;
    if (!local && !plat_net_send(conn, tx, (uint32_t)(1 + HEADER + n))) return false;
    if (kind == K_DGRAM || kind == K_IPX) {
        if (local || is_broadcast(to)) deliver(kind == K_IPX ? WS_AF_IPX : WS_AF_INET, mine, from_port, to_port, d, n);
    } else if (local) {
        deliver_stream(mine, from_port, to_port, kind, d, n);
    }
    return true;
}

int WS_sendto(int s, const void *buf, int len, int flags, const WsAddr *to, int tolen)
{
    Sock *k = sock(s);
    if (!k) return fail(WSAENOTSOCK);
    if (k->type == WS_SOCK_STREAM) return WS_send(s, buf, len, flags);
    if (!k->port) WS_bind(s, NULL, 0);
    if (conn <= 0 || me < 0) return fail(WSAENETDOWN);
    if (len < 0 || len > MAXMSG) return fail(10040 /* WSAEMSGSIZE */);
    uint8_t host[4];
    uint16_t port = addr_parts(to, k->af, host);
    if (!emit(host, port, k->port, k->af == WS_AF_IPX ? K_IPX : K_DGRAM, buf, len)) return fail(WSAENETDOWN);
    return len;
}

/* ---- streams ---- */

int WS_listen(int s, int backlog)
{
    Sock *k = sock(s);
    if (!k) return fail(WSAENOTSOCK);
    if (k->type != WS_SOCK_STREAM || !k->port || k->connected || k->connecting) return fail(WSAEINVAL);
    k->listening = true;
    return 0;
}

int WS_accept(int s, WsAddr *a, int *len)
{
    Sock *k = sock(s);
    if (!k) return fail(WSAENOTSOCK);
    if (!k->listening) return fail(WSAEINVAL);
    WS_pump();
    while (!k->npend) {
        if (k->nonblocking) return fail(WSAEWOULDBLOCK);
        plat_thread_sleep_ms(1);
        WS_pump();
        if (!k->used || !k->listening) return fail(WSAENOTSOCK);
    }
    int n = k->pend[0];
    memmove(k->pend, k->pend + 1, sizeof k->pend[0] * (size_t)--k->npend);
    Sock *c = sock(n);
    if (!c) return fail(WSAECONNRESET);
    if (a) {
        memset(a, 0, sizeof *a);
        a->family = WS_AF_INET;
        a->port = WS_htons(c->peerport);
        memcpy(a->addr, c->peer, 4);
    }
    if (len) *len = (int)sizeof *a;
    return n;
}

int WS_connect(int s, const WsAddr *a, int len)
{
    Sock *k = sock(s);
    if (!k) return fail(WSAENOTSOCK);
    if (k->type != WS_SOCK_STREAM || k->listening) return fail(WSAEINVAL);
    if (k->connected) return fail(WSAEISCONN);
    if (k->connecting) return fail(WSAEALREADY);
    if (!k->port) WS_bind(s, NULL, 0);
    if (conn <= 0 || me < 0) return fail(WSAENETDOWN);
    memcpy(k->peer, a->addr, 4);
    k->peerport = WS_htons(a->port);
    k->connecting = true;
    k->fin = k->failed = false;
    k->error = 0;
    k->until = plat_time_ms() + CONNECT_MS;
    emit(k->peer, k->peerport, k->port, K_SYN, NULL, 0);
    if (k->nonblocking) return fail(WSAEWOULDBLOCK);
    while (k->connecting) {
        plat_thread_sleep_ms(1);
        WS_pump();
    }
    if (!k->connected) {
        int e = k->error;
        k->error = 0;
        return fail(e ? e : (int)WSAECONNREFUSED);
    }
    return 0;
}

int WS_send(int s, const void *buf, int len, int flags)
{
    Sock *k = sock(s);
    if (!k) return fail(WSAENOTSOCK);
    if (!k->connected) return fail(k->error ? k->error : (int)WSAENOTCONN);
    for (int done = 0; done < len;) {
        int n = len - done < MAXMSG ? len - done : MAXMSG;
        if (!emit(k->peer, k->peerport, k->port, K_DATA, (const uint8_t *)buf + done, n)) return fail(WSAENETDOWN);
        done += n;
    }
    return len;
}

int WS_recv(int s, void *buf, int len, int flags)
{
    Sock *k = sock(s);
    if (!k) return fail(WSAENOTSOCK);
    if (k->type != WS_SOCK_STREAM) return WS_recvfrom(s, buf, len, flags, NULL, NULL);
    WS_pump();
    while (!k->rlen) {
        if (k->fin) {
            if (k->error == WSAECONNRESET) return fail(WSAECONNRESET);
            return 0;
        }
        if (!k->connected) return fail(WSAENOTCONN);
        if (k->nonblocking) return fail(WSAEWOULDBLOCK);
        plat_thread_sleep_ms(1);
        WS_pump();
        if (!k->used) return fail(WSAENOTSOCK);
    }
    int n = k->rlen < len ? k->rlen : len;
    if (n > 0) memcpy(buf, k->rbuf, (size_t)n);
    if (!(flags & 2)) {   /* (MSG_PEEK leaves it) */
        memmove(k->rbuf, k->rbuf + n, (size_t)(k->rlen - n));
        k->rlen -= n;
    }
    return n;
}

int WS_shutdown(int s, int how)
{
    Sock *k = sock(s);
    if (!k) return fail(WSAENOTSOCK);
    if (k->type == WS_SOCK_STREAM && k->connected && how >= 1) emit(k->peer, k->peerport, k->port, K_FIN, NULL, 0);
    return 0;
}

int WS_getsockopt(int s, int level, int name, void *v, int *len)
{
    Sock *k = sock(s);
    if (!k) return fail(WSAENOTSOCK);
    int32_t r = 0;
    if (level == 0xffff && name == 0x1007) {   /* SOL_SOCKET, SO_ERROR (read, cleared) */
        r = k->error;
        k->error = 0;
    }
    if (v && len && *len >= 4) memcpy(v, &r, 4), *len = 4;
    return 0;
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
    if (fromlen) *fromlen = addr_len(k->af);
    free(g->data);
    k->head = (k->head + 1) % QUEUE;
    k->count--;
    return n;   /* (a longer one is cut, WSAEMSGSIZE in Windows; the game reads short ones) */
}

/* Readable: datagrams queued, connections to accept, a stream's data or its end; writable: a datagram
   socket, a connected stream (the relay takes all we send); exceptional: a connect that failed. */
static bool is_readable(Sock *k)
{
    if (k->type == WS_SOCK_DGRAM) return k->count != 0;
    if (k->listening) return k->npend != 0;
    return k->rlen != 0 || (k->fin && !k->failed);
}
static bool is_writable(Sock *k) { return k->type == WS_SOCK_DGRAM || k->connected; }
static bool is_failed(Sock *k) { return k->type == WS_SOCK_STREAM && k->failed; }

static int ready(WsFdSet *set, bool (*test)(Sock *), bool keep_only)
{
    if (!set) return 0;
    uint32_t keep = 0;
    for (uint32_t i = 0; i < set->count; i++) {
        Sock *k = sock(set->fd[i]);
        if (k && test(k)) {
            if (keep_only) set->fd[keep] = set->fd[i];
            keep++;
        }
    }
    if (keep_only) set->count = keep;
    return (int)keep;
}

/* (A socket closed while it is waited on ends the wait with an error, as WinSock's.) */
static bool gone(const WsFdSet *set)
{
    for (uint32_t i = 0; set && i < set->count; i++)
        if (!sock(set->fd[i])) return true;
    return false;
}

static int any_ready(WsFdSet *r, WsFdSet *w, WsFdSet *e)
{
    return ready(r, is_readable, false) + ready(w, is_writable, false) + ready(e, is_failed, false);
}

/* (All three sets empty is an error in WinSock, at once.) */
int WS_select(int n, WsFdSet *r, WsFdSet *w, WsFdSet *e, const WsTimeval *t)
{
    if ((!r || !r->count) && (!w || !w->count) && (!e || !e->count)) return fail(WSAEINVAL);
    double until = t ? plat_time_ms() + t->sec * 1000.0 + t->usec / 1000.0 : -1;
    WS_pump();
    while (!any_ready(r, w, e)) {
        if (gone(r) || gone(w) || gone(e)) return fail(WSAENOTSOCK);
        if (t && plat_time_ms() >= until) break;
        plat_thread_sleep_ms(1);
        WS_pump();
    }
    return ready(r, is_readable, true) + ready(w, is_writable, true) + ready(e, is_failed, true);
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
