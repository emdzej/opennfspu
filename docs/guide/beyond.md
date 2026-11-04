# Beyond the original

OpenNFSPU's aim is the original game as it was, so the port's own additions never change it unless you
ask. Without them it draws, drives and sounds as Porsche Unleashed 3.5 does. These are the things it
adds.

## True widescreen: 16:9 and 16:10

![The Alps in 16:9, against three opponents](/screenshots/alps-wide.jpg)

The original's "wide screen" option is a letterbox: it crops the 4:3 picture to a band with black bars
above and below. OpenNFSPU can draw the races at the display's real shape instead. The view keeps its
height and widens at the sides (Hor+), so you see more of the road, and the HUD stays in the corners.

| Shape | Screen sizes added to the game's options |
|---|---|
| 16:9 | 1280 x 720, 1600 x 900, 1920 x 1080, 2560 x 1440 |
| 16:10 | 1280 x 800, 1680 x 1050, 1920 x 1200 |

To turn it on, pick one of the new sizes in the game's options (Options, Graphics, screen size). They
come after the original's five. A race at one of the original's sizes draws exactly as the original's
does. The menus stay 4:3, with black bars at the sides. Pick the shape of the screen you play on:
16:10 for most laptops, 16:9 for most monitors and TVs.

To keep the list to one shape:

- **In the browser:** pick the screen shape on the [play page](/play/){target="_self"} before you start.
  The window takes that shape, and the choice is kept in the browser. "Match my screen" picks the shape
  nearest your screen's.
- **On the desktop:** start the game with `--param aspect=16:9` or `--param aspect=16:10` (for example
  `./play --param aspect=16:9`), or `--param aspect=auto` for the shape nearest the window's (or the
  screen's, started fullscreen) when the game starts. With `auto`, the shape then follows the window:
  resize it from 16:10 to 16:9 (or back), or go fullscreen, and the next race starts in the new shape at
  the size you chose.

See [Running on gasm](/guide/gasm#graphics).

## Network games without EA's servers

The original's network races (peer to peer over IP, and LAN) meet in a room on a relay server here,
`gasm-relay`, so they work across the Internet, through firewalls, and from the browser too. Both of the
original's race modes are there, as are the players' pictures. EA's servers for the Internet game are
gone, so a copy of the game can stand in for them: started as a lobby server, it gives the Internet game
its server list, lobby, chat and games again. See [Network games](/guide/multiplayer).

## Play anywhere, from your own copy

- **One module everywhere:** the same `opennfspu.wasm` runs on macOS, Linux and Windows, and in a recent
  browser. See [Install](/guide/install).
- **No installer:** it reads the installed game's folder, or an `.iso` image of it, as it is.
- **Saves that travel:** the play page exports your players, options, records and replays as a zip and
  imports them again, in the same layout the desktop version keeps, so they move both ways.

## What stays as it was

Everything else: the handling, the AI, the cameras, the HUD's sizes, the menus, the sounds and the
original's quirks. Where the port differs from the original by mistake, that is a bug to fix. See
[Status](/guide/status).
