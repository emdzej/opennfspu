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

gasm has no raw sockets: its `gasm:net` reaches other players through a relay server (`gasm-relay`, from
gasm). The port puts the original's network library on it: each player in a relay room gets an address
`10.0.0.<n>`, in the order they joined, the first `10.0.0.1`. Start the relay, then each player's game:

```sh
gasm-relay 0.0.0.0:9123 --max-peers 8
gasm-run opennfspu.wasm --asset-dir <game folder> --allow-net \
  --param relay=ws://<relay host>:9123 --param room=<name>
```

All players use the same `room`. In the game: Multiplayer, Peer to peer IP. The host chooses create, the
others type the host's address (shown at the top right of the host's create screen) and connect.
Or Multiplayer, LAN: the room is the local network, the host creates a game and the others see it in
their list.

The browser player takes the same two settings in its address:
`https://opennfspu.emdzej.pl/play/?relay=wss://<relay host>:9123&room=<name>`. It then lets the game connect to
that host only. A page served over https can only reach a `wss://` relay (`gasm-relay` with `--tls-cert` and
`--tls-key`); `ws://` works from a page on `localhost`. Browser and `gasm-run` players can share a room.

## Graphics

The original renders through Direct3D 7. OpenNFSPU uses `gasm:gl`, OpenGL ES 3.0 with WebGL 2's rules:
natively `gasm-run` runs it on ANGLE (Metal, Direct3D 11 or Vulkan), in the browser on WebGL 2. Headless
runs use gasm's null GL, so the hashes are the same everywhere.

## Determinism

gasm runs are deterministic: the same module, data and input give the same frame and audio hashes on every
runner, natively and in the browser. The tests rely on that.
