/* The network library's address providers and connections (\real\cmn): the providers' descriptors of 24
   words (UDP 0x5df6b0, IPX 0x5df710, TCP/IP 0x5e1a78), the registry of the ones this machine has
   (0x6b49e0, their transport classes 0x6b4a20, the count 0x6abd7c), and the connections opened on them
   (0x57ff60..0x580240, the open ones in 0x6b4a00) with their calls into the provider (0x580340..0x5803c0).
   See docs/re/network.md. */
#pragma once
#include "real/netlist.h"
#include <stdbool.h>
#include <stdint.h>

/* An address as the library keeps it (20 bytes): for UDP the sockaddr_in (16 bytes, then zeros), for IPX
   the network, the node and the socket (0x586f80). */
typedef struct NetAddr { uint8_t b[20]; } NetAddr;

/* A socket's receive callback: the owner (its connection) and the new packet's sender. */
typedef void (*NetRecvFn)(void *owner, const NetAddr *from);

typedef struct NetProvider {
    const char *name;                                              /* +0x00 */
    uint8_t guid[16];                                              /* +0x04 */
    int (*startup)(void);                                          /* +0x14: the largest datagram, 0 none */
    bool (*cleanup)(void);                                         /* +0x18 */
    void *(*open)(int port, void *owner, NetRecvFn recv);         /* +0x1c: a socket */
    int (*close)(void *s);                                         /* +0x20 */
    int (*port)(void *s);                                          /* +0x24 */
    int (*advertise)(void *s, const char *name);                   /* +0x28: a hosted game's name on */
    int (*unadvertise)(void *s, const char *name);                 /* +0x2c */
    int (*broadcasttext)(void *s, const char *text);               /* +0x30: (to find the games) */
    int (*endbroadcast)(void *s);                                  /* +0x34 */
    int (*parse)(void *s, const char *text, int port, NetAddr *out);   /* +0x38 */
    int (*totext)(void *s, const NetAddr *a, char *text, uint32_t *port);   /* +0x3c */
    bool (*same)(void *s, const NetAddr *a, const NetAddr *b);     /* +0x40 */
    int (*localaddr)(void *s, NetAddr *out);                       /* +0x44 */
    bool (*sendempty)(void *s);                                    /* +0x48 */
    int (*received)(void *s);                                      /* +0x4c */
    int (*send)(void *s, const NetAddr *to, uint8_t type, const void *data, int len);   /* +0x50 */
    int (*recv)(void *s, NetAddr *from, uint8_t *type, void *data, int max);           /* +0x54 */
    int (*discard)(void *s, const NetAddr *from, uint8_t type);   /* +0x58 */
    int (*setidle)(void *s, void (*idle)(void *owner));           /* +0x5c */
} NetProvider;

/* A player as the gathering sends them (0x5c bytes): the address, the name and a second text. */
typedef struct NetPlayerInfo {
    NetAddr addr;      /* +0x00 */
    char name[0x34];   /* +0x14 */
    char extra[0x14];  /* +0x48 */
} NetPlayerInfo;

/* A game's description (0x34 bytes, the host's): its name, then values of the game's, the last the most
   players (0 at hosting: 0x20). */
typedef struct NetGameInfo {
    char name[32];       /* +0x00 */
    uint8_t b[4];        /* +0x20 */
    int16_t s[2];        /* +0x24 */
    int32_t i[2];        /* +0x28 */
    int32_t maxplayers;  /* +0x30 */
} NetGameInfo;

struct NetConn;
typedef int (*NetGameFn)(struct NetConn *c, const NetGameInfo *g, int players, const NetAddr *from);
typedef int (*NetPlayerFn)(struct NetConn *c, const NetPlayerInfo *p, int number, int ishost);

/* A request waiting for its answer (0x50 bytes, on the asker's stack; the connection's +0x20). */
typedef struct NetReq {
    uint32_t id;           /* +0x00: 0 once answered */
    int32_t result;        /* +0x04 */
    uint32_t players;      /* +0x08: the host's mask of joined players */
    int32_t user;          /* +0x0c */
    struct NetConn *conn;  /* +0x10 */
    NetGameFn games;       /* +0x14 */
    NetPlayerFn playerfn;  /* +0x18 */
    NetGameInfo info;      /* +0x1c */
} NetReq;

/* A joined player at the host (100 bytes). */
typedef struct NetSlot {
    NetPlayerInfo info;    /* +0x00 */
    int32_t alive;         /* +0x5c: ticks to its time-out */
    int32_t index;         /* +0x60 */
} NetSlot;

/* A hosted game ("NETSRVR", 0x24 + 100 bytes a player). */
typedef struct NetServer {
    uint32_t unused00;
    struct NetConn *conn;        /* +0x04 */
    int32_t hidden;              /* +0x08: not advertised */
    char advert[0x30];           /* +0x0c: "EAGAME:<guid>" */
    char password[16];           /* +0x3c */
    NetPlayerFn joinfn;          /* +0x4c: a joining player, 0 refuses */
    uint32_t mask;               /* +0x50: the slots in use */
    NetGameInfo info;            /* +0x54 */
    NetSlot slot[];              /* +0x88: maxplayers - 1 */
} NetServer;

typedef struct NetConn NetConn;

/* A transport class (0x5e1cf0 the datagram gathering, 0x5e1a28 the stream one). */
typedef struct NetTransport {
    int kind;                                                                     /* +0x00: 0 datagrams */
    int anyprovider;                                                              /* +0x04: else the GUID's */
    int (*open)(NetConn *c, const uint8_t *game, const uint8_t *provider, int port, const char *name, const char *extra);   /* +0x08 */
    int (*close)(NetConn *c);                                                     /* +0x0c */
    int (*find)(NetConn *c, NetGameFn fn, int ticks, int user);                  /* +0x10 */
    int (*join)(NetConn *c, const NetAddr *host, const char *password);           /* +0x14 */
    bool (*query)(NetConn *c, const NetAddr *host, NetGameInfo *out);             /* +0x18 */
    int (*host)(NetConn *c, NetGameInfo *info, const char *password, NetPlayerFn joinfn);   /* +0x1c */
    int (*setinfo)(NetConn *c, const NetGameInfo *info);                          /* +0x20 */
    int (*hide)(NetConn *c, int free_it);                                         /* +0x24 */
    int (*show)(NetConn *c);                                                      /* +0x28 */
    int (*leave)(NetConn *c);                                                     /* +0x2c */
    unsigned (*players)(NetConn *c, const NetAddr *host, NetPlayerFn fn);        /* +0x30 */
    int (*kill)(NetConn *c, const NetAddr *who);                                  /* +0x34 */
    int (*packets)(NetConn *c, void *pc, int maxdata, uint32_t flags);           /* +0x38: a packet connection on it */
    int (*config)(NetConn *c, const void *cfg);                                   /* +0x3c: (the stream one's) */
    bool (*receive)(NetConn *c, const NetAddr *from, uint8_t type, const uint8_t *data, int len, uint32_t *more);   /* +0x40 */
    int (*setmessage)(NetConn *c, void (*fn)(NetConn *, const uint8_t *, int));   /* +0x44 */
    void (*message)(NetConn *c, const void *data, int len);                       /* +0x48 */
} NetTransport;

/* A connection (0xb4 bytes). */
struct NetConn {
    struct NetConn *next;           /* +0x00: in the open connections 0x6b4a00 */
    void *owner;                    /* +0x04: its user's (CONMAN), kept across 0x57ff60's clear */
    const NetTransport *transport;  /* +0x08 */
    const NetProvider *provider;    /* +0x0c */
    void *socket;                   /* +0x10: the provider's socket */
    int (*joinfilter)(NetConn *c, const NetAddr *from);   /* +0x14: 0 answers a 'q' with '!' */
    void (*onmessage)(NetConn *c, const uint8_t *data, int len);   /* +0x18: a '<' */
    NetServer *server;              /* +0x1c: the game hosted */
    NetReq *req;                    /* +0x20 */
    NetAddr host;                   /* +0x24: the joined game's host */
    NetPlayerInfo self;             /* +0x38 */
    uint8_t guid[16];               /* +0x94: the game's */
    int32_t opened;                 /* +0xa4 */
    int32_t keepalive;              /* +0xa8: ticks to the next pings */
    int32_t joined;                 /* +0xac: ticks to the host's time-out, 0 not joined */
    int32_t packets;                /* +0xb0: a packet connection runs on it */
};

#define NET_MAXPROVIDERS 16

extern const NetProvider *g_net_providers[NET_MAXPROVIDERS];   /* 0x6b49e0 */
extern const NetTransport *g_net_transports[NET_MAXPROVIDERS]; /* 0x6b4a20 */
extern int g_net_nproviders;                                   /* 0x6abd7c */
extern int g_net_resend;                                       /* 0x6abd74: ticks between a request's sends */
extern NetList g_net_conns;                                    /* 0x6b4a00 */
extern CritSec *g_net_sendcs;                                  /* 0x6a5720: the providers' sends, the timer's list */

void NET_addprovider(const NetProvider *p, const NetTransport *transport);

/* A connection on the first provider that takes it (a transport open on any provider, else on the one of
   that GUID) for the game of that GUID. 0x57ff60 */
int NET_open(NetConn *c, int (*joinfilter)(NetConn *, const NetAddr *), const uint8_t *game, const uint8_t *provider,
             int port, const char *name, const char *extra);
int NET_close(NetConn *c);            /* 0x580130 */
int NET_leave(NetConn *c);            /* 0x580240 */

/* The transport's calls (0x580170..0x5802d0). */
int NET_find(NetConn *c, NetGameFn fn, int ticks, int user);                          /* 0x580170 */
int NET_join(NetConn *c, const NetAddr *host, const char *password);                  /* 0x580190 */
bool NET_query(NetConn *c, const NetAddr *host, NetGameInfo *out);                    /* 0x5801b0 */
int NET_host(NetConn *c, NetGameInfo *info, const char *password, NetPlayerFn joinfn);   /* 0x5801d0 */
int NET_setinfo(NetConn *c, const NetGameInfo *info);                                 /* 0x5801f0 */
int NET_hide(NetConn *c, int free_it);                                                /* 0x580210 */
int NET_show(NetConn *c);                                                             /* 0x580230 */
unsigned NET_players(NetConn *c, const NetAddr *host, NetPlayerFn fn);               /* 0x580270 */
int NET_kill(NetConn *c, const NetAddr *who);                                         /* 0x580290 */
int NET_config(NetConn *c, const void *cfg);                                          /* 0x5802b0 */
int NET_setmessage(NetConn *c, void (*fn)(NetConn *, const uint8_t *, int));          /* 0x5802d0 */
void NET_closeall(void);              /* 0x5800e0 (at exit) */

bool NETCONN_same(NetConn *c, const NetAddr *a, const NetAddr *b);                /* 0x580340 */
int NETCONN_parse(NetConn *c, const char *text, int port, NetAddr *out);          /* 0x580360 */
int NETCONN_totext(NetConn *c, const NetAddr *a, char *text, uint32_t *port);     /* 0x580390 */
int NETCONN_port(NetConn *c);                                                     /* 0x5803c0 */
