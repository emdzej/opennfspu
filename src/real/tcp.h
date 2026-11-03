/* The TCP/IP stream provider (\real\patch3\pc\tcp.c; the provider 0x5e1a78 "TCP_IP", 0x581630..0x581fd0,
   its transport class 0x5e1a28, 0x5813b0..0x581620, and the stream protocol 0x5e1a10, 0x580d10..0x581320).

   CONMAN opens it next to UDP (a second connection on the same port number): a guest whose join over
   UDP gets no answer tries a stream connection to the host instead. The provider's socket is a listening
   stream socket with a thread of its own (0x581800) that selects on it and on every stream packet
   connection: it accepts one connection at a time, asks the connection's join filter about it, and keeps
   it for four seconds for a packet connection to take; it reads the connections' packets and finishes
   their sends. A packet is [length 2, big-endian][data], at most 0x400 bytes (WinSock's largest datagram
   less 2, 0x400 when it gives none). Opening a packet connection with no address takes the accepted
   socket; with one it connects (non-blocking, polled). The transport has no games to find, host or join:
   only the packet connections. */
#pragma once
#include "real/llpacket.h"
#include "real/netprov.h"

const uint8_t *TCP_register(const char *name);   /* 0x581ff0: the provider's GUID, NULL without TCP/IP */

extern const NetTransport TCP_transport;   /* 0x5e1a28 */
extern const PacketProto TCP_proto;        /* 0x5e1a10 */

void TCP_selftest(void);   /* --param thrash_test=10 */
