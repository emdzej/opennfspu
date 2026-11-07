# Credits and licences

**OpenNFSPU** is released under the [GNU General Public License v3.0](https://github.com/emdzej/opennfspu/blob/main/LICENSE).
It contains no code, art, sound or data from the original game: everything is read from your own copy at run
time.

**Need for Speed: Porsche Unleashed** is © 2000 Electronic Arts; Porsche is a trademark of Dr. Ing. h.c. F. Porsche AG. OpenNFSPU is not affiliated with them.

## Published and community sources

- **Community notes on EA's formats** — first guesses, each then checked against the data and the exe; no code
  was copied.

## Reference implementations

- [FFmpeg](https://ffmpeg.org)'s EA demuxer and `ffprobe` — identified the MAD video, EA-XA ADPCM and the SCxl chunks.
- **[IJG libjpeg](https://ijg.org)** — the front end's JPEG decoder follows its integer IDCT, range limits and
  upsampling, and is checked against `djpeg`; the game's own `gimme.dll` linked it.
- **libtiff** — the TIFF reader accepts what it accepts.

## Tools

[Ghidra](https://ghidra-sre.org), [ghidra-cli](https://github.com/akiselev/ghidra-cli), the [Unicorn](https://www.unicorn-engine.org) emulator, [wasi-sdk](https://github.com/WebAssembly/wasi-sdk), Binaryen, [VitePress](https://vitepress.dev); [ANGLE](https://chromium.googlesource.com/angle/angle) and SwiftShader, shipped in the desktop bundles with `gasm-run` (their notices are in `ANGLE-NOTICES.txt`). For getting the game off a disc: [The Unarchiver](https://theunarchiver.com/command-line) and [Wine](https://www.winehq.org).

## Runtime

[gasm](https://gasm.emdzej.pl) (MIT): `gasm-run`, the C SDK, and on this site `@emdzej/gasm-host`. OpenNFSPU is part of
[ENCORE](https://encore.emdzej.pl), with its sibling reimplementations.

If we have missed someone whose work this project relied on, please
[open an issue](https://github.com/emdzej/opennfspu/issues) — gaps in these credits are bugs.
