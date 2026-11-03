/* The IPX address provider (0x5df710, "IPX") and its games' SAP advertising: see ipx.c. On the port's
   WinSock (wsock.h: IPX datagrams on the relay room, the room the LAN). */
#pragma once
#include "real/wsock.h"
#include <stdbool.h>
#include <stdint.h>

const uint8_t *IPX_register(const char *name);   /* 0x560840: its GUID, NULL without IPX */

/* A datagram from SAP's socket to a socket looking for games (0x560990): a response's games of that name
   sent the name; true when it was SAP's (the socket's receive drops it then). */
bool IPX_sapfilter(void *s, const WsAddr *from, const uint8_t *data, int len);
