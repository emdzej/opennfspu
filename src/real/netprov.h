/* The network library's address providers (\real\cmn and \real\pc): the descriptors of 24 words the
   connections call through (UDP 0x5df6b0, IPX 0x5df710, TCP/IP 0x5e1a78), the registry of the ones this
   machine has (0x6b49e0, their transport classes 0x6b4a20, the count 0x6abd7c) and a connection's calls
   into its provider (0x580340..0x5803c0). See docs/re/network.md. */
#pragma once
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
    int (*slot28)(void *s);                                        /* +0x28 */
    int (*slot2c)(void *s);                                        /* +0x2c */
    void (*broadcasttext)(void *s, const char *text);              /* +0x30 */
    int (*slot34)(void *s);                                        /* +0x34 */
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

/* A connection (0xb4 bytes, 0x57ff60): the parts the providers' layer reads. */
typedef struct NetConn {
    struct NetConn *next;           /* +0x00: in the open connections 0x6b4a00 */
    uint32_t keep04;                /* +0x04: kept across 0x57ff60's clear */
    const void *transport;          /* +0x08: the transport class (0x5e1cf0 the datagram one) */
    const NetProvider *provider;    /* +0x0c */
    void *socket;                   /* +0x10: the provider's socket */
    uint32_t arg14;                 /* +0x14 */
    uint8_t rest[0xb4 - 0x18];
} NetConn;

#define NET_MAXPROVIDERS 16

extern const NetProvider *g_net_providers[NET_MAXPROVIDERS];   /* 0x6b49e0 */
extern const void *g_net_transports[NET_MAXPROVIDERS];         /* 0x6b4a20 */
extern int g_net_nproviders;                                   /* 0x6abd7c */

void NET_addprovider(const NetProvider *p, const void *transport);

bool NETCONN_same(NetConn *c, const NetAddr *a, const NetAddr *b);                /* 0x580340 */
int NETCONN_parse(NetConn *c, const char *text, int port, NetAddr *out);          /* 0x580360 */
int NETCONN_totext(NetConn *c, const NetAddr *a, char *text, uint32_t *port);     /* 0x580390 */
int NETCONN_port(NetConn *c);                                                     /* 0x5803c0 */
