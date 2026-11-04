# AGENTS.md

Guidance for coding agents working in this repository. Humans: see [README.md](README.md) and
[opennfspu.emdzej.pl](https://opennfspu.emdzej.pl).

## What this is

OpenNFSPU is a faithful, from-scratch reimplementation of **Need for Speed: Porsche Unleashed** (EA Canada,
2000; also sold as Need for Speed: Porsche 2000), the Windows build `Porsche.exe` version 3.5 (2,129,965 bytes,
linked 2000-03-13), in C11. It targets [gasm](https://github.com/emdzej/gasm) (`opennfspu.wasm`: native runners
on macOS, Linux and Windows, and the browser at https://opennfspu.emdzej.pl/play/). A native build exists for
tools and headless tests only. Sibling projects with the same conventions: `../openrf` (Return Fire),
`../opengta` (GTA), `../openballance` (Ballance).

It is not an emulator or a wrapper: every subsystem is ported function by function from the original
executable. **Fidelity is the product.** Reproduce the original's behaviour, including its quirks and bugs.
If something looks wrong, check the disassembly; if the original does it, keep it and add a comment. Only
fix deviations *of the port* from the original.

The original is C++ (EA's "real" library `\real\cmn`, `\real\pc`; the EAGL2 graphics library; the `fert`
front-end runtime; the game under `d:\Nfs\Nfs5\game\...`). The port is C11: classes become structs, virtual
calls an explicit vtable struct where the original dispatches virtually.

Rendering goes through EA's **THRASH** driver interface (`_THRASH_*`, 42 entry points), which the original
loads from `Drivers/dx7z.dll` (Direct3D 7) or `voodoo2z.dll` (Glide). The port implements THRASH on OpenGL ES
3.0 (`gasm:gl`; WebGL 2 in browsers) following dx7z.dll, and ports the game above it unchanged in structure.

**State:** single player playable (docs/guide/status.md): the front end, Quick Race, the knockouts, the Evolution
career, Factory Driver, the races with AI, traffic and police, the HUD, sound and replays. Multiplayer: peer to
peer IP and LAN (IPX + SAP, src/real/ipx.c) games over a gasm-relay room (src/real/wsock.c: peer i is
10.0.0.(i+1)), both race modes, the TCP/IP stream fallback (src/real/tcp.c), the players' pictures
(docs/guide/multiplayer.md), the Internet game through a lobby server (`--param netserver=1`, in place of EA's; `Net_LobbyServer`), the modem and serial
games are out of scope (their `PENDING_NOTE`s stay). True widescreen (`src/wide.c`, a port addition): the
options list 16:9 and 16:10 sizes after the original's five, Hor+ at a size wider than 4:3, `--param aspect=`
narrows the list; the default sizes must stay hash-identical. Fidelity checks against the original's code run
under the x86 emulator in `tools/emu` (README there; `wholerace.sh` for a whole replay). Work is paused after
0.1.8: what's left is in docs/guide/roadmap.md (the whole-race free run, widescreen menus, split screen, a
public lobby; force feedback and voice wait on gasm). Viewers: `--param car=<name>`, `track=<name>`, `drive=<track>`, `fe=<layout>`.

## Hard rules

1. **Never commit original data or anything derived from it.** `game/` (the installed game), `porsche.exe`,
   `dx7z.dll` (Ghidra copies), `re/` (decompiler dumps), `out/` (decoded assets, renders) are git-ignored:
   keep it that way. No `.iso`, `.viv`, `.fsh`, `.crp`, extracted textures, models, sounds or movies.
2. **No data copied from the executables in the source tree.** Tables the game needs from `Porsche.exe` are
   **loaded at runtime** by `src/exe.c` (checks size + CRC-32 `fd2f5573`). Small algorithmic constants and
   instruction operands are fine; tables, geometry and strings are not.
3. **Cite the original.** Every ported function names its source: `/* Name 0x4xxxxx */` for the exe,
   `/* _THRASH_setstate@8 0x60003ac0 */` for dx7z.dll. Keep names consistent with the Ghidra project (rename
   there as you learn; `tools/ghidra/names.tsv` and `names-dx7z.tsv` are the record).
4. **Don't paste decompiler output** into source or docs. Write the port and the docs in your own words;
   describe layouts, formulas and addresses.
5. Community knowledge (NFS modding tools and format notes) may inform format understanding; the exe is the
   authority and no code is copied.

## Layout

| Path | What |
|---|---|
| `src/` | Portable core. `pending.h`: calls into code not ported yet (docs/internals/porting.md). File layer `vfs.c` (from OpenRF/OpenGTA: directories, ISO 9660 images with Joliet names), `exe.c` (the original exe read at runtime), `app.c` (per-frame state machine). Backends: `vfs_host.c` + `platform_host.c` (native tests), `platform_gasm.c` (the only file that includes `gasm.h`) |
| `src/render/` | `thrash.c`: the THRASH driver on GLES 3 (the only GL user; not in the native build). EAGL and the game renderer to come |
| `src/real/`, `src/eagl/`, `src/front/`, `src/game/` | EA real library, EAGL2, front end, game (to come; mirror the original's modules) |
| `tests/` | Headless tests (`*_test.c`, one executable each, `ctest`) against the real data; skip without it |
| `tools/` | `nfsfmt.py` (reference decoders, stdlib only), gasm SDK/runner fetchers, bundle packaging (`package-gasm.sh`, `smoke-gasm-bundle.sh`, `gasm-bundle/`), `screenshots.sh`, `web-play-test.mjs`, `ghidra/` scripts |
| `docs/` | VitePress site -> opennfspu.emdzej.pl: user guide, how-tos, internals. `formats.md` and the reverse-engineering notes `docs/re/` live here too but are internal: excluded from the site (`srcExclude`) |
| `docs/public/play/` | Browser player (static, served at `/play/`) |
| `.github/workflows/` | `ci.yml`, `release.yml`, `pages.yml` |

## Game data

The installed game folder: `Porsche.exe`, `GameData/` (CarModel, Track, Render, Simulation, Sounds, Speech,
Music, DashHud), `FEData/` (Art, Layouts, Controls, Movies, Locale, ...). Development copy in `./game`.

The user's copy came as a repack ISO (since deleted) whose `Setup/install.exe` is a RAR
self-extractor (`unar` extracts it; 7-Zip/p7zip fail with "Unsupported Method"). A plain image of the installed
folder is at `/Volumes/dropbox/NFS_PU.iso` (ISO 9660 + Joliet, made with
`hdiutil makehybrid -iso -joliet -default-volume-name NFS_PU -o NFS_PU.iso game`). The primary ISO tree only
has 8.3 names: the reader uses the Joliet tree when there is one (names up to 35 characters here).

`OPENNFSPU_DATA=<dir|image>` is the data root for tests and tools (default `./game`). **All data access goes
through `src/vfs.h`**: no `fopen` in the core. Names match case-insensitively, `\` or `/`.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
(cd build && ctest --output-on-failure)
OPENNFSPU_DATA=/Volumes/dropbox/NFS_PU.iso ./build/vfs_test     # the same against the disc image
```

gasm module: gasm **0.16.1** (the pin is `GASM_VERSION` in `tools/fetch-gasm-sdk.sh`; it fetches wasi-sdk and the
C SDK and Binaryen's `wasm-opt` into `.deps/`, `tools/fetch-gasm-runner.sh macos-universal` the released `gasm-run`). The module is
built `LOOP THREADS` (gasm_loop.c, Asyncify: `src/platform_gasm.c`'s `gasm_main` waits a frame between the game's
frames; gasm's cooperative threads carry the network library's), `-DOPENNFSPU_LOOP_THREADS=OFF` for the plain exports:

```sh
tools/fetch-gasm-sdk.sh && RUN="$(tools/fetch-gasm-runner.sh macos-universal)/gasm-run"
cmake -S . -B build-gasm -DOPENNFSPU_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk"
cmake --build build-gasm -j
$RUN build-gasm/opennfspu.wasm --asset-dir game --headless 60 --screenshot /tmp/p.png
$RUN build-gasm/opennfspu.wasm --asset cd=/Volumes/dropbox/NFS_PU.iso --headless 60   # same hash line
```

`./play` builds the module if needed and opens it in `gasm-run`'s window on `./game` (`./play --help`; for the user,
not for unattended runs). Unattended runs: always `--headless` (never open a window, never use macOS `screencapture`). The folder and
the image must give identical hash lines. A change that alters the hashes must be explained by the change.

## gasm backend

`src/platform_gasm.c`: title "Need for Speed: Porsche Unleashed" (`GASM_TITLE`); `gasm:gl` (GLES 3.0, WebGL 2
rules; `#include <GLES3/gl3.h>`, `${GASM_GL_SOURCE}`); the keyboard raw (`input_mode(KEYS_RAW)`) mapped to
DirectInput codes (`DIK_*`), which the original reads; pads from `input_pad(0..3)`; storage `gasm:storage`.
Data: asset `cd` (or `iso`, `rom`) = disc image, else the folder's files as assets (`--asset-dir`), listed with
`asset_count`/`asset_name` for `vfs_list`.

## Reverse engineering

- Ghidra project `nfspu`, programs `porsche.exe` (a copy of `game/Porsche.exe`, image base 0x400000,
  6,006 functions after analysis) and `dx7z.dll` (`game/Drivers/dx7z.dll`, base 0x60000000), via ghidra-cli:
  `ghidra decompile 0xADDR --project nfspu --program porsche.exe`, plus `x-ref`, `disasm`, `memory`, `function`.
- The bridge imported `dx7z.dll` without analysing it (0 functions): `tools/ghidra/AnalyzeAll.java` runs the
  auto-analysis (385 functions).
- Full dumps for grepping: `ghidra script run tools/ghidra/DumpAllNamed.java --project nfspu --program
  porsche.exe -- "$PWD/re/all.c"` (and `dx7z.dll` -> `re/dx7z.c`); `re/funcs.txt` is the address/name list.
- Global constructors run from MSVC's static-initializer table 0x5cb004..0x5cb780 before `WinMain` (no direct
  callers): ported ones go into `PORT_StaticInit` (`src/static_init.c`) in table order
  (docs/internals/porting.md).
- Source-file strings (`\real\pc\startup.c`, `d:\Nfs\Nfs5\frontend\fert\src\Window.cpp`, ...) identify modules.
- Maps and plan: `docs/re/architecture.md`, `docs/re/inventory.md`, `docs/re/port-plan.md`, `docs/re/thrash.md`.

## Docs site

VitePress in `docs/` (pnpm, `cd docs && pnpm install && pnpm build`), published to https://opennfspu.emdzej.pl by
`.github/workflows/pages.yml` (CNAME in `docs/public/CNAME`). Theme `docs/.vitepress/theme/porsche.css`. **No
emojis**, no version menu. The reverse-engineering notes (`docs/re/`, `docs/formats.md`) are not published: keep
them out of the nav, sidebar and site pages. Don't overstate the state (`docs/guide/status.md`); don't host or link copies of the
game. The "Built for gasm" badge is gasm's official one, hotlinked from
`https://gasm.emdzej.pl/badge/built-for-gasm-{light,dark,flat}.svg` (rules: https://gasm.emdzej.pl/dev/badge;
don't recolor, stretch or crop): light/dark by theme on the site, dark on the play page, flat in the README. New
notes go in the sidebar in `docs/.vitepress/config.ts`; dead links fail the build. Screenshots come from
`tools/screenshots.sh` (headless renders of the port; never game files).

## Browser player

`docs/public/play/` runs `opennfspu.wasm` in gasm's Worker mode: the user picks the installed game folder
(`data.js` checks `Porsche.exe` 2,129,965 bytes + `GameData/` + `FEData/`) and it is imported into OPFS with
csfs. **Never put game data in `docs/public/`.** `vendor/` is generated from the npm packages pinned in
`docs/package.json` (`docs/scripts/vendor-web.sh`; keep `@emdzej/gasm-host` equal to `GASM_VERSION`).

## gasm bundles

`tools/fetch-gasm-runner.sh <platform>` fetches the released `gasm-run` with ANGLE + SwiftShader (gasm:gl needs
them) into `.deps/gasm-runner-<platform>/`; `tools/package-gasm.sh` builds the macOS (universal `.app`), Linux
and Windows bundles. ANGLE sits next to gasm-run on Linux and Windows; in the `.app` it is in
`Resources/angle/` and the launcher passes `--gl-lib`, because codesign `--strict` allows only code in
`Frameworks/` and gasm-run reads SwiftShader's ICD file from the ANGLE folder. `tools/smoke-gasm-bundle.sh`
checks a bundle without data (`OPENNFSPU_SMOKE_DATA=<folder>[:<iso>]` adds hash runs).

## Releases

Plain semver tags, **no `v` prefix**: bump `project(OpenNFSPU VERSION ...)` in `CMakeLists.txt`, then
`git tag 0.1.0 && git push origin 0.1.0`. `release.yml` builds `opennfspu-<version>.wasm` and the gasm bundles
(macOS universal, Linux x86_64/arm64, Windows x86_64), smoke-tests each and attaches them.

## Commits

Imperative subject, body explaining why when non-obvious. Never commit `game/`, `porsche.exe`, `dx7z.dll`,
`re/`, `out/`, `build*/`, `.deps/`.
