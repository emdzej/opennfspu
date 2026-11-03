/* The memory link (\real\pc, protocol 0x5e1af8, 0x5825a0..0x583400): a packet connection between two
   ends in one process, as the host's own player reaches the game it hosts. Each end makes a pipe of its
   own under the name (<name>_SERVER, <name>_CLIENT: a mapping "<..>Pipe" with a ring of length-prefixed
   packets, a semaphore "<..>Semaphore" counting them and a mutex "<..>Mutex"), writes into it and reads
   the other's; a thread of each end hands the other's packets on. Each tick each end counts on in both
   pipes' headers; one that stops counting for the time-out (0x5df6a8 ticks) is gone.

   The original's are Windows' named file mappings, semaphores and mutexes; the port keeps named objects
   of its own (one process). The ring's read and write places are kept as offsets: the original keeps the
   writer's view's addresses, which the reader's view of the same memory reads alike. */
#pragma once
#include "real/llpacket.h"

extern const PacketProto MEMPIPE_proto;   /* 0x5e1af8 */

/* A memory link of the name to the other end (which 0: this end the server; 1 the client), size bytes
   each way. 0x5833f0 */
int MEMPIPE_connect(PacketConn *pc, const char *name, int size, int which);
