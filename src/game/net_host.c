/* The game's network server (see net.h): CConHost over CONMAN's host, its players, rooms and the
   messages it takes (NetServer_Process 0x490fc0). Its log (0x492ca0: the server's channel 0x184, -1 in
   the game) and the results page (Net_WriteResultsHtml 0x492b10, the same channel) write nothing here. */
#include "game/net.h"
#include "exe.h"
#include "pending.h"
#include "real/systask.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

NetSrv *g_net_server;
static CritSec **s_cs;       /* 0x628c78 */
static bool s_stopping;      /* 0x5d07d0 */

static int32_t tickcount(void) { return (int32_t)((uint64_t)TIMER_count * 1000 / TIMER_RATE); }
static int32_t get24(const uint8_t *p) { return (int32_t)(((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8)) >> 8; }
static int32_t get32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]); }
#define LOBBY_PORT ((int)exe_u32(0x5b4990))
#define BIT(n) ((uint8_t)(1u << ((n) & 0x1f)))

static void server_log(NetHostPlayer *p, const char *text) { (void)p; (void)text; }   /* 0x492ca0 */

static void racing_cb(void);
static void server_cb(void);

/* ================================================================ a server's player */

static NetHostPlayer *hp_new(const NetPlayerInfo *who)   /* 0x48a4a0 */
{
    NetHostPlayer *p = calloc(1, sizeof *p);
    p->who = who;
    p->ring = 0xffffffff;
    p->number = -1;
    p->idle = 1;
    NetLst_init(&p->queue);
    NetLst_init(&p->kept);
    return p;
}

static void hp_free(NetHostPlayer *p)   /* 0x48a6d0 */
{
    NETLST_EACH(&p->kept, n) free(n->p);
    NetLst_clear(&p->kept);
    NETLST_EACH(&p->queue, n) free(n->p);
    NetLst_clear(&p->queue);
    free(p);
}

/* Its hello: the extra text "<name>~<protocol>~<version>"; another protocol than "2.8" is refused
   ("Wrong Version", 10), else "Connection Ok" (4). 0x48a630 */
static void hp_hello(NetHostPlayer *p)
{
    p->hello = 1;
    if (p->who) sscanf(p->who->extra, "%31[^~]~%31[^~]~%31s", p->name, p->protocol, p->version);   /* (0x5d0618) */
    if (strcmp(p->protocol, exe_str(0x5d06fc))) {
        p->status = 10;
        p->refused = 1;
        p->ready = 1;
        return;
    }
    p->status = 4;
    p->ready = 1;
}

/* A reliable packet to it, one a tick: 1 if it went now (and the player is to be idle again next tick). 0x48a300 */
static int hp_queue(NetHostPlayer *p, const void *data, int len, ConServer *cm)
{
    NetPkt *k = calloc(1, sizeof *k);
    k->len = len < (int)sizeof k->data ? len : (int)sizeof k->data;
    memcpy(k->data, data, (size_t)k->len);
    NetLst_push(&p->queue, k);
    int n = ++p->queued;
    if (!p->idle) {
        if (n == 1) return 0;
    } else {
        if (cm) {
            NetPkt *h = NetLst_front(&p->queue);
            if (CONMAN_sendto(cm, p->who, h->data, (uint32_t)h->len)) {
                free(h);
                NetLst_popfront(&p->queue);
                p->queued--;
            }
        }
        p->idle = 0;
        if (p->queued == 0) return 1;
    }
    return p->queued;
}

static int hp_flush(NetHostPlayer *p, ConServer *cm)   /* 0x48a410 */
{
    if (!NetLst_empty(&p->queue) && p->hello && cm) {
        NetPkt *h = NetLst_front(&p->queue);
        if (CONMAN_sendto(cm, p->who, h->data, (uint32_t)h->len)) {
            free(h);
            NetLst_popfront(&p->queue);
            p->queued--;
        }
        p->idle = 0;
    }
    if (p->queued == 0) p->idle = 1;
    return p->queued;
}

/* The latest of each of its option packets (by the first option's id), for the players who come later. 0x48a220 */
static void hp_keep(NetHostPlayer *p, const uint8_t *data, int len)
{
    int32_t key;
    memcpy(&key, data + 4, 4);
    NETLST_EACH(&p->kept, n) {
        NetPkt *k = n->p;
        int32_t kk;
        memcpy(&kk, k->data + 4, 4);
        if (kk == key) {
            NetLst_remove(&p->kept, k);
            free(k);
        }
    }
    NetPkt *k = calloc(1, sizeof *k);
    k->len = len < (int)sizeof k->data ? len : (int)sizeof k->data;
    memcpy(k->data, data, (size_t)k->len);
    NetLst_push(&p->kept, k);
}

static void hp_sendkept(NetHostPlayer *p, NetSender *s, NetHostPlayer *to)   /* 0x48a2c0 */
{
    NETLST_EACH(&p->kept, n) {
        NetPkt *k = n->p;
        s->wrap->send(s->wrap, to, k->data, k->len);
    }
}

/* ================================================================ CConHost */

static void host_send(NetWrap *w, void *to, const void *data, int len)   /* CConHost_v0 0x48b270 */
{
    NetHost *h = (NetHost *)w;
    NetHostPlayer *p = to;
    if (!p->who) return;
    CS_enter(h->cs);
    if (hp_queue(p, data, len, h->hosting ? h->cm : NULL) == 1) NetLst_push(&h->pending, p);
    CS_leave(h->cs);
}

static NetHost *host_new(uintptr_t provider, ConEventFn event, ConEventFn lobbyevent, NetSrv *server,
                         const NetGameInfo *info, const char *lobby, int port)   /* NetLobby_Connect 0x48aeb0 */
{
    NetHost *h = calloc(1, sizeof *h);
    h->cs = CS_create();
    h->base.send = host_send;
    NetLst_init(&h->pending);
    h->lobbyevent = lobbyevent;
    h->provider = provider;
    h->server = server;
    NetGameInfo gi;
    memset(&gi, 0, sizeof gi);
    if (!info) snprintf(gi.name, sizeof gi.name, "%s", exe_str(0x5d06b4));   /* "NfsNet" */
    else memcpy(&gi, info, sizeof gi);
    if (lobbyevent) port = LOBBY_PORT;
    h->cm = CONMAN_host(event, provider, exe_data(0x5b4980, 16), &gi, (intptr_t)server, port, NULL);
    if (!h->cm) h->cm = CONMAN_host(event, provider, exe_data(0x5b4980, 16), &gi, (intptr_t)server, 0, NULL);
    h->hosting = 1;
    if (lobbyevent) {
        NetAddr me, at;
        char text[64];
        uint32_t myport = (uint32_t)port;
        CONMAN_localaddr(&h->cm->cm, &me);   /* 0x55d3b0 */
        CONMAN_totext(&h->cm->cm, &me, text, &myport);
        snprintf(h->lobbyaddr, sizeof h->lobbyaddr, "%s", lobby ? lobby : exe_str(0x5d06e0));   /* porsche-entry.earacing.com */
        CONMAN_parse(&h->cm->cm, h->lobbyaddr, LOBBY_PORT, &at);
        if (!CONMAN_sameaddr(&h->cm->cm, &me, &at)) h->tolobby = 1;   /* 0x55d440 */
        else h->atlobby = 1;
        snprintf(h->text, sizeof h->text, exe_str(0x5d06a8), h->atlobby ? 'p' : 's', text, (int)myport, gi.name);   /* "%c%s.%d.%s" */
    }
    return h;
}

/* (port, Net_LobbyServer) The name a dedicated server is listed under at the lobby: its player name there
   (the extra text's first part, which the players' server list shows, pings and joins by), empty as the
   original's (whose servers were EA's own program). */
static char s_listas[0x20];
void NetServer_ListAs(const char *address) { snprintf(s_listas, sizeof s_listas, "%s", address); }

static void host_lobby(NetHost *h)   /* 0x48b2f0, 0x48b380 */
{
    h->lobby = NetClient_New(h->provider, h->lobbyevent, (intptr_t)h->server, h->text, s_listas, "", "", 0);
    NetClient_JoinText(h->lobby, h->lobbyaddr, LOBBY_PORT);
    h->lobbysend = calloc(1, sizeof *h->lobbysend);
    h->lobbysend->wrap = &h->lobby->base;
}

static void host_droplobby(NetHost *h)   /* 0x48b0f0 */
{
    if (!h->lobby) return;
    free(h->lobbysend);
    h->lobbysend = NULL;
    NetClient_Delete(h->lobby);
    h->lobby = NULL;
}

static void host_end(NetHost *h)   /* 0x48b240 */
{
    CS_enter(h->cs);
    CONMAN_hostend(h->cm);
    h->hosting = 0;
    CS_leave(h->cs);
}

static void host_delete(NetHost *h)   /* 0x48b180 */
{
    host_droplobby(h);
    NetLst_clear(&h->pending);
    CS_destroy(h->cs);
    free(h);
}

static void host_flush(NetHost *h)   /* 0x48b420 */
{
    CS_enter(h->cs);
    if (h->hosting) {
        if (h->lobby) NetClient_Flush(h->lobby);
        NETLST_EACH(&h->pending, n) {
            NetHostPlayer *p = n->p;
            if (hp_flush(p, h->cm) == 0) NetLst_remove(&h->pending, p);
        }
    }
    CS_leave(h->cs);
}

static void host_unpend(NetHost *h, NetHostPlayer *p)   /* 0x48b4d0 */
{
    CS_enter(h->cs);
    if (0 < p->queued) NetLst_remove(&h->pending, p);
    CS_leave(h->cs);
}

/* The host's own player joins in this process: the memory link of the GUID's name. 0x48b540 */
static void host_call(NetHost *h, NetClient *own)
{
    char name[33];
    memcpy(name, exe_data(0x5d06bc, 33), 33);   /* "16611901C20A11d39B6A00902724AB1E" */
    CONMAN_call(h->cm, name, 0x1000);
    CONMAN_dial(own->cm, name, 0x1000);
}

/* ================================================================ rooms */

static void room_split(NetRoom *r)
{
    r->name[0] = r->version[0] = 0;
    sscanf(r->info.name, "%63[^~]~%31s", r->name, r->version);   /* (0x5d0710) */
}

static NetRoom *room_new(int32_t id, const NetGameInfo *info, NetSrv *s)   /* 0x48b890 */
{
    NetRoom *r = calloc(1, sizeof *r);
    r->server = s;
    r->send.wrap = &s->host->base;
    r->send.players = &r->players;
    r->id = id;
    NetLst_init(&r->players);
    r->out = 0xff;
    memcpy(&r->info, info, sizeof r->info);
    room_split(r);
    return r;
}

static void room_free(NetRoom *r)
{
    NetLst_clear(&r->players);
    free(r);
}

static void room_setinfo(NetRoom *r, const NetGameInfo *info)   /* 0x48b950 */
{
    memcpy(&r->info, info, sizeof r->info);
    room_split(r);
}

/* [0xb4][id][0x14 zeros][info][count, little-endian] (0x48bbf0) */
static void room_info(const NetRoom *r, uint8_t m[0x50])
{
    memset(m, 0, 0x50);
    m[0] = 0xb4;
    m[1] = (uint8_t)(r->id >> 16);
    m[2] = (uint8_t)(r->id >> 8);
    m[3] = (uint8_t)r->id;
    memcpy(m + 0x18, &r->info, 0x34);
    memcpy(m + 0x4c, &r->count, 4);
}

static void room_tell(NetRoom *r, NetHostPlayer *to)   /* 0x48b980 */
{
    uint8_t m[0x50];
    room_info(r, m);
    r->send.wrap->send(r->send.wrap, to, m, sizeof m);
}

static void room_gone(NetRoom *r, NetHostPlayer *to) { Net_Msg(&r->send, 0xb5, r->id, 0, NULL, to); }   /* 0x48ba10 */

static void server_tellroom(NetSrv *s, NetRoom *r)   /* 0x490b20 */
{
    NETLST_EACH(&s->allp, n) room_tell(r, n->p);
}

/* A player into the room: the lowest free number, the room told it, the others' kept options, its number
   (b2), the others (b0, and c5 for those racing), then it to all. NULL full. 0x48ba30 */
static NetRoom *room_add(NetRoom *r, NetHostPlayer *p)
{
    if (7 < r->count) return NULL;
    int n = 0;
    while (r->joined & BIT(n)) n++;
    p->number = n;
    r->out |= BIT(n);
    r->joined |= BIT(n);
    uint8_t m[0x50];
    room_info(r, m);
    r->send.wrap->send(r->send.wrap, p, m, sizeof m);
    NETLST_EACH(&r->players, e) hp_sendkept(e->p, &r->send, p);
    Net_Msg(&r->send, 0xb2, n, NET_NONE, r->name, p);
    NETLST_EACH(&r->players, e) {
        NetHostPlayer *o = e->p;
        Net_Msg(&r->send, 0xb0, o->number, NET_NONE, o->name, p);
        if (!(r->out & BIT(o->number))) Net_Msg(&r->send, 0xc5, o->number, NET_NONE, NULL, p);
    }
    NetLst_push(&r->players, p);
    Net_Msg(&r->send, 0xb0, n, NET_NONE, p->name, NULL);
    r->count++;
    server_tellroom(r->server, r);
    return r;
}

static void room_finish(NetRoom *r, NetHostPlayer *p, int32_t time)   /* 0x48c100 (its best lap not passed on) */
{
    Net_Msg(&r->send, 0xb7, (int8_t)p->number, time, NULL, NULL);
    if (time != -2) return;
    r->out |= BIT(p->number);
    if ((r->joined & r->out) == r->joined) r->racing = 0;
    NETLST_EACH(&r->players, e) hp_sendkept(e->p, &r->send, p);
}

static void room_remove(NetRoom *r, NetHostPlayer *p)   /* 0x48bc80 */
{
    int n = (int8_t)p->number;
    if (r->racing && !(r->out & BIT(n))) room_finish(r, p, -2);
    r->joined &= (uint8_t)~BIT(n);
    Net_Msg(&r->send, 0xb1, n, NET_NONE, NULL, NULL);
    NetLst_remove(&r->players, p);
    p->room = NULL;
    NETLST_EACH(&p->kept, e) free(e->p);   /* (0x48c1e0) */
    NetLst_clear(&p->kept);
    if (p->who) {
        NETLST_EACH(&r->players, e) Net_Msg(&r->send, 0xb1, ((NetHostPlayer *)e->p)->number, NET_NONE, NULL, p);
        Net_Msg(&r->send, 0xb2, -2, NET_NONE, "", p);
    }
    r->count--;
    server_tellroom(r->server, r);
}

static NetHostPlayer *room_find(NetRoom *r, int32_t number)   /* 0x48bdc0 */
{
    NETLST_EACH(&r->players, n) if (((NetHostPlayer *)n->p)->number == number) return n->p;
    return NULL;
}

static void room_start(NetRoom *r, NetHostPlayer *p)   /* 0x48bdf0 */
{
    r->out &= (uint8_t)~BIT(p->number);
    p->ring = 0;
    if (r->racing) return;
    r->racing = 1;
    r->go = 0;
    memset(r->ring, 0, sizeof r->ring);
    r->ringpos = 0;
    r->got = 0;
    r->pingclock = tickcount();
    Net_Msg(&r->send, 0xb3, 0, tickcount(), NULL, NULL);
}

static void room_go(NetRoom *r)   /* 0x48be90 */
{
    if (r->go) return;
    int32_t now = tickcount();
    r->clock16 = now << 4;
    r->clock32 = now << 5;
    Net_Msg(&r->send, 0xc5, 0, now, NULL, NULL);
    r->go = 1;
}

static void server_tolobby(NetSrv *s, NetHostPlayer *p);

static void room_closeall(NetRoom *r)   /* 0x48bee0: the host left, all back to the lobby */
{
    NETLST_EACH(&r->players, n) {
        NetHostPlayer *p = n->p;
        if (p->own) continue;
        server_tolobby(r->server, p);
        Net_Msg(&r->send, 0xc6, 0xc, NET_NONE, NULL, p);
    }
}

/* Options to those in the race as the sender is, or not; kept when it is out of it. 0x48bf30 */
static void room_options(NetRoom *r, const uint8_t *data, int len, NetHostPlayer *from)
{
    bool out = (r->out & BIT(from->number)) != 0;
    NETLST_EACH(&r->players, n) {
        NetHostPlayer *p = n->p;
        if (((r->out & BIT(p->number)) != 0) == out) r->send.wrap->send(r->send.wrap, p, data, len);
    }
    if (out) hp_keep(from, data, len);
}

static void room_relay(NetRoom *r, const uint8_t *data, int len, NetHostPlayer *from)   /* 0x48bfd0 */
{
    uint8_t t = data[0];
    NETLST_EACH(&r->players, n) {
        if (n->p != from || (t != 0xbf && t != 0xc0 && t != 0xc1 && t != 0xc2))
            r->send.wrap->send(r->send.wrap, n->p, data, len);
    }
}

static void room_unreliable(NetRoom *r, const uint8_t *data, int len, NetHostPlayer *from)   /* 0x48c050 */
{
    NetHost *h = (NetHost *)r->send.wrap;
    NETLST_EACH(&r->players, n) {
        NetHostPlayer *p = n->p;
        if (p != from && p->who) CONMAN_sendto_unreliable(h->cm, p->who, data, (uint32_t)len);
    }
}

static void room_ping(NetRoom *r, int32_t number, NetHostPlayer *to)   /* 0x48c0b0 */
{
    NETLST_EACH(&r->players, n) {
        NetHostPlayer *p = n->p;
        if (p->number == number) Net_Msg(&r->send, 0xc3, number, p->rtt, NULL, to);
    }
}

static void room_controls(NetRoom *r, const uint8_t *data, int number)   /* 0x48c190 */
{
    memcpy(r->ring[r->ringpos & 0x7f] + number * 3, data, 3);
    r->got |= BIT(number);
}

/* A racing room each tick: the host's clock to all each second (c4); with all the race's controls in, a
   step at 16 Hz (a player out of the race steps with neutral controls, one 128 steps behind is put back
   in the lobby as lost); at 32 Hz each player's next step. 0x48b590 */
static void room_tick(NetRoom *r)
{
    if (!r->racing || !r->joined) return;
    NetHost *h = (NetHost *)r->send.wrap;
    int32_t now = tickcount();
    if (now != r->pingclock && 0 <= now - r->pingclock) {
        do {
            uint8_t m[8] = {0xc4, 0, 0, 0, (uint8_t)(now >> 24), (uint8_t)(now >> 16), (uint8_t)(now >> 8), (uint8_t)now};
            NETLST_EACH(&r->players, n) {
                NetHostPlayer *p = n->p;
                if (p->who) CONMAN_sendto_unreliable(h->cm, p->who, m, 8);
            }
            r->pingclock += 1000;
        } while (now != r->pingclock && 0 <= now - r->pingclock);
    }
    uint8_t in = (uint8_t)(~r->out & r->joined);
    if (!r->go || (r->got & in) != in) return;
    int32_t t16 = now * 16;
    if (t16 != r->clock16 && 0 <= t16 - r->clock16) {
        do {
            uint8_t *cur = r->ring[r->ringpos & 0x7f];
            r->ringpos++;
            uint8_t *next = r->ring[r->ringpos & 0x7f];
            memcpy(next, cur, 0x18);
            NETLST_EACH(&r->players, n) {
                NetHostPlayer *p = n->p;
                int i = p->number;
                next[i * 3] &= 7;
                if (!(r->out & BIT(i))) {
                    if (((p->ring ^ r->ringpos) & 0x7f) == 0 && p->who) {
                        server_tolobby(r->server, p);
                        Net_Msg(&r->send, 0xc6, 5, NET_NONE, NULL, p);
                    }
                } else {
                    cur[i * 3] = 8;
                    cur[i * 3 + 1] = 0;
                    cur[i * 3 + 2] = 0x1f;
                }
            }
            r->clock16 += 1000;
        } while (t16 != r->clock16 && 0 <= t16 - r->clock16);
    }
    int32_t t32 = now * 32;
    if (0 < t32 - r->clock32) {
        do {
            NETLST_EACH(&r->players, n) {
                NetHostPlayer *p = n->p;
                if (!p->who || (r->out & BIT(p->number))) continue;
                uint32_t k = p->ring;
                if (k != r->ringpos) {
                    if (!CONMAN_sendto(h->cm, p->who, r->ring[k & 0x7f], 0x18)) break;
                    k++;
                }
                p->ring = k;
            }
            r->clock32 += 1000;
        } while (t32 != r->clock32 && 0 <= t32 - r->clock32);
    }
}

/* ================================================================ the server */

static uint32_t server_newid(NetSrv *s)   /* 0x490aa0 */
{
    bool ok;
    do {
        ok = true;
        s->lastid = (s->lastid + 1) & 0x7fffff;
        NETLST_EACH(&s->allp, n) if (s->lastid == (uint32_t)((NetHostPlayer *)n->p)->number) ok = false;
        NETLST_EACH(&s->rooms, n) if (s->lastid == (uint32_t)((NetRoom *)n->p)->id) ok = false;
        NETLST_EACH(&s->racing, n) if (s->lastid == (uint32_t)((NetRoom *)n->p)->id) ok = false;
    } while (!ok);
    return s->lastid;
}

static void server_counts(NetSrv *s)   /* (b8 to EA's lobby: rooms << 16 | players) */
{
    NetHost *h = s->host;
    if (!h->atlobby && h->lobbysend) Net_Msg(h->lobbysend, 0xb8, 0, s->nrooms * 0x10000 + s->nplayers, NULL, NULL);
}

static void server_join(NetSrv *s, NetHostPlayer *p, NetRoom *r)   /* 0x492480 */
{
    if (p->room) return;
    Net_Msg(s->lobby, 0xb1, p->number, NET_NONE, NULL, NULL);
    NetLst_remove(&s->inlobby, p);
    NETLST_EACH(&s->inlobby, n) Net_Msg(s->lobby, 0xb1, ((NetHostPlayer *)n->p)->number, NET_NONE, NULL, p);
    p->room = room_add(r, p);
    server_log(p, "Joined");
}

static void server_unrace(NetSrv *s, NetRoom *r)   /* a room's race over: back with the rooms */
{
    if (--s->nracing == 0) TIMER_removecallback(racing_cb);
    NetLst_remove(&s->racing, r);
    NetLst_push(&s->rooms, r);
}

static void server_tolobby(NetSrv *s, NetHostPlayer *p)   /* 0x492550 */
{
    NetRoom *r = p->room;
    if (!r) return;
    uint8_t was = r->racing;
    room_remove(r, p);
    if (was && !r->racing) server_unrace(s, r);
    p->number = (int32_t)server_newid(s);
    if (s->own) {
        p->refused = 1;
        return;
    }
    Net_Msg(s->lobby, 0xb0, p->number, NET_NONE, p->name, NULL);
    NetLst_push(&s->inlobby, p);
    NETLST_EACH(&s->inlobby, n) {
        NetHostPlayer *o = n->p;
        Net_Msg(s->lobby, 0xb0, o->number, NET_NONE, o->name, p);
    }
    NETLST_EACH(&s->rooms, n) room_tell(n->p, p);
    NETLST_EACH(&s->racing, n) room_tell(n->p, p);
}

static void server_drop(NetSrv *s, NetHostPlayer *p)   /* 0x492720 */
{
    host_unpend(s->host, p);
    if (!p->isserver) {
        NetLst_remove(&s->allp, p);
        if (!p->refused) {
            s->nplayers--;
            server_counts(s);
            NetRoom *r = p->room;
            if (!r) {
                NetLst_remove(&s->inlobby, p);
                Net_Msg(s->lobby, 0xb1, p->number, NET_NONE, NULL, NULL);
            } else {
                uint8_t was = r->racing;
                room_remove(r, p);
                if (was && !r->racing) server_unrace(s, r);
            }
        }
    } else {
        NetLst_remove(&s->serverlist, p);
        Net_Msg(s->servers, 0xb9, p->number, NET_NONE, NULL, NULL);
    }
    hp_free(p);
}

static void server_closeroom(NetSrv *s, NetRoom *r, NetLst *from)   /* 0x492240, 0x492350 */
{
    if (from == &s->racing && --s->nracing == 0) TIMER_removecallback(racing_cb);
    NetLst_remove(from, r);
    s->nrooms--;
    server_counts(s);
    NETLST_EACH(&s->allp, n) room_gone(r, n->p);
    room_free(r);
}

static NetRoom *server_newroom(NetSrv *s, const NetGameInfo *info)
{
    NetRoom *r = room_new((int32_t)server_newid(s), info, s);
    NetLst_push(&s->rooms, r);
    s->nrooms++;
    server_counts(s);
    server_tellroom(s, r);
    return r;
}

/* Each tick (Net_StatusText 0x490770): a lobby server's empty rooms closed, the gone ones dropped, the
   players whose hello came told the lobby (and it them) or their refusal. */
static void server_status(NetSrv *s)
{
    if (!s->own) {
        NETLST_EACH(&s->rooms, n) if (((NetRoom *)n->p)->count == 0) server_closeroom(s, n->p, &s->rooms);
        NETLST_EACH(&s->racing, n) if (((NetRoom *)n->p)->count == 0) server_closeroom(s, n->p, &s->racing);
    }
    NETLST_EACH(&s->serverlist, n) if (!((NetHostPlayer *)n->p)->who) server_drop(s, n->p);
    NETLST_EACH(&s->allp, n) {
        NetHostPlayer *p = n->p;
        if (!p->ready) continue;
        if (!p->who) {
            server_drop(s, p);
            continue;
        }
        if (!p->hello || p->announced) continue;
        p->announced = 1;
        if (!p->refused) {
            s->nplayers++;
            server_counts(s);
            Net_Msg(s->lobby, 0xb0, p->number, NET_NONE, p->name, NULL);
            NetLst_push(&s->inlobby, p);
            NETLST_EACH(&s->rooms, e) room_tell(e->p, p);
            NETLST_EACH(&s->racing, e) room_tell(e->p, p);
            NETLST_EACH(&s->inlobby, e) {
                NetHostPlayer *o = e->p;
                Net_Msg(s->lobby, 0xb0, o->number, NET_NONE, o->name, p);
            }
            NETLST_EACH(&s->games, e) {
                NetGameRec *g = e->p;
                Net_Msg(s->all, 0xb8, g->id, g->players | g->info.i[0], g->name, p);
            }
        }
        Net_Msg(s->all, 0xc6, p->status, NET_NONE, NULL, p);
        server_log(p, "status");
    }
}

/* A room's version against the player's: 4 in, 10 another version, 11 full. */
static int server_tryroom(NetSrv *s, NetHostPlayer *p, NetRoom *r)
{
    if (strcmp(p->version, r->version)) {
        p->refused = 1;
        return 10;
    }
    if (r->count == 8) {
        p->refused = 1;
        return 0xb;
    }
    server_join(s, p, r);
    return 4;
}

static int server_event(ConMan *cm, const NetPlayerInfo *who, const ConEvent *e)   /* NetServer_Process 0x490fc0 */
{
    NetSrv *s = (NetSrv *)cm->user;
    if (s_stopping || !s_cs) return 0;
    CritSec *cs = *s_cs;
    CS_enter(cs);
    int r = 1;
    NetHostPlayer *p = NULL;
    if (s_stopping) goto out0;
    if (e->code == CON_ASKS) {
        bool asserver = who->name[0] == 's';
        if (!asserver && s->host->atlobby && NetLst_empty(&s->serverlist)) asserver = true;
        if (asserver) {
            if (NetLst_empty(&s->serverlist) && who->name[0] != 'p') goto out0;
            p = hp_new(who);
            *CONMAN_user(who) = p;
            p->isserver = 1;
            p->number = (int32_t)server_newid(s);
            NetLst_push(&s->serverlist, p);
        } else {
            p = hp_new(who);
            *CONMAN_user(who) = p;
            if (s->own && NetLst_empty(&s->allp)) p->own = 1;
            p->number = (int32_t)server_newid(s);
            NetLst_push(&s->allp, p);
        }
    } else {
        p = who ? *CONMAN_user(who) : NULL;
    }
    if (!p || !p->who) goto out0;
    NetRoom *room = p->room;
    switch (e->code) {
    case CON_ASKS:
        break;
    case CON_JOINED: {
        hp_hello(p);
        if (!s->own) {
            if (p->isserver) {
                NETLST_EACH(&s->games, n) {
                    NetGameRec *g = n->p;
                    Net_Msg(s->all, 0xb8, g->id, g->players | g->info.i[0], g->name, p);
                }
                Net_Msg(s->servers, 0xb8, p->number, 0, p->name, NULL);
                p->announced = 1;
            }
            break;
        }
        int code;
        if (p->own) {
            NETLST_EACH(&s->inlobby, n) {
                NetHostPlayer *o = n->p;
                Net_Msg(s->lobby, 0xb0, o->number, NET_NONE, o->name, p);
            }
            NetRoom *nr = server_newroom(s, &s->info);
            if (nr) server_join(s, p, nr);
            p->announced = 1;
            Net_Msg(s->all, 0xc6, 4, NET_NONE, NULL, p);
            break;
        }
        NetLst *l = !NetLst_empty(&s->rooms) ? &s->rooms : !NetLst_empty(&s->racing) ? &s->racing : NULL;
        if (!l) {
            r = 0;
            break;
        }
        NETLST_EACH(&s->inlobby, n) {
            NetHostPlayer *o = n->p;
            Net_Msg(s->lobby, 0xb0, o->number, NET_NONE, o->name, p);
        }
        code = server_tryroom(s, p, NetLst_front(l));
        Net_Msg(s->all, 0xc6, code, NET_NONE, NULL, p);
        p->announced = 1;
        break;
    }
    case CON_DROPPED:
    case CON_LOST:
    case CON_TIMEDOUT:
        server_log(p, e->code == CON_LOST ? "Left Server" : exe_str(0x5d08d0));   /* "Lost" */
        p->who = NULL;   /* 0x48a7f0 */
        break;
    case CON_PACKET: {
        const uint8_t *m = e->u.data;
        int len = e->len;
        if (m[0] < 0xb0) {
            if (room) room_controls(room, m, p->number);   /* 0x48a1f0 */
            break;
        }
        int32_t a = 3 < len ? get24(m + 1) : 0;
        bool ok = p->announced && !p->refused;
        switch (m[0]) {
        case 0xb1:   /* the host puts a player out (itself: all) */
            if (ok && room) {
                NetHostPlayer *o = room_find(room, a);
                if (!o) break;
                if (!o->own) {
                    Net_Msg(s->all, 0xc6, 9, NET_NONE, NULL, o);
                    server_tolobby(s, o);
                    server_log(p, "Ejected by host");
                } else {
                    room_closeall(room);
                    server_log(p, "Host shutting down");
                }
            }
            break;
        case 0xb3:   /* the race starts */
            if (ok && room) {
                server_log(p, "Start Race");
                if (!room->racing) {
                    room_start(room, p);
                    NetLst_remove(&s->rooms, room);
                    NetLst_push(&s->racing, room);
                    if (s->nracing++ == 0) TIMER_addcallback(racing_cb);
                } else {
                    room_start(room, p);
                }
            }
            break;
        case 0xb4:   /* a game's info: a new room (a lobby server's), else the room's anew */
            if (!ok) break;
            if (!room) {
                if (!s->own) {
                    NetRoom *nr = server_newroom(s, (const NetGameInfo *)(m + 0x18));
                    if (nr) server_join(s, p, nr);
                }
            } else {
                if (s->own) CONMAN_setinfo(s->host->cm, (const NetGameInfo *)(m + 0x18));
                room_setinfo(room, (const NetGameInfo *)(m + 0x18));
                NETLST_EACH(&s->allp, n) room_tell(room, n->p);
            }
            break;
        case 0xb6:   /* into a room by its id (one waiting, then one racing) */
            if (!ok) break;
            NETLST_EACH(&s->rooms, n) {
                NetRoom *rr = n->p;
                if (rr->id != a) continue;
                int code = server_tryroom(s, p, rr);
                if (code != 4) Net_Msg(s->all, 0xc6, code, NET_NONE, NULL, p);
                break;
            }
            NETLST_EACH(&s->racing, n) {
                NetRoom *rr = n->p;
                if (rr->id != a) continue;
                int code = server_tryroom(s, p, rr);
                if (code != 4) Net_Msg(s->all, 0xc6, code, NET_NONE, NULL, p);
                break;
            }
            break;
        case 0xb7:   /* a finish ("Returning to frontend" -2, "Leaving game" -1, a time) */
            if (room) {
                server_log(p, "finish");   /* (Net_RaceMessages 0x492960) */
                uint8_t was = room->racing;
                room_finish(room, p, a);
                if (was && !room->racing) server_unrace(s, room);
            }
            break;
        case 0xb8:
            Net_Msg(s->servers, 0xb8, p->number, get32(m + 4), p->name, NULL);
            break;
        case 0xba:   /* back to the lobby */
            if (ok) {
                server_log(p, "Leaving");
                server_tolobby(s, p);
            }
            break;
        case 0xbb: {   /* chat: [from][to][text], to its player (or all) and back to the sender */
            if (!ok) break;
            const char *text = (const char *)m + 4;
            server_log(p, text);
            NetSender *via = room ? &room->send : s->lobby;
            NetLst *l = room ? &room->players : &s->inlobby;
            NetHostPlayer *to = NULL, *back = NULL;
            NETLST_EACH(l, n) {
                if (((NetHostPlayer *)n->p)->number == a) to = n->p;   /* (0x48c8b0) */
                if (((NetHostPlayer *)n->p)->number == p->number) back = n->p;
            }
            Net_Msg(via, 0xbb, p->number, a, text, to);
            if (a != -1 && a != p->number) Net_Msg(via, 0xbb, p->number, a, text, room ? back : p);
            break;
        }
        case 0xbc:
            if (room) room_options(room, m, len, p);
            break;
        case 0xbd:
        case 0xbf:
        case 0xc0:
        case 0xc1:
        case 0xc2:
            if (room) room_relay(room, m, len, p);
            break;
        case 0xbe:
            if (room) room_unreliable(room, m, len, p);
            break;
        case 0xc3:   /* a ping: its round trip kept, the asked player's back */
            p->rtt = get32(m + 4);
            if (!room) {
                NETLST_EACH(&s->inlobby, n) {
                    NetHostPlayer *o = n->p;
                    if (o->number == a) Net_Msg(s->lobby, 0xc3, a, o->rtt, NULL, p);
                }
            } else {
                room_ping(room, a, p);
            }
            break;
        case 0xc5:
            if (room) room_go(room);
            break;
        }
        break;
    }
    default:
        r = 0;
        break;
    }
    CS_leave(cs);
    return r;
out0:
    CS_leave(cs);
    return 0;
}

/* The lobby guest's events (a lobby server's, 0x490dd0): in, out, lost, and the lobby's servers' games
   passed on to all (b8, b9). */
static int lobby_event(ConMan *cm, const NetPlayerInfo *who, const ConEvent *e)
{
    (void)who;
    NetSrv *s = (NetSrv *)cm->user;
    if (s_stopping || !s_cs) return 0;
    CS_enter(*s_cs);
    int r = 1;
    switch (e->code) {
    case CON_IN: {   /* 0x48b140 */
        NetHost *h = s->host;
        CS_enter(h->cs);
        if (!h->tolobby) {
            Net_Msg(h->lobbysend, 0xb8, 0, 0, NULL, NULL);
            h->lobby->in = true;
        }
        CS_leave(h->cs);
        s->lobbyup = 1;
        break;
    }
    case CON_NOTIN:
        s->lobbyup = 0;
        s->relobby = 1;
        break;
    case CON_LOST:
    case CON_TIMEDOUT:
        s->lobbyup = 0;
        NETLST_EACH(&s->games, n) free(n->p);
        NetLst_clear(&s->games);
        s->relobby = 1;
        break;
    case CON_PACKET: {
        const uint8_t *m = e->u.data;
        int32_t a = get24(m + 1);
        if (m[0] == 0xb8) {
            if (a) Net_Msg(s->all, 0xb8, a, get32(m + 4), (const char *)m + 8, NULL);
        } else if (m[0] == 0xb9) {
            Net_Msg(s->all, 0xb9, a, NET_NONE, NULL, NULL);
        } else {
            break;
        }
        Net_GameList(&s->games, m, e->len);
        break;
    }
    default:
        r = 0;
        break;
    }
    CS_leave(*s_cs);
    return r;
}

static void racing_cb(void)   /* 0x4905f0 */
{
    NetSrv *s = g_net_server;
    if (!s || !s_cs) return;
    CS_enter(*s_cs);
    if (!s_stopping) NETLST_EACH(&s->racing, n) room_tick(n->p);
    CS_leave(*s_cs);
}

static void server_cb(void)   /* 0x490660 */
{
    NetSrv *s = g_net_server;
    if (!s || !s_cs) return;
    CS_enter(*s_cs);
    if (!s_stopping && s->host) {
        NetHost *h = s->host;
        if (h->tolobby) {   /* 0x48b380 */
            if (!h->lobby && h->lobbyevent) host_lobby(h);
            h->tolobby = 0;
        }
        if (s->relobby) {
            if (s->relobbyticks == 0) host_droplobby(h);
            if (TIMER_RATE * 10 < ++s->relobbyticks) {
                s->relobbyticks = 0;
                h->tolobby = 1;   /* 0x48b130 */
                s->relobby = 0;
            }
        }
        int32_t now = tickcount();
        while (0 < now * 32 - s->clock) {
            host_flush(h);
            s->clock += 1000;
        }
        server_status(s);
    }
    CS_leave(*s_cs);
}

NetSrv *NetServer_New(uintptr_t provider, const NetGameInfo *info, NetClient *own, const char *lobby, int port, int channel)
{
    NetSrv *s = calloc(1, sizeof *s);
    s->own = own;
    s->lastid = 0xffffffff;
    NetLst_init(&s->allp);
    NetLst_init(&s->inlobby);
    NetLst_init(&s->serverlist);
    NetLst_init(&s->games);
    NetLst_init(&s->rooms);
    NetLst_init(&s->racing);
    s->channel = channel;
    if (!s_cs) {
        s_cs = malloc(sizeof *s_cs);
        *s_cs = CS_create();
    }
    CritSec **cs = s_cs;
    CS_enter(*cs);
    s_stopping = false;
    memcpy(&s->info, info, sizeof s->info);
    s->host = host_new(provider, server_event, own ? NULL : lobby_event, s, &s->info, lobby, port);
    s->hosting = 1;
    s->all = calloc(1, sizeof *s->all);
    s->all->wrap = &s->host->base;
    s->all->players = &s->allp;
    s->lobby = calloc(1, sizeof *s->lobby);
    s->lobby->wrap = &s->host->base;
    s->lobby->players = &s->inlobby;
    s->servers = calloc(1, sizeof *s->servers);
    s->servers->wrap = &s->host->base;
    s->servers->players = &s->serverlist;
    if (provider == 1) {
        PENDING_NOTE(0x55e920, "the server's serial port's name (a modem game)");
    } else {
        NetAddr a;
        uint32_t p = 0;
        CONMAN_localaddr(&s->host->cm->cm, &a);
        CONMAN_totext(&s->host->cm->cm, &a, s->address, &p);
        s->port = (int32_t)p;
    }
    if (s->address[1] == 'x') s->address[0] = 0;
    s->clock = tickcount() << 5;
    TIMER_addcallback(server_cb);
    g_net_server = s;
    /* (Net_WriteResultsHtml 0x492b10: to the channel, -1 here) */
    CS_leave(*cs);
    if (!own) {
        if (s->host->atlobby) host_lobby(s->host);   /* 0x48b2f0 */
    } else {
        host_call(s->host, own);
    }
    return s;
}

void NetServer_Stop(void)
{
    s_stopping = true;
    if (g_net_server && g_net_server->hosting) {
        g_net_server->hosting = 0;
        host_end(g_net_server->host);
    }
}

void NetServer_Delete(NetSrv *s)
{
    s_stopping = true;
    CS_enter(*s_cs);
    s->relobby = 0;
    TIMER_removecallback(server_cb);
    if (s->hosting) {
        host_end(s->host);
        s->hosting = 0;
    }
    while (!NetLst_empty(&s->allp)) server_drop(s, NetLst_back(&s->allp));
    while (!NetLst_empty(&s->serverlist)) server_drop(s, NetLst_back(&s->serverlist));
    NETLST_EACH(&s->games, n) free(n->p);
    NetLst_clear(&s->games);
    free(s->servers);
    free(s->lobby);
    free(s->all);
    host_delete(s->host);
    CritSec **cs = s_cs;
    s_cs = NULL;
    CS_leave(*cs);
    CS_destroy(*cs);
    free(cs);
    NETLST_EACH(&s->racing, n) room_free(n->p);
    NetLst_clear(&s->racing);
    NETLST_EACH(&s->rooms, n) room_free(n->p);
    NetLst_clear(&s->rooms);
    NetLst_clear(&s->inlobby);
    if (g_net_server == s) g_net_server = NULL;
    free(s);
}
