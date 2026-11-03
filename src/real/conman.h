/* The network library's session manager ("CONMAN", 0x55a800..0x55d4b0): the game's host and its guests,
   each a thread of its own over a connection (the gathering's, netgathr.c) with a packet connection
   (llpacket.c) to each player.

   The host (0x55a800, kind 1) hosts the game on the connection; a player the gathering lets in is asked
   of the game (event 6), then the host opens a packet connection to it and waits for its hello
   ([0xff][password][name][text]); it answers [0xfe][1] (in) or [0xfe][2] (a wrong password) and the
   player is the game's (event 7). The guest (0x55c0d0, kind 2) finds games (event 0 each, 1 at the end),
   lists a host's players (2 each, 3), joins one through the gathering, opens its packet connection and
   sends its hello (event 4 in, 5 not with the reasons). Then both send the game's packets (event 0xc
   each arriving); a lost connection is event 9 (10 timed out), a dropped player 8, the gathering's
   messages 0xb.

   The provider is a provider's GUID, or 1 for a modem (serial lines: pending, as the TCP/IP stream
   provider CONMAN adds next to UDP). */
#pragma once
#include "real/llpacket.h"
#include "real/netlist.h"
#include "real/netprov.h"
#include "real/thread.h"
#include <stdbool.h>
#include <stdint.h>

/* What CONMAN tells the game (on the stack, 0x5c bytes and up). */
typedef struct ConEvent {
    int32_t code;   /* +0x00 */
    int32_t len;    /* +0x04: of what follows */
    union {
        int32_t reason;            /* 5, 8 */
        const uint8_t *data;       /* 0xb, 0xc: len bytes */
        NetPlayerInfo player;      /* 2 */
        struct {
            NetAddr addr;
            NetGameInfo info;
            int32_t players;       /* -1 from a query */
        } game;                    /* 0 */
    } u;
} ConEvent;

enum {
    CON_GAME = 0, CON_FOUND = 1, CON_PLAYER = 2, CON_LISTED = 3, CON_IN = 4, CON_NOTIN = 5, CON_ASKS = 6,
    CON_JOINED = 7, CON_DROPPED = 8, CON_LOST = 9, CON_TIMEDOUT = 10, CON_MESSAGE = 0xb, CON_PACKET = 0xc,
    CON_FAILED = 0xd,
};

/* CON_NOTIN's reasons */
#define CON_BADPASSWORD 1
#define CON_NOANSWER    2
#define CON_FULL        4
#define CON_REFUSED     8

typedef struct ConMan ConMan;
/* who: the player's (the host's events), NULL for the guest's. CON_ASKS: non-zero lets the player in. */
typedef int (*ConEventFn)(ConMan *cm, const NetPlayerInfo *who, const ConEvent *e);

/* A message to the thread (0x74 bytes). */
typedef struct ConMsg {
    struct ConMsg *next;
    int32_t type;                 /* +0x04 */
    union {
        uint8_t raw[0x68];
        int32_t ticks;            /* 0 */
        NetAddr addr;             /* 1, 3 */
        struct {
            NetAddr addr;
            char password[16];    /* +0x1c */
            int32_t ticks;        /* +0x2c: 0 no limit */
        } join;                   /* 2 */
        char text[0x68];          /* 4 */
        PacketConn *pc;           /* 7 */
    } u;                          /* +0x08 */
    int32_t arg;                  /* +0x70 */
} ConMsg;

/* A packet connection with its deadline and owner (0x78 bytes in the original: the serial lines'
   fields follow the connection). */
typedef struct ConLink {
    void *owner;           /* +0x00: the CONMAN */
    PacketConn pc;         /* +0x04 */
    int32_t deadline;      /* +0x74: a tick, 0 none */
} ConLink;

/* A player at the host (0xe4 bytes). */
typedef struct ConPlayer {
    struct ConPlayer *next;
    NetPlayerInfo info;    /* +0x04 */
    uint32_t unused60;
    ConLink link;          /* +0x64 */
    uint32_t flags;        /* +0xdc: CONP_* */
    uint32_t hello;        /* +0xe0: 1 its hello came, 2 with the password */
} ConPlayer;

#define CONP_OPEN    0x01   /* the packet connection is open */
#define CONP_INGAME  0x02   /* the gathering has it (killed when it goes) */
#define CONP_MODEM   0x04
#define CONP_CALLED  0x08   /* it called the host (a text address) */
#define CONP_TIMEOUT 0x10
#define CONP_LIVE    0x20
#define CONP_STREAM  0x40   /* on the TCP/IP connection */

struct ConMan {
    int32_t user;               /* +0x00 */
    int32_t kind;               /* +0x04: 1 the host, 2 a guest */
    NetConn conn;               /* +0x08 (its owner the CONMAN) */
    uintptr_t provider;         /* +0xbc: a GUID, or 1 a modem */
    const uint8_t *game;        /* +0xc0 */
    char password[16];          /* +0xc4 */
    int32_t port;               /* +0xd4 */
    ConEventFn event;           /* +0xd8 */
    NetConn conn2;              /* +0xdc: TCP/IP next to UDP */
    uint32_t conn2open;         /* +0x190 */
    Event *ev;                  /* +0x194 */
    NetList freemsgs;           /* +0x198 */
    NetList queue;              /* +0x1b4 */
};

typedef struct ConServer {
    ConMan cm;
    uint32_t unused1d0;
    NetGameInfo info;           /* +0x1d4 */
    ThreadRec thread;           /* +0x208 */
    ThreadRec answer;           /* +0x224: the modem's */
    uint32_t flags;             /* +0x240: 1 running, 2 advertised, 4 answering */
    NetList free;               /* +0x244 */
    NetList asking;             /* +0x260: let in by the gathering, the game asked */
    NetList connecting;         /* +0x27c */
    NetList players;            /* +0x298 */
    NetList dead;               /* +0x2b4 */
    ConMsg msgs[5];             /* +0x2d0 */
    ConPlayer slots[];          /* +0x514 */
} ConServer;

typedef struct ConClient {
    ConMan cm;
    NetPlayerInfo self;         /* +0x1d0 */
    ThreadRec thread;           /* +0x22c */
    uint32_t flags;             /* +0x248: 1 running, 2 the connection open, 4 the host answered */
    int32_t reply;              /* +0x24c */
    ConLink link;               /* +0x250 */
    uint32_t linkflags;         /* +0x2c8: CONP_OPEN, CONP_TIMEOUT, CONP_LIVE */
    ConMsg msgs[10];            /* +0x2cc */
} ConClient;

/* The host: a game of info (its most players) on the provider; NULL if its thread didn't start. 0x55a800 */
ConServer *CONMAN_host(ConEventFn event, uintptr_t provider, const uint8_t *game, const NetGameInfo *info, int32_t user,
                       int32_t port, const char *password);
void CONMAN_hostend(ConServer *s);                                                   /* 0x55bd30 */
int CONMAN_setinfo(ConServer *s, const NetGameInfo *info);                           /* 0x55be10 */
int CONMAN_call(ConServer *s, const char *text, int32_t arg);                        /* 0x55bef0 */
int CONMAN_sendto(ConServer *s, const NetPlayerInfo *who, const void *data, uint32_t len);        /* 0x55bfd0 */
int CONMAN_sendto_unreliable(ConServer *s, const NetPlayerInfo *who, const void *data, uint32_t len);   /* 0x55c010 */

/* A guest of the player self. 0x55c0d0 */
ConClient *CONMAN_guest(ConEventFn event, uintptr_t provider, const uint8_t *game, const NetPlayerInfo *self, int32_t user,
                        int32_t port);
void CONMAN_guestend(ConClient *c);                                                  /* 0x55cf10 */
int CONMAN_find(ConClient *c, int32_t ticks);                                        /* 0x55cfd0 */
int CONMAN_join(ConClient *c, const NetAddr *host, int32_t ticks, const char *password);   /* 0x55d0d0 */
int CONMAN_dial(ConClient *c, const char *text, int32_t arg);                        /* 0x55d190 */
int CONMAN_send(ConClient *c, const void *data, uint32_t len);                       /* 0x55d200 */
int CONMAN_send_unreliable(ConClient *c, const void *data, uint32_t len);            /* 0x55d230 */
/* (The guest's thread also takes messages 1, a host's players, and 3, a game's description, that nothing
   in the game posts.) */

int CONMAN_totext(ConMan *cm, const NetAddr *a, char *text, uint32_t *port);         /* 0x55d300 */
int CONMAN_parse(ConMan *cm, const char *text, int port, NetAddr *out);              /* 0x55d340 */
bool CONMAN_isnet(const ConMan *cm);                                                 /* 0x55d490 */
bool CONMAN_ismodem(const ConMan *cm);                                               /* 0x55d4b0 */

void CONMAN_selftest(void);   /* --param thrash_test=7 */
