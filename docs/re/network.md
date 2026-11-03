# The network game (internal notes)

The multiplayer code in Porsche.exe, as mapped for the port, and how it lands on gasm.

## Layers, bottom up

1. **WinSock** (WSOCK32.DLL, 27 imports by ordinal: socket 23, bind 2, sendto 20, recvfrom 17, select 18,
   ioctlsocket 12, setsockopt 21, gethostbyname 52, gethostname 57, listen 13, accept 1, connect 4,
   WSAStartup 115, WSACleanup 116, ...) and TAPI for modems (`\real\pc\tapipkt.c`, 0x55d000..0x55f000,
   0x584810 TAPI_load).
2. **Address providers** (descriptors of 24 words: +0 the name, +4 a 16-byte GUID, then 19 functions):
   - IPX 0x5df710 (name "IPX" 0x5d07c4; registered by 0x560840, which calls WSAStartup and the probe
     0x5605f0: an AF_IPX (6) datagram socket, port bound, closed);
   - UDP 0x5df6b0 ("UDP" 0x5d07c0; registered by 0x560580 if 0x583780 finds a host address that is not
     127.* or 0.*);
   - TCP/IP 0x5e1a78 ("TCP_IP"; registered by 0x581ff0, the stream transport).
   The providers' slots: +0x14 the startup (0x5873b0: WSAStartup, returns the max datagram), +0x18 the
   cleanup, +0x1c open a socket (IPX 0x5605f0, UDP 0x560300), +0x20 close (0x587610), +0x24..+0x34 the
   local address and its name, +0x38 parse an address from text (UDP 0x560410: dotted or a host name,
   gethostbyname), +0x3c the address as text (0x5604e0, IPX 0x5607c0 as "0x.. " bytes), +0x40 compare
   two addresses (0x560540, IPX 0x560820), then the socket's send/receive threads (0x587180 the receive
   thread: select 25 ms, recvfrom; 0x587290 the send thread: queued packets, sendto).
   The registry: 0x6b49e0 the providers, 0x6b4a20 their transport classes (0x5e1cf0 the datagram one,
   0x5e1a28 the stream one), 0x6abd7c the count.
3. **Connections** (`\real\cmn\packet.c`, `llpacket.c`, `unrely.c`, `nasync.c`: 0x57fd70..0x580200 and
   0x585540..0x589xxx): 0x57ff60 opens a connection on each registered provider in turn (the transport
   class's +8 open) until one takes it; PACKET_init 0x585540, UNRELY_init 0x5862e0 (the unreliable
   packets), CONN_send 0x581210 ("PACKET TOO LONG"), CONNpackettimeout 0x594f50, NET_killplayer
   0x5885b0 ("ONLY THE GAME SERVER MAY CALL killnetplayer").
4. **The session manager** ("CONMAN", 0x55a800: 0x514 + 0xe4 per player bytes): created by the game's
   host and join, it runs its own thread (0x55a950 by THREAD_create 0x55f3a0) that opens the providers'
   connections, then loops over the session's message queue (0x57fe60: type 4 a message, 5 a lost
   connection, 7 a player's state) and its players' lists (+0x260, +0x27c, +0x298, +0x2b4).
   0x55bd30 ends it (message 6, then waits for the thread).
5. **The game's network** (0x48a000..0x494000, about 90 functions): Net_Construct 0x48fbe0 (the session
   object, 0x268 bytes at 0x628c70; IPX and UDP registered at once, else the first modem 0x55e920),
   Net_HostGame 0x48dbe0, Net_JoinGame 0x48e0d0, Net_Tick 0x48ed20, NetServer_Process 0x490fc0,
   Net_SyncBarrier 0x48cd20, Net_ReadVersion 0x48db10, Net_StatusText 0x490770, Net_RaceMessages
   0x492960, Net_WriteResultsHtml 0x492b10, Net_VoiceInit 0x493320, NetLobby_Connect 0x48aeb0. The car
   states are sent and corrected on arrival (0x4900a0), not run in lockstep.
6. **The front end's screens** (0x508700..0x50b400): FECommsCreateScreen, FECommsGamesScreen,
   FECommsJoinScreen, FECommsLanGamesScreen, FECommsModemScreen, FECommsPeerToPeerIP,
   FECommsServerScreen, FECommsWebGamesScreen, FECommsErrorHandler, FEdaComms, FELobbyPlayerListAccessor;
   the layouts' ID_COMMSLAN, ID_COMMSINTERNET, ID_COMMSMODEM.

## On gasm

gasm:net only opens WebSocket connections out to a server (reliable, ordered, binary messages): no
listening, no datagrams, no broadcast. gasm-relay (gasm/runners/native/relay; the protocol: WELCOME 1
[you][peers], JOIN 2 [index], LEAVE 3 [index], FULL 4, DATA 0x10 [from][payload] to the clients and
[payload] to it, broadcast to the room's other peers) plays the network segment: a room is the LAN, a
peer's relay index its address, a broadcast reaches the room, and a send to one address goes to the room
with the address in front, dropped by the others.

The plan: the providers' socket slots on the relay (one provider standing for IPX and UDP, the room and
the relay's address from the game's address text), everything above them ported as it is. The threads
(the providers' send and receive, the session manager's) step once per frame on the main thread: the
port has one thread, and the frame-driven schedule keeps the hash runs repeatable. Modems (TAPI) find no
lines. The web games list asks EA's servers, which are gone.

Tests: two `gasm-run --allow-net --realtime` against a local gasm-relay (`--max-peers 8`):
`--param thrash_test=3` the WinSock layer, `=4` the UDP provider (src/real/udp.c: each broadcasts its
address as a type 0x77 packet and reads the other's), `=5` the gathering (one instance with
`--param nethost=1` hosts, the other finds, joins and lists the players).

## Ported

- WinSock over the relay (src/real/wsock.c), threads/events/timer callbacks (src/real/thread.c), the lists
  0x5803e0..0x580b70 (src/real/netlist.c).
- The providers' registry and a connection's provider calls 0x580340..0x5803c0 (src/real/netprov.c).
- UDP (src/real/udp.c): register 0x560580, probe 0x583780, host address 0x5834f0 (no RAS, no autodial
  registry), the slots 0x5602e0..0x560540, the datagram socket 0x586f80..0x587950 with its threads.
  The socket's receive match 0x587840 takes a packet of another type than asked, or of that type from
  the address given: a caller wanting any packet asks for type 0 (never kept: type 0 datagrams are
  dropped on arrival).
- The connections 0x57ff60..0x580240 and the datagram transport, game gathering (\real\patch3\cmn\
  netgathr.c, class 0x5e1cf0, src/real/netgathr.c; the packet types are listed in netgathr.h). A socket
  bound to any address reports 0.0.0.0, as on Windows: a player's address in the protocol is 0.x, which
  CONMAN 0x55d3b0 replaces with the host address text 0x6b49c0.
- The packet connections (src/real/llpacket.c): the generic layer 0x55ff80..0x560240, CONMAN's protocol
  0x5e2e58 (llpacket.c 0x594f50..0x595f30, a sliding window with piggybacked packets and Jacobson's
  time-out) over the gathering's link layer 0x5e1ad8 (0x582220..0x5824b0, the table 0x6abf28 that the
  socket callback looks in first), PACKET_connect 0x582500. Self-test `--param thrash_test=6`. The other
  two protocols (PACKET 0x5e1c50 with flag 0x4000, UNRELY 0x5e1c70 with no window bits) and the packet
  filters (+0x34) are pending notes: CONMAN's flags 0x8004 use neither.
- The port's timer moves at frames; the main thread's sleeps inside a frame move it on by their time
  (src/real/thread.c), so the requests' tick time-outs end while the main thread waits, as the
  original's timer thread makes them.
