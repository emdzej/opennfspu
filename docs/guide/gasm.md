# Running on gasm

[gasm](https://gasm.emdzej.pl) is a WebAssembly game runtime: one module runs natively through
`gasm-run` (macOS, Linux, Windows) and in the browser. OpenNFSPU is built for gasm **0.16.0**. The bundles
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

## Graphics

The original renders through Direct3D 7. OpenNFSPU uses `gasm:gl`, OpenGL ES 3.0 with WebGL 2's rules:
natively `gasm-run` runs it on ANGLE (Metal, Direct3D 11 or Vulkan), in the browser on WebGL 2. Headless
runs use gasm's null GL, so the hashes are the same everywhere.

## Determinism

gasm runs are deterministic: the same module, data and input give the same frame and audio hashes on every
runner, natively and in the browser. The tests rely on that.
