# Running on gasm

[gasm](https://gasm.emdzej.pl) is a WebAssembly game runtime: one module runs natively through
`gasm-run` (macOS, Linux, Windows) and in the browser. OpenNFSPU is built for gasm **0.16.1**. The bundles
include the right `gasm-run`; to use it directly:

```sh
gasm-run opennfspu.wasm --asset-dir "/path/to/Need For Speed - Porsche Unleashed"
```

`--asset-dir` hands the game folder to the game as its assets (read on demand). Useful options:

| Option | What |
|---|---|
| `--window 1280x960` | window size |
| `--fullscreen` | start in fullscreen (<kbd>F11</kbd> switches) |
| `--mute` | no sound |
| `--headless N` | run N frames without a window and print frame and audio hashes |
| `--screenshot out.png` | with `--headless`: save the last frame |
| `--input "60:KEY(Enter)"` | with `--headless`: scripted input |
| `--param name=value` | a launch parameter for the game |

## Network games

gasm has no raw sockets: `gasm:net` reaches other players through a relay server (`gasm-relay`, from
gasm). The port runs the original's network library over it. Each game in a relay room gets the address
`10.0.0.<n>`, in the order the games joined:

```sh
gasm-relay 0.0.0.0:9123 --max-peers 8
gasm-run opennfspu.wasm --asset-dir <game folder> --allow-net=<relay host> \
  --param relay=ws://<relay host>:9123 --param room=<name>
```

[Multiplayer](/guide/multiplayer) covers the relay, the bundles and the browser player, and the game's
peer to peer IP and LAN games.

## Graphics

The original renders through Direct3D 7. OpenNFSPU uses `gasm:gl`, OpenGL ES 3.0 with WebGL 2's rules:
natively `gasm-run` runs it on ANGLE (Metal, Direct3D 11 or Vulkan), in the browser on WebGL 2. Headless
runs use gasm's null GL, so the hashes are the same everywhere.

The original's "wide screen" option is a letterbox: it crops the 4:3 picture to a band with black bars above
and below. OpenNFSPU adds true widescreen, which the original doesn't have: the options' screen size list
gets 1280 x 720, 1280 x 800, 1600 x 900, 1680 x 1050, 1920 x 1080, 1920 x 1200 and 2560 x 1440 after the
original's five, and a race at one of them draws at its real shape. `--param aspect=16:9` keeps the list to
the 16:9 sizes (1280 x 720, 1600 x 900, 1920 x 1080), `aspect=16:10` to the 16:10 ones, and `aspect=auto` to
the shape nearer the display's when the game starts (a 4:3 display keeps the whole list); with `auto` it
then follows the window: resized to the other wide shape, the next race starts in it at the same place in
the list. The vertical view stays as it is
and the sides widen (Hor+), and the HUD keeps to the corners. The front end stays 640 x 480 with black bars
at the sides. In the browser player the start page's screen shape choice (or `/play/?aspect=16:9` in
the address) also gives the window its shape; with `./play`, `./play --param aspect=16:9`. At the original's
sizes the game draws exactly as before. The HUD is drawn at its art's
pixel size, as the original does at every size, so it is smaller at 1920 x 1080 than at 1280 x 720, as it is
at 1600 x 1200 against 640 x 480.

## Determinism

gasm runs are deterministic: the same module, data and input give the same frame and audio hashes on every
runner, natively and in the browser. The tests rely on that.
