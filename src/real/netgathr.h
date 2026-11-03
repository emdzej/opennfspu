/* The datagram transport (\real\patch3\cmn\netgathr.c, class 0x5e1cf0, 0x5879a0..0x589270): game
   gathering on a datagram provider. A host advertises a game ("EAGAME:<guid>"); the others find it by
   broadcast, ask for its description and its players and join it; the host pings its players and drops
   the silent ones. The packets are typed by one character:

     'w' (0x77) who hosts this game? (the advert text)   'd' (0x64) a game's description
     'r' (0x72) describe your game                       'g' (0x67) send player n's info (-1 the host's)
     'p' (0x70) a player's info   '?' (0x3f) no such     'j' (0x6a) let me join   'a' (0x61) the answer
     '*' (0x2a) ping (both ways)  'b' (0x62) leaving     'K' (0x4b) dropped by the host
     'q' (0x71) may I? ('!' 0x21 no)  '<' (0x3c) a message to all   '=' (0x3d) a packet connection's

   Numbers are big-endian (0x56e040 / 0x56e070); a request's first 4 bytes are its id. */
#pragma once
#include "real/netprov.h"

extern const NetTransport NETGATHR_transport;   /* 0x5e1cf0 */

/* A connection's socket callback: its packets to the packet connections or the transport. 0x5820d0 */
void NET_onreceive(void *owner, const NetAddr *from);

void NETGATHR_selftest(void);   /* --param thrash_test=5 */
