/* The network library's pings (0x55d520..0x55d7c0): ICMP.DLL's echo from a thread of their own, asked
   through a queue. On the port's WinSock echo (wsock.h WS_echo): the relay's other hosts answer. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef void (*IcmpAnswerFn)(void *arg, int32_t ms);   /* the round trip, -1 none */

bool ICMP_start(void);                                            /* 0x55d520 */
void ICMP_stop(void);                                             /* 0x55d760 */
/* A dotted address pinged (four tries); the answer comes from the thread. 0x55d7c0 */
bool ICMP_ping(const char *addr, IcmpAnswerFn fn, void *arg);

void ICMP_selftest(void);   /* --param thrash_test=11 */
