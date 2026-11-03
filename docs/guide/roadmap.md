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
- **Next:** a switch in the game's own graphics options and on the play page, in place of the parameter.
- **Next:** other shapes: 16:10, and the window's own shape when it is resized.
- **Next:** the HUD checked at every 16:9 size. It has only been looked at at 1280 x 720.
- **Later:** the front end. Its screens are laid out for 640 x 480 and are shown with black bars at the sides.
  Widening them means placing each screen's parts again.

## Fidelity

- **The handling.** Replays recorded by the original drift from the port's physics after a few seconds. The
  goal is the same car state frame by frame, so that the original's recordings play back exactly.
- **Two-player split screen.** The race code has a split-screen mode (the game setup's communication mode 1:
  the second player's camera, the catch-up logic). Next is finding out whether the PC's front end can reach it.
- **The network game's leftovers:** the observer's view and one race-progress message.
- **The front end's save:** the track, colour and car choices aren't restored from the player's save yet, and
  the dealer's colour slots are missing.

## Out of scope

- The Internet game: EA's servers are gone.
- The modem and serial cable game: gasm has no serial ports.
- Voice chat and force feedback: gasm has no microphone or force feedback device.
