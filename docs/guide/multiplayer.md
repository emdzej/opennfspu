# Multiplayer

The original's network game is ported: up to eight players race each other from the Multiplayer menu,
by IP address or on a LAN, with the host's track, direction, laps and race mode, the chat and the
players' list. gasm has no raw network access, so the players' games meet in a room on a **relay server**
(`gasm-relay`, part of gasm). The room stands in for the network: every game in it gets an address, and the
LAN game sees only the games in its room.

The Internet game works too. EA's servers are gone, so a game in the room started as a lobby server
stands in for them (see [The Internet game](#the-internet-game)). What isn't there: EA's account
registration, the modem and serial cable games (out of scope), voice chat, and the observer's view.

## The relay

`gasm-relay` comes with gasm: it is in each of
[gasm's release archives](https://github.com/emdzej/gasm/releases) (`gasm-<version>-<system>`), next to
`gasm-run`. One player, or any machine all players can reach, starts it:

```sh
gasm-relay 0.0.0.0:9123 --max-peers 8
```

It listens for WebSocket connections on port 9123, and a room holds up to `--max-peers` games. It only
passes the games' packets on; it needs neither the game nor its data. Players on other networks need the
port forwarded to it or a server with a public address. Browser players on the
[site's player](/play/){target="_self"} need a `wss://` relay: start it with `--tls-cert chain.pem --tls-key key.pem`.

## Joining a room

Every player starts the game with the same relay and the same room name:

```sh
gasm-run opennfspu.wasm --asset-dir <game folder> \
  --param relay=ws://<relay host>:9123 --param room=<name>
```

The bundles pass the same options on to `gasm-run`:

| | |
|---|---|
| Linux | `./opennfspu.sh <game folder> --param relay=ws://<relay host>:9123 --param room=<name>` |
| macOS | `open "Porsche Unleashed (gasm).app" --args --param relay=ws://<relay host>:9123 --param room=<name>` |
| Windows | `OpenNFSPU.cmd <game folder> --param relay=ws://<relay host>:9123 --param room=<name>` |
| Browser | `https://opennfspu.emdzej.pl/play/?relay=wss://<relay host>:9123&room=<name>` |

The first time the game connects, `gasm-run` asks whether it may reach the relay. `--allow-net=<relay host>`
answers that in advance. The browser player lets the game reach the relay's host only. Native and browser
players can share a room.

Each game in the room has the address `10.0.0.<n>`, in the order the games joined: the first is `10.0.0.1`.
A game keeps its address until it quits.

## Starting a game

In the front end, **Multiplayer**, then:

- **Peer to peer IP.** The host chooses create. Its address is shown at the top right of the create
  screen. The others type that address and connect.
- **LAN.** The host creates a game. The others see it in their list of games and join it.

The host sets up the race: the track, its direction, the laps and the race mode. Everyone picks a car and
says they are ready, and the race starts for all of them together. The players' list shows each player's
car, readiness and ping, and the chat goes to everyone.

The two race modes are the original's:

- **Positional.** Each game drives its own car and sends where it is. The other cars are moved towards the
  places they report. This mode tolerates a slow or uneven connection. With the collisions setting off, the
  cars pass through each other. As in the original, the game doesn't send that setting to the other
  players: each game uses the choice made on its own screen.
- **Input exchange.** Every game sends its controls, and each game runs the whole race from everyone's
  controls, in step. The races stay the same everywhere, but each game waits for the slowest player's
  controls. All players need the same `opennfspu.wasm`.

A game that joins over UDP and gets no answer tries again over TCP, as the original does.

## The Internet game

The original's Internet game signed on to EA's entry server, `porsche-entry.earacing.com`, which listed
EA's game servers. Players entered one of them, where they met in a lobby with a chat, and created and
joined games there. OpenNFSPU runs that server side in the game itself: one game in the room, started
with `--param netserver=1`, is the room's lobby server. It doesn't play. It stays on a black screen and
answers the others, and the Internet game's entry name means that game.

Start the lobby server first, so that it is the room's first game (peer 0), for example headless on the
relay's machine:

```sh
gasm-run opennfspu.wasm --asset-dir <game folder> --headless 100000000 --realtime --allow-net \
  --param relay=ws://<relay host>:9123 --param room=<name> --param netserver=1
```

Then the players join the same room and choose **Multiplayer**, **Internet**:

1. **Sign on.** The account and password can be anything. EA's accounts are gone, and the lobby server
   doesn't check them. The register button did open EA's page, which is gone too.
2. **Server selection.** The lobby server is listed by its address in the room, with its ping. Choose it
   and **enter**.
3. **Internet games.** The players in the lobby are listed, and the chat goes to all of them. **Create**
   makes a game that the others see in their list and **join**. The race is set up and started as in the
   other modes.

If the lobby server isn't the room's first game, give the players `--param entry=<its index>`: the
index is its address's last number less one (`10.0.0.3` is 2). A lobby server is a port addition. The
original's game code has it (the game's own server with no player of its own, the way EA's servers ran
it), but no way to start one.

## In the race

The HUD's standings show each player's ping, and the leader's picture: the driver's picture or the
player's own picture file (see [Status](/guide/status)). The loading screen shows every player's picture
and progress.

## Trying it on one machine

Start the relay on `127.0.0.1:9123`, then two games in their own windows with separate saves, so that each
has its own player:

```sh
gasm-relay 127.0.0.1:9123 --max-peers 8 &
gasm-run opennfspu.wasm --asset-dir game --storage-dir /tmp/p1 \
  --param relay=ws://127.0.0.1:9123 --param room=test &
gasm-run opennfspu.wasm --asset-dir game --storage-dir /tmp/p2 \
  --param relay=ws://127.0.0.1:9123 --param room=test
```

The first game to connect is `10.0.0.1`.
