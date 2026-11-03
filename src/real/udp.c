/* The UDP address provider and the WinSock datagram socket: see udp.h. */
#include "real/udp.h"
#include "exe.h"
#include "platform.h"
#include "real/wsock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char g_net_hostaddr[32];
static CritSec *send_cs;   /* 0x6a5720: around each sendto */
static bool registered;    /* 0x6a5a78 */

/* ---------------------------------------------------------------- the datagram socket */

/* A WinSock address as the library's (its port returned). 0x586f80 */
static int from_sockaddr(const DgSocket *s, const WsAddr *a, NetAddr *out)
{
    const uint8_t *p = (const uint8_t *)a;
    if (s->kind == 0) {
        memcpy(out->b, a, (size_t)s->addrlen);
        return WS_htons(a->port);
    }
    if (s->kind == 1) {   /* SOCKADDR_IPX: the network, the node, the socket */
        memcpy(out->b, p + 2, 4);
        memcpy(out->b + 4, p + 6, 6);
        memcpy(out->b + 10, p + 12, 2);
        uint16_t port;
        memcpy(&port, p + 12, 2);
        return WS_htons(port);
    }
    return 0;
}

/* The other way. 0x587000 */
static void to_sockaddr(const DgSocket *s, const NetAddr *a, WsAddr *out)
{
    uint8_t *p = (uint8_t *)out;
    if (s->kind == 0) {
        memcpy(out, a->b, (size_t)s->addrlen);
        out->family = WS_AF_INET;
    } else if (s->kind == 1) {
        memcpy(p + 2, a->b, 4);
        memcpy(p + 6, a->b + 4, 6);
        memcpy(p + 12, a->b + 10, 2);
        out->family = WS_AF_IPX;
    }
}

/* Everyone's address at the port. 0x587070 */
static void broadcast_sockaddr(const DgSocket *s, int port, WsAddr *out)
{
    uint8_t *p = (uint8_t *)out;
    if (s->kind == 0) {
        out->family = WS_AF_INET;
        out->port = WS_htons((uint16_t)port);
        memset(out->addr, 0xff, 4);
        memset(out->zero, 0, 8);
    } else if (s->kind == 1) {
        out->family = WS_AF_IPX;
        uint16_t v = WS_htons((uint16_t)port);
        memcpy(p + 12, &v, 2);
        memset(p + 2, 0, 4);
        memset(p + 6, 0xff, 6);
    }
}

/* A datagram into the packet if one comes within 25 ms: true if it is one to keep (its type byte not 0,
   from an address of the socket's size). 0x587180 */
static bool receive(DgSocket *s, DgPacket *pk)
{
    int sock = s->sock;
    WsFdSet set = {1, {sock}};
    WsTimeval t = {0, 25000};
    if (WS_select(sock + 1, &set, NULL, NULL, &t) == WS_SOCKET_ERROR || !WS_fdisset(sock, &set)) return false;
    WsAddr from;
    int fromlen = (int)sizeof from;
    int n = WS_recvfrom(sock, pk->buf, DGRAM_DATA, 0, &from, &fromlen);
    pk->len = n;
    if (n <= 0) return false;
    /* (An IPX socket with a name passes its datagrams to 0x560990 first; there is no IPX here.) */
    if (*pk->type && fromlen == s->addrlen) {
        from_sockaddr(s, &from, &pk->addr);
        return true;
    }
    *pk->type = 0;
    pk->len = 0;
    return false;
}

/* The receive thread: a free packet (else the oldest received one) filled and queued, the owner told. 0x5870f0 */
static void recv_thread(void *arg)
{
    DgSocket *s = arg;
    s->recvalive = true;
    while (s->running) {
        DgPacket *pk = NetList_Pop(&s->freerecv);
        if (!pk) pk = NetList_Pull(&s->received);
        if (receive(s, pk)) {
            NetList_Push(&s->received, pk);
            if (s->recv) s->recv(s->owner, &pk->addr);
        } else {
            NetList_Push(&s->freerecv, pk);
        }
    }
    s->recvalive = false;
}

/* The send thread: the queue's packets out in order, waiting on the event while there are none (the
   idle callback first). 0x587290 */
static void send_thread(void *arg)
{
    DgSocket *s = arg;
    s->wake = EVENT_create();
    s->sendalive = true;
    while (s->running) {
        if (s->idle && NetList_Count(&s->sendq) == 0) s->idle(s->owner);
        if (NetList_Count(&s->sendq) == 0) EVENT_wait(s->wake);
        for (DgPacket *pk; (pk = NetList_Pull(&s->sendq));) {
            WsAddr to;
            if (!pk->broadcast) to_sockaddr(s, &pk->addr, &to);
            else broadcast_sockaddr(s, s->port, &to);
            CS_enter(send_cs);
            WS_sendto(s->sock, pk->buf, pk->len, 0, &to, (int)sizeof to);
            CS_leave(send_cs);
            NetList_Push(&s->freesend, pk);
        }
    }
    s->sendalive = false;
    EVENT_close(s->wake);
}

DgSocket *DGRAM_new(void *owner, NetRecvFn recv, int sock, int kind, int addrlen, const void *bindaddr)
{
    DgSocket *s = calloc(1, sizeof *s);   /* MEM ("socket", 0x1520 bytes) */
    if (!s) return NULL;
    NetList_Init(&s->freerecv, NULL, NULL);
    NetList_Init(&s->freesend, NULL, NULL);
    NetList_Init(&s->received, NULL, NULL);
    NetList_Init(&s->sendq, NULL, NULL);
    uint32_t blocking = 0;
    WS_ioctlsocket(sock, 0x8004667e /* FIONBIO */, &blocking);
    s->owner = owner;
    s->sock = sock;
    s->addrlen = addrlen;
    s->recv = recv;
    s->kind = kind;
    WsAddr local;
    int len = addrlen;
    if (WS_bind(sock, bindaddr, addrlen) == 0) {
        memcpy(&local, bindaddr, sizeof local);
        if (WS_getsockname(sock, &local, &len) == 0 && len == addrlen) {
            s->addrlen = len;
            s->port = (int16_t)from_sockaddr(s, &local, &s->local);
            for (int i = 0; i < 8; i++) {
                s->rpk[i].type = s->rpk[i].buf;
                s->rpk[i].data = s->rpk[i].buf + 1;
                NetList_Push(&s->freerecv, &s->rpk[i]);
            }
            for (int i = 0; i < 8; i++) {
                s->spk[i].type = s->spk[i].buf;
                s->spk[i].data = s->spk[i].buf + 1;
                NetList_Push(&s->freesend, &s->spk[i]);
            }
            if (owner) {
                ThreadRec r1, r2;
                s->running = true;
                THREAD_createarg(recv_thread, s, 0, 2, -1, &r1);   /* 0x55f3a0 */
                THREAD_createarg(send_thread, s, 0, 2, -1, &r2);
                while (!s->recvalive || !s->sendalive) THREAD_sleep(1);
            }
            return s;
        }
    }
    NetList_Free(&s->freerecv);
    NetList_Free(&s->received);   /* (the original frees these two only) */
    free(s);
    return NULL;
}

int DGRAM_close(void *p)
{
    DgSocket *s = p;
    s->running = false;
    if (s->wake) EVENT_set(s->wake);
    while (s->recvalive || s->sendalive) THREAD_sleep(1);
    if (WS_closesocket(s->sock) < 0) plat_log("Yag! bad socket?");
    NetList_Free(&s->freerecv);
    NetList_Free(&s->freesend);
    NetList_Free(&s->received);
    NetList_Free(&s->sendq);
    free(s);
    return 1;
}

/* WinSock 1.1 up: its largest datagram (WSADATA's iMaxUdpDg), 0x2000 if it gives none. */
int DGRAM_startup(void)
{
    uint8_t data[400] = {0};
    if (WS_startup(0x101, data) != 0) return 0;
    uint16_t max;
    memcpy(&max, data + 392, 2);
    return max ? max : 0x2000;
}

bool DGRAM_cleanup(void) { return WS_cleanup() == 0; }

int DGRAM_localaddr(void *p, NetAddr *out)
{
    *out = ((DgSocket *)p)->local;
    return 1;
}

bool DGRAM_sendempty(void *p) { return NetList_Count(&((DgSocket *)p)->sendq) == 0; }

int DGRAM_received(void *p) { return NetList_Count(&((DgSocket *)p)->received); }

/* Into a free send packet (none: 0) and queued; a NULL address broadcasts. Its length back. */
int DGRAM_send(void *p, const NetAddr *to, uint8_t type, const void *data, int len)
{
    DgSocket *s = p;
    DgPacket *pk = NetList_Pop(&s->freesend);
    if (!pk) return 0;
    *pk->type = type;
    pk->len = len + 1;
    memcpy(pk->data, data, (size_t)len);
    pk->broadcast = to == NULL;
    if (to) pk->addr = *to;
    NetList_Push(&s->sendq, pk);
    EVENT_set(s->wake);
    return len;
}

typedef struct Match {
    const NetAddr *addr;
    DgSocket *s;
    void *owner;
    uint8_t type;
} Match;

/* 0x587840: a packet of another type, or of the type from the address. */
static bool match_recv(void *node, void *arg)
{
    DgPacket *pk = node;
    Match *m = arg;
    if (m->type != *pk->type) return true;
    return NETCONN_same(m->owner, m->addr, &pk->addr);
}

/* 0x5878f0: of the type, from the address if there is one. */
static bool match_discard(void *node, void *arg)
{
    DgPacket *pk = node;
    Match *m = arg;
    if (!m->addr) return m->type == *pk->type;
    return m->type == *pk->type && NETCONN_same(m->owner, m->addr, &pk->addr);
}

/* The first received packet that matches (match_recv, by the type and address passed in): its sender,
   its type and up to max bytes of its data out, its length back; none: type 0 and 0. */
int DGRAM_recv(void *p, NetAddr *from, uint8_t *type, void *data, int max)
{
    DgSocket *s = p;
    Match m = {from, s, s->owner, *type};
    DgPacket *pk = NetList_Take(&s->received, match_recv, &m);
    if (!pk) {
        *type = 0;
        return 0;
    }
    int n = pk->len - 1;
    if (n > max) n = max;
    *from = pk->addr;
    *type = *pk->type;
    if (n) memcpy(data, pk->data, (size_t)n);
    NetList_Push(&s->freerecv, pk);
    return n;
}

int DGRAM_discard(void *p, const NetAddr *from, uint8_t type)
{
    DgSocket *s = p;
    Match m = {from, s, s->owner, type};
    for (DgPacket *pk; (pk = NetList_Take(&s->received, match_discard, &m));) NetList_Push(&s->freerecv, pk);
    return 1;
}

int DGRAM_setidle(void *p, void (*idle)(void *owner))
{
    ((DgSocket *)p)->idle = idle;
    return 1;
}

/* ---------------------------------------------------------------- the UDP provider */

/* Startup, and the host's address noted. 0x5602e0 */
static int udp_startup(void)
{
    int max = DGRAM_startup();
    if (max) NET_hostaddress(g_net_hostaddr);
    return max;
}

/* A broadcasting datagram socket bound to the port on any address. 0x560300 */
static void *udp_open(int port, void *owner, NetRecvFn recv)
{
    int sock = WS_socket(WS_AF_INET, WS_SOCK_DGRAM, 17 /* IPPROTO_UDP */);
    if (sock == WS_INVALID_SOCKET) return NULL;
    int on = 1;
    WS_setsockopt(sock, 0xffff /* SOL_SOCKET */, 0x20 /* SO_BROADCAST */, &on, 4);
    WsAddr a;
    memset(&a, 0, sizeof a);
    a.family = WS_AF_INET;
    a.port = WS_htons((uint16_t)port);
    uint32_t any = WS_htonl(0);
    memcpy(a.addr, &any, 4);
    DgSocket *s = DGRAM_new(owner, recv, sock, 0, (int)sizeof a, &a);
    if (!s) WS_closesocket(sock);
    return s;
}

static int udp_port(void *s) { return ((DgSocket *)s)->port; }   /* 0x5603a0 */
static int udp_one(void *s) { return 1; }                        /* 0x5603b0, 0x5603c0, 0x560400 */

/* The text to everyone as a type 0x77 packet, its 0 included. 0x5603d0 */
static void udp_broadcasttext(void *s, const char *text)
{
    UDP_provider.send(s, NULL, 0x77, text, (int)strlen(text) + 1);
}

/* An address from text: dotted, else a host name; the port given, else the socket's. Not an empty text
   or one ending in a dot. 0x560410 */
static int udp_parse(void *p, const char *text, int port, NetAddr *out)
{
    DgSocket *s = p;
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
    a->port = WS_htons((uint16_t)(port ? port : s->port));
    return 1;
}

/* The address as dotted text, and its port. 0x5604e0 */
static int udp_totext(void *s, const NetAddr *p, char *text, uint32_t *port)
{
    const WsAddr *a = (const WsAddr *)p->b;
    strcpy(text, WS_inet_ntoa(a->addr));
    if (port) *port = WS_htons(a->port);
    return 1;
}

/* The same host, and the same port unless either is 0. 0x560540 */
static bool udp_same(void *s, const NetAddr *x, const NetAddr *y)
{
    const WsAddr *a = (const WsAddr *)x->b, *b = (const WsAddr *)y->b;
    if (a->port && b->port && a->port != b->port) return false;
    return !memcmp(a->addr, b->addr, 4);
}

NetProvider UDP_provider = {
    "UDP", {0}, udp_startup, DGRAM_cleanup, udp_open, DGRAM_close, udp_port, udp_one, udp_one,
    udp_broadcasttext, udp_one, udp_parse, udp_totext, udp_same, DGRAM_localaddr, DGRAM_sendempty,
    DGRAM_received, DGRAM_send, DGRAM_recv, DGRAM_discard, DGRAM_setidle,
};

/* The host's address: a dial-up connection's first (RAS, which the port hasn't), else the host name's
   (with Internet Explorer's autodial off meanwhile: the port has no registry). */
bool NET_hostaddress(char *out)
{
    *out = 0;
    char name[256];
    bool found = false;
    if (WS_gethostname(name, sizeof name) == 0) {
        WsHost *h = WS_gethostbyname(name);
        if (h && h->addrtype == WS_AF_INET && h->length == 4) {
            strcpy(out, WS_inet_ntoa((const uint8_t *)h->addr_list[0]));
            found = true;
        }
    }
    return found;
}

/* WinSock up, a host address that is not a loopback or 0.*, and a socket to be had. */
bool UDP_probe(void)
{
    bool ok = false;
    uint8_t data[400];
    if (WS_startup(0x101, data) != 0) return false;
    if (NET_hostaddress(g_net_hostaddr) && strncmp(g_net_hostaddr, "127.", 4) && strncmp(g_net_hostaddr, "0.", 2)) {
        int sock = WS_socket(WS_AF_INET, WS_SOCK_STREAM, 0);
        if (sock != WS_INVALID_SOCKET) {
            ok = true;
            WS_closesocket(sock);
        }
    }
    WS_cleanup();
    return ok;
}

const uint8_t *UDP_register(const char *name)
{
    if (!send_cs) send_cs = CS_create();
    if (!registered && UDP_probe()) {
        registered = true;
        memcpy(UDP_provider.guid, exe_data(0x5df6b4, 16), 16);
        NET_addprovider(&UDP_provider, NULL /* the datagram transport 0x5e1cf0: to come */);
        if (name) UDP_provider.name = name;
    }
    return registered ? UDP_provider.guid : NULL;
}

/* ---------------------------------------------------------------- self-test */

static int got;
static void on_recv(void *owner, const NetAddr *from) { got++; }

/* Two instances in a room: each opens port 0x5000, broadcasts its host address and reads the other's
   (on a thread of its own, as the game's network code runs). */
static void selftest_thread(void *arg)
{
    char m[160];
    const uint8_t *g = UDP_register(NULL);
    snprintf(m, sizeof m, "udptest: registered %d host %s", g != NULL, g_net_hostaddr);
    plat_log(m);
    if (!g) return;
    int max = UDP_provider.startup();
    static NetConn conn;
    conn.provider = &UDP_provider;
    void *s = UDP_provider.open(0x5000, &conn, on_recv);
    conn.socket = s;
    snprintf(m, sizeof m, "udptest: max %d socket %p port %d host %s", max, s, s ? UDP_provider.port(s) : -1, g_net_hostaddr);
    plat_log(m);
    if (!s) return;
    for (int round = 0; round < 40; round++) {
        UDP_provider.broadcasttext(s, g_net_hostaddr);
        THREAD_sleep(100);
        NetAddr from;
        uint8_t type = 0, buf[64];   /* (0: any packet, by match_recv) */
        int n;
        while ((n = UDP_provider.recv(s, &from, &type, buf, sizeof buf - 1)) > 0 || type) {
            buf[n] = 0;
            char t[32];
            uint32_t port;
            UDP_provider.totext(s, &from, t, &port);
            snprintf(m, sizeof m, "udptest: from %s:%u type %02x \"%s\" (callbacks %d)", t, port, type, buf, got);
            plat_log(m);
            type = 0;
        }
    }
    UDP_provider.close(s);
    UDP_provider.cleanup();
    plat_log("udptest: done");
}

void UDP_selftest(void)
{
    static ThreadRec rec;
    static bool started_test;
    if (!started_test) {
        started_test = true;
        if (!THREAD_createarg(selftest_thread, NULL, 0x4000, 0, 0, &rec)) plat_log("udptest: no threads");
    }
}
