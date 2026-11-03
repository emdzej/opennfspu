/* The game's network session: the session object, its guest and what the race asks of it (see net.h).
   GetTickCount is the real library's timer here (it runs on while the main thread waits). */
#include "game/net.h"
#include "exe.h"
#include "game/camera.h"
#include "game/car.h"
#include "game/input.h"
#include "game/loadscreen.h"
#include "game/option_rec.h"
#include "game/race_progress.h"
#include "pending.h"
#include "platform.h"
#include "real/systask.h"
#include "real/udp.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

NetSession *g_net;
void (*g_net_chathook)(const char *text, int32_t to);   /* 0x6afb24 */

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static double exe_f64(uint32_t va)
{
    const uint8_t *p = exe_data(va, 8);
    double d = 0;
    if (p) memcpy(&d, p, 8);
    return d;
}

static int32_t tickcount(void) { return (int32_t)((uint64_t)TIMER_count * 1000 / TIMER_RATE); }

static const uint8_t *game_guid(void) { return exe_data(0x5b4980, 16); }
static const char *s_empty = "";   /* 0x5e8e50 */
static const int s_voice_option = 0;   /* (0x657aa4: the voice chat's option, never set: gasm has no microphone) */
#define PROTOCOL exe_str(0x5d06fc)   /* "2.8" */
#define SEP exe_str(0x5d066c)        /* "~" */
#define LOBBY_PORT ((int)exe_u32(0x5b4990))   /* 9400 */

/* ================================================================ lists */

void NetLst_init(NetLst *l) { l->head.next = l->head.prev = &l->head; }

void NetLst_push(NetLst *l, void *p)
{
    NetLNode *n = malloc(sizeof *n);
    n->p = p;
    n->next = &l->head;
    n->prev = l->head.prev;
    l->head.prev->next = n;
    l->head.prev = n;
}

static void unlink_node(NetLNode *n)
{
    n->prev->next = n->next;
    n->next->prev = n->prev;
    free(n);
}

void NetLst_remove(NetLst *l, void *p)
{
    NETLST_EACH(l, n) if (n->p == p) unlink_node(n);
}

void NetLst_popfront(NetLst *l)
{
    if (!NetLst_empty(l)) unlink_node(l->head.next);
}

void NetLst_clear(NetLst *l)
{
    while (!NetLst_empty(l)) unlink_node(l->head.next);
}

/* ================================================================ messages */

static void put24(uint8_t *p, int32_t v) { p[0] = (uint8_t)(v >> 16); p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)v; }
static void put32(uint8_t *p, int32_t v) { p[0] = (uint8_t)(v >> 24); put24(p + 1, v); }
static int32_t get24(const uint8_t *p) { return (int32_t)(((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8)) >> 8; }
static int32_t get32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]); }

void Net_Msg(NetSender *s, uint8_t type, int32_t a, int32_t b, const char *text, void *to)
{
    uint8_t m[200];
    unsigned n = 0;
    m[n++] = type;
    if (a != NET_NONE) put24(m + n, a), n += 3;
    if (b != NET_NONE) put32(m + n, b), n += 4;
    if (text) {
        while (n < 199 && *text) m[n++] = (uint8_t)*text++;
        m[n++] = 0;
    }
    if (!to && s->players) {
        NETLST_EACH(s->players, e) s->wrap->send(s->wrap, e->p, m, (int)n);
    } else {
        s->wrap->send(s->wrap, to, m, (int)n);
    }
}

/* ================================================================ the guest (CConClient) */

static void client_send(NetWrap *w, void *to, const void *data, int len)   /* CConClient_v0 0x48aca0 */
{
    (void)to;
    NetClient *c = (NetClient *)w;
    CS_enter(c->cs);
    NetPkt *p = calloc(1, sizeof *p);
    p->len = len < (int)sizeof p->data ? len : (int)sizeof p->data;
    memcpy(p->data, data, (size_t)p->len);
    NetLst_push(&c->queue, p);
    if (c->idle) {
        NetPkt *h = NetLst_front(&c->queue);
        if (CONMAN_send(c->cm, h->data, (uint32_t)h->len)) {
            free(h);
            NetLst_popfront(&c->queue);
        }
        c->idle = false;
    }
    CS_leave(c->cs);
}

NetClient *NetClient_New(uintptr_t provider, ConEventFn event, intptr_t user, const char *name, const char *extra,
                         const char *version, const char *name2, int port)
{
    NetClient *c = calloc(1, sizeof *c);
    c->cs = CS_create();
    c->base.send = client_send;
    NetLst_init(&c->queue);
    c->idle = true;
    NetPlayerInfo self;
    memset(&self, 0, sizeof self);
    /* the name "<name>[~<name2>]", the extra text "<extra>[~]2.8[~<version>]" (the original's strcats into
       its 52- and 20-byte fields; here cut to them) */
    snprintf(self.name, sizeof self.name, "%s%s%s", name, name2[0] ? SEP : "", name2);
    snprintf(self.extra, sizeof self.extra, "%s%s%s%s%s", extra, extra[0] ? SEP : "", PROTOCOL, version[0] ? SEP : "",
             version);
    c->cm = CONMAN_guest(event, provider, game_guid(), &self, user, port);
    return c;
}

void NetClient_Find(NetClient *c) { CONMAN_find(c->cm, TIMER_RATE * 4); }   /* 0x48ab80 */

static void client_join(NetClient *c, const NetAddr *a) { CONMAN_join(c->cm, a, TIMER_RATE << 4, NULL); }   /* 0x48aba0 */

void NetClient_JoinText(NetClient *c, const char *text, int port)
{
    int a = 0, b = 0, cc = 0, d = 0;
    NetAddr addr;
    if (sscanf(text, exe_str(0x5d067c), &a, &b, &cc, &d, &port) < 4) {   /* "%d.%d.%d.%d.%d" */
        CONMAN_parse(&c->cm->cm, text, port, &addr);
    } else {
        char t[32];
        snprintf(t, sizeof t, exe_str(0x5d0670), a & 0xff, b & 0xff, cc & 0xff, d & 0xff);   /* "%d.%d.%d.%d" */
        CONMAN_parse(&c->cm->cm, t, port, &addr);
    }
    client_join(c, &addr);
}

bool NetClient_Flush(NetClient *c)
{
    CS_enter(c->cs);
    if (!NetLst_empty(&c->queue) && c->in) {
        NetPkt *h = NetLst_front(&c->queue);
        if (CONMAN_send(c->cm, h->data, (uint32_t)h->len)) {
            free(h);
            NetLst_popfront(&c->queue);
        }
        c->idle = false;
        CS_leave(c->cs);
        return true;
    }
    c->idle = true;
    CS_leave(c->cs);
    return false;
}

void NetClient_Delete(NetClient *c)
{
    if (!c) return;
    CONMAN_guestend(c->cm);
    NETLST_EACH(&c->queue, n) free(n->p);
    NetLst_clear(&c->queue);
    CS_destroy(c->cs);
    free(c);
}

/* ================================================================ records */

static void game_split(NetGameRec *g)   /* (0x48c2b0, 0x48c370: the name "<name>~<version>") */
{
    g->name[0] = g->version[0] = 0;
    char fmt[16];
    snprintf(fmt, sizeof fmt, "%%63[^~]~%%31s");   /* (0x5d0710 "%[^~]~%s", bounded) */
    sscanf(g->info.name, fmt, g->name, g->version);
}

static void game_set(NetGameRec *g, const uint8_t *ev)   /* 0x48c370: address, info and players (0x4c) */
{
    if (ev) {
        memcpy(&g->addr, ev, sizeof g->addr);
        memcpy(&g->info, ev + 0x14, sizeof g->info);
        memcpy(&g->players, ev + 0x48, 4);
        game_split(g);
    }
    g->stale = 0;
}

static NetGameRec *game_new(const uint8_t *ev, int32_t id)   /* 0x48c2b0 */
{
    NetGameRec *g = calloc(1, sizeof *g);
    g->id = id;
    g->ping = -1;
    game_set(g, ev);
    return g;
}

NetGameRec *NetGame_FromText(const char *text, int32_t id)   /* 0x48c240 */
{
    NetGameRec *g = calloc(1, sizeof *g);
    g->id = id;
    g->ping = -1;
    snprintf(g->info.name, 0x20, "%s", text);
    snprintf(g->name, 0x40, "%s", text);
    return g;
}

/* ICMP pings of the games (0x48c3b0 starts the pinger 0x55d520, 0x48c460 stops it, 0x48c5c0 pings a game
   every 5 s by its address text, 0x48c540 the answer): gasm has no ICMP, the games' pings stay as found. */
static void pinger_start(void) { PENDING_NOTE(0x48c3b0, "the games' ICMP pings (0x55d520)"); }
static void pinger_stop(void) {}
static void game_free(NetGameRec *g) { free(g); }   /* (0x48c310: and its pings' answers forgotten) */

int Net_GamePing(NetGameRec *g) { return g->ping; }   /* 0x48c5c0 */

static NetPlayer *player_new(const char *name, int32_t number)
{
    NetPlayer *p = calloc(1, sizeof *p);
    snprintf(p->name, sizeof p->name, "%s", name);   /* 0x48c770 */
    p->number = number;
    p->ping = -1;
    NetLst_init(&p->data);
    p->finish = p->progress = p->car = -1;
    return p;
}

static void data_free(NetData *d)
{
    free(d->data);
    free(d);
}

static void player_free(NetPlayer *p)   /* 0x490480 */
{
    NETLST_EACH(&p->data, n) data_free(n->p);
    NetLst_clear(&p->data);
    free(p);
}

static NetData *player_data(NetPlayer *p, int16_t id)   /* 0x4903a0 */
{
    NETLST_EACH(&p->data, n) if (((NetData *)n->p)->id == id) return n->p;
    return NULL;
}

static void player_adddata(NetPlayer *p, int16_t id, const void *data, int size)   /* 0x4903d0 */
{
    if (player_data(p, id)) return;
    NetData *d = calloc(1, sizeof *d);
    d->id = id;
    d->data = calloc(1, size > 0 ? (size_t)size : 1);   /* ("CDataPacket") */
    d->size = size;
    if (data) memcpy(d->data, data, (size_t)size);
    NetLst_push(&p->data, d);
}

static bool player_racing(const NetPlayer *p)   /* 0x490340 */
{
    return Net_InGame() && (g_net->racing & (1u << (p->number & 0x1f)));
}

int Net_PlayerPing(NetPlayer *p)
{
    if (Net_Live() && g_net->in && !p->pinging && tickcount() - p->pinged > 1000) {
        p->pinging = 1;
        p->pinged = tickcount();
        Net_Ping(p->number);
    }
    return p->ping;
}

static void player_pong(NetPlayer *p, int32_t v)   /* 0x4905b0 */
{
    if (Net_Live()) g_net->rtt = tickcount() - p->pinged;   /* (0x48d960) */
    p->ping = v;
    p->pinging = 0;
}

NetPlayer *Net_PlayerByNumber(int number)
{
    if (!Net_Live()) return NULL;
    NETLST_EACH(&g_net->plist, n) if (((NetPlayer *)n->p)->number == number) return n->p;
    return NULL;
}

NetPlayer *Net_PlayerByCar(int car)
{
    if (!Net_Live()) return NULL;
    NETLST_EACH(&g_net->plist, n) if (((NetPlayer *)n->p)->car == car) return n->p;
    return NULL;
}

/* ================================================================ the clock (0x48a8e0..0x48a930) */

static void clock_set(NetSession *s, int32_t host) { s->clock = host - tickcount(); }

static void clock_follow(NetSession *s, int32_t host)   /* only on, or back a ms at a time */
{
    int32_t d = host - s->clock - tickcount();
    if (0 < d) s->clock = host - tickcount();
    if (d < 0) s->clock--;
}

static int32_t clock_now(NetSession *s) { return tickcount() + s->clock; }

/* ================================================================ the session */

void Net_Clear(void)
{
    NetSession *s = g_net;
    NETLST_EACH(&s->games, n) free(n->p);
    NetLst_clear(&s->games);
    NETLST_EACH(&s->ports, n) game_free(n->p);
    NetLst_clear(&s->ports);
    NETLST_EACH(&s->found, n) game_free(n->p);
    NetLst_clear(&s->found);
    NETLST_EACH(&s->plist, n) player_free(n->p);
    NetLst_clear(&s->plist);
    while (!NetLst_empty(&s->blocks)) {
        NetData *d = NetLst_back(&s->blocks);
        NetLst_remove(&s->blocks, d);
        data_free(d);
    }
    s->nplayers = 0;
    s->me = -2;
    s->in = 0;
    s->raceover = 1;
    s->startwait = 0;
    s->players = s->racing = 0;
    s->ready = 0;
    s->ending = 0;
    s->mode = 1;
}

static void read_version(NetSession *s)   /* Net_ReadVersion 0x48db10 */
{
    /* (the registry's values 8 and 7 first, 0x4b7030: the port's registry has none of them) */
    snprintf(s->version2, sizeof s->version2, "%s", exe_str(0x5d07a0));
    snprintf(s->version, sizeof s->version, "%s", exe_str(0x5d07a0));
    /* version.txt (0x5d0790, 0x59ddf0), its first 5 bytes, when it has any */
    size_t n = 0;
    uint8_t *t = vfs_read_all(exe_str(0x5d0790), &n);
    if (t && n) {
        if (5 < n) n = 5;
        memcpy(s->version, t, n);
        s->version[n] = 0;
    }
    free(t);
}

static void timer_cb(void);
static int client_event(ConMan *cm, const NetPlayerInfo *who, const ConEvent *e);

static void voice_new(NetSession *s)
{
    (void)s;
    /* (0x657aa4 1: the voice chat, 0x493190, CVoicePlybkHdrs over DirectSound capture: gasm has no microphone) */
}

static void voice_end(NetSession *s)   /* 0x48dae0 */
{
    s->voice = NULL;
}

/* A new guest of the session's on the provider (the name "c" and the session's nick, version). */
static void session_client(NetSession *s, uintptr_t provider, const char *name, const char *name2, int port)
{
    s->client = NetClient_New(provider, client_event, (intptr_t)s, name, s->nick, g_net ? g_net->version : exe_str(0x5d07a0),
                              name2, port);
    s->sender = calloc(1, sizeof *s->sender);
    s->sender->wrap = &s->client->base;
    s->sender->players = NULL;
}

static void session_reset(NetSession *s)
{
    s->state = 0;
    s->lostroom = 0;
    s->inroom = 0;
    s->lost = 0;
    s->frames = 0;
    s->server = 0;
    s->sendclock = tickcount() << 5;
}

void Net_End(void)
{
    NetSession *s = g_net;
    if (s->pinger) {
        pinger_stop();
        s->pinger = 0;
    }
    if (s->server) {
        if (s->mode != 2) {
            Net_Msg(s->sender, 0xb1, Net_Live() ? s->me : -2, NET_NONE, NULL, NULL);
            THREAD_sleepticks(TIMER_RATE / 2);   /* 0x53c0f0 */
        }
        NetServer_Stop();
    }
    voice_end(s);
    CS_enter(*s->cs);
    s->ending = 1;
    CS_leave(*s->cs);
    if (s->timer) {
        TIMER_removecallback(timer_cb);
        s->timer = 0;
    }
    free(s->sender);
    s->sender = NULL;
    if (s->client) {
        NetClient_Delete(s->client);
        s->client = NULL;
    }
    if (s->server && g_net_server) {
        NetServer_Delete(g_net_server);
        g_net_server = NULL;
    }
    Net_Clear();
    s->state = 0;
    s->lostroom = 0;
    s->inroom = 0;
    s->lost = 0;
    s->frames = 0;
    s->server = 0;
    s->sendclock = tickcount() << 5;
}

NetSession *Net_Construct(void)
{
    NetSession *s = calloc(1, sizeof *s);
    /* (0x560840: IPX, which the socket call refuses on gasm) */
    s->ipx = NULL;
    s->udp = UDP_register(exe_str(0x5d07c0));   /* "UDP" */
    NetLst_init(&s->events);
    s->optlen = 4;
    NetLst_init(&s->games);
    NetLst_init(&s->ports);
    NetLst_init(&s->found);
    NetLst_init(&s->plist);
    NetLst_init(&s->blocks);
    s->raceover = 0;
    s->aborted = 0;
    session_reset(s);
    if (!s->ipx && !s->udp) {
        /* (and no modem, 0x55e920(0): the serial ports pending) */
        free(s);
        return NULL;
    }
    g_net = s;
    s->cs = malloc(sizeof *s->cs);
    *s->cs = CS_create();
    snprintf(s->version, sizeof s->version, "%s", exe_str(0x5d07a0));    /* "0.0" */
    snprintf(s->version2, sizeof s->version2, "%s", exe_str(0x5d07a0));
    snprintf(s->name, sizeof s->name, "%s", exe_str(0x5d07b8));          /* "Player" */
    snprintf(s->nick, sizeof s->nick, "%s", exe_str(0x5d079c));          /* "c" */
    snprintf(s->password, sizeof s->password, "%s", s_empty);
    /* (0x55ea80(2, 1), 0x55ed40(5, 1): the debug print's device and channel) */
    s->options[0] = 0xbc;
    s->options[1] = s->options[2] = s->options[3] = 0;
    Net_Clear();
    g_net_chathook = Net_Chat;
    return s;
}

void Net_Destroy(void)
{
    NetSession *s = g_net;
    if (!s) return;
    g_net_chathook = NULL;
    Net_End();
    CS_destroy(*s->cs);
    free(s->cs);
    PACKET_closeall();   /* 0x560160 */
    NetLst_clear(&s->events);
    g_net = NULL;
    free(s);
}

bool Net_HasProvider(int mode)
{
    if (!g_net) return false;
    if (mode == 0) return g_net->ipx != NULL;
    if (mode == 1) return g_net->udp != NULL;
    if (mode == 2) PENDING_NOTE(0x55e920, "the serial ports (a modem game)");
    return false;
}

int32_t Net_State(void) { return g_net ? g_net->state : 0; }

bool Net_IsFirst(void)
{
    if (!g_net) return false;
    NetSession *s = g_net;
    CS_enter(*s->cs);
    bool r = false;
    if (Net_InGame() && !NetLst_empty(&s->plist)) r = ((NetPlayer *)NetLst_front(&s->plist))->number == s->me;
    CS_leave(*s->cs);
    return r;
}

/* The game's info message to the host ([0xb4][0 0 0][0x14 zeros][info][0 0 0 0]). */
static void send_info(NetSession *s, const NetGameInfo *info)
{
    uint8_t m[0x50];
    memset(m, 0, sizeof m);
    m[0] = 0xb4;
    memcpy(m + 0x18, info, 0x34);
    s->sender->wrap->send(s->sender->wrap, NULL, m, sizeof m);
}

void Net_Leave(void)   /* 0x48e620 */
{
    NetSession *s = g_net;
    if (s->mode == 0) {
        Net_End();
        if (g_net) Net_JoinGame(0, 0, NULL);
    } else if (s->mode == 2) {
        Net_End();
        if (g_net) Net_JoinGame(2, 0, NULL);
    } else if (s->inroom) {
        voice_end(s);
        Net_Msg(s->sender, 0xba, NET_NONE, NET_NONE, NULL, NULL);
    } else {
        Net_End();
    }
}

void Net_HostGame(const NetGameInfo *given, int port)
{
    NetSession *s = g_net;
    if (!s) return;
    if (s->mode == 0 ? !s->ipx : s->mode == 1 ? !s->udp : s->mode == 2 ? !Net_HasProvider(2) : true) return;
    read_version(s);
    NetGameInfo info;
    if (!given) {
        memset(&info, 0, sizeof info);
        snprintf(info.name, 0x1f, "%s", exe_str(0x5d06b4));   /* "NfsNet" */
        info.maxplayers = 8;
    } else {
        memcpy(&info, given, sizeof info);
    }
    /* the name "<name or Game>~<version>" */
    char name[0x40];
    snprintf(name, sizeof name, "%s%s%s", info.name[0] ? info.name : exe_str(0x5d07a4), SEP, g_net ? s->version : exe_str(0x5d07a0));
    NET_field(info.name, name, sizeof info.name);
    if (Net_InGame()) {
        send_info(s, &info);
        goto voice;
    }
    if (s->ipxhost) {
        if (s->in) {
            if (s->mode == 0) {
                Net_End();
                if (g_net) Net_JoinGame(0, 0, NULL);
            } else if (s->mode == 2) {
                Net_End();
                if (g_net) Net_JoinGame(2, 0, NULL);
            } else if (!s->inroom) {
                Net_End();
            } else {
                voice_end(s);
                Net_Msg(s->sender, 0xba, NET_NONE, NET_NONE, NULL, NULL);
            }
        }
    }
    if (s->in) {
        send_info(s, &info);
        s->inroom = 1;
        goto voice;
    }
    s->state = 2;
    uintptr_t provider = 0;
    if (s->mode == 0) {
        provider = (uintptr_t)s->ipx;
        /* (the first time, 0x5d0728: a CONMAN host started and ended at once on IPX) */
    } else if (s->mode == 1) {
        provider = (uintptr_t)s->udp;
        port = LOBBY_PORT;
    } else if (s->mode == 2) {
        provider = 1;
    }
    int mode = s->mode;
    Net_End();
    if (mode == 1) {
        pinger_start();
        s->pinger = 1;
    }
    /* the host's own player: a guest of its server on the memory link */
    session_client(s, 3, exe_str(0x5d079c), s_empty, 0);
    s->mode = mode;
    s->ipxhost = provider == (uintptr_t)s->ipx && s->ipx;
    session_reset(s);
    TIMER_addcallback(timer_cb);
    s->timer = 1;
    if (!g_net_server) NetServer_New(provider, &info, s->client, NULL, port, -1);
    s->server = 1;
voice:
    if (s_voice_option == 1 && !s->voice) voice_new(s);
}

void Net_JoinGame(int mode, int id, const char *addr)
{
    NetSession *s = g_net;
    if (!s) return;
    if (mode == 0 ? !s->ipx : mode == 1 ? !s->udp : mode == 2 ? !Net_HasProvider(2) : true) return;
    read_version(s);
    if (mode == 0) {
        if (!s->ipxhost || !s->timer) {
            /* the LAN's games looked for */
            Net_End();
            CS_enter(*s->cs);
            session_client(s, (uintptr_t)s->ipx, exe_str(0x5d079c), s_empty, 0);
            s->ipxhost = 1;
            s->mode = 0;
            session_reset(s);
            s->state = 1;
            TIMER_addcallback(timer_cb);
            s->timer = 1;
            NetClient_Find(s->client);
            s->rtt = tickcount();
            CS_leave(*s->cs);
            return;
        }
        NETLST_EACH(&s->found, n) {
            NetGameRec *g = n->p;
            if (g->id == id) {
                s->state = 3;
                client_join(s->client, &g->addr);
                break;
            }
        }
        goto voice;
    }
    if (mode == 2) {
        if (!addr) {
            /* (the modems listed, 0x55e920: pending) */
            Net_End();
            s->mode = 2;
            return;
        }
        Net_End();
        s->mode = 2;
        session_client(s, 1, exe_str(0x5d079c), s_empty, id);
        s->ipxhost = 0;
        session_reset(s);
        s->state = 3;
        TIMER_addcallback(timer_cb);
        s->timer = 1;
        NetClient_JoinText(s->client, addr, id);
        goto voice;
    }
    if (addr) {
        Net_End();
        CS_enter(*s->cs);
        pinger_start();
        s->pinger = 1;
        session_client(s, (uintptr_t)s->udp, s->name, s->password, 0);
        s->ipxhost = 0;
        session_reset(s);
        s->state = 3;
        TIMER_addcallback(timer_cb);
        s->timer = 1;
        NetClient_JoinText(s->client, addr, LOBBY_PORT);
        if (s_voice_option == 1 && !s->voice) voice_new(s);
        CS_leave(*s->cs);
        return;
    }
    if (!Net_Live() || !s->in) return;
    Net_Msg(s->sender, 0xb6, id, NET_NONE, NULL, NULL);   /* into the server's game */
    s->inroom = 1;
voice:
    if (s_voice_option == 1 && !s->voice) voice_new(s);
}

/* ================================================================ the race's */

void Net_Send(uint8_t type, int32_t a, int32_t b, void *to) { Net_Msg(g_net->sender, type, a, b, NULL, to); }
void Net_SendStart(void) { Net_Msg(g_net->sender, 0xb3, NET_NONE, NET_NONE, NULL, NULL); }
void Net_SendGo(void) { Net_Msg(g_net->sender, 0xc5, NET_NONE, NET_NONE, NULL, NULL); }

void Net_Controls(void)
{
    if (Net_Live()) {
        NetSession *s = g_net;
        if (s->me < 0 || g_race_network || s->raceover) {
            if (!g_input_stopped) {
                Input_Enqueue(g_race_local, g_input_packet);
                g_input_count++;
            }
            return;
        }
        /* (0x48ea50) the packet's event (the packet's high bits) queued, one a network packet */
        uint8_t ev = g_input_packet[0] >> 3;
        if (ev) NetLst_push(&s->events, (void *)(uintptr_t)ev);
        if (s->ctlstep == 0) {
            /* the second step: the two averaged into 3 bytes and sent */
            s->ctlprev[0] = (int8_t)(((int)s->ctlprev[0] + (int8_t)g_input_packet[1]) / 2);
            s->ctlprev[1] = (int8_t)(((unsigned)g_input_packet[2] + (uint8_t)s->ctlprev[1]) / 2);
            s->ctlprev[2] = (int8_t)(((uint8_t)s->ctlprev[2] + (unsigned)g_input_packet[3]) / 2);
            uint8_t flags = (uint8_t)s->ctlprev[3] | (g_input_packet[0] & 7);
            s->ctlprev[3] = (int8_t)flags;
            s->ctl[0] = flags;
            if (!NetLst_empty(&s->events)) {
                s->ctl[0] |= (uint8_t)((uintptr_t)NetLst_front(&s->events) << 3);
                NetLst_popfront(&s->events);
            }
            s->ctl[1] = (uint8_t)(((uint8_t)s->ctlprev[0] & 0xfe) << 1);
            s->ctl[1] = (uint8_t)(s->ctl[1] + ((uint8_t)s->ctlprev[1] >> 5 & 3));
            s->ctl[2] = (uint8_t)(((uint8_t)s->ctlprev[1] & 0xfc) << 3);
            s->ctl[2] = (uint8_t)(s->ctl[2] + ((uint8_t)s->ctlprev[2] >> 2 & 0x1f));
            s->ctlstep = 3;
            if (s->toggled) {
                s->toggle[s->me] = !s->toggle[s->me];
                s->toggledat[s->me] = tickcount();
                s->toggled = 0;
            }
            s->ctl[0] = (uint8_t)((s->ctl[0] & 0xfb) | (s->toggle[s->me] ? 4 : 0));
            CONMAN_send_unreliable(s->client->cm, s->ctl, 3);
            s->ctlout++;
            return;
        }
        s->ctlprev[0] = (int8_t)g_input_packet[1];
        s->ctlprev[1] = (int8_t)g_input_packet[2];
        s->ctlprev[2] = (int8_t)g_input_packet[3];
        s->ctlprev[3] = (int8_t)(g_input_packet[0] & 7);
        s->ctlstep = 0;
        s->ctlout++;
        return;
    }
    Input_Enqueue(g_race_local, g_input_packet);
    g_input_count++;
}

/* Waits for all players: up (bd with my number), all up, again with +8, then down twice. 0x48cd20 */
static bool barrier_abort(void)
{
    /* ("Abort", 0x55ea00) */
    if (Net_InGame() && !g_net->aborted) {
        g_net->aborted = 1;
        g_net->raceover = 1;
        g_net->startwait = 0;
        Net_Finish(-2, -1);
    }
    return false;
}

static int key_get(void)   /* (the real library's key buffer, 0x55fd10: Escape here) */
{
    uint8_t keys[KEY_COUNT];
    return plat_keys(keys) && keys[1] ? 0x1b : 0;
}

/* One wait of the barrier: 1 done, 0 aborted, -1 the connection gone. */
static int barrier_wait(bool (*done)(void))
{
    for (;;) {
        LoadingScreen(-1);
        if (!g_net || !g_net->client) return 0;
        if (!g_net->lost && g_net->aborted) return -1;
        if (!Net_InGame() || key_get() == 0x1b) return 0;
        THREAD_sleep(1);     /* 0x55f4f0 */
        SYNCTASK_run(0);     /* 0x536450 */
        if (done()) return 1;
    }
}

static bool all_up(void) { return (g_net->ready & g_net->racing) == g_net->racing; }
static bool all_up2(void) { return ((g_net->ready >> 8) & g_net->racing) == g_net->racing; }
static bool all_down(void) { return (g_net->ready & g_net->racing) == 0; }
static bool all_down2(void) { return ((g_net->ready >> 8) & g_net->racing) == 0; }

bool Net_SyncBarrier(void)
{
    NetSession *s = g_net;
    bool (*waits[4])(void) = {all_up, all_up2, all_down, all_down2};
    for (int i = 0; i < 4; i++) {
        Net_Msg(s->sender, 0xbd, s->me + (i & 1) * 8, NET_NONE, NULL, NULL);
        /* ("Wait to go high1" .. "Wait to go low2", 0x55ea00) */
        int r = barrier_wait(waits[i]);
        if (r == 1) continue;
        if (r == 0) barrier_abort();
        return false;
    }
    return true;
}

void Net_Sync(void)
{
    if (Net_Live()) Net_SyncBarrier();
}

void Net_StartRace(void)
{
    if (!Net_Live()) return;
    Net_SyncBarrier();
    if (!Net_Live()) return;
    NETLST_EACH(&g_net->plist, n) {
        NetPlayer *p = n->p;
        p->finish = p->progress = p->car = -1;
    }
}

void Net_FlushOptions(void)
{
    NetSession *s = g_net;
    if (Net_Live() && s->optlen > 4) {
        s->sender->wrap->send(s->sender->wrap, NULL, s->options, s->optlen);
        s->optlen = 4;
    }
}

void Net_AddOption(const int32_t *rec)
{
    NetSession *s = g_net;
    if (!Net_Live()) return;
    int n = Option_Size(rec) * 4;
    if (0xf0 < s->optlen + n) Net_FlushOptions();
    if (s->optlen + n > (int)sizeof s->options) return;
    memcpy(s->options + s->optlen, rec, (size_t)n);
    s->optlen += n;
}

void Net_AddOptions(const int32_t *recs)
{
    if (!Net_Live()) return;
    while (*recs) {
        Net_AddOption(recs);
        Net_FlushOptions();
        recs += Option_Size(recs);
    }
}

void Net_SendBlock(int16_t id, const void *data, int size)
{
    NetSession *s = g_net;
    if (!(Net_InGame() || size < 1)) return;
    int me = Net_Live() ? s->me : -2;
    if (Net_Live()) {
        NETLST_EACH(&s->plist, n) {
            NetPlayer *p = n->p;
            if (p->number != me) continue;
            if (!player_data(p, id)) {
                player_adddata(p, id, data, size);
                player_data(p, id)->done += size;
            }
            break;
        }
    }
    NETLST_EACH(&s->blocks, n) if (((NetData *)n->p)->id == id) return;
    NetData *d = calloc(1, sizeof *d);
    d->id = id;
    d->data = calloc(1, (size_t)size);
    d->size = size;
    if (data) memcpy(d->data, data, (size_t)size);
    NetLst_push(&s->blocks, d);
}

/* A piece of the first block to send ([0xbf: the size | 0xc0: the offset][me][id][int], 0xe8 bytes at most). 0x48cbf0 */
static void send_block(NetSession *s)
{
    if (NetLst_empty(&s->blocks)) return;
    NetData *d = NetLst_front(&s->blocks);
    if (d->done >= d->size) return;
    uint8_t m[8 + 0xe8];
    m[0] = d->done == 0 ? 0xbf : 0xc0;
    m[1] = Net_Live() ? (uint8_t)s->me : 0xfe;
    memcpy(m + 2, &d->id, 2);
    int32_t v = d->done == 0 ? d->size : d->done;
    memcpy(m + 4, &v, 4);
    int n = d->size - d->done;
    if (0xe7 < n) n = 0xe8;
    memcpy(m + 8, d->data + d->done, (size_t)n);
    d->done += n;
    s->client->base.send(&s->client->base, NULL, m, n + 8);
    if (d->done == d->size) {
        NetLst_remove(&s->blocks, d);
        data_free(d);
    }
}

int Net_FinishTime(int car)
{
    if (!Net_Live()) return -1;
    int mine = -1, best = -1;
    NETLST_EACH(&g_net->plist, n) {
        NetPlayer *p = n->p;
        if (p->car == car) mine = p->finish;
        else if (p->finish == -1 && (best == -1 || p->progress < best)) best = p->progress;
    }
    if (!g_race_network || best == -1 || mine < best) return mine;
    return -1;
}

int Net_Place(int car)
{
    int t = Net_FinishTime(car);
    if (t < 0) return -1;
    int place = 1;
    NETLST_EACH(&g_net->plist, n) {
        NetPlayer *p = n->p;
        if (-1 < p->finish && p->finish < t) place++;
    }
    return place;
}

int Net_RaceTime(void)
{
    /* (as the original: in a game and not in one at once, so 0) */
    if (Net_InGame() && (!g_net->in || g_net->me < 0 || !g_net->startwait)) return clock_now(g_net) - g_net->gostart;
    return 0;
}

void Net_Kick(int number)
{
    NetSession *s = g_net;
    if (!Net_InGame()) return;
    if (number != s->me) {
        Net_Msg(s->sender, 0xb1, number, NET_NONE, NULL, NULL);
        return;
    }
    Net_Leave();
}

void Net_Chat(const char *text, int32_t to)
{
    if (Net_Live()) Net_Msg(g_net->sender, 0xbb, to, NET_NONE, text, NULL);
}

void Net_Ping(int32_t number)
{
    if (Net_Live()) Net_Msg(g_net->sender, 0xc3, number, g_net->rtt, NULL, NULL);
}

void Net_Finish(int32_t time, int32_t best)
{
    if (!Net_Live()) return;
    CS_enter(*g_net->cs);
    Net_Msg(g_net->sender, 0xb7, time, best, NULL, NULL);
    g_net->finished = 1;
    CS_leave(*g_net->cs);
}

void Net_RaceDone(void)
{
    if (!Net_Live()) return;
    if (!g_net->finished) Net_Finish(-1, -1);
    if (Net_Live()) Net_Finish(-2, -1);
    g_net->raceover = 1;
}

int Net_CarLag(int car)
{
    NetSession *s = g_net;
    if (!Net_Live()) return 3;
    NetPlayer *p = NULL;
    NETLST_EACH(&s->plist, n) if (((NetPlayer *)n->p)->car == car) { p = n->p; break; }
    if (!p) return 3;
    if (!player_racing(p)) return 4;
    if (g_race_network == 0) {
        int lag = Net_InGame() ? s->lag[car & 7] : 0;   /* (by the car's index, as the original) */
        double d = lag * exe_f64(0x5b49a8);
        if (exe_f64(0x5b2778) < d) return 2;
        return d <= exe_f64(0x5b4998) ? 0 : 1;
    }
    float d;
    if (car == g_race_local) {
        d = (float)Net_PlayerPing(p) * (float)exe_f64(0x5b49a8);
    } else {
        d = (float)(g_sim_tick - *CAR_I(g_cars[car], 0x3c)) * exe_f32(0x5b2828);
    }
    if ((float)exe_f64(0x5b2750) < d) return 2;
    return d <= (float)exe_f64(0x5b49a0) ? 0 : 1;
}

void Net_Tick(void)
{
    if (!Net_Live() || g_race_network != 1 || (g_sim_tick & 7) != 7) return;
    uint8_t m[0x28];
    memset(m, 0, sizeof m);
    m[0] = 0xbe;
    m[1] = Net_Live() ? (uint8_t)g_net->me : 0xfe;
    NetCar_GetState(m + 4, g_cars[g_race_local]);
    CONMAN_send_unreliable(g_net->client->cm, m, sizeof m);
}

/* The timer's callback (0x48edc0): the chat's CTRL+ALT, the host's clock followed, and at 32 Hz the
   guest's queue flushed (or a block's piece sent). */
static void timer_cb(void)
{
    NetSession *s = g_net;
    if (!s || !s->client) return;
    CS_enter(*s->cs);
    uint8_t keys[KEY_COUNT];
    bool ctrlalt = plat_keys(keys) && keys[0x1d] && keys[0x38];   /* (0x5de045, 0x5de060: the key states) */
    if (!ctrlalt) {
        s->chatkey = 0;
    } else if (!s->chatkey) {
        if (Net_Live()) Net_Msg(s->sender, 0xbb, -2, NET_NONE, exe_str(0x5d07ac), NULL);   /* "CTRL ALT" */
        s->chatkey = 1;
    }
    if (s->hostclock) clock_follow(s, s->hostclock - s->hostclockat + (Net_InGame() ? tickcount() : 0));
    if (s->lost) {
        s->frames++;
        CS_leave(*s->cs);
        return;
    }
    int32_t now = tickcount();
    while (0 < now * 32 - s->sendclock) {
        if (!NetClient_Flush(s->client) && Net_InGame()) send_block(s);
        s->sendclock += 1000;
    }
    CS_leave(*s->cs);
}

/* ================================================================ the guest's events (0x48ef90) */

static void abort_race(void)
{
    if (Net_InGame() && !g_net->aborted) {
        g_net->aborted = 1;
        g_net->raceover = 1;
        g_net->startwait = 0;
        Net_Finish(-2, -1);
    }
}

static void got_controls(NetSession *s, const uint8_t *m)
{
    if (g_input_stopped || g_race_over || s->raceover) return;
    for (int i = 0; i < 8; i++) {
        bool bit = (m[i * 3] & 4) == 4;
        uint8_t t = s->toggle[i];
        if (i == s->me) {
            if (bit == (bool)t && !s->toggled) {
                s->lag[i] = tickcount() - s->toggledat[i];
                s->toggled = 1;
            }
        } else if (bit == (bool)t) {
            s->toggle[i] = !t;
            if (0 < s->toggledat[i]) s->lag[i] = tickcount() - s->toggledat[i];
            s->toggledat[i] = tickcount();
        }
    }
    for (int step = 0; step < 2; step++) {
        for (int n = 0; n < 8; n++) {
            const uint8_t *p = m + n * 3;
            if (!Net_Live()) continue;
            NetPlayer *pl = NULL;
            NETLST_EACH(&g_net->plist, e) if (((NetPlayer *)e->p)->number == n) { pl = e->p; break; }
            if (!pl || pl->car < 0) continue;
            uint8_t pk[4];
            pk[0] = (uint8_t)(step == 0 ? p[0] & 0xfb : p[0] & 3);
            pk[1] = (uint8_t)(((int8_t)p[1] >> 1) & 0xfe);
            pk[2] = (uint8_t)((p[1] & 3) * 0x20 + (p[2] >> 3 & 0x1c));
            pk[3] = (uint8_t)((p[2] & 0x1f) << 2);
            if (!Input_Enqueue(pl->car, pk)) abort_race();
        }
        s->ctlin++;
        g_input_count++;
    }
}

/* The server's lists of games: b4 a game (new or its info again), b5 / b9 one gone, b8 a server's game
   with its counts (b9 gone). 0x490b60 */
void Net_GameList(NetLst *l, const uint8_t *m, int len)
{
    (void)len;
    int32_t id = get24(m + 1);
    bool found = false;
    switch (m[0]) {
    case 0xb4:
        NETLST_EACH(l, n) {
            NetGameRec *g = n->p;
            if (g->id == id) {
                game_set(g, m + 4);
                found = true;
            }
        }
        if (!found) NetLst_push(l, game_new(m + 4, id));
        break;
    case 0xb5:
    case 0xb9:
        NETLST_EACH(l, n) {
            NetGameRec *g = n->p;
            if (g->id != id) continue;
            NetLst_remove(l, g);
            game_free(g);
        }
        break;
    case 0xb8:
        NETLST_EACH(l, n) {
            NetGameRec *g = n->p;
            if (g->id != id) continue;
            uint32_t v = (uint32_t)get32(m + 4);
            g->players = (int32_t)(v & 0xffff);
            g->info.i[0] = (int32_t)(v & 0xffff0000u);
            found = true;
        }
        if (!found) NetLst_push(l, NetGame_FromText((const char *)m + 8, id));
        break;
    }
}

static void got_packet(NetSession *s, const uint8_t *m, int len)
{
    int type = m[0];
    if (type < 0xb0) {
        got_controls(s, m);
        return;
    }
    int32_t a = get24(m + 1);
    switch (type) {
    case 0xb0: {   /* a player joined */
        NetPlayer *p = player_new((const char *)m + 4, a);
        p->pinged = 0;
        NetLst_push(&s->plist, p);
        if (-1 < s->me) s->players |= (uint8_t)(1u << (a & 0x1f));
        s->nplayers++;
        break;
    }
    case 0xb1:   /* a player left */
        NETLST_EACH(&s->plist, n) {
            NetPlayer *p = n->p;
            if (p->number != a) continue;
            NetLst_remove(&s->plist, p);
            player_free(p);
            s->nplayers--;
        }
        if (-1 < s->me) {
            s->players &= (uint8_t)~(1u << (a & 0x1f));
            s->racing &= (uint8_t)~(1u << (a & 0x1f));
        }
        /* (the original jumps into 0xc3's: the player gone, no record answers) */
        /* fall through */
    case 0xc3:   /* a player's ping */
        NETLST_EACH(&s->plist, n) if (((NetPlayer *)n->p)->number == a) player_pong(n->p, get32(m + 4));
        break;
    case 0xb2:   /* my number and the session's name */
        snprintf(s->joined, sizeof s->joined, "%s", (const char *)m + 4);
        s->racing = 0;
        s->me = a;
        NETLST_EACH(&s->found, n) game_free(n->p);
        NetLst_clear(&s->found);
        break;
    case 0xb3:   /* the race starts */
        s->racing = s->players;
        s->ctlstep = 3;
        s->ctlin = s->ctlout = 0;
        s->raceover = 0;
        s->aborted = 0;
        s->startwait = 1;
        s->finished = 0;
        for (int i = 0; i < 8; i++) {
            s->toggle[i] = 0;
            s->toggledat[i] = 0;
            s->lag[i] = 0;
        }
        s->toggled = 1;
        s->ready = 0;
        s->hostclock = 0;
        clock_set(s, get32(m + 4));
        break;
    case 0xb4:
    case 0xb5:
        Net_GameList(&s->found, m, len);
        break;
    case 0xb7:   /* a player's finish */
        NETLST_EACH(&s->plist, n) {
            NetPlayer *p = n->p;
            if (p->number != a) continue;
            int32_t t = get32(m + 4);
            if (t == -2) s->racing &= (uint8_t)~(1u << (m[3] & 0x1f));
            else p->finish = p->progress = t;
        }
        break;
    case 0xb8:
    case 0xb9:
        /* (twice, as the original) */
        Net_GameList(&s->ports, m, len);
        Net_GameList(&s->ports, m, len);
        break;
    case 0xbb: {   /* chat */
        int32_t to = get32(m + 4);
        if (s->chat && to != -2) s->chat((const char *)m + 8, a, to);
        break;
    }
    case 0xbc:   /* options */
        for (const uint8_t *r = m + 4; r < m + (len / 4) * 4; r += Option_Size((const int32_t *)r) * 4)
            Option_Apply((const int32_t *)r);   /* 0x4b4950 */
        break;
    case 0xbd:
        s->ready ^= (uint16_t)(1u << (m[3] & 0x1f));
        break;
    case 0xbe: {   /* a car's state */
        if (!g_input_stopped) {
            uint32_t me = Net_Live() ? (uint32_t)s->me : 0xfffffffe;
            if (m[1] != me && Net_Live()) {
                NETLST_EACH(&g_net->plist, n) {
                    NetPlayer *p = n->p;
                    if ((uint32_t)p->number != m[1]) continue;
                    if (-1 < p->car) NetCar_SetState(g_humans.c[p->car], m + 4);
                    break;
                }
            }
        }
        int32_t prog;
        memcpy(&prog, m + 8, 4);
        NETLST_EACH(&s->plist, n) if ((uint32_t)((NetPlayer *)n->p)->number == m[1]) ((NetPlayer *)n->p)->progress = prog;
        break;
    }
    case 0xbf:
    case 0xc0: {   /* a block's piece */
        uint32_t me = Net_Live() ? (uint32_t)s->me : 0xfffffffe;
        if (m[1] == me || !Net_Live()) break;
        NETLST_EACH(&g_net->plist, n) {
            NetPlayer *p = n->p;
            if ((uint32_t)p->number != m[1]) continue;
            int16_t id;
            int32_t v;
            memcpy(&id, m + 2, 2);
            memcpy(&v, m + 4, 4);
            if (type == 0xbf) player_adddata(p, id, NULL, v);
            NetData *d = player_data(p, id);
            if (d && d->done < d->size && d->done + len - 8 <= d->size) {
                memcpy(d->data + d->done, m + 8, (size_t)(len - 8));
                d->done += len - 8;
            }
            break;
        }
        break;
    }
    case 0xc1:
    case 0xc2:
        /* (the voice chat's, 0x493240) */
        break;
    case 0xc4:   /* the host's clock */
        s->hostclockat = Net_InGame() ? tickcount() : 0;
        s->hostclock = get32(m + 4);
        break;
    case 0xc5:   /* go */
        if (!s->startwait) {
            s->racing |= (uint8_t)(1u << (m[3] & 0x1f));
        } else {
            s->startwait = 0;
            s->gostart = get32(m + 4);
        }
        break;
    case 0xc6:   /* the session's state */
        s->state = a;
        if (a == 4) {
            s->in = 1;
        } else if (!s->inroom) {
            s->lost = 1;
            s->lostroom = 1;
            Net_Clear();
        }
        break;
    }
}

static int client_event(ConMan *cm, const NetPlayerInfo *who, const ConEvent *e)
{
    (void)who;
    NetSession *s = (NetSession *)cm->user;
    CritSec *cs = *s->cs;
    CS_enter(cs);
    if (s->ending) {
        /* (the original returns without leaving the section, which then stays the CONMAN thread's; the
           port's cooperative threads would wait on it for good) */
        CS_leave(cs);
        return 0;
    }
    int r = 1;
    switch (e->code) {
    case CON_GAME: {
        NetGameRec *g = NULL;
        NETLST_EACH(&s->found, n) {
            if (!memcmp(&((NetGameRec *)n->p)->addr, &e->u.game.addr, sizeof(NetAddr))) {
                g = n->p;
                game_set(g, (const uint8_t *)&e->u.game);
                break;
            }
        }
        if (!g) {
            s->gameid = (s->gameid + 1) & 0xffffff;
            g = game_new((const uint8_t *)&e->u.game, s->gameid);
            NetLst_push(&s->found, g);
        }
        g->pinging = 0;
        g->ping = tickcount() - s->rtt;   /* (+0x148: the find's start here) */
        break;
    }
    case CON_FOUND:
        NETLST_EACH(&s->found, n) {
            NetGameRec *g = n->p;
            if (!g->stale) {
                g->stale = 1;
            } else {
                NetLst_remove(&s->found, g);
                game_free(g);
            }
        }
        if (!(Net_Live() && g_net->in) && s->client) {
            NetClient_Find(s->client);
            s->rtt = tickcount();
        }
        break;
    case CON_IN:
        s->client->in = true;
        break;
    case CON_NOTIN:
        s->state = 5;
        s->me = -1;
        break;
    case CON_LOST:
    case CON_TIMEDOUT:
        s->state = 5;
        s->lost = 1;
        Net_Clear();
        break;
    case CON_PACKET:
        got_packet(s, e->u.data, e->len);
        break;
    default:
        r = 0;
        break;
    }
    CS_leave(cs);
    return r;
}

/* ================================================================ the self-test */

/* --param thrash_test=9: with --param nethost=1 this instance hosts a game (its server and its own
   player over the memory link), the other joins it by the relay's address of the first (10.0.0.1); they
   chat, the host starts the race, both send controls through the room's ring and leave. */
static int st_chats;
static void st_chat(const char *text, int32_t from, int32_t to)
{
    char m[160];
    snprintf(m, sizeof m, "net: chat from %d to %d: %s", from, to, text);
    plat_log(m);
    st_chats++;
}

static bool st_wait(bool (*cond)(void), int ms)
{
    for (int t = 0; t < ms; t += 20) {
        if (cond()) return true;
        THREAD_sleep(20);
    }
    return cond();
}

static bool st_ingame(void) { return Net_InGame(); }
static bool st_two(void) { return Net_InGame() && g_net->nplayers >= 2; }
static bool st_racing(void) { return Net_InGame() && !g_net->raceover && (g_net->racing & 3) == 3; }
static bool st_none(void) { return false; }
static bool st_go(void) { return Net_InGame() && !g_net->startwait; }
static bool st_ctl(void) { return Net_InGame() && g_net->ctlin >= 40; }

static void st_thread(void *arg)
{
    (void)arg;
    char v[8], m[160];
    bool hosting = plat_param("nethost", v, sizeof v) && v[0] == '1';
    if (!g_net && !Net_Construct()) {
        plat_log("net: no session (no provider)");
        return;
    }
    g_net->mode = 1;
    g_net->chat = st_chat;
    if (hosting) Net_HostGame(NULL, 0);
    else Net_JoinGame(1, 0, "10.0.0.1");
    bool in = st_wait(st_ingame, 15000);
    snprintf(m, sizeof m, "net: %s, in %d, me %d, state %d, name '%s'", hosting ? "hosting" : "joined", in, g_net->me,
             g_net->state, g_net->joined);
    plat_log(m);
    if (!in) return;
    bool two = st_wait(st_two, 15000);
    snprintf(m, sizeof m, "net: players %d (mask %02x)", g_net->nplayers, g_net->players);
    plat_log(m);
    if (!two) return;
    NETLST_EACH(&g_net->plist, n) ((NetPlayer *)n->p)->car = ((NetPlayer *)n->p)->number;
    THREAD_sleep(300);
    Net_Chat(hosting ? "hello from the host" : "hello from the guest", -1);
    THREAD_sleep(500);
    Net_SendStart();   /* (each player's race start: the room's first starts it) */
    bool racing = st_wait(st_racing, 5000);
    st_wait(st_none, 500);
    snprintf(m, sizeof m, "net: racing %d (mask %02x)", racing, g_net->racing);
    plat_log(m);
    Net_SendGo();
    bool go = st_wait(st_go, 5000);
    for (int i = 0; i < 400 && go && !st_ctl(); i++) {
        g_input_packet[0] = (uint8_t)(i & 1);
        g_input_packet[1] = (uint8_t)(i * 3);
        g_input_packet[2] = 0x40;
        g_input_packet[3] = 0;
        Net_Controls();
        THREAD_sleep(16);
    }
    snprintf(m, sizeof m, "net: go %d, controls in %d out %d, chats %d", go, g_net->ctlin, g_net->ctlout, st_chats);
    plat_log(m);
    Net_RaceDone();
    THREAD_sleep(hosting ? 1500 : 500);
    Net_Destroy();
    plat_log("net: done");
}

void Net_Selftest(void)
{
    static ThreadRec rec;
    static bool started;
    if (!started) {
        started = true;
        if (!THREAD_createarg(st_thread, NULL, 0x8000, 0, 0, &rec)) plat_log("net: no threads");
    }
}
