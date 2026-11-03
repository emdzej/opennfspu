/* The network library's packet connections (\real\cmn\llpacket.c and the generic layer 0x55ff80..0x560240):
   a connection to one remote address that carries the game's data, reliable and in order or not, through
   a protocol over a link layer.

   The protocol here is the one CONMAN's flags pick (0x5e2e58, 0x594f50..0x595f30): a sliding window of
   sequence numbers 0..0xfffb (0xfffe an unreliable packet, 0xfffd an acknowledgement alone, 0xffff the
   end), each packet [ack 2][seq 2][id 1][len 1][data], big-endian, carrying up to two of the sender's
   unacknowledged packets after it ([len 1][seq distance 1][data]); the receiver keeps one slot per
   sequence number it will accept, the sender resends on a time-out adapted to the round trip (Jacobson's).
   The link layer is the gathering connection's (0x5e1ad8, 0x582220..0x5824b0): '=' datagrams to the
   address on the connection's socket. */
#pragma once
#include "real/netprov.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct PacketConn PacketConn;
typedef struct PktState PktState;

/* A link layer (0x5e1ad8): the protocol's way to the wire. */
typedef struct PacketLink {
    int (*open)(PacketConn *pc, void (*notify)(PacketConn *));     /* [0] */
    int (*close)(PacketConn *pc);                                   /* [1] */
    int (*isup)(PacketConn *pc);                                    /* [2] */
    int (*cansend)(PacketConn *pc);                                 /* [3] */
    int (*pending)(PacketConn *pc);                                 /* [4] */
    int (*recv)(PacketConn *pc, uint8_t *buf, int max);             /* [5] */
    int (*send)(PacketConn *pc, const uint8_t *data, int len);      /* [6] */
    uint16_t maxpacket;   /* +0x1c */
    uint8_t mindata;      /* +0x1e */
    uint8_t decodes;      /* +0x1f: the link layer frames the packets (serial lines don't) */
} PacketLink;

/* A protocol (0x5e2e58; PACKET 0x5e1c50 and UNRELY 0x5e1c70 the others). */
typedef struct PacketProto {
    int kind;
    int (*init)(PacketConn *pc, const PacketLink *link, int maxdata, int flags);   /* +0x04 */
    int (*send)(PacketConn *pc, const void *data, uint32_t lenflags);              /* +0x08 */
    int (*freeslots)(PacketConn *pc);                                              /* +0x0c */
    int (*tick)(PacketConn *pc);                                                   /* +0x10 */
    int (*close)(PacketConn *pc);                                                  /* +0x14 */
} PacketProto;

struct PacketConn {
    struct PacketConn *next;                  /* +0x00: in the open or the closed ones */
    void *owner;                              /* +0x04: its user's (CONMAN's player) */
    const PacketProto *proto;                 /* +0x08 */
    void (*notify)(PacketConn *pc);           /* +0x0c: data on the link */
    uint32_t unused10;
    PktState *st;                             /* +0x14 */
    int (*idle)(PacketConn *pc);              /* +0x18: while connecting, non-zero gives up */
    void (*closed)(PacketConn *pc);           /* +0x1c */
    int (*timeout)(PacketConn *pc);           /* +0x20: silent too long: new ticks, 0 closes */
    int (*receive)(PacketConn *pc, const uint8_t *data, int len);   /* +0x24: 0 holds it */
    int32_t alive;                            /* +0x28: ticks of silence left, 0 closed */
    int16_t timing[4];                        /* +0x2c */
    void *filters;                            /* +0x34 */
    NetAddr addr;                             /* +0x38: the other end */
    int32_t link;                             /* +0x4c: -1 asking, 1 up, 0 down */
    NetConn *conn;                            /* +0x50 */
};

#define PACKET_UNRELIABLE 0x80000000u   /* in send's lenflags */

/* A packet connection over the gathering connection to pc->addr (the caller sets it). 0x582500 */
int PACKET_connect(PacketConn *pc, int maxdata, uint32_t flags, NetConn *conn);
int PACKET_open(PacketConn *pc, const PacketProto *proto, const PacketLink *link, int maxdata, uint32_t flags);   /* 0x55ff80 */
int PACKET_close(PacketConn *pc);                                    /* 0x560240 */
int PACKET_send(PacketConn *pc, const void *data, uint32_t lenflags); /* 0x5601d0 */
void PACKET_closeall(void);                                          /* 0x560160 */

/* The socket callback's first look: a packet connection to the sender takes the data. 0x5821c0 */
bool PACKET_dispatch(NetConn *c, const NetAddr *from);

extern const PacketProto LLPACKET_proto;   /* 0x5e2e58 */
extern const PacketLink GATHR_link;        /* 0x5e1ad8 */

void PACKET_selftest(void);   /* --param thrash_test=6 */
