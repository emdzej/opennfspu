# Roadmap

What comes next, roughly in order. There are no dates. [Status](/guide/status) describes what runs today.

## True 16:9 widescreen

The original's "wide screen" option is a letterbox. It crops the 4:3 picture to a band with black bars above
and below. True widescreen is a port addition, so it stays off unless asked for, and the game then draws as
the original does.

- **Done:** `--param aspect=16:9` (in the browser player `/play/?aspect=16:9`). The options' screen size list
  gets 1280 x 720, 1600 x 900 and 1920 x 1080, and the race draws at the display's real shape. The vertical
  view stays and the sides widen (Hor+). The world's and the sky's culling widen with it, and the HUD keeps to
  the corners. See [Running on gasm](/guide/gasm#graphics).
- **Done:** 16:10 (`aspect=16:10`: 1280 x 800, 1680 x 1050, 1920 x 1200), a screen shape choice on the
  play page, and the HUD checked at every size (it keeps to the corners at its art's pixel size, as the
  original's does at every size).
- **Next:** a switch in the game's own graphics options, in place of the parameter.
- **Next:** the window's own shape when it is resized.
- **Later:** the front end. Its screens are laid out for 640 x 480 and are shown with black bars at the sides.
  Widening them means placing each screen's parts again.

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
  of the game's own replay.rpl. Not covered yet: the parts of the race loop outside the car's own steps
  (the world's objects, the other cars at once), and a whole race from its start.
- **Two-player split screen.** The race code has a split-screen mode: communication mode 1, with a second
  player's camera and the catch-up logic. The original never runs it: the front end's race set-up
  (0x4eab10) writes the mode before every race, 0 (or 4 in a network game), over `fe.txt`'s `comm_mode`.
  So it is dead code in the shipped game, and turning it on would be a port addition, opt-in like true
  widescreen. Porting it means the two views, cameras and HUDs, and the second player's controls; parts
  of the race already follow the mode (the sound's panning, the traffic's cap, the police).
- **The front end's save:** the opponents' cars aren't read back from the player's save. The race picks
  them again anyway.

## Online and devices

- **An Internet game of our own.** EA's servers are gone. The front end's Internet game (its lobby of
  players and games) can run over a gasm relay instead, the same way peer IP and LAN games do now.
- **Force feedback.** The original sends the hits' jolts and the road's feel to a force-feedback wheel. This
  needs rumble or force-feedback support from gasm.
- **Voice chat (maybe).** The original has voice chat between the players of a network game. It needs
  microphone input from gasm.

## Out of scope

- The modem and serial cable game: gasm has no serial ports.
