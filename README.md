# OpenNFSPU

[![Built for gasm](https://gasm.emdzej.pl/badge/built-for-gasm-flat.svg)](https://gasm.emdzej.pl)

A from-scratch, portable reimplementation of **Need for Speed: Porsche Unleashed** (Electronic Arts, 2000)
in C11, based on the Windows version of the game (`Porsche.exe`, version 3.5). It runs as `opennfspu.wasm`,
a module for the [gasm](https://gasm.emdzej.pl) WebAssembly game runtime (macOS, Linux, Windows, browser),
drawing through gasm's OpenGL ES 3.0 (`gasm:gl`; WebGL 2 in the browser) where the original used Direct3D 7.
It contains no original code, assets or data: it reads the data files, and the tables of the original game
program, from your own copy of the game.

**Status: in progress.** Single player is playable from the menus through the races (Quick Race, the
knockouts, the Evolution career, Factory Driver); multiplayer works as peer to peer IP and LAN games
over gasm's relay, in `gasm-run` and the browser, still in progress. Details:
[opennfspu.emdzej.pl/guide/status](https://opennfspu.emdzej.pl/guide/status).

## Game data

OpenNFSPU reads the installed game folder, updated to version 3.5: `GameData/`, `FEData/` and
`Porsche.exe` (2,129,965 bytes), or an `.iso` image of that folder (`hdiutil makehybrid -iso -joliet`,
`genisoimage -J`). A repack disc image whose `Setup/install.exe` is a RAR self-extractor can be
extracted with `unar` instead of running the setup: see
[Extract the game](https://opennfspu.emdzej.pl/howto/extract-game).

## Build

```sh
tools/fetch-gasm-sdk.sh
cmake -S . -B build-gasm -DOPENNFSPU_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk"
cmake --build build-gasm -j                 # -> build-gasm/opennfspu.wasm
gasm-run build-gasm/opennfspu.wasm --asset-dir <your game folder>
```

Native build (tools and headless tests): `cmake -S . -B build && cmake --build build -j`.

Play in the browser: [opennfspu.emdzej.pl/play](https://opennfspu.emdzej.pl/play/).

## License

OpenNFSPU is free software under the [GNU GPL v3](LICENSE). Need for Speed: Porsche Unleashed is © 2000
Electronic Arts; Porsche is a trademark of Dr. Ing. h.c. F. Porsche AG. OpenNFSPU contains none of the
game's code or assets and is not affiliated with them.
