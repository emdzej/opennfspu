# Roadmap

What comes next, roughly in order. There are no dates. [Status](/guide/status) describes what runs today.

Work is paused after release 0.1.8. Every part of the original game that a player can reach is ported. What
is left below is a check of the whole race against the original, additions the original doesn't have, and
features that wait on gasm.

## Fidelity

- **The handling and the AI.** The goal is the same car state frame by frame as the original, so that the
  original's recordings play back exactly. The port's simulation steps are run against the original's code
  under an x86 emulator (`tools/emu`), each from the same state. The car's drive, forces, moves and turns,
  its walls, world objects and car-to-car collisions, the road tracking, the race's progress and hits, the
  opponents' racing line driving, the traffic's lane driving, the police's chase and turning round, and the
  AI drive of opponents, traffic and police now give the same results bit for bit, through the start's
  countdown, a car put back on the road and the opponents' finish. Each step is compared from the port's
  own state, and in a free run: the original's code for one car, every step in order, each on the state
  the original left, gives the same car as the port over 600 ticks for the player and 300 for an opponent
  of the game's own replay.rpl, all four racers at once, each carried on its own, over 100 ticks, and every
  car with the track's knockable objects through their waking on Monaco (130 ticks, 10,971 calls).
- **Next: the whole race.** A first free run of the whole replay (6,663 ticks, every racer) found two
  differences, both fixed in 0.1.6: the car-to-car impulse's rounding order and the near-zero values of the
  drive's torque split. The run that confirms them over the whole race is still to do
  (`tools/emu/wholerace.sh`). The race loop's parts outside the cars' tasks aren't covered yet.
- **The front end's save:** the opponents' cars aren't read back from the player's save. The race picks
  them again anyway.

## Additions

- **Done: true widescreen.** The original's "wide screen" option is a letterbox. The port adds 16:9 and
  16:10 sizes to the game's own screen size list, and a race at one of them draws at its true shape (Hor+):
  more of the road at the sides, the HUD in the corners. At the original's sizes the game draws as the
  original. `--param aspect=16:9`, `16:10` or `auto` keeps the list to one shape (`auto`: the display's,
  following the window when it is resized); the play page has the same choice. See
  [Beyond the original](/guide/beyond).
- **Later: widescreen menus.** The front end's screens are laid out for 640 x 480 and are shown with black
  bars at the sides. Widening them means placing each screen's parts again.
- **Later: two-player split screen.** The race code has a split-screen mode: communication mode 1, with a
  second player's camera and the catch-up logic. The original never runs it: the front end's race set-up
  (0x4eab10) writes the mode before every race, 0 (or 4 in a network game), over `fe.txt`'s `comm_mode`.
  So it is dead code in the shipped game, and turning it on would be a port addition, opt-in like true
  widescreen. Porting it means the two views, cameras and HUDs, and the second player's controls; parts
  of the race already follow the mode (the sound's panning, the traffic's cap, the police).

## Online and devices

- **Done: the Internet game.** EA's servers are gone. A game started with `--param netserver=1` is the
  room's lobby server, and the front end's Internet game (its sign on, server list, lobby, chat and games)
  runs through it. See [Network games](/guide/multiplayer#the-internet-game).
- **Later: a public lobby.** A relay with a lobby server always running, so that players need no
  server of their own.
- **Waiting on gasm: force feedback.** The original sends the hits' jolts and the road's feel to a
  force-feedback wheel. This needs rumble or force-feedback support from gasm, which it doesn't have yet.
- **Waiting on gasm: voice chat.** The original has voice chat between the players of a network game. It
  needs microphone input from gasm.

## Out of scope

- The modem and serial cable game: gasm has no serial ports.
- The Glide (3dfx) driver path: the port draws through OpenGL ES 3.0 in place of Direct3D 7.
- EA's account registration: its web page is gone, and the Internet game's lobby server takes any account.
