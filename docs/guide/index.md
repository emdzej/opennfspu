# Introduction

OpenNFSPU is a from-scratch reimplementation of **Need for Speed: Porsche Unleashed** (Electronic Arts
Canada, 2000; *Need for Speed: Porsche 2000* in Europe), the Windows game, in C11. It is not an emulator
or a wrapper: each part of the game is ported function by function from the original program,
`Porsche.exe` (version 3.5), and reads the original data files: cars, tracks, the frontend, sounds and
music. **Fidelity is the goal**: the original's behaviour, quirks included.

It runs as `opennfspu.wasm`, a module for the [gasm](https://gasm.emdzej.pl) WebAssembly game runtime:
natively on macOS, Linux and Windows (`gasm-run`, or the ready-made bundles) and in the browser. The
original draws with Direct3D 7; OpenNFSPU draws the same scenes with OpenGL ES 3.0 through `gasm:gl`
(WebGL 2 in the browser).

OpenNFSPU contains no code, art, sound or data from the game. You need your own copy:
see [Game data](/guide/game-data).

- [Status](/guide/status): what works so far (read this first).
- [Installing](/guide/install): the bundles for macOS, Linux and Windows.
- [Playing in the browser](/guide/playing): no installation.
- [Running on gasm](/guide/gasm): `gasm-run` directly.
