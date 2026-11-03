/* The IPX address provider (descriptor 0x5df710, "IPX"; 0x5605f0..0x560e50) and its games' advertising
   by Novell's SAP (the Service Advertising Protocol: socket 0x452, service type 0x8be). The socket is the
   datagram one UDP's is (udp.h, kind 1: SOCKADDR_IPX's 14 bytes, the library's address the network, the
   node and the socket).

   A hosted game is advertised by a thread of its own on the SAP socket: it answers each general query
   for the type (1, 0x8be or every type 0xffff) with a response (2) to the asker: the type, the game's
   name (0x30), its address (12 bytes) and the hops (1; 16 when it goes down); and once the first wait is
   over it broadcasts the response, then once a minute after a query. Looking for games sends a general
   query from the game's socket; the responses that come back to it name the games (the socket's +0x3c
   is the name looked for), and each one found is sent the name as a type 0x77 packet, which a UDP find
   broadcasts to everyone instead; the hosts answer it the same way. */
#include "real/ipx.h"
#include "exe.h"
#include "real/netgathr.h"
#include "real/systask.h"
#include "real/thread.h"
#include "real/udp.h"
#include "real/wsock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAP_SOCKET 0x452
#define SAP_TYPE 0x8be
#define IPX_PROTO 1000   /* NSPROTO_IPX */

static bool registered;   /* 0x6a5afc */

static uint32_t get_be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }   /* 0x56e040 */
static void put_be16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8), p[1] = (uint8_t)v; }   /* 0x56e070 */

/* The port's GetTickCount for the network library (net.c's): the timer's ticks as milliseconds. */
static int32_t tickcount(void) { return (int32_t)((uint64_t)TIMER_count * 1000 / TIMER_RATE); }

/* A SOCKADDR_IPX: the network and the node (a byte each, repeated), the socket. */
static void sap_addr(WsAddr *a, uint8_t net, uint8_t node, uint16_t socket)
{
    uint8_t *p = (uint8_t *)a;
    memset(a, 0, sizeof *a);
    a->family = WS_AF_IPX;
    memset(p + 2, net, 4);
    memset(p + 6, node, 6);
    uint16_t v = WS_htons(socket);
    memcpy(p + 12, &v, 2);
}

/* Broadcasts on, the packet type 0 (0x400f twice: at the socket level 0, at IPX's 1000). */
static void ipx_options(int sock)
{
    int32_t v = 1;
    WS_setsockopt(sock, 0xffff, 0x20, &v, 4);
    v = 0;
    WS_setsockopt(sock, 0xffff, 0x400f, &v, 4);
    WS_setsockopt(sock, IPX_PROTO, 0x400f, &v, 4);
}

/* ---------------------------------------------------------------- the advertising (SAP) */

static volatile int sap_running;   /* 0x6a5aa0 */
static volatile int sap_done;      /* 0x6a5a9c */
static int sap_sock = -1;          /* 0x6a5aa4 */
static uint8_t sap_resp[0x42];     /* 0x6a5aba: [2][type][name 0x30][address 12][hops] */
static ThreadRec sap_thread_rec;   /* 0x6a5a80 */

/* The SAP socket: an IPX datagram socket bound to 0x452, its address read back whole. 0x560b50 */
static int sap_socket(void)
{
    int s = WS_socket(WS_AF_IPX, WS_SOCK_DGRAM, IPX_PROTO);
    if (s == -1) return -1;
    ipx_options(s);
    WsAddr a;
    sap_addr(&a, 0, 0, SAP_SOCKET);
    if (WS_bind(s, &a, 0xe) == 0) {
        int len = 0xe;
        if (WS_getsockname(s, &a, &len) == 0 && len == 0xe) return s;
    }
    WS_closesocket(s);
    return -1;
}

static void sap_send(const WsAddr *to)   /* 0x560dc0 */
{
    CS_enter(g_net_sendcs);
    WS_sendto(sap_sock, sap_resp, 0x42, 0, to, 0xe);
    CS_leave(g_net_sendcs);
}

/* A general query (1) for the game's type or every type. 0x560e00 */
static bool sap_query_for_us(const uint8_t *b)
{
    uint32_t type = get_be16(b + 2);
    return get_be16(b) == 1 && (type == SAP_TYPE || type == 0xffff);
}

/* The advertising thread. Its wait is the deadline's tick count over a thousand in seconds (and the rest
   in microseconds): on Windows, with days of uptime, hours, so a broadcast only follows a query; the
   port's ticks start at the game's start. 0x560c20 */
static void sap_thread(void)
{
    bool broadcast = false;
    int32_t deadline = tickcount() + 4;
    WsAddr all;
    sap_addr(&all, 0, 0xff, SAP_SOCKET);
    sap_running = 1;
    do {
        WsFdSet r = {1, {sap_sock}};
        WsTimeval t = {deadline / 1000, deadline % 1000 * 1000};
        int n = WS_select(0, &r, NULL, NULL, &t);
        if (n != 0) {
            if (n == -1) {
                sap_running = 0;
                break;
            }
            uint8_t buf[0x200];
            WsAddr from;
            int fromlen = 0xe;
            CS_enter(g_net_sendcs);
            int got = WS_recvfrom(sap_sock, buf, 0x200, 0, &from, &fromlen);
            CS_leave(g_net_sendcs);
            if (got == 4 && sap_query_for_us(buf)) sap_send(&from);
        }
        if (!sap_running) break;
        if (deadline < tickcount()) {
            sap_send(&all);
            broadcast = true;
            deadline = tickcount() + 60000;
        }
    } while (sap_running);
    if (broadcast) {
        put_be16(sap_resp + 0x40, 0x10);   /* (down) */
        sap_send(&all);
    }
    sap_done = 1;
}

/* The game advertised under the name at the address: the response made, the thread started and waited
   for. 0x560a40 */
static int sap_start(void *owner, const NetAddr *addr, const char *name)
{
    if (!sap_running && (sap_sock = sap_socket()) != -1) {
        (void)owner;   /* (kept at 0x6a5aa8, never read) */
        put_be16(sap_resp, 2);
        put_be16(sap_resp + 2, SAP_TYPE);
        put_be16(sap_resp + 0x40, 1);
        NET_field((char *)sap_resp + 4, name, 0x30);
        sap_done = 0;
        memcpy(sap_resp + 0x34, addr->b, 12);
        CS_enter(g_net_sendcs);
        void *t = THREAD_create(sap_thread, 0, 1, -1, &sap_thread_rec);
        CS_leave(g_net_sendcs);
        if (t) {
            while (!sap_running) {
                if (sap_done) break;
                THREAD_sleep(1);
            }
        }
        if (sap_done) WS_closesocket(sap_sock);
    }
    return sap_running;
}

/* The advertising stopped: its socket closed (which ends the thread's wait) and the thread waited for.
   0x560e50 */
static int sap_stop(void)
{
    if (sap_running) {
        sap_running = 0;
        WS_closesocket(sap_sock);
        while (!sap_done) THREAD_sleep(1);
    }
    return 1;
}

/* A general query for the game's type to everyone's SAP socket, from the game's socket. 0x5608f0 */
static bool sap_find(int sock)
{
    WsAddr all;
    sap_addr(&all, 0xff, 0xff, SAP_SOCKET);
    uint8_t q[4];
    put_be16(q, 1);
    put_be16(q + 2, SAP_TYPE);
    CS_enter(g_net_sendcs);
    int r = WS_sendto(sock, q, 4, 0, &all, 0xe);
    CS_leave(g_net_sendcs);
    return r != -1;
}

bool IPX_sapfilter(void *p, const WsAddr *from, const uint8_t *b, int len)
{
    DgSocket *s = p;
    uint16_t sock;
    memcpy(&sock, (const uint8_t *)from + 12, 2);
    if (sock != WS_htons(SAP_SOCKET) || get_be16(b) != 2) return false;
    for (int n = (int)((unsigned)(len - 2) >> 6), i = 0; i < n; i++) {
        const uint8_t *e = b + 2 + 0x40 * i;
        const char *name = (const char *)e + 2;
        if (get_be16(e) == SAP_TYPE && strncmp(name, s->ipxname, 0x30) == 0) {
            NetAddr to;
            memset(&to, 0, sizeof to);
            memcpy(to.b, e + 0x32, 12);
            DGRAM_send(s, &to, 0x77, name, (int)strlen(name) + 1);   /* (0x5df760: the provider's send) */
        }
    }
    return true;
}

/* ---------------------------------------------------------------- the provider */

/* An IPX datagram socket bound to the port. 0x5605f0 */
static void *ipx_open(int port, void *owner, NetRecvFn recv)
{
    int sock = WS_socket(WS_AF_IPX, WS_SOCK_DGRAM, IPX_PROTO);
    if (sock == WS_INVALID_SOCKET) return NULL;
    ipx_options(sock);
    WsAddr a;
    sap_addr(&a, 0, 0, (uint16_t)port);
    DgSocket *s = DGRAM_new(owner, recv, sock, 1, 0xe, &a);
    if (!s) WS_closesocket(sock);
    return s;
}

static int ipx_port(void *s) { return ((DgSocket *)s)->port; }   /* 0x5606c0 */

static int ipx_advertise(void *p, const char *name)   /* 0x5606d0 */
{
    DgSocket *s = p;
    return sap_start(s, &s->local, name);
}

static int ipx_unadvertise(void *p, const char *name) { return sap_stop(); }   /* 0x5606f0 */

/* The games named so looked for. 0x560700 */
static int ipx_broadcasttext(void *p, const char *text)
{
    DgSocket *s = p;
    NET_field(s->ipxname, text, 0x30);
    return sap_find(s->sock);
}

static int ipx_endbroadcast(void *p)   /* 0x560730 */
{
    ((DgSocket *)p)->ipxname[0] = 0;
    return 1;
}

/* An address from text: ten numbers (C's: 0x.. hexadecimal, 0.. octal) the network and the node, each
   up to 255, any one character apart; the port given after them. Fewer: none. 0x560740 */
static int ipx_parse(void *p, const char *text, int port, NetAddr *out)
{
    memset(out, 0, sizeof *out);
    const char *c = text;
    for (int i = 0; i < 10; i++) {
        if (!*c) return 0;
        char *end;
        long v = strtol(c, &end, 0);
        if (c != end) {
            if (v < 0 || 0xff < v) return 0;
            out->b[i] = (uint8_t)v;
        }
        c = end + 1;
    }
    put_be16(out->b + 10, (uint32_t)port);
    return 1;
}

/* The network and the node as "0x.. " each (hexadecimal, lower case), and the port. 0x5607c0 */
static int ipx_totext(void *s, const NetAddr *a, char *text, uint32_t *port)
{
    text[0] = 0;
    for (int i = 0; i < 10; i++) {
        size_t l = strlen(text);
        snprintf(text + l, 8, "0x%x ", a->b[i]);
    }
    *port = get_be16(a->b + 10);
    return 1;
}

/* The node and the socket the same (the network isn't compared). 0x560820 */
static bool ipx_same(void *s, const NetAddr *x, const NetAddr *y) { return !memcmp(x->b + 4, y->b + 4, 8); }

static NetProvider ipx_provider = {
    NULL, {0}, DGRAM_startup, DGRAM_cleanup, ipx_open, DGRAM_close, ipx_port, ipx_advertise, ipx_unadvertise,
    ipx_broadcasttext, ipx_endbroadcast, ipx_parse, ipx_totext, ipx_same, DGRAM_localaddr, DGRAM_sendempty,
    DGRAM_received, DGRAM_send, DGRAM_recv, DGRAM_discard, DGRAM_setidle,
};

/* Registered once if an IPX socket can be made (with the datagram transport), named as given. 0x560840 */
const uint8_t *IPX_register(const char *name)
{
    if (!g_net_sendcs) g_net_sendcs = CS_create();
    if (!registered) {
        uint8_t data[400];
        if (WS_startup(0x101, data) == 0) {
            void *s = ipx_provider.open(0, NULL, NULL);
            if (s) {
                registered = true;
                ipx_provider.close(s);
                ipx_provider.name = exe_str(0x5d07c4);   /* "IPX" */
                memcpy(ipx_provider.guid, exe_data(0x5df714, 16), 16);
                NET_addprovider(&ipx_provider, &NETGATHR_transport);
                if (name) ipx_provider.name = name;
            }
            WS_cleanup();
        }
    }
    return registered ? ipx_provider.guid : NULL;
}
