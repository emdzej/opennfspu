/* The UDP address provider (descriptor 0x5df6b0, "UDP") and the WinSock datagram socket it opens
   (0x586f80..0x587950, IPX's too): a socket with a receive thread and a send thread, 8 packets to receive
   into and 8 to send from. On the port's WinSock (wsock.h: datagrams on a gasm-relay room). */
#pragma once
#include "real/netlist.h"
#include "real/netprov.h"
#include "real/thread.h"
#include <stdbool.h>
#include <stdint.h>

#define DGRAM_DATA 0x11a   /* a packet's bytes: the type byte, then the data */

typedef struct DgPacket {
    struct DgPacket *next;   /* +0x00 */
    NetAddr addr;            /* +0x04: the sender, or where to */
    int32_t broadcast;       /* +0x18 */
    uint8_t *type;           /* +0x1c: buf[0] */
    uint8_t *data;           /* +0x20: buf + 1 */
    int32_t len;             /* +0x24: with the type byte */
    uint8_t buf[0x11c];      /* +0x28 */
} DgPacket;

typedef struct DgSocket {
    uint32_t unused00;
    void *owner;                    /* +0x04: the connection */
    NetAddr local;                  /* +0x08 */
    int sock;                       /* +0x1c */
    NetRecvFn recv;                 /* +0x20 */
    void (*idle)(void *owner);      /* +0x24: called while there is nothing to send */
    volatile bool running;          /* +0x28 */
    volatile bool recvalive;        /* +0x29 */
    volatile bool sendalive;        /* +0x2a */
    Event *wake;                    /* +0x2c: the send thread's */
    int kind;                       /* +0x30: 0 UDP, 1 IPX */
    int addrlen;                    /* +0x34 */
    int port;                       /* +0x38 */
    bool ipxname;                   /* +0x3c: (IPX's, 0x560990) */
    NetList freerecv;               /* +0x70 */
    NetList freesend;               /* +0x8c */
    NetList received;               /* +0xa8 */
    NetList sendq;                  /* +0xc4 */
    DgPacket rpk[8];                /* +0xe0 */
    DgPacket spk[8];                /* +0xb00 */
} DgSocket;

extern NetProvider UDP_provider;   /* 0x5df6b0 */

const uint8_t *UDP_register(const char *name);   /* 0x560580: its GUID, NULL if this machine has no UDP */
bool UDP_probe(void);                            /* 0x583780 */
bool NET_hostaddress(char *out);                 /* 0x5834f0: the host's address as text */
extern char g_net_hostaddr[32];                  /* 0x6b49c0 */

DgSocket *DGRAM_new(void *owner, NetRecvFn recv, int sock, int kind, int addrlen, const void *bindaddr);   /* 0x587410 */
int DGRAM_close(void *s);                                                                   /* 0x587610 */
int DGRAM_startup(void);                                                                    /* 0x5873b0 */
bool DGRAM_cleanup(void);                                                                   /* 0x587400 */
int DGRAM_localaddr(void *s, NetAddr *out);                                                 /* 0x5876a0 */
bool DGRAM_sendempty(void *s);                                                              /* 0x5876c0 */
int DGRAM_received(void *s);                                                                /* 0x5876e0 */
int DGRAM_send(void *s, const NetAddr *to, uint8_t type, const void *data, int len);       /* 0x587700 */
int DGRAM_recv(void *s, NetAddr *from, uint8_t *type, void *data, int max);                /* 0x587790 */
int DGRAM_discard(void *s, const NetAddr *from, uint8_t type);                             /* 0x587880 */
int DGRAM_setidle(void *s, void (*idle)(void *owner));                                     /* 0x587950 */

void UDP_selftest(void);   /* --param thrash_test=4 */
