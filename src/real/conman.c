/* The session manager CONMAN: see conman.h. */
#include "real/conman.h"
#include "exe.h"
#include "pending.h"
#include "platform.h"
#include "real/mem.h"
#include "real/systask.h"
#include "real/udp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CON_RUNNING 1u
#define CON_HOSTED  2u   /* the host: advertised; the guest: its connection open */
#define CON_ANSWER  4u   /* the host: answering a modem; the guest: the host's reply came */

static uint32_t flags_of(void) { return exe_u32(0x5df404); }   /* the packet connections' (0x8004) */
/* (0x5e8e50: an empty text in the zeroed data, the gathering's password and the finds' user) */
static const char no_password[1] = "";

/* ---- the messages to the thread (0x57fd70..0x57fed0) ---- */

static void msgs_init(ConMan *cm, ConMsg *m, int n)   /* 0x57fd70 */
{
    NetList_Init(&cm->freemsgs, NULL, NULL);
    NetList_Init(&cm->queue, NULL, NULL);
    for (int i = 0; i < n; i++) NetList_Push(&cm->freemsgs, &m[i]);
    cm->ev = EVENT_create();
}

static void msgs_free(ConMan *cm)   /* 0x57fdd0 */
{
    NetList_Free(&cm->freemsgs);
    NetList_Free(&cm->queue);
    EVENT_close(cm->ev);
}

static ConMsg *msg_new(ConMan *cm)   /* 0x57fe00 */
{
    return NetList_Count(&cm->freemsgs) ? NetList_Pop(&cm->freemsgs) : NULL;
}

static void msg_post(ConMan *cm, ConMsg *m)   /* 0x57fe30 */
{
    NetList_Push(&cm->queue, m);
    EVENT_set(cm->ev);
}

/* The oldest message, waiting for one ticks (0 for ever). 0x57fe60 */
static ConMsg *msg_get(ConMan *cm, int ticks)
{
    if (!NetList_Count(&cm->queue)) {
        if (ticks == 0) EVENT_wait(cm->ev);
        else EVENT_waitticks(cm->ev, ticks);
    }
    return NetList_Count(&cm->queue) ? NetList_Pull(&cm->queue) : NULL;
}

static void msg_done(ConMan *cm, ConMsg *m) { NetList_Push(&cm->freemsgs, m); }   /* 0x57fed0 */

/* ---- a link's deadline (0x582060, 0x5820a0) ---- */

static void deadline(ConLink *l, int ms)
{
    l->deadline = ms ? (int32_t)TIMER_count + TIMER_RATE * ms / 1000 : 0;
}

static bool expired(const ConLink *l) { return l->deadline && l->deadline <= (int32_t)TIMER_count; }

/* ---- the providers CONMAN asks for that the port doesn't have ---- */

/* The TCP/IP stream provider (0x5e1a78, transport 0x5e1a28) added after UDP: its GUID, NULL. 0x581ff0 */
static const uint8_t *tcp_register(const char *name)
{
    PENDING_NOTE(0x581ff0, "the TCP/IP stream provider");
    return NULL;
}

/* A packet connection over a TCP/IP connection to a text address (protocol 0x5e1af8). 0x5833f0 */
static int stream_open(PacketConn *pc, const char *text, int32_t arg, int flags)
{
    PENDING_NOTE(0x5833f0, "the TCP/IP packet connection");
    return 0;
}

/* A packet connection on a serial line (the modem's link 0x5df480, PACKET or UNRELY). 0x55e890 */
static int serial_open(PacketConn *pc, int maxdata, uint32_t flags)
{
    PENDING_NOTE(0x55e890, "the serial packet connection");
    return 0;
}

bool CONMAN_isnet(const ConMan *cm) { return 3 < cm->provider; }
bool CONMAN_ismodem(const ConMan *cm) { return cm->provider == 1; }

static bool udp_guid(uintptr_t provider)
{
    return !memcmp((const void *)provider, exe_data(0x5df6b4, 16), 16);
}

int CONMAN_totext(ConMan *cm, const NetAddr *a, char *text, uint32_t *port)
{
    return CONMAN_isnet(cm) ? NETCONN_totext(&cm->conn, a, text, port) : 0;
}

int CONMAN_parse(ConMan *cm, const char *text, int port, NetAddr *out)
{
    if (CONMAN_isnet(cm)) return NETCONN_parse(&cm->conn, text, port, out);
    if (CONMAN_ismodem(cm)) {
        /* (a phone number: its first 19 characters in the address) */
        strncpy((char *)out->b, text, 0x13);
        out->b[0x13] = 0;
        return 1;
    }
    return 0;
}

/* A player's address as the gathering gave it, "0." (the host's bound socket's) made the host's own
   address. 0x55d3b0 */
static bool fix_address(ConMan *cm, NetPlayerInfo *p)
{
    if (!CONMAN_isnet(cm)) return false;
    p->addr = cm->conn.host;
    char text[256];
    uint32_t port;
    NETCONN_totext(&cm->conn, &p->addr, text, &port);
    if (text[0] == '0' && text[1] == '.') NETCONN_parse(&cm->conn, g_net_hostaddr, (int)port, &p->addr);
    return true;
}

static bool same_addr(ConMan *cm, const NetAddr *a, const NetAddr *b)   /* 0x55d440 */
{
    if (CONMAN_isnet(cm)) return NETCONN_same(&cm->conn, a, b);
    return !memcmp(a, b, sizeof *a);
}

/* The gathering's messages: event 0xb. 0x55d280 */
static void on_message(NetConn *c, const uint8_t *data, int len)
{
    ConMan *cm = c->owner;
    ConEvent e = {CON_MESSAGE, len, {.data = data}};
    cm->event(cm, NULL, &e);
}

static int event(ConMan *cm, const NetPlayerInfo *who, int code, int len, int32_t reason)
{
    ConEvent e;
    memset(&e, 0, sizeof e);
    e.code = code;
    e.len = len;
    e.u.reason = reason;
    return cm->event(cm, who, &e);
}

/* ================================================================ the host */

/* A player of that address (on the TCP/IP connection, or the other). 0x55bcd0 */
static bool player_at(void *node, void *arg)
{
    ConPlayer *p = node;
    ConMan *cm = p->link.owner;
    if (p->flags & CONP_STREAM) return NETCONN_same(&cm->conn2, &p->info.addr, arg);
    return same_addr(cm, &p->info.addr, arg);
}

static bool player_expired(void *node, void *arg) { return expired(&((ConPlayer *)node)->link); }   /* 0x55bd10 */

/* The packet connection's calls (0x55b1a0..0x55b240): connecting goes on until the deadline or the end,
   a closed one is a message 7 to the thread, one silent too long is marked and closed. */
static int player_idle(PacketConn *pc)
{
    ConPlayer *p = pc->owner;
    if (p) {
        ConServer *s = p->link.owner;
        if (!expired(&p->link) && (s->flags & CON_RUNNING)) return 0;
    }
    return 1;
}

static void player_closed(PacketConn *pc)
{
    ConPlayer *p = pc->owner;
    if (!p) return;
    ConServer *s = p->link.owner;
    ConMsg *m = msg_new(&s->cm);
    if (m) {
        m->type = 7;
        m->u.pc = pc;
        msg_post(&s->cm, m);
    }
}

static int player_timeout(PacketConn *pc)
{
    ConPlayer *p = pc->owner;
    if (p) p->flags |= CONP_TIMEOUT;
    return 0;
}

/* A player's hello [0xff][password][name][text] (each ends in a 0): the password checked (its first
   16 characters at most, as many as came), the name and the text taken. 0x55b2b0 */
static int player_hello(ConPlayer *p, const uint8_t *data, uint32_t len)
{
    if (data[0] != 0xff) return 1;
    ConServer *s = p->link.owner;
    uint32_t n = len - 1;
    if (0xf < n) n = 0x10;
    if (!strncmp((const char *)data + 1, s->cm.password, n)) p->hello |= 2;
    uint32_t pw = (uint32_t)strlen((const char *)data + 1) + 1;
    uint32_t at = pw + 1;
    if (at < len) {
        uint32_t k = len - at;
        strncpy(p->info.name, (const char *)data + at, k < 0x34 ? k : 0x34);
    }
    uint32_t name = (uint32_t)strlen((const char *)data + at) + 1;
    at = pw + name + 1;
    if (at < len) {
        uint32_t k = len - at;
        strncpy(p->info.extra, (const char *)data + at, k < 0x14 ? k : 0x14);
    }
    p->hello |= 1;
    return 1;
}

/* A packet from a player: CONMAN's own (a first byte over 200), else the game's once it is in. */
static int player_receive(PacketConn *pc, const uint8_t *data, int len)
{
    ConPlayer *p = pc->owner;
    if (!p) return 0;
    if (200 < data[0]) return player_hello(p, data, (uint32_t)len);
    if (p->flags & CONP_LIVE) {
        ConServer *s = p->link.owner;
        ConEvent e = {CON_PACKET, len, {.data = data}};
        return s->cm.event(&s->cm, &p->info, &e);
    }
    return 0;
}

static void player_calls(ConPlayer *p)
{
    p->link.pc.owner = p;
    p->link.pc.idle = player_idle;
    p->link.pc.closed = player_closed;
    p->link.pc.timeout = player_timeout;
    p->link.pc.receive = player_receive;
}

/* A player from the list (NULL none) dropped: event 8 with the reason, onto the dead ones. 0x55b390 */
static void drop(ConServer *s, ConPlayer *p, int32_t reason, NetList *from)
{
    event(&s->cm, &p->info, CON_DROPPED, 4, reason);
    if (from) {
        CS_enter(from->cs);
        CS_enter(s->dead.cs);
        NetList_Remove(from, p);
    }
    NetList_Push(&s->dead, p);
    if (from) {
        CS_leave(from->cs);
        CS_leave(s->dead.cs);
    }
}

/* A player in: from the list onto the players, event 7. 0x55b5e0 */
static void admit(ConServer *s, ConPlayer *p, NetList *from)
{
    if (from) {
        CS_enter(from->cs);
        CS_enter(s->players.cs);
        NetList_Remove(from, p);
    }
    NetList_Push(&s->players, p);
    if (from) {
        CS_leave(from->cs);
        CS_leave(s->players.cs);
    }
    event(&s->cm, &p->info, CON_JOINED, 0, 0);
    p->flags |= CONP_LIVE;
}

/* The gathering's join callback: a new player (number -1: on the TCP/IP connection) asked of the game
   (event 6); in, it waits in the asking ones for its packet connection. 0x55b030 */
static int on_join(NetConn *c, const NetPlayerInfo *info, int number, int ishost)
{
    ConServer *s = c->owner;
    if (NetList_Find(&s->asking, player_at, (void *)&info->addr)) return 1;
    if (NetList_Find(&s->players, player_at, (void *)&info->addr)) return 0;
    ConPlayer *p = NetList_Pop(&s->free);
    if (!p) return 0;
    memset(p, 0, sizeof *p);
    if (number == -1) p->flags |= CONP_STREAM;
    p->link.owner = s;
    p->flags |= CONP_INGAME;
    p->info = *info;
    deadline(&p->link, 10000);
    player_calls(p);
    int r = event(&s->cm, &p->info, CON_ASKS, 0, 0);
    if (r) {
        NetList_Push(&s->asking, p);
        return r;
    }
    memset(p, 0, sizeof *p);
    NetList_Push(&s->free, p);
    return 0;
}

/* The connection's join filter: a player the game let in, its packet connection asked for (the thread
   opens it); on the TCP/IP connection one the gathering never saw is let in on the spot. 0x55aef0 */
static int join_filter(NetConn *c, const NetAddr *from)
{
    ConServer *s = c->owner;
    ConPlayer *p = NetList_Take(&s->asking, player_at, (void *)from);
    if (!p) {
        if (NetList_Find(&s->players, player_at, (void *)from)) return 1;
        if (NetList_Find(&s->connecting, player_at, (void *)from)) return 1;
        if (!(s->cm.conn2open & 1) || c != &s->cm.conn2) return 0;
        NetPlayerInfo info;
        info.addr = *from;
        info.name[0] = 0;
        info.extra[0] = 0;
        on_join(c, &info, -1, 0);
        p = NetList_Take(&s->asking, player_at, (void *)from);
        if (!p) return 0;
    }
    if (s->flags & CON_HOSTED) {
        p->link.pc.addr = *from;
        NetList_Append(&s->connecting, p);
        ConMsg *m = msg_new(&s->cm);
        if (m) {
            m->type = 6;
            msg_post(&s->cm, m);
        }
        return 1;
    }
    NetList_Push(&s->dead, p);
    return 0;
}

/* The players connecting: a packet connection to each, its hello waited for 5 seconds, answered (in,
   or the wrong password). (The reason is the last one set: the original never clears it between them.)
   0x55b430 */
static void connect_players(ConServer *s)
{
    int32_t reason = 0;
    for (ConPlayer *p; (p = NetList_At(&s->connecting, 0));) {
        deadline(&p->link, 5000);
        NetConn *c = (p->flags & CONP_STREAM) ? &s->cm.conn2 : &s->cm.conn;
        if (!PACKET_connect(&p->link.pc, 0xfa, flags_of(), c)) {
            drop(s, p, reason, &s->connecting);
            continue;
        }
        p->flags |= CONP_OPEN;
        deadline(&p->link, 5000);
        while (!(p->hello & 1) && !expired(&p->link) && (p->flags & CONP_OPEN) && (s->flags & CON_RUNNING))
            THREAD_sleep(1);
        if (p->hello & 1) {
            bool bad = !(p->hello & 2);
            if (bad) reason = CON_BADPASSWORD;
            uint8_t answer[2] = {0xfe, (uint8_t)(bad + 1)};
            PACKET_send(&p->link.pc, answer, 2);
            if (!bad) {
                admit(s, p, &s->connecting);
                continue;
            }
        } else if (expired(&p->link)) {
            reason = CON_NOANSWER;
        }
        p->flags &= ~CONP_OPEN;
        PACKET_close(&p->link.pc);
        drop(s, p, reason, &s->connecting);
    }
}

/* The dead players: out of the gathering, their connections closed (a modem's lost while running: a
   message 5 to answer again, or event 0xd), back to the free ones. 0x55b6a0 */
static void bury(ConServer *s)
{
    for (ConPlayer *p; (p = NetList_Pop(&s->dead));) {
        if (p->flags & CONP_INGAME) NET_kill((p->flags & CONP_STREAM) ? &s->cm.conn2 : &s->cm.conn, &p->info.addr);
        if (p->flags & CONP_OPEN) {
            PACKET_close(&p->link.pc);
            if ((s->flags & CON_RUNNING) && (p->flags & CONP_MODEM)) {
                ConMsg *m = msg_new(&s->cm);
                if (!m) {
                    event(&s->cm, NULL, CON_FAILED, 0, 0);
                } else {
                    m->type = 5;
                    msg_post(&s->cm, m);
                    s->flags |= CON_RUNNING;
                }
            }
        }
        memset(p, 0, sizeof *p);
        NetList_Push(&s->free, p);
    }
}

/* A call to a text address (message 4: a direct connection), the player asked of the game and its hello
   waited for. 0x55b7b0 */
static void call_player(ConServer *s, const char *text, int32_t arg)
{
    int32_t reason = 0;
    ConPlayer *p = NetList_Pop(&s->free);
    if (!p) return;
    memset(p, 0, sizeof *p);
    p->link.owner = s;
    p->flags |= CONP_CALLED;
    deadline(&p->link, 0);
    player_calls(p);
    fix_address(&s->cm, &p->info);
    memcpy(p->info.name, exe_data(0x5d93ec, 6), 6);   /* "local" */
    if (event(&s->cm, &p->info, CON_ASKS, 0, 0) && stream_open(&p->link.pc, text, arg, 0)) {
        p->flags |= CONP_OPEN;
        deadline(&p->link, 5000);
        while (!(p->hello & 1)) {
            if (expired(&p->link) || !(p->flags & CONP_OPEN) || !(s->flags & CON_RUNNING)) break;
            THREAD_sleep(1);
        }
        if (p->hello & 1) {
            uint8_t answer[2] = {0xfe, 1};   /* (in, whatever the password) */
            PACKET_send(&p->link.pc, answer, 2);
            admit(s, p, NULL);
            return;
        }
        if (expired(&p->link)) reason = CON_NOANSWER;
        p->flags &= ~CONP_OPEN;
        PACKET_close(&p->link.pc);
    }
    drop(s, p, reason, NULL);
}

/* The modem's answering thread: a player on the serial line at a time until one is in. 0x55ba00 */
static void answer_thread(void *arg)
{
    ConServer *s = arg;
    bool in = false;
    ConPlayer *p = NetList_Pop(&s->free);
    if (p) {
        s->flags |= CON_ANSWER;
        do {
            memset(p, 0, sizeof *p);
            p->link.owner = s;
            deadline(&p->link, 0);
            p->flags |= CONP_MODEM;
            player_calls(p);
            if (serial_open(&p->link.pc, 0xfa, flags_of())) {
                p->flags |= CONP_OPEN;
                deadline(&p->link, 5000);
                while (!(p->hello & 1) && !expired(&p->link) && (p->flags & CONP_OPEN) && (s->flags & CON_RUNNING))
                    THREAD_sleep(1);
                if (p->hello & 1) {
                    if ((p->hello & 2) && event(&s->cm, &p->info, CON_ASKS, 0, 0)) in = true;
                    uint8_t answer[2] = {0xfe, (uint8_t)(!in + 1)};
                    PACKET_send(&p->link.pc, answer, 2);
                }
                if (!in) {
                    p->flags &= ~CONP_OPEN;
                    PACKET_close(&p->link.pc);
                }
            }
            if (!(s->flags & CON_RUNNING) || !(s->flags & CON_ANSWER)) {
                if (in) admit(s, p, NULL);
                else NetList_Push(&s->free, p);
                goto out;
            }
        } while (!in);
        admit(s, p, NULL);
    }
out:
    s->flags &= ~CON_ANSWER;
}

/* (It waits on the host's own thread's end, not the answering one's: the original's.) 0x55b990 */
static bool answer(ConServer *s)
{
    if (!THREAD_createarg(answer_thread, s, 0, 0, -1, &s->answer)) return false;
    while (!(s->flags & CON_ANSWER) && !THREAD_ended(&s->answer)) THREAD_sleep(1);
    return !THREAD_ended(&s->thread);
}

static void answer_end(ConServer *s)   /* 0x55bc20 */
{
    if (s->flags & CON_ANSWER) {
        s->flags &= ~CON_ANSWER;
        THREAD_wait(&s->answer, 0);
    }
}

/* What the host gives its transport's configuration (the stream one's: the game's description, then
   values the datagram one never reads). */
typedef struct {
    NetGameInfo info;
    int32_t w[45];
} ConConfig;

/* The host's thread: the game hosted on the connection (the modem answered), then its messages and
   players until the end; then every player dropped and the connections closed. 0x55a950 */
static void host_thread(void *arg)
{
    ConServer *s = arg;
    ConMan *cm = &s->cm;
    bool opened = false, hosted = false, shown = false;
    if (CONMAN_isnet(cm)) {
        if (NET_open(&cm->conn, join_filter, cm->game, (const uint8_t *)cm->provider, cm->port, "<<longname>>",
                     "<<shortname>>")) {
            static ConConfig cfg;
            memset(&cfg, 0, sizeof cfg);
            cfg.w[0] = 1;
            cfg.w[1] = 0x20;
            cfg.w[9] = 1;
            opened = true;
            cm->conn2open &= ~1u;
            const uint8_t *tcp;
            if (udp_guid(cm->provider) && (tcp = tcp_register(exe_str(0x5bc6a8)))) {
                cm->conn2.owner = cm;
                if (NET_open(&cm->conn2, join_filter, cm->game, tcp, cm->port, "<<longname>>", "<<shortname>>"))
                    cm->conn2open |= 1;
            }
            cfg.info = s->info;
            NET_config(&cm->conn, &cfg);
            NET_setmessage(&cm->conn, on_message);
            if (NET_host(&cm->conn, &s->info, no_password, on_join)) {
                shown = hosted = true;
                s->flags |= CON_RUNNING | CON_HOSTED;
            }
        }
    } else if (!CONMAN_ismodem(cm)) {
        s->flags |= CON_RUNNING;
    } else if (answer(s)) {
        s->flags |= CON_RUNNING;
    }
    while (s->flags & CON_RUNNING) {
        ConMsg *m = msg_get(cm, TIMER_RATE);
        if (m) {
            if (m->type == 4) {
                call_player(s, m->u.text, m->arg);
            } else if (m->type == 5) {
                if (!answer(s)) event(cm, NULL, CON_FAILED, 0, 0);
            } else if (m->type == 7) {
                ConPlayer *p = m->u.pc->owner;
                if (p && (p->flags & CONP_OPEN)) {
                    if (!(p->flags & CONP_LIVE)) {
                        /* not in yet: again later */
                        THREAD_sleep(1);
                        msg_post(cm, m);
                        goto players;
                    }
                    if (NetList_Take(&s->players, player_at, &p->info.addr)) {
                        p->flags &= ~CONP_LIVE;
                        event(cm, &p->info, (p->flags & CONP_TIMEOUT) ? CON_TIMEDOUT : CON_LOST, 0, 0);
                        NetList_Push(&s->dead, p);
                    }
                }
            }
            msg_done(cm, m);
        }
    players:
        if (CONMAN_isnet(cm)) {
            /* advertised as flag 2 says */
            if (shown) {
                if (s->flags & CON_HOSTED) goto asking;
                NET_hide(&cm->conn, 0);
                shown = false;
            }
            if (s->flags & CON_HOSTED) {
                NET_show(&cm->conn);
                shown = true;
            }
        }
    asking:
        for (ConPlayer *p; (p = NetList_Find(&s->asking, player_expired, NULL));) drop(s, p, CON_NOANSWER, &s->asking);
        connect_players(s);
        bury(s);
    }
    answer_end(s);
    for (ConPlayer *p; (p = NetList_Pop(&s->asking));) NetList_Push(&s->dead, p);
    for (ConPlayer *p; (p = NetList_Pop(&s->players));) NetList_Push(&s->dead, p);
    for (ConPlayer *p; (p = NetList_Pop(&s->connecting));) NetList_Push(&s->dead, p);
    bury(s);
    if (hosted) NET_leave(&cm->conn);
    if (opened) NET_close(&cm->conn);
    if (cm->conn2open & 1) {
        NET_close(&cm->conn2);
        cm->conn2open &= ~1u;
    }
}

static void players_init(ConServer *s, int n)   /* 0x55bc50 */
{
    NetList_Init(&s->free, NULL, NULL);
    NetList_Init(&s->asking, NULL, NULL);
    NetList_Init(&s->players, NULL, NULL);
    NetList_Init(&s->connecting, NULL, NULL);
    NetList_Init(&s->dead, NULL, NULL);
    for (int i = 0; i < n; i++) NetList_Push(&s->free, &s->slots[i]);
}

static void players_free(ConServer *s)   /* 0x55bdc0 */
{
    NetList_Free(&s->free);
    NetList_Free(&s->asking);
    NetList_Free(&s->players);
    NetList_Free(&s->connecting);
    NetList_Free(&s->dead);
}

ConServer *CONMAN_host(ConEventFn fn, uintptr_t provider, const uint8_t *game, const NetGameInfo *info, int32_t user,
                       int32_t port, const char *password)
{
    int n = info->maxplayers;
    int size = (int)sizeof(ConServer) + n * (int)sizeof(ConPlayer);
    ConServer *s = MEM_alloc("CONMAN", size, exe_u32(0x5deb60));
    if (!s) return NULL;
    memset(s, 0, (size_t)size);
    msgs_init(&s->cm, s->msgs, 5);
    players_init(s, n);
    s->cm.game = game;
    s->cm.user = user;
    s->cm.provider = provider;
    s->info = *info;
    s->cm.kind = 1;
    s->cm.event = fn;
    s->cm.port = port;
    /* (the original copies the whole text over the 16 bytes and on) */
    if (password) snprintf(s->cm.password, sizeof s->cm.password, "%s", password);
    s->info.maxplayers++;   /* (the host too) */
    s->cm.conn.owner = s;
    if (!THREAD_createarg(host_thread, s, 0, 1, -1, &s->thread)) return NULL;
    while (!(s->flags & CON_RUNNING) && !THREAD_ended(&s->thread)) THREAD_sleep(1);
    if (!THREAD_ended(&s->thread)) return s;
    MEM_purge(s);
    return NULL;
}

void CONMAN_hostend(ConServer *s)
{
    if (!(s->flags & CON_RUNNING)) return;
    s->flags &= ~CON_RUNNING;
    ConMsg *m;
    while (!(m = msg_new(&s->cm))) {
        if (THREAD_isthread(NULL)) SYNCTASK_run(0);
        THREAD_sleep(1);
    }
    m->type = 6;
    msg_post(&s->cm, m);
    THREAD_wait(&s->thread, 0);
    msgs_free(&s->cm);
    players_free(s);
    MEM_purge(s);
}

int CONMAN_setinfo(ConServer *s, const NetGameInfo *info)
{
    if (!CONMAN_isnet(&s->cm)) return 0;
    s->info = *info;
    return NET_setinfo(&s->cm.conn, &s->info);
}

int CONMAN_call(ConServer *s, const char *text, int32_t arg)
{
    ConMsg *m = msg_new(&s->cm);
    if (!m) return 0;
    m->type = 4;
    snprintf(m->u.text, sizeof m->u.text, "%s", text);
    m->arg = arg;
    msg_post(&s->cm, m);
    return 1;
}

/* (who is a player's info: its player around it, 0x55bfc0) */
int CONMAN_sendto(ConServer *s, const NetPlayerInfo *who, const void *data, uint32_t len)
{
    ConPlayer *p = (ConPlayer *)((uint8_t *)who - offsetof(ConPlayer, info));
    return (p->flags & CONP_OPEN) ? PACKET_send(&p->link.pc, data, len) : 0;
}

int CONMAN_sendto_unreliable(ConServer *s, const NetPlayerInfo *who, const void *data, uint32_t len)
{
    ConPlayer *p = (ConPlayer *)((uint8_t *)who - offsetof(ConPlayer, info));
    return (p->flags & CONP_OPEN) ? PACKET_send(&p->link.pc, data, len | PACKET_UNRELIABLE) : 0;
}

/* ================================================================ a guest */

static int guest_filter(NetConn *c, const NetAddr *from) { return 0; }   /* 0x55ca30 */

/* A game found: event 0. 0x55ca40 */
static int on_game(NetConn *c, const NetGameInfo *g, int players, const NetAddr *from)
{
    ConClient *cl = c->owner;
    if (!(cl->flags & CON_RUNNING)) return 0;
    ConEvent e;
    memset(&e, 0, sizeof e);
    e.code = CON_GAME;
    e.len = 0x4c;
    e.u.game.addr = *from;
    e.u.game.info = *g;
    e.u.game.players = players;
    return cl->cm.event(&cl->cm, NULL, &e);
}

/* A host's player: event 2 (not the host). 0x55cab0 */
static int on_player(NetConn *c, const NetPlayerInfo *p, int number, int ishost)
{
    ConClient *cl = c->owner;
    if (!(cl->flags & CON_RUNNING)) return 0;
    if (ishost) return 1;
    ConEvent e;
    memset(&e, 0, sizeof e);
    e.code = CON_PLAYER;
    e.len = 0x5c;
    e.u.player = *p;
    return cl->cm.event(&cl->cm, NULL, &e);
}

/* The packet connection's calls (0x55cb10..0x55cc00). */
static int guest_idle(PacketConn *pc)
{
    ConClient *cl = pc->owner;
    return (!expired(&cl->link) && (cl->flags & CON_RUNNING)) ? 0 : 1;
}

static void guest_closed(PacketConn *pc)
{
    ConClient *cl = pc->owner;
    ConMsg *m = msg_new(&cl->cm);
    if (m) {
        m->type = 7;
        m->u.pc = pc;
        msg_post(&cl->cm, m);
    }
}

static int guest_timeout(PacketConn *pc)
{
    ((ConClient *)pc->owner)->linkflags |= CONP_TIMEOUT;
    return 0;
}

static int guest_receive(PacketConn *pc, const uint8_t *data, int len)
{
    ConClient *cl = pc->owner;
    if (200 < data[0]) {
        if (data[0] == 0xfe) {   /* the host's answer (0x55cc00) */
            cl->reply = data[1];
            cl->flags |= CON_ANSWER;
        }
        return 1;
    }
    if (cl->linkflags & CONP_LIVE) {
        ConEvent e = {CON_PACKET, len, {.data = data}};
        return cl->cm.event(&cl->cm, NULL, &e);
    }
    return 0;
}

static void guest_calls(ConClient *cl)
{
    cl->link.pc.owner = cl;
    cl->link.pc.idle = guest_idle;
    cl->link.pc.closed = guest_closed;
    cl->link.pc.timeout = guest_timeout;
    cl->link.pc.receive = guest_receive;
    cl->link.owner = cl;
}

/* The hello [0xff][password][name][text] and the host's answer waited for: 1 in, 2 the wrong password,
   3 no answer in time, 0 no connection. 0x55cc30 */
static int hello(ConClient *cl, const char *password)
{
    cl->flags &= ~CON_ANSWER;
    cl->reply = 0;
    uint8_t b[0x5a];
    memset(b, 0, 0x59);
    b[0] = 0xff;
    char *t = (char *)b + 1;
    strncpy(t, password, 0xf);
    int n = (int)strlen(t) + 1;
    strncpy(t + n, cl->self.name, 0x34);
    n += (int)strlen(t + n) + 1;
    strncpy(t + n, cl->self.extra, 0x14);
    n += (int)strlen(t + n) + 1;
    if (!PACKET_send(&cl->link.pc, b, (uint32_t)(n + 1))) return 0;
    deadline(&cl->link, 5000);
    while (!(cl->flags & CON_ANSWER)) {
        if (expired(&cl->link) || !(cl->linkflags & CONP_OPEN) || !(cl->flags & CON_RUNNING)) break;
        THREAD_sleep(1);
    }
    if (!(cl->flags & CON_ANSWER)) return expired(&cl->link) ? 3 : 0;
    return cl->reply;
}

/* A dialled host (the modem's number). 0x55cd70 */
static bool dial(ConClient *cl, const char *number)
{
    deadline(&cl->link, 0);
    return serial_open(&cl->link.pc, 0xfa, flags_of()) != 0;
}

/* A host at a text address (message 4). 0x55cdf0 */
static int call_host(ConClient *cl, const char *text, int32_t arg)
{
    deadline(&cl->link, 0);
    cl->linkflags &= ~CONP_OPEN;
    if (stream_open(&cl->link.pc, text, arg, 1)) {
        deadline(&cl->link, 5000);
        cl->linkflags |= CONP_OPEN;
        if (hello(cl, no_password) == 1) {
            cl->link.owner = cl;
        } else {
            cl->linkflags &= ~CONP_OPEN;
            PACKET_close(&cl->link.pc);
        }
    }
    if (cl->linkflags & CONP_OPEN) {
        event(&cl->cm, NULL, CON_IN, 0, 0);
        cl->linkflags |= CONP_LIVE;
    } else {
        event(&cl->cm, NULL, CON_NOTIN, 4, 0);
    }
    return (int)(cl->linkflags & CONP_OPEN);
}

/* A join (message 2): through the gathering, again and again (after 2, 4 .. 60 seconds) for 5 seconds
   a time until ticks run out, then the packet connection and the hello. */
static bool join_host(ConClient *cl, ConMsg *m, bool *connected)
{
    ConMan *cm = &cl->cm;
    int joined = 0;
    uint32_t reason = 0;
    memset(&cl->link.pc, 0, sizeof cl->link.pc);
    guest_calls(cl);
    if (!CONMAN_isnet(cm)) {
        if (CONMAN_ismodem(cm) && dial(cl, m->u.text)) {
            *connected = true;
            goto hello;
        }
        goto failed;
    }
    int32_t until = 0;
    if (m->u.join.ticks) {
        until = (int32_t)TIMER_count + m->u.join.ticks;
        if (!until) until = 1;
    }
    for (int attempt = 0; (!until || (int32_t)TIMER_count < until) && !joined; attempt++) {
        if (!(cl->flags & CON_RUNNING)) goto reasons;
        if (0 < attempt) {
            int wait = 1 << (attempt & 0x1f);
            THREAD_sleep((uint32_t)((wait > 0x3c ? 0x3c : wait) * 1000));
        }
        int32_t end = (int32_t)TIMER_count + TIMER_RATE * 5;
        if (until && until <= end) end = until;
        bool tried = false;
        while ((int32_t)TIMER_count < end) {
            tried = true;
            if (joined || !(cl->flags & CON_RUNNING)) break;
            int keep = g_net_resend;
            g_net_resend = TIMER_RATE;
            joined = NET_join(&cm->conn, &m->u.join.addr, no_password);
            g_net_resend = keep;
        }
        if (tried && joined) continue;
        /* the TCP/IP connection, while there is time */
        if ((cl->flags & CON_RUNNING) && (cm->conn2open & 1)) {
            int32_t t = (attempt + 5) * TIMER_RATE;
            if (until && until - (int32_t)TIMER_count <= t) t = until - (int32_t)TIMER_count;
            deadline(&cl->link, t * 1000 / TIMER_RATE);
            cl->link.pc.addr = m->u.join.addr;
            if (PACKET_connect(&cl->link.pc, 0xfa, flags_of(), &cm->conn2)) {
                joined = 1;
                *connected = true;
            } else {
                joined = 0;
            }
        }
    }
    if (0 < joined) {
        if (*connected) goto hello;
        if (until) {
            until -= (int32_t)TIMER_count;
            if (!until) until = 1;
        }
        deadline(&cl->link, until * 1000 / TIMER_RATE);
        cl->link.pc.addr = m->u.join.addr;
        if (PACKET_connect(&cl->link.pc, 0xfa, flags_of(), &cm->conn)) {
            *connected = true;
            goto hello;
        }
        NET_leave(&cm->conn);
        goto failed;
    }
reasons:
    switch (joined) {
    case 0: reason = CON_NOANSWER; break;
    case -3: reason = CON_REFUSED; break;
    case -2: reason = CON_FULL; break;
    case -1: reason = CON_BADPASSWORD; break;
    }
    if (!*connected) goto failed;
hello:
    cl->linkflags |= CONP_OPEN;
    deadline(&cl->link, 5000);
    switch (hello(cl, m->u.join.password)) {
    case 1:
        event(cm, NULL, CON_IN, 0, 0);
        cl->linkflags |= CONP_LIVE;
        return true;
    case 2: reason |= CON_BADPASSWORD; break;
    case 3: reason |= CON_NOANSWER; break;
    }
    *connected = false;
    cl->linkflags &= ~CONP_OPEN;
    PACKET_close(&cl->link.pc);
failed:
    event(cm, NULL, CON_NOTIN, 4, (int32_t)reason);
    return false;
}

/* The guest's thread: the connection opened, then the game's requests one at a time. 0x55c1e0 */
static void guest_thread(void *arg)
{
    ConClient *cl = arg;
    ConMan *cm = &cl->cm;
    bool connected = false;
    if (!CONMAN_isnet(cm)) {
        cl->flags |= CON_RUNNING;
    } else {
        if (!g_net_resend) g_net_resend = (TIMER_RATE + 3) / 4;
        if (NET_open(&cm->conn, guest_filter, cm->game, (const uint8_t *)cm->provider, cm->port, cl->self.name,
                     cl->self.extra)) {
            cm->conn2open &= ~1u;
            const uint8_t *tcp;
            if (udp_guid(cm->provider) && (tcp = tcp_register(exe_str(0x5bc6a8)))) {
                cm->conn2.owner = cm;
                if (NET_open(&cm->conn2, guest_filter, cm->game, tcp, cm->port, "<<longname>>", "<<shortname>>"))
                    cm->conn2open |= 1;
            }
            NET_setmessage(&cm->conn, on_message);
            cl->flags |= CON_HOSTED | CON_RUNNING;
        }
    }
    if (cl->flags & CON_RUNNING) {
        do {
            ConMsg *m = msg_get(cm, 0);
            if (!m) continue;
            switch (m->type) {
            case 0:
                NET_find(&cm->conn, on_game, m->u.ticks, 0);
                event(cm, NULL, CON_FOUND, 0, 0);
                break;
            case 1:
                NET_players(&cm->conn, &m->u.addr, on_player);
                event(cm, NULL, CON_LISTED, 0, 0);
                break;
            case 2:
                if (!connected) join_host(cl, m, &connected);
                break;
            case 3: {
                ConEvent e;
                memset(&e, 0, sizeof e);
                e.code = CON_GAME;
                e.len = 0x4c;
                e.u.game.addr = m->u.addr;
                e.u.game.players = -1;
                if (NET_query(&cm->conn, &m->u.addr, &e.u.game.info)) {
                    e.u.game.info.maxplayers--;   /* (less the host) */
                    cm->event(cm, NULL, &e);
                }
                event(cm, NULL, CON_FOUND, 0, 0);
                break;
            }
            case 4:
                if (!connected) {
                    guest_calls(cl);
                    connected = call_host(cl, m->u.text, m->arg) != 0;
                }
                break;
            case 7: {
                ConClient *o = m->u.pc->owner;
                if (o && (o->linkflags & CONP_OPEN)) {
                    if (!(o->linkflags & CONP_LIVE)) {
                        THREAD_sleep(1);
                        msg_post(&o->cm, m);
                        continue;
                    }
                    o->linkflags &= ~CONP_LIVE;
                    event(&o->cm, NULL, (o->linkflags & CONP_TIMEOUT) ? CON_TIMEDOUT : CON_LOST, 0, 0);
                }
                break;
            }
            }
            msg_done(cm, m);
        } while (cl->flags & CON_RUNNING);
        if (connected) {
            if (cl->flags & CON_HOSTED) NET_leave(&cm->conn);
            PACKET_close(&cl->link.pc);
            cl->linkflags &= ~CONP_OPEN;
        }
    }
    if (cl->flags & CON_HOSTED) {
        NET_close(&cm->conn);
        cl->flags &= ~CON_HOSTED;
    }
    if (cm->conn2open & 1) {
        NET_close(&cm->conn2);
        cm->conn2open &= ~1u;
    }
    cl->flags &= ~CON_RUNNING;
}

ConClient *CONMAN_guest(ConEventFn fn, uintptr_t provider, const uint8_t *game, const NetPlayerInfo *self, int32_t user,
                        int32_t port)
{
    ConClient *cl = MEM_alloc("CONMAN", (int)sizeof(ConClient), exe_u32(0x5deb60));
    if (!cl) return NULL;
    memset(cl, 0, sizeof *cl);
    msgs_init(&cl->cm, cl->msgs, 10);
    cl->cm.game = game;
    cl->cm.user = user;
    cl->cm.provider = provider;
    cl->cm.kind = 2;
    cl->self = *self;
    cl->cm.event = fn;
    cl->cm.port = port;
    cl->cm.conn.owner = cl;
    if (THREAD_createarg(guest_thread, cl, 0, 1, -1, &cl->thread)) {
        while (!(cl->flags & CON_RUNNING) && !THREAD_ended(&cl->thread)) THREAD_sleep(1);
        if (!THREAD_ended(&cl->thread)) return cl;
        msgs_free(&cl->cm);
        MEM_purge(cl);
    }
    return NULL;
}

void CONMAN_guestend(ConClient *cl)
{
    if (!(cl->flags & CON_RUNNING)) return;
    ConMsg *m;
    while (!(m = msg_new(&cl->cm))) {
        if (THREAD_isthread(NULL)) SYNCTASK_run(0);
        THREAD_sleep(1);
    }
    cl->flags &= ~CON_RUNNING;
    m->type = 6;
    msg_post(&cl->cm, m);
    THREAD_wait(&cl->thread, 0);
    msgs_free(&cl->cm);
    MEM_purge(cl);
}

int CONMAN_find(ConClient *cl, int32_t ticks)
{
    if (!(cl->flags & CON_HOSTED)) return 0;
    ConMsg *m = msg_new(&cl->cm);
    if (!m) return 0;
    memset(m->u.raw, 0, sizeof m->u.raw);
    m->type = 0;
    m->u.ticks = ticks;
    msg_post(&cl->cm, m);
    return 1;
}

int CONMAN_join(ConClient *cl, const NetAddr *host, int32_t ticks, const char *password)
{
    if (!CONMAN_isnet(&cl->cm) && !CONMAN_ismodem(&cl->cm)) return 0;
    if (cl->linkflags & CONP_OPEN) return 0;
    ConMsg *m = msg_new(&cl->cm);
    if (!m) return 0;
    memset(m->u.raw, 0, sizeof m->u.raw);
    m->type = 2;
    /* (the original copies the whole text; the ticks after it overwrite its 17th character on) */
    if (password) snprintf(m->u.join.password, sizeof m->u.join.password, "%s", password);
    m->u.join.addr = *host;
    m->u.join.ticks = ticks;
    msg_post(&cl->cm, m);
    return 1;
}

int CONMAN_dial(ConClient *cl, const char *text, int32_t arg)
{
    if (cl->linkflags & CONP_OPEN) return 0;
    ConMsg *m = msg_new(&cl->cm);
    if (!m) return 0;
    m->type = 4;
    snprintf(m->u.text, sizeof m->u.text, "%s", text);
    m->arg = arg;
    msg_post(&cl->cm, m);
    return 1;
}

int CONMAN_send(ConClient *cl, const void *data, uint32_t len)
{
    return (cl->linkflags & CONP_OPEN) ? PACKET_send(&cl->link.pc, data, len) : 0;
}

int CONMAN_send_unreliable(ConClient *cl, const void *data, uint32_t len)
{
    return (cl->linkflags & CONP_OPEN) ? PACKET_send(&cl->link.pc, data, len | PACKET_UNRELIABLE) : 0;
}

/* ---------------------------------------------------------------- self-test */

static volatile int test_state;   /* the guest: 1 found, 2 in, 3 failed; the host: 2 a player in */
static NetAddr test_host;
static const NetPlayerInfo *test_player;
static int test_got;

static int test_event(ConMan *cm, const NetPlayerInfo *who, const ConEvent *e)
{
    char m[160];
    if (e->code == CON_GAME) {
        snprintf(m, sizeof m, "conman: game \"%s\" players %d max %d", e->u.game.info.name, e->u.game.players,
                 e->u.game.info.maxplayers);
        test_host = e->u.game.addr;
        test_state = 1;
    } else if (e->code == CON_PACKET) {
        test_got++;
        snprintf(m, sizeof m, "conman: packet \"%.*s\"%s%s", e->len, (const char *)e->u.data, who ? " from " : "",
                 who ? who->name : "");
    } else {
        snprintf(m, sizeof m, "conman: event %d len %d reason %d%s%s", e->code, e->len, e->len == 4 ? e->u.reason : 0,
                 who ? " player " : "", who ? who->name : "");
        if (e->code == CON_IN) test_state = 2;
        if (e->code == CON_NOTIN) test_state = 3;
        if (e->code == CON_JOINED) {
            test_player = who;
            test_state = 2;
        }
    }
    plat_log(m);
    return 1;
}

/* --param nethost=1 hosts a game, the other instance finds it, joins with the password, and they trade
   five packets each way. */
static void selftest_thread(void *arg)
{
    char v[8], t[48];
    const uint8_t *prov = UDP_register(NULL);
    if (!prov) {
        plat_log("conman: no UDP");
        return;
    }
    static const uint8_t game[16] = {'O', 'p', 'e', 'n', 'N', 'F', 'S', 'P', 'U', ' ', 'c', 'o', 'n', 0, 0, 1};
    bool hosting = plat_param("nethost", v, sizeof v) && v[0] == '1';
    if (hosting) {
        NetGameInfo gi;
        memset(&gi, 0, sizeof gi);
        snprintf(gi.name, sizeof gi.name, "conman race");
        gi.maxplayers = 4;
        ConServer *s = CONMAN_host(test_event, (uintptr_t)prov, game, &gi, 0, 0x5003, "pw");
        plat_log(s ? "conman: hosting" : "conman: host failed");
        if (!s) return;
        for (int i = 0; i < 200 && test_state != 2; i++) THREAD_sleep(50);
        for (int i = 0; test_player && i < 5; i++) {
            snprintf(t, sizeof t, "host packet %d", i);
            CONMAN_sendto(s, test_player, t, (uint32_t)strlen(t));
            THREAD_sleep(50);
        }
        THREAD_sleep(3000);
        CONMAN_hostend(s);
    } else {
        NetPlayerInfo self;
        memset(&self, 0, sizeof self);
        snprintf(self.name, sizeof self.name, "guest");
        snprintf(self.extra, sizeof self.extra, "car");
        THREAD_sleep(500);
        ConClient *cl = CONMAN_guest(test_event, (uintptr_t)prov, game, &self, 0, 0x5003);
        plat_log(cl ? "conman: guest" : "conman: guest failed");
        if (!cl) return;
        CONMAN_find(cl, TIMER_RATE * 2);
        for (int i = 0; i < 100 && test_state != 1; i++) THREAD_sleep(50);
        if (test_state == 1) {
            CONMAN_join(cl, &test_host, TIMER_RATE * 16, "pw");
            for (int i = 0; i < 200 && test_state == 1; i++) THREAD_sleep(50);
            for (int i = 0; test_state == 2 && i < 5; i++) {
                snprintf(t, sizeof t, "guest packet %d", i);
                CONMAN_send(cl, t, (uint32_t)strlen(t));
                THREAD_sleep(50);
            }
        }
        THREAD_sleep(2000);
        CONMAN_guestend(cl);
    }
    snprintf(t, sizeof t, "conman: done, %d packets", test_got);
    plat_log(t);
}

void CONMAN_selftest(void)
{
    static ThreadRec rec;
    static bool started_test;
    if (!started_test) {
        started_test = true;
        if (!THREAD_createarg(selftest_thread, NULL, 0x4000, 0, 0, &rec)) plat_log("conman: no threads");
    }
}
