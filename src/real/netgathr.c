/* The datagram transport, game gathering: see netgathr.h. */
#include "real/netgathr.h"
#include "real/llpacket.h"
#include "exe.h"
#include "pending.h"
#include "platform.h"
#include "real/real.h"
#include "real/systask.h"
#include "real/thread.h"
#include "real/udp.h"
#include "real/wsock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int nopen;            /* 0x6ad688: connections open on the transport */
static uint32_t deadline;    /* 0x6ad684 */
static uint32_t next_id;     /* 0x5e1ce8 (from 1) */
static bool ids_ready;

/* The ticks a player or the host stays without a word (0x5e1a04) and between the host's pings (0x5e1a08). */
static int32_t alive_ticks(void) { return (int32_t)exe_u32(0x5e1a04); }
static int32_t ping_ticks(void) { return (int32_t)exe_u32(0x5e1a08); }

static uint32_t new_id(void)
{
    if (!ids_ready) {
        next_id = exe_u32(0x5e1ce8);
        ids_ready = true;
    }
    return next_id++;
}

/* Big-endian numbers of 1 to 4 bytes. 0x56e040, 0x56e070 */
static uint32_t get_be(const uint8_t *p, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v = v << 8 | (i < n ? p[i] : 0);
    return n >= 4 ? v : v >> (32 - 8 * n);
}

static void put_be(uint8_t *p, uint32_t v, int n)
{
    for (int i = 0; i < n; i++) p[i] = (uint8_t)(v >> 8 * (n - 1 - i));
}

static void set_deadline(int ticks) { deadline = TIMER_count + (uint32_t)ticks; }   /* 0x587da0 */
static bool past_deadline(void) { return (int32_t)(TIMER_count - deadline) >= 0; }  /* 0x587dc0 */

static int popcount(uint32_t m)   /* 0x589030 */
{
    int n = 0;
    for (; m; m &= m - 1) n++;
    return n;
}

static int send(NetConn *c, const NetAddr *to, uint8_t type, const void *data, int len)
{
    return c->provider->send(c->socket, to, type, data, len);
}

/* A wait between two sends: on the main thread the synchronous tasks run (a negative result gives up). */
static bool wait_ticks(int ticks)
{
    if (!THREAD_isthread(NULL)) {
        THREAD_sleepticks(ticks);
        return true;
    }
    return SYNCTASK_wait(ticks) >= 0;
}

/* "EAGAME:" and the game's GUID as text. 0x587d40 */
static void advert_name(const uint8_t *g, char *out)
{
    uint32_t d1;
    uint16_t d2, d3;
    memcpy(&d1, g, 4);
    memcpy(&d2, g + 4, 2);
    memcpy(&d3, g + 6, 2);
    sprintf(out, "EAGAME:%08x.%04x%04x.%02x%02x%02x%02x.%02x%02x%02x%02x", d1, d2, d3, g[8], g[9], g[10], g[11],
            g[12], g[13], g[14], g[15]);
}

/* The request sent to the joined host until it is answered or the time is up: its id in the packet's first
   4 bytes, a send every resend interval (each tick while the sends fail). 0x587ee0 */
static void request(NetConn *c, NetReq *r, uint8_t type, uint8_t *buf, int len, int ticks)
{
    r->id = new_id();
    r->conn = c;
    r->result = 0;
    put_be(buf, r->id, 4);
    c->req = r;
    set_deadline(ticks);
    int wait = 1;
    do {
        if (past_deadline()) break;
        if (send(c, &c->host, type, buf, len)) wait = g_net_resend;
        if (!wait_ticks(wait)) break;
    } while (r->result == 0);
    c->req = NULL;
}

/* ---------------------------------------------------------------- the host's slots */

/* The joined player at the address, its time-out started again. 0x588640 */
static NetSlot *find_slot(NetServer *sv, const NetAddr *who)
{
    for (int i = 0; i < sv->info.maxplayers - 1; i++)
        if (sv->mask & 1u << i && NETCONN_same(sv->conn, who, &sv->slot[i].info.addr)) {
            sv->slot[i].alive = alive_ticks();
            return &sv->slot[i];
        }
    return NULL;
}

/* The first free slot for the player (0 when there is none, as the first). 0x5891f0 */
static int add_slot(NetServer *sv, const NetPlayerInfo *p)
{
    for (int i = 0; i < sv->info.maxplayers - 1; i++)
        if (!(sv->mask & 1u << i)) {
            sv->mask |= 1u << i;
            sv->slot[i].info = *p;
            sv->slot[i].alive = alive_ticks();
            sv->slot[i].index = i;
            return i;
        }
    return 0;
}

/* The game's description to the address ('d'), with the request's id (0 an advert: not while hidden).
   0x589050 */
static void describe(NetServer *sv, const NetAddr *to, uint32_t id)
{
    NetConn *c = sv->conn;
    if (id == 0 && sv->hidden) return;
    uint8_t b[0xac] = {0};
    put_be(b, id, 4);
    memcpy(b + 4, &c->host, 20);
    uint32_t d1;
    uint16_t d2, d3;
    memcpy(&d1, c->guid, 4);
    memcpy(&d2, c->guid + 4, 2);
    memcpy(&d3, c->guid + 6, 2);
    put_be(b + 0x18, d1, 4);
    put_be(b + 0x1c, d2, 2);
    put_be(b + 0x1e, d3, 2);
    memcpy(b + 0x20, c->guid + 8, 8);
    strncpy((char *)b + 0x28, sv->info.name, 0x20);
    for (int i = 0; i < 4; i++) put_be(b + 0x48 + i, sv->info.b[i], 1);
    put_be(b + 0x4c, (uint32_t)sv->info.s[0], 2);
    put_be(b + 0x4e, (uint32_t)sv->info.s[1], 2);
    put_be(b + 0x50, (uint32_t)sv->info.i[0], 4);
    put_be(b + 0x54, (uint32_t)sv->info.i[1], 4);
    put_be(b + 0x58, (uint32_t)sv->info.maxplayers, 4);
    put_be(b + 0x6c, sv->mask, 4);
    send(c, to, 'd', b, 0xac);   /* (the bytes 0x5c..0x6b and from 0x70 are the original's stack) */
}

/* ---------------------------------------------------------------- the class */

/* The transport up on the provider (its startup with the first connection, then the timer's tick), the
   socket opened, the player's address, the game's GUID and the two texts noted. 0x5879a0 */
static void tick(void);

static int gathr_open(NetConn *c, const uint8_t *game, const uint8_t *provider, int port, const char *name, const char *extra)
{
    if (nopen++ == 0) {
        if (!c->provider->startup()) return 0;   /* (the count stays up, as the original's) */
        CS_enter(g_net_sendcs);
        TIMER_addsecondary(tick);
        CS_leave(g_net_sendcs);
    }
    c->socket = NULL;
    c->socket = c->provider->open(port, c, NET_onreceive);
    if (!c->socket) return 0;
    c->provider->localaddr(c->socket, &c->self.addr);
    memcpy(c->guid, game, 16);
    c->opened = 1;
    strncpy(c->self.name, name, 0x33);
    strncpy(c->self.extra, extra, 0x13);
    return 0xfa;
}

/* Left, the socket closed, and with the last connection the tick and the provider down. 0x587b90 */
static int gathr_close(NetConn *c)
{
    int r = 1;
    void *s = c->socket;
    if (!s) return r;
    if (c->server || c->keepalive) r = NET_leave(c);
    c->opened = 0;
    if (c->packets) {
        /* (A packet connection still on it: the original waits here for ever, running the tasks on the
           main thread.) */
        for (;;) {
            while (THREAD_isthread(NULL)) SYNCTASK_run(0);
            THREAD_sleep(0);
        }
    }
    const NetProvider *p = c->provider;
    p->close(s);
    nopen--;
    c->socket = NULL;
    if (nopen == 0) {
        CS_enter(g_net_sendcs);
        TIMER_removesecondary(tick);
        CS_leave(g_net_sendcs);
        return p->cleanup();
    }
    return r;
}

/* The games answering for ticks: the advert text broadcast once, each answer to fn until it returns 0.
   0x587c50 */
static int gathr_find(NetConn *c, NetGameFn fn, int ticks, int user)
{
    NetReq r;
    memset(&r, 0, sizeof r);
    char text[48];
    advert_name(c->guid, text);
    r.games = fn;
    r.conn = c;
    r.user = user;
    r.id = 0;
    c->req = &r;
    set_deadline(ticks);
    int sent = 0;
    for (;;) {
        if (!sent) sent = c->provider->broadcasttext(c->socket, text);
        if (!wait_ticks(1)) break;
        if (past_deadline() || !r.games) break;
    }
    if (sent) c->provider->endbroadcast(c->socket);
    c->req = NULL;
    return sent;
}

/* A join asked of the host ('j': the address it was found at, the password, the player): its answer (1
   in, -1 the password, -2 full, -3 refused, 0 none in 2 seconds). Already in its game: 1. 0x587de0 */
static int gathr_join(NetConn *c, const NetAddr *host, const char *password)
{
    NetReq r;
    memset(&r, 0, sizeof r);
    if (c->joined) {
        if (NETCONN_same(c, &c->host, host)) return 1;
        NET_leave(c);
        c->joined = 0;
    }
    uint8_t b[0x88] = {0};
    memcpy(b + 0x28, &c->self, 0x5c);
    memcpy(b + 4, host, 20);
    c->host = *host;
    if (password) strncpy((char *)b + 0x18, password, 0x10);
    else if (c->server) c->server->password[0] = 0;   /* (the original writes it whether there is one or not) */
    request(c, &r, 'j', b, 0x88, TIMER_RATE * 2);
    return r.result;
}

/* A game's description ('r'): of the joined game's host (host NULL) or of another while not joined. 0x587f90 */
static bool gathr_query(NetConn *c, const NetAddr *host, NetGameInfo *out)
{
    NetReq r;
    memset(&r, 0, sizeof r);
    if (!host) {
        if (!c->joined) return false;
    } else {
        if (c->joined) return false;
        c->host = *host;
    }
    uint8_t b[0xac] = {0};
    request(c, &r, 'r', b, 0xac, TIMER_RATE * 2);
    *out = r.info;
    return r.result != 0;
}

/* A game hosted: its description, password and join callback; advertised at once. 0x588050 */
static int gathr_host(NetConn *c, NetGameInfo *info, const char *password, NetPlayerFn joinfn)
{
    if (info->maxplayers == 0) info->maxplayers = 0x20;
    if (c->joined) {
        NET_leave(c);
        c->joined = 0;
    }
    NetServer *sv = calloc(1, sizeof *sv + (size_t)info->maxplayers * sizeof(NetSlot));   /* "NETSRVR" */
    c->server = sv;
    if (!sv) return 0;
    sv->conn = c;
    sv->info = *info;
    sv->hidden = 0;
    sv->mask = 0;
    sv->joinfn = joinfn;
    c->keepalive = ping_ticks();
    c->host = c->self.addr;
    c->joined = alive_ticks();
    if (password) strncpy(sv->password, password, 0x10);
    else sv->password[0] = 0;
    advert_name(c->guid, sv->advert);
    if (c->provider->advertise(c->socket, sv->advert)) return 1;
    free(sv);
    c->server = NULL;
    return 0;
}

static int gathr_setinfo(NetConn *c, const NetGameInfo *info)   /* 0x588160 */
{
    if (!c->server) return 0;
    c->server->info = *info;
    return 1;
}

/* The game no longer advertised (and the host's record freed). 0x588190 */
static int gathr_hide(NetConn *c, int free_it)
{
    NetServer *sv = c->server;
    if (!sv) return 0;
    if (!sv->hidden) {
        c->provider->unadvertise(c->socket, sv->advert);
        sv->advert[0] = 0;
        sv->hidden = 1;
    }
    if (free_it) {
        free(sv);
        c->server = NULL;
    }
    return 1;
}

/* Advertised again, the players' time-outs started anew. 0x5881f0 */
static int gathr_show(NetConn *c)
{
    NetServer *sv = c->server;
    if (!sv || !sv->hidden) return 0;
    sv->hidden = 0;
    advert_name(c->guid, sv->advert);
    if (c->provider->advertise(c->socket, sv->advert)) {
        c->keepalive = 1;
        for (int i = 0; i < sv->info.maxplayers - 1; i++) sv->slot[i].alive = alive_ticks();
        return 1;
    }
    sv->hidden = 0;
    return 0;
}

/* The game left ('b' to its host; a host's to each of its players, its record freed). 0x588280 */
static int gathr_leave(NetConn *c)
{
    if (!c->joined) return 1;
    uint8_t b[0x18];
    uint32_t id = new_id();
    memcpy(b, &id, 4);   /* (as the machine's word, not big-endian as the requests' are) */
    memcpy(b + 4, &c->host, 20);
    NetServer *sv = c->server;
    if (!sv) {
        send(c, &c->host, 'b', b, 0x18);
    } else {
        if (!sv->hidden) {
            c->provider->unadvertise(c->socket, sv->advert);
            sv->advert[0] = 0;
            for (int i = 0; sv->mask && i < sv->info.maxplayers - 1; i++)
                if (sv->mask & 1u << i) {
                    sv->mask &= ~(1u << i);
                    send(c, &sv->slot[i].info.addr, 'b', b, 0x18);
                }
        }
        free(sv);
        c->server = NULL;
    }
    c->keepalive = 0;
    c->joined = 0;
    return 1;
}

/* The players of a game, each to fn (the host as 0) until it returns 0: the host's own from its record,
   another's asked for ('r' for the mask, then 'g' for the host and each player). Their count. 0x588380 */
static unsigned gathr_players(NetConn *c, const NetAddr *host, NetPlayerFn fn)
{
    unsigned n = 0;
    if (!host) {
        if (!c->joined) return 0;
        host = &c->host;
    } else {
        if (c->joined) return 0;
        c->host = *host;
    }
    if (c->server && NETCONN_same(c, &c->host, host)) {
        if (fn(c, &c->self, 0, 1)) {
            NetServer *sv = c->server;
            n = 1;
            for (int i = 0; i < sv->info.maxplayers - 1; i++) {
                if (sv->mask & 1u << i) {
                    if (!fn(c, &sv->slot[i].info, i + 1, 0)) return n;
                    n++;
                }
                sv = c->server;
            }
        }
        return n;
    }
    NetReq r;
    memset(&r, 0, sizeof r);
    uint8_t b[0xac] = {0};
    request(c, &r, 'r', b, 0xac, TIMER_RATE * 2);
    uint32_t mask = r.players;
    if (r.result) {
        r.playerfn = fn;
        r.result = 1;
        put_be(b + 0x18, 0xffffffffu, 4);
        memcpy(b + 4, &c->host, 20);
        request(c, &r, 'g', b, 0x1c, TIMER_RATE * 2);
        n = r.result != 0;
        if (mask)
            for (int i = 0; i < 0x1f && r.playerfn && n; i++)
                if (mask & 1u << i) {
                    put_be(b + 0x18, (uint32_t)i, 4);
                    request(c, &r, 'g', b, 0x1c, TIMER_RATE * 2);
                    n = r.result != 0;
                    /* (the original then clears bit i of the counter, not of the mask: no change) */
                }
    }
    return n;
}

/* A player dropped by the host ('K'). 0x5885b0 */
static int gathr_kill(NetConn *c, const NetAddr *who)
{
    NetServer *sv = c->server;
    if (!sv) {
        REAL_abort("\\real\\patch3\\cmn\\netgathr.c", 0x3ba, "NET_killplayer - ONLY THE GAME SERVER MAY CALL killnetplayer.\n");
        return 0;
    }
    NetSlot *s = find_slot(sv, who);
    if (s) {
        send(c, &s->info.addr, 'K', NULL, 0);
        sv->mask &= ~(1u << (int)(s - sv->slot));
    }
    return 0;
}

/* A packet connection on the transport: its timing defaulted (a 30th and two 15ths of a second), the
   protocol by the flags (none: 0x5e1c70; else 0x5e1c50 with 0x4000, 0x5e2e58 without). 0x5886c0 */
static int gathr_packets(NetConn *c, void *p, int maxdata, uint32_t flags)
{
    PacketConn *pc = p;
    const PacketProto *proto = &LLPACKET_proto;
    if ((flags & 0xff) == 0 || flags & 0x4000) {
        PENDING_NOTE(0x5e1c50, "the PACKET and UNRELY protocols (0x5e1c50, 0x5e1c70)");
        return 0;
    }
    if (pc) {
        if (!pc->timing[1]) pc->timing[1] = (int16_t)(TIMER_RATE / 30);
        if (!pc->timing[2]) pc->timing[2] = (int16_t)(TIMER_RATE / 15);
        if (!pc->timing[3]) pc->timing[3] = (int16_t)(TIMER_RATE / 15);
        pc->timing[0] = 0;
    }
    return PACKET_open(pc, proto, &GATHR_link, maxdata, flags);
}

static int gathr_zero(NetConn *c) { return 0; }   /* 0x588770 */
static int gathr_one(NetConn *c) { return 1; }    /* 0x589260 */

/* A message to everyone ('<'). 0x589270 */
static void gathr_message(NetConn *c, const void *data, int len)
{
    send(c, NULL, '<', data, len);
}

/* One packet: by its type. 0x588830 */
static void dispatch(NetReq *r, const NetAddr *from, uint8_t type, const uint8_t *data, int len)
{
    NetConn *c = r->conn;
    switch (type) {
    case '*':
        if (c->server) {
            find_slot(c->server, from);
            return;
        }
        if (c->joined && c->keepalive && NETCONN_same(c, from, &c->host)) {
            c->joined = alive_ticks();
            send(c, &c->host, '*', NULL, 0);
        }
        return;
    case '<':
        if (c->onmessage) c->onmessage(c, data, len);
        return;
    case '=':
        if (c->server) {
            find_slot(c->server, from);
            return;
        }
        if (NETCONN_same(c, from, &c->host) && c->keepalive) c->keepalive = ping_ticks();
        return;
    case '?':
    case 'p':
        if (NETCONN_same(c, from, &c->host)) {
            if (c->keepalive) c->keepalive = ping_ticks();
            uint32_t id = get_be(data, 4);
            if (r->playerfn && id == r->id) {
                if (type == 'p') {
                    int32_t idx = (int32_t)get_be(data + 0x18, 4);
                    if (!r->playerfn(c, (const NetPlayerInfo *)(data + 0x1c), idx + 1, idx == -1)) r->playerfn = NULL;
                }
                r->result = 1;
                r->id = 0;
            }
        }
        return;
    case 'K':
    case 'b': {
        NetServer *sv = c->server;
        if (!sv) {
            c->keepalive = 0;
            c->joined = 0;
            return;
        }
        NetSlot *s = find_slot(sv, from);
        if (s) {
            s->alive = 0;
            sv->mask &= ~(1u << (int)(s - sv->slot));
        }
        return;
    }
    case 'a':
        if (NETCONN_same(c, from, &c->host) && get_be(data, 4) == r->id) {
            int32_t res = (int32_t)get_be(data + 0x18, 4);
            r->result = res;
            r->id = 0;
            if (res == 1) {
                c->joined = alive_ticks();
                memcpy(&c->host, data + 4, 20);
                c->keepalive = ping_ticks();
            }
        }
        return;
    case 'd': {
        uint32_t id = get_be(data, 4);
        NetGameFn games = NULL;
        if (id == 0 && r->id == 0 && r->games) {
            games = r->games;   /* an advert while finding */
        } else {
            if (r->games || r->id == 0 || id != r->id || !NETCONN_same(c, from, &c->host)) return;
        }
        if (c->keepalive && !games) c->keepalive = ping_ticks();
        for (int i = 0; i < 4; i++) r->info.b[i] = (uint8_t)get_be(data + 0x48 + i, 1);
        r->info.s[0] = (int16_t)get_be(data + 0x4c, 2);
        r->info.s[1] = (int16_t)get_be(data + 0x4e, 2);
        r->info.i[0] = (int32_t)get_be(data + 0x50, 4);
        r->info.i[1] = (int32_t)get_be(data + 0x54, 4);
        r->info.maxplayers = (int32_t)get_be(data + 0x58, 4);
        strncpy(r->info.name, (const char *)data + 0x28, 0x20);
        r->players = get_be(data + 0x6c, 4);
        r->result = 1;
        if (r->games) {
            uint32_t raw;
            memcpy(&raw, data + 0x6c, 4);   /* (counted as it came: the same count either way) */
            if (!r->games(c, &r->info, popcount(raw) + 1, from)) r->games = NULL;
        }
        return;
    }
    case 'g': {
        NetServer *sv = c->server;
        if (!sv) return;
        uint8_t b[0x78] = {0};
        memcpy(b, data, 4);
        memcpy(b + 4, data + 4, 20);
        memcpy(b + 0x18, data + 0x18, 4);
        uint8_t reply = 'p';
        int len_ = 0x78;
        int32_t idx = (int32_t)get_be(data + 0x18, 4);
        if (idx == -1) memcpy(b + 0x1c, &c->self, 0x5c);
        else if (!(sv->mask & 1u << idx)) {
            reply = '?';
            len_ = 0x1c;
        } else {
            memcpy(b + 0x1c, &sv->slot[idx].info, 0x5c);
        }
        send(c, from, reply, b, len_);
        return;
    }
    case 'j': {
        NetServer *sv = c->server;
        if (!sv || sv->hidden) return;
        uint8_t b[0x20] = {0};
        memcpy(b, data, 4);
        memcpy(b + 4, data + 4, 20);   /* (a player already in: the original's are its stack's) */
        NetSlot *s = find_slot(sv, from);
        if (s) {
            put_be(b + 0x18, 1, 4);
            put_be(b + 0x1c, (uint32_t)(s - sv->slot), 4);
        } else if (popcount(sv->mask) < sv->info.maxplayers - 1) {
            if (sv->password[0] && strncmp(sv->password, (const char *)data + 0x18, 0x10)) {
                put_be(b + 0x18, 0xffffffffu, 4);
            } else {
                uint8_t info[0x5c];
                memcpy(info, data + 0x28, 0x5c);
                int i = add_slot(sv, (const NetPlayerInfo *)info);   /* (the address the player gave) */
                memcpy(info, from, 20);                              /* (the one it came from, to the callback) */
                if (!sv->joinfn || sv->joinfn(c, (const NetPlayerInfo *)info, i + 1, 0)) {
                    put_be(b + 0x18, 1, 4);
                    put_be(b + 0x1c, (uint32_t)i, 4);
                    memcpy(b + 4, data + 4, 20);
                } else {
                    sv->mask &= ~(1u << i);
                    put_be(b + 0x18, 0xfffffffdu, 4);
                }
            }
        } else {
            put_be(b + 0x18, 0xfffffffeu, 4);
        }
        send(c, from, 'a', b, 0x20);
        return;
    }
    case 'q':
        if (c->joinfilter && !c->joinfilter(c, from)) send(c, from, '!', NULL, 0);
        return;
    case 'r':
        if (c && c->server) describe(c->server, from, get_be(data, 4));
        return;
    case 'w':
        if (c && c->server && !strncmp(c->server->advert, (const char *)data, 0x20)) describe(c->server, from, 0);
        return;
    }
}

/* A packet for the transport, through the waiting request (or one of its own); the '=' packets dropped
   unless a packet connection runs. True for a '=', *more false for a '!'. 0x588780 */
static bool gathr_receive(NetConn *c, const NetAddr *from, uint8_t type, const uint8_t *data, int len, uint32_t *more)
{
    NetReq local;
    memset(&local, 0, sizeof local);
    local.conn = c;
    if (!c->req) c->req = &local;
    dispatch(c->req, from, type, data, len);
    if (c->req == &local) c->req = NULL;
    if (!c->packets) c->provider->discard(c->socket, NULL, '=');
    *more = type != '!';
    return type == '=';
}

/* The timer's tick on each connection: a joined player's host time-out runs down; a host pings its
   players every so often and drops the ones gone silent. 0x587a70, 0x587a90 */
static bool tick_conn(void *node, void *arg)
{
    NetConn *c = node;
    if (c->transport->kind != 0) return true;
    if (!c->packets && c->socket && c->joined && !c->server) c->joined--;
    NetServer *sv = c->server;
    if (sv && !sv->hidden && sv->mask) {
        int32_t k = c->keepalive;
        if (k > 1) c->keepalive = k - 1;
        else c->keepalive = ping_ticks();
        for (int i = 0; i < sv->info.maxplayers - 1; i++)
            if (sv->mask & 1u << i) {
                if (sv->slot[i].alive-- == 0) sv->mask &= ~(1u << i);
                else if (k <= 1) send(c, &sv->slot[i].info.addr, '*', NULL, 0);
            }
    }
    return true;
}

static void tick(void) { NetList_Walk(&g_net_conns, tick_conn, (void *)1); }

const NetTransport NETGATHR_transport = {
    0, 0, gathr_open, gathr_close, gathr_find, gathr_join, gathr_query, gathr_host, gathr_setinfo,
    gathr_hide, gathr_show, gathr_leave, gathr_players, gathr_kill, gathr_packets, gathr_zero,
    gathr_receive, gathr_one, gathr_message,
};

void NET_onreceive(void *owner, const NetAddr *from)
{
    NetConn *c = owner;
    if (PACKET_dispatch(c, from)) return;   /* (a packet connection to the sender, 0x5821c0) */
    if (!c->transport->receive || !c->socket) return;
    int n;
    do {
        NetAddr a = *from;
        uint8_t type = '=';
        uint8_t data[0xac];
        n = c->provider->recv(c->socket, &a, &type, data, 0xac);
        if (n == 0 && type == 0) return;
        uint32_t more;
        c->transport->receive(c, &a, type, data, n, &more);
    } while (n);
}

/* ---------------------------------------------------------------- self-test */

static NetAddr found_at;

static int on_game(NetConn *c, const NetGameInfo *g, int players, const NetAddr *from)
{
    char m[128], t[32];
    uint32_t port;
    NETCONN_totext(c, from, t, &port);
    snprintf(m, sizeof m, "gathr: found \"%s\" at %s:%u players %d max %d", g->name, t, port, players, g->maxplayers);
    plat_log(m);
    found_at = *from;
    return 0;
}

static int on_player(NetConn *c, const NetPlayerInfo *p, int number, int ishost)
{
    char m[128];
    snprintf(m, sizeof m, "gathr: player %d%s \"%s\" (%s)", number, ishost ? " host" : "", p->name, p->extra);
    plat_log(m);
    return 1;
}

static int on_join(NetConn *c, const NetPlayerInfo *p, int number, int ishost)
{
    char m[128], t[32];
    uint32_t port;
    NETCONN_totext(c, &p->addr, t, &port);
    snprintf(m, sizeof m, "gathr: %s joins as %d from %s:%u", p->name, number, t, port);
    plat_log(m);
    return 1;
}

/* The instance with --param nethost=1 hosts a game, the others find it, join it and list its players. */
static void selftest_thread(void *arg)
{
    static NetConn conn;
    char m[160], me[32];
    const uint8_t *prov = UDP_register(NULL);
    if (!prov) {
        plat_log("gathr: no UDP");
        return;
    }
    static const uint8_t game[16] = {'O', 'p', 'e', 'n', 'N', 'F', 'S', 'P', 'U', ' ', 't', 'e', 's', 't', 0, 1};
    WS_gethostname(me, sizeof me);
    char v[8];
    bool hosting = plat_param("nethost", v, sizeof v) && v[0] == '1';
    int r = NET_open(&conn, NULL, game, prov, 0x5001, hosting ? "host" : "guest", me);
    snprintf(m, sizeof m, "gathr: %s open %d", me, r);
    plat_log(m);
    if (!r) return;
    if (hosting) {
        NetGameInfo gi;
        memset(&gi, 0, sizeof gi);
        snprintf(gi.name, sizeof gi.name, "test race");
        gi.maxplayers = 4;
        r = conn.transport->host(&conn, &gi, NULL, on_join);
        snprintf(m, sizeof m, "gathr: host %d", r);
        plat_log(m);
        for (int i = 0; i < 60; i++) THREAD_sleep(100);
        conn.transport->players(&conn, NULL, on_player);
    } else {
        THREAD_sleep(500);
        r = conn.transport->find(&conn, on_game, TIMER_RATE * 2, 0);
        snprintf(m, sizeof m, "gathr: find %d", r);
        plat_log(m);
        r = conn.transport->join(&conn, &found_at, NULL);
        snprintf(m, sizeof m, "gathr: join %d", r);
        plat_log(m);
        if (r == 1) conn.transport->players(&conn, NULL, on_player);
        THREAD_sleep(2000);
    }
    NET_close(&conn);
    plat_log("gathr: done");
}

void NETGATHR_selftest(void)
{
    static ThreadRec rec;
    static bool started_test;
    if (!started_test) {
        started_test = true;
        if (!THREAD_createarg(selftest_thread, NULL, 0x4000, 0, 0, &rec)) plat_log("gathr: no threads");
    }
}
