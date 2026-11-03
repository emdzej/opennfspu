# Build from source

OpenNFSPU is C11 with CMake. The product is `opennfspu.wasm`, the gasm module; a native build exists for
the tools and the headless tests.

```sh
git clone https://github.com/emdzej/opennfspu && cd opennfspu
```

## opennfspu.wasm

`tools/fetch-gasm-sdk.sh` downloads wasi-sdk (clang and the C library for wasm32) and gasm's C SDK
(`gasm.h`, the GLES 3.0 headers with `gasm_gl.c`, and a CMake toolchain) into `.deps/`. The gasm version is
pinned there (`GASM_VERSION`, currently 0.16.1).

```sh
tools/fetch-gasm-sdk.sh
cmake -S . -B build-gasm -DOPENNFSPU_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk"
cmake --build build-gasm -j                    # -> build-gasm/opennfspu.wasm
```

Run it with the released runner, fetched into `.deps/` at the pinned version:

```sh
RUN="$(tools/fetch-gasm-runner.sh macos-universal)/gasm-run"   # or linux-x86_64, linux-arm64, windows-x86_64
$RUN build-gasm/opennfspu.wasm --asset-dir game
```

## Native build (tools and tests)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
(cd build && ctest --output-on-failure)
```

The tests read the game data from `./game`, or `OPENNFSPU_DATA=<folder>`; without data they are skipped.

## Game data in the checkout

The tests and scripts look for the installed game in `./game` (see [Extract the game](/howto/extract-game)).
It is git-ignored, as are `porsche.exe` and `dx7z.dll` (working copies for Ghidra), `out/` (decoded
assets and renders) and `re/` (decompiler dumps). Never commit them, and never copy data from the
executable into the source: tables are read from it at run time.

## The site

This site is VitePress in `docs/` (Node 22, pnpm):

```sh
cd docs && pnpm install
scripts/vendor-web.sh        # the play page's gasm and csfs runtime, from the pinned npm packages
scripts/copy-wasm.sh         # build-gasm/opennfspu.wasm -> public/play/ (skip it if there is no module yet)
pnpm dev                     # http://localhost:5173 (the player: /play/index.html)
pnpm build                   # -> docs/.vitepress/dist
```

The site builds without the module; the player then says it can't load `opennfspu.wasm`.
`tools/screenshots.sh` regenerates the screenshots from `opennfspu.wasm`.
