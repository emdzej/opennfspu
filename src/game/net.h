/* The game's network session (d:\Nfs\Nfs5\game, 0x48a000..0x493800): over CONMAN (src/real/conman.c) the
   players' lobby, its games (rooms) and their races.

   A session object (Net_Construct 0x48fbe0, 0x268 bytes at 0x628c70) holds this machine's client: a guest
   of a server, by the network (UDP; IPX is absent on gasm) or a modem (pending), or the in-process guest
   of the server this machine runs (Net_HostGame: provider 3, the memory link). The server (0x491c80, 0x188
   bytes at 0x628c74) keeps the players and their rooms (games of up to 8, 0x48b890); a hosted game is one
   room the host's own player opens. A server without a player of this machine's is a lobby server that
   reports to EA's lobby (porsche-entry.earacing.com, long gone).

   The messages are [type][a: 3 bytes][b: 4 bytes][text], big-endian (0x48c7a0), types 0xb0..0xc6; a first
   byte below 0xb0 is the 8 cars' controls, 3 bytes each and two steps a packet (the host's ring at 16 Hz,
   0x48b590). The original's std::lists of pointers are NetList here (a sentinel node and the items'
   pointers), its CritSecs the real library's. */
#pragma once
#include "real/conman.h"
#include "real/thread.h"
#include <stdbool.h>
#include <stdint.h>

/* std::list<T *> */
typedef struct NetLNode {
    struct NetLNode *next, *prev;
    void *p;
} NetLNode;
typedef struct {
    NetLNode head;
} NetLst;

void NetLst_init(NetLst *l);
void NetLst_push(NetLst *l, void *p);     /* at the back */
void NetLst_remove(NetLst *l, void *p);   /* every node of it (std::list::remove) */
void NetLst_popfront(NetLst *l);
void NetLst_clear(NetLst *l);
static inline bool NetLst_empty(const NetLst *l) { return l->head.next == &l->head; }
static inline void *NetLst_front(const NetLst *l) { return l->head.next->p; }
static inline void *NetLst_back(const NetLst *l) { return l->head.prev->p; }
/* (n's next taken before the body: the body may remove n) */
#define NETLST_EACH(l, n) \
    for (NetLNode *n = (l)->head.next, *n##_nx = n->next; n != &(l)->head; n = n##_nx, n##_nx = n->next)

/* A wrapper of a CONMAN end (vtable 0x5b4970 and its classes): slot 0 sends a reliable packet to a player
   (the host's: its record; the guest's: NULL, the host). */
typedef struct NetWrap NetWrap;
struct NetWrap {
    void (*send)(NetWrap *w, void *to, const void *data, int len);
};

/* What a message goes through (8 bytes): the wrapper and the players it goes to when to none (NULL: to
   the host). */
typedef struct {
    NetWrap *wrap;
    NetLst *players;   /* of NetHostPlayer */
} NetSender;

#define NET_NONE ((int32_t)0x80000000)
/* [type][a][b][text] to a player or to all (to NULL). 0x48c7a0 */
void Net_Msg(NetSender *s, uint8_t type, int32_t a, int32_t b, const char *text, void *to);

/* A packet waiting for its turn (0xfc bytes: the list node, then the length and the data). */
typedef struct {
    int32_t len;
    uint8_t data[0xf0];
} NetPkt;

/* The guest (CConClient, 0x14 bytes, 0x48a940): one reliable packet a tick (the 32 Hz flush 0x48ad60). */
typedef struct {
    NetWrap base;
    ConClient *cm;      /* +0x04 */
    CritSec *cs;        /* +0x08 */
    NetLst queue;       /* +0x0c: NetPkt */
    bool idle;          /* +0x10: nothing sent this tick (the next goes at once) */
    bool in;            /* +0x11: the host let it in */
} NetClient;

/* A game in the lists (0xbc bytes: 0x48c2b0 from CONMAN's find, 0x48c240 from the server's lists). */
typedef struct {
    NetAddr addr;           /* +0x00 */
    NetGameInfo info;       /* +0x14 */
    int32_t players;        /* +0x48 */
    char name[0x40];        /* +0x4c: the info's name to its '~' */
    char version[0x20];     /* +0x8c: after it */
    int32_t pinged;         /* +0xac: the ticks of the last ping */
    int32_t id;             /* +0xb0 */
    int32_t ping;           /* +0xb4: ms, -1 none */
    uint8_t stale;          /* +0xb8: not found again since the last find */
    uint8_t pinging;        /* +0xb9 */
} NetGameRec;

/* A received block (CDataPacket, 0x10 bytes). */
typedef struct {
    int16_t id;
    uint8_t *data;    /* +0x04 */
    int32_t size;     /* +0x08 */
    int32_t done;     /* +0x0c: sent or received so far */
} NetData;

/* A player of the session's (0x60 bytes, from 0xb0). */
typedef struct {
    char name[0x40];
    int32_t number;     /* +0x40 */
    int32_t pinged;     /* +0x44: GetTickCount of the last ping */
    int32_t progress;   /* +0x48 */
    int32_t car;        /* +0x4c: the race's car, -1 none */
    int32_t finish;     /* +0x50: -1 racing */
    int32_t ping;       /* +0x54: the round trip it reported */
    uint8_t pinging;    /* +0x58 */
    NetLst data;        /* +0x5c: NetData */
} NetPlayer;

/* The session (0x268 bytes). */
typedef struct NetSession {
    const uint8_t *ipx, *udp;   /* +0x00, +0x04: the providers' GUIDs, NULL without */
    NetClient *client;          /* +0x08 */
    NetSender *sender;          /* +0x0c: {client, NULL} */
    CritSec **cs;               /* +0x10 */
    void *voice;                /* +0x14: (pending) */
    void *unused18;             /* +0x18 */
    int32_t sendclock;          /* +0x1c: GetTickCount * 32, 1000 a flush */
    void (*chat)(const char *text, int32_t from, int32_t to);   /* +0x20 */
    int32_t state;              /* +0x24: 0..5 (c6's 4 in) */
    int32_t mode;               /* +0x28: 0 IPX, 1 the network, 2 a modem */
    char version[6];            /* +0x2c: the game's ("0.0", version.txt) */
    char version2[6];           /* +0x32 */
    char joined[0x20];          /* +0x38: the session's name the host gave */
    char name[0x40];            /* +0x58: "Player" */
    char password[0x10];        /* +0x98 */
    char nick[0x10];            /* +0xa8: "c" */
    int32_t nplayers;           /* +0xb8 */
    uint8_t raceover;           /* +0xbc: not racing (controls stay local) */
    uint8_t finished;           /* +0xbd: my 0xb7 sent */
    uint8_t in;                 /* +0xbe: in a game (c6 4) */
    uint8_t lost;               /* +0xbf: the connection lost */
    uint8_t lostroom;           /* +0xc0 */
    uint8_t ipxhost;            /* +0xc1: on IPX (the LAN's games found) */
    uint8_t server;             /* +0xc2: this machine runs the server */
    uint8_t ending;             /* +0xc3: no events taken */
    uint8_t startwait;          /* +0xc4: the race's start (c5) to come */
    uint8_t aborted;            /* +0xc5 */
    uint8_t inroom;             /* +0xc6 */
    uint8_t pinger;             /* +0xc7: the ICMP pinger runs */
    int32_t me;                 /* +0xdc: my number, -1 refused, -2 none */
    int32_t frames;             /* +0xe0: ticks since the loss */
    uint8_t players;            /* +0xe4: the joined players' mask */
    uint8_t racing;             /* +0xe5: those in the race */
    uint16_t ready;             /* +0xe6: the barrier's (bd) toggles, bits 0-7 and 8-15 */
    uint8_t ctl[3];             /* +0xe8: my controls' packet */
    int32_t ctlstep;            /* +0xec: 0 the second step of a packet next */
    int32_t ctlin;              /* +0xf0: packets of the cars' controls taken */
    int32_t ctlout;             /* +0xf4 */
    int32_t gameid;             /* +0xf8 */
    uint8_t chatkey;            /* +0xfc */
    uint8_t toggle[8];          /* +0xfd: a bit each player flips to time its round trip */
    uint8_t toggled;            /* +0x105: mine to flip */
    uint8_t timer;              /* +0x106: the timer callback is in */
    int32_t toggledat[8];       /* +0x108 */
    int32_t lag[8];             /* +0x128 */
    int32_t rtt;                /* +0x148: my last ping's round trip */
    int8_t ctlprev[4];          /* +0x14c: the first step of a packet */
    NetLst events;              /* +0x150: (bytes) my controls' events, one a packet */
    int32_t hostclock;          /* +0x154: the host's GetTickCount (c4), 0 none */
    int32_t hostclockat;        /* +0x158: mine then */
    int32_t gostart;            /* +0x15c: the race's start on the host's clock (c5) */
    uint8_t options[0xf0];      /* +0x160: [0xbc][0 0 0] then option records */
    int32_t optlen;             /* +0x250 */
    NetLst games;               /* +0x254 */
    NetLst ports;               /* +0x258: the modems, then the server's games (b8/b9) */
    NetLst found;               /* +0x25c: NetGameRec (b4/b5 and CONMAN's finds) */
    NetLst plist;               /* +0x260: NetPlayer */
    NetLst blocks;              /* +0x264: NetData to send */
    int32_t clock;              /* (the 0x48a8e0 clock: the host's GetTickCount less mine) */
} NetSession;

extern NetSession *g_net;   /* 0x628c70 */

/* In a session: its guest, the connection not lost. */
static inline bool Net_Live(void) { return g_net && g_net->client && !g_net->lost; }
/* In a game: and let in, a number given. */
static inline bool Net_InGame(void) { return Net_Live() && g_net->in && g_net->me >= 0; }

NetSession *Net_Construct(void);                                      /* 0x48fbe0 (Sys_Init 0x4a51e0) */
void Net_Destroy(void);                                               /* 0x48fe90 */
bool Net_HasProvider(int mode);                                       /* 0x48c980 */
void Net_HostGame(const NetGameInfo *info, int port);                 /* 0x48dbe0 */
void Net_JoinGame(int mode, int id, const char *addr);                /* 0x48e0d0 */
void Net_Leave(void);                                                 /* 0x48e620 */
void Net_End(void);                                                   /* 0x48e8f0 */
void Net_Clear(void);                                                 /* 0x48e6c0 */
int32_t Net_State(void);                                              /* 0x48c970 */
bool Net_IsFirst(void);                                               /* 0x48c9d0 */
void Net_Tick(void);                                                  /* 0x48ed20 (Sim_Step) */
void Net_Controls(void);                                              /* 0x48c930 */
bool Net_SyncBarrier(void);                                           /* 0x48cd20 */
void Net_Sync(void);                                                  /* 0x48d5d0 */
void Net_StartRace(void);                                             /* 0x48d300 */
void Net_SendStart(void);                                             /* 0x48d2a0 */
void Net_SendGo(void);                                                /* 0x48d2d0 */
void Net_Send(uint8_t type, int32_t a, int32_t b, void *to);          /* 0x48d280 */
void Net_FlushOptions(void);                                          /* 0x48d370 */
void Net_AddOption(const int32_t *rec);                               /* 0x48d3c0 */
void Net_AddOptions(const int32_t *recs);                             /* 0x48d480 */
void Net_SendBlock(int16_t id, const void *data, int size);           /* 0x48ca80 */
NetPlayer *Net_PlayerByNumber(int number);                            /* 0x48d600 */
NetPlayer *Net_PlayerByCar(int car);                                  /* 0x48d650 */
int Net_FinishTime(int car);                                          /* 0x48d6a0 */
int Net_Place(int car);                                               /* 0x48d720 */
int Net_RaceTime(void);                                               /* 0x48d770 */
void Net_Kick(int number);                                            /* 0x48d7d0 */
void Net_Chat(const char *text, int32_t to);                          /* 0x48d8e0 (0x6afb24) */
void Net_Ping(int32_t number);                                        /* 0x48d920 */
void Net_Finish(int32_t time, int32_t best);                          /* 0x48d990 */
void Net_RaceDone(void);                                              /* 0x48da00 */
int Net_CarLag(int car);                                              /* 0x4900a0: 0 fine .. 2 too late, 3 none, 4 out */
int Net_PlayerPing(NetPlayer *p);                                     /* 0x490550 */
void Net_GameList(NetLst *l, const uint8_t *m, int len);              /* 0x490b60 */
NetGameRec *NetGame_FromText(const char *text, int32_t id);           /* 0x48c240 */
int Net_GamePing(NetGameRec *g);                                      /* 0x48c5c0 */
int Option_Size(const int32_t *rec);                                  /* 0x4b4aa0 */
extern void (*g_net_chathook)(const char *text, int32_t to);          /* 0x6afb24 */

/* (Car state packets of the state mode, g_race_network 1: 0x49ff00 out, 0x4a0bd0 in, net_car.c) */
void NetCar_GetState(uint8_t out[0x24], const void *car);             /* 0x49ff00 */
void NetCar_SetState(void *car, const uint8_t in[0x24]);              /* 0x4a0bd0 */

/* ---- the server (net_host.c) ---- */

/* A server's player (0xcc bytes, 0x48a4a0). */
typedef struct NetRoom NetRoom;
typedef struct {
    const NetPlayerInfo *who;   /* +0x00: CONMAN's, NULL once gone */
    char name[0x20];            /* +0x24: from the extra text "<name>~<protocol>~<version>" */
    char unused44[0x20];        /* +0x44 */
    char protocol[0x20];        /* +0x64: "2.8" */
    char version[0x20];         /* +0x84 */
    NetRoom *room;              /* +0xa4 */
    uint32_t ring;              /* +0xa8: the room's ring entry sent next */
    int32_t number;             /* +0xac: in the room; else an id */
    int32_t rtt;                /* +0xb0 */
    int32_t queued;             /* +0xb4 */
    uint8_t isserver;           /* +0xb8: a server's connection ('s') */
    uint8_t own;                /* +0xb9: the host's own player */
    uint8_t announced;          /* +0xba */
    uint8_t hello;              /* +0xbb */
    uint8_t ready;              /* +0xbc: its state to tell */
    uint8_t refused;            /* +0xbd */
    uint8_t idle;               /* +0xbe */
    int32_t status;             /* +0xc0: Net_StatusText's */
    NetLst queue;               /* +0xc4: NetPkt */
    NetLst kept;                /* +0xc8: NetPkt, the latest of each sender's (0xbc) */
} NetHostPlayer;

typedef struct NetSrv NetSrv;
/* A room: a game of up to 8 (0xcc8 bytes, 0x48b890). */
struct NetRoom {
    char name[0x40];            /* +0x00 */
    char version[0x20];         /* +0x40 */
    NetSrv *server;          /* +0x60 */
    NetSender send;             /* +0x64 */
    int32_t id;                 /* +0x70 */
    NetGameInfo info;           /* +0x74 */
    NetLst players;             /* +0xa8: NetHostPlayer */
    int32_t count;              /* +0xac */
    uint8_t ring[128][0x18];    /* +0xb0: the cars' controls, 3 bytes each */
    uint32_t ringpos;           /* +0xcb0 */
    int32_t pingclock;          /* +0xcb4 */
    int32_t clock16;            /* +0xcb8 */
    int32_t clock32;            /* +0xcbc */
    uint8_t joined;             /* +0xcc0 */
    uint8_t got;                /* +0xcc1: the controls of this step */
    uint8_t out;                /* +0xcc2: not in the race */
    uint8_t racing;             /* +0xcc3 */
    uint8_t go;                 /* +0xcc4 */
};

/* CConHost (0x168 bytes, NetLobby_Connect 0x48aeb0). */
typedef struct {
    NetWrap base;
    ConServer *cm;              /* +0x04 */
    CritSec *cs;                /* +0x08 */
    NetClient *lobby;           /* +0x0c: to EA's lobby */
    NetSender *lobbysend;       /* +0x10 */
    uint8_t atlobby;            /* +0x14: the lobby's address is this one's ('p' in the name) */
    uint8_t tolobby;            /* +0x15: to connect to the lobby (the server's timer) */
    uint8_t hosting;            /* +0x16 */
    NetLst pending;             /* +0x18: NetHostPlayer with packets this tick */
    ConEventFn lobbyevent;      /* +0x1c */
    uintptr_t provider;         /* +0x20 */
    NetSrv *server;          /* +0x24 */
    char text[0x100];           /* +0x28: "<s|p><address>.<port>.<name>" */
    char lobbyaddr[0x40];       /* +0x128 */
} NetHost;

struct NetSrv {
    NetHost *host;              /* +0x00 */
    NetClient *own;             /* +0x04: the host's own player's guest, NULL a lobby server */
    NetSender *all;             /* +0x08: {host, all} */
    NetSender *lobby;           /* +0x0c: {host, inlobby} */
    NetSender *servers;         /* +0x10: {host, servers} */
    NetGameInfo info;           /* +0x14 */
    int32_t clock;              /* +0x4c */
    char address[0x100];        /* +0x50 */
    uint32_t lastid;            /* +0x150: the ids' counter (0x490aa0), -1 */
    int32_t nplayers;           /* +0x154 */
    int32_t nrooms;             /* +0x158 */
    int32_t nracing;            /* +0x15c */
    int32_t port;               /* +0x160 */
    NetLst allp;                /* +0x164 */
    NetLst inlobby;             /* +0x168 */
    NetLst serverlist;          /* +0x16c */
    NetLst games;               /* +0x170: NetGameRec (other servers') */
    NetLst rooms;               /* +0x174 */
    NetLst racing;              /* +0x178 */
    uint8_t lobbyup;            /* +0x17c */
    uint8_t relobby;            /* +0x17d */
    uint8_t hosting;            /* +0x17e */
    int32_t relobbyticks;       /* +0x180 */
    int32_t channel;            /* +0x184: the log's, -1 none */
};

extern NetSrv *g_net_server;   /* 0x628c74 */

NetSrv *NetServer_New(uintptr_t provider, const NetGameInfo *info, NetClient *own, const char *lobby, int port,
                         int channel);   /* 0x491c80 */
void NetServer_Delete(NetSrv *s);      /* 0x491f70 */
void NetServer_Stop(void);                /* 0x492210 */

/* ---- the guest (net.c, for net_host.c) ---- */
NetClient *NetClient_New(uintptr_t provider, ConEventFn event, intptr_t user, const char *name, const char *extra,
                         const char *version, const char *name2, int port);   /* 0x48a940 */
void NetClient_Delete(NetClient *c);         /* 0x48ae00 */
void NetClient_Find(NetClient *c);           /* 0x48ab80 */
void NetClient_JoinText(NetClient *c, const char *text, int port);   /* 0x48abc0 */
bool NetClient_Flush(NetClient *c);          /* 0x48ad60 */
void Net_Selftest(void);                     /* --param thrash_test=9 */
