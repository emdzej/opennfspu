/* The port's WinSock (WSOCK32.DLL, which the original's network library calls by ordinal): datagram and
   stream sockets on a gasm-relay room (platform.h plat_net_*), the room standing for the network segment.

   The relay (gasm's gasm-relay: ws://host:port/<room>) numbers its peers 0, 1, ... and forwards each
   peer's messages to the room's other peers. Here a peer is a host: relay index i is the address
   10.0.0.(i + 1), and the host's sockets are told apart by their ports. A datagram travels as one relay
   message, [destination address (4 bytes, 255.255.255.255 a broadcast)][destination port][source port]
   (2 bytes each, network order)[kind (1 byte)] then the data; the others drop it unless it is theirs or a
   broadcast. Kind 0 is a UDP datagram, 6 an IPX one; the others carry streams (TCP's): a connect (1)
   answered by an accept (2) or a refusal (5, also a reset), the data (3), the end (4). The relay is
   reliable and in order, so a stream needs no more; a peer leaving the room resets its streams. Streams,
   datagrams and IPX have ports of their own. IPX's addresses (SOCKADDR_IPX, 14 bytes) have network 0 and
   the node 00:00 and the host's four bytes (ff:ff:ff:ff:ff:ff everyone).
   The room and the relay come from the launch parameters relay=ws://host:port and room=<name> (default
   "nfs5"); without a relay there is no network, as on a machine whose TCP/IP isn't set up.

   Addresses are Windows' sockaddr_in (16 bytes: family 2, the port and the address in network order).
   The calls block as WinSock's do, by sleeping the calling thread a frame at a time. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define WS_AF_INET 2
#define WS_AF_IPX 6
#define WS_SOCK_STREAM 1
#define WS_SOCK_DGRAM 2
#define WS_INVALID_SOCKET (-1)
#define WS_SOCKET_ERROR (-1)
#define WS_INADDR_NONE 0xffffffffu

typedef struct WsAddr {
    uint16_t family;
    uint16_t port;      /* network order */
    uint8_t addr[4];
    uint8_t zero[8];
} WsAddr;

typedef struct WsHost {   /* hostent, the parts read */
    char *name;
    char **aliases;
    int16_t addrtype;
    int16_t length;
    char **addr_list;
} WsHost;

typedef struct WsFdSet {  /* fd_set */
    uint32_t count;
    int32_t fd[64];
} WsFdSet;

typedef struct WsTimeval { int32_t sec, usec; } WsTimeval;

int WS_startup(uint16_t version, void *data);   /* Ordinal 115 WSAStartup: 0 on success */
int WS_cleanup(void);                           /* Ordinal 116 WSACleanup */
int WS_socket(int af, int type, int protocol);  /* Ordinal 23 */
int WS_closesocket(int s);                      /* Ordinal 3 */
int WS_bind(int s, const WsAddr *a, int len);   /* Ordinal 2 */
int WS_getsockname(int s, WsAddr *a, int *len); /* Ordinal 6 */
int WS_setsockopt(int s, int level, int name, const void *v, int len);   /* Ordinal 21 */
int WS_ioctlsocket(int s, uint32_t cmd, uint32_t *arg);                  /* Ordinal 12 */
int WS_sendto(int s, const void *buf, int len, int flags, const WsAddr *to, int tolen);   /* Ordinal 20 */
int WS_recvfrom(int s, void *buf, int len, int flags, WsAddr *from, int *fromlen);       /* Ordinal 17 */
int WS_select(int n, WsFdSet *r, WsFdSet *w, WsFdSet *e, const WsTimeval *t);            /* Ordinal 18 */
int WS_fdisset(int s, const WsFdSet *set);      /* Ordinal 151 __WSAFDIsSet */
int WS_gethostname(char *name, int len);        /* Ordinal 57 */
WsHost *WS_gethostbyname(const char *name);     /* Ordinal 52 */
uint32_t WS_inet_addr(const char *s);           /* Ordinal 10 (network order) */
char *WS_inet_ntoa(const uint8_t addr[4]);      /* Ordinal 11 */
uint16_t WS_htons(uint16_t v);                  /* Ordinal 9 (and 15 ntohs) */
uint32_t WS_htonl(uint32_t v);                  /* Ordinal 8 */
int WS_lasterror(void);                         /* Ordinal 111 WSAGetLastError */
int WS_listen(int s, int backlog);              /* Ordinal 13 */
int WS_accept(int s, WsAddr *a, int *len);      /* Ordinal 1 */
int WS_connect(int s, const WsAddr *a, int len);   /* Ordinal 4 */
int WS_send(int s, const void *buf, int len, int flags);   /* Ordinal 19 */
int WS_recv(int s, void *buf, int len, int flags);         /* Ordinal 16 (flags 2: MSG_PEEK) */
int WS_shutdown(int s, int how);                /* Ordinal 22 */
int WS_getsockopt(int s, int level, int name, void *v, int *len);   /* Ordinal 7 (SO_ERROR) */

/* ICMP.DLL's IcmpSendEcho to the address (its echo answered by the other host's WinSock, kind 7 and 8 on
   the relay): the round trip in ms, -1 with no answer in the time. */
int WS_echo(const uint8_t addr[4], const void *data, int len, uint32_t timeout_ms);

/* The relay's messages read into the sockets' queues (each blocking call does it; the frame too). */
void WS_pump(void);

void WS_selftest(void);   /* --param thrash_test=3 */
