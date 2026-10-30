# Make a release

Releases ship the gasm module and the gasm bundles only, no native app, and never any game data.

## Tag it

Tags are plain semantic versions, **without a `v` prefix**. Set the version in `CMakeLists.txt` first
(`project(OpenNFSPU VERSION 0.1.0 ...)`), commit, then:

```sh
git tag 0.1.0
git push origin 0.1.0
```

The release workflow (`.github/workflows/release.yml`) refuses a tag that differs from the CMake version.

## What the workflow does

| Job | What |
|---|---|
| `wasm` | builds `opennfspu-<version>.wasm` + `.sha256` (wasi-sdk and the gasm C SDK from `tools/fetch-gasm-sdk.sh`) |
| `gasm-macos` | packages `opennfspu-gasm-<version>-macos-universal.zip` (`Porsche Unleashed (gasm).app`, id `pl.emdzej.opennfspu.gasm`, signed ad hoc) and smoke-tests it |
| `gasm-package` | packages the `linux-x86_64`, `linux-arm64` (`.tar.gz`) and `windows-x86_64` (`.zip`) bundles |
| `gasm-smoke-linux` | smoke-tests the Linux bundles on x86_64 and on `ubuntu-24.04-arm` |
| `gasm-smoke-windows` | checks the Windows bundle: `gasm-run.exe` without data, `OpenNFSPU.cmd --help` and `--dry-run` |
| `release` | needs all of the above; creates the GitHub Release with every archive, the `.wasm` and their `.sha256` files, and `.github/release-notes.md` as the notes (followed by GitHub's generated changelog) |

Run it by hand from the Actions tab (*Release > Run workflow*) for a snapshot: the same files, version
`0.0.0-snapshot.<sha>`, kept as workflow artifacts and not published. CI (`ci.yml`) packages the bundles on
every push and pull request too, and smoke-tests the macOS and Linux x86_64 ones.

The gasm version is pinned in one place, `GASM_VERSION` in `tools/fetch-gasm-sdk.sh`: the module's SDK and
the bundled `gasm-run` both come from it. Keep `@emdzej/gasm-host` in `docs/package.json` in step.

## The bundles

`tools/fetch-gasm-runner.sh <platform>` downloads the released `gasm-run` and gasm's MIT `LICENSE` into
`.deps/gasm-runner-<platform>/`; `tools/package-gasm.sh` puts it together with the module, the launcher,
a `README.txt`, the licences and the icon:

```sh
tools/package-gasm.sh <version> <macos-universal|linux-x86_64|linux-arm64|windows-x86_64> <runner dir> <out dir>
```

`OPENNFSPU_WASM=<file>` picks the module (default `build-gasm/opennfspu.wasm`); it is always bundled as
`opennfspu.wasm`, which names gasm's storage for the saves. The launchers are in `tools/gasm-bundle/`
(`launch-macos.sh`, `opennfspu.sh` + `opennfspu-gasm.desktop`, `OpenNFSPU.cmd` + `opennfspu.ps1`). The icon is
OpenNFSPU's own mark (the site's favicon: a bend of road, no manufacturer's crest), drawn at any size by `tools/gasm-bundle/icon.py` (standard library only);
`tools/make-icns.sh` makes the macOS `.icns` with `iconutil`.

## Test a bundle locally

```sh
tools/fetch-gasm-sdk.sh    # then build build-gasm/opennfspu.wasm (Build from source)
tools/package-gasm.sh 0.0.0-dev macos-universal "$(tools/fetch-gasm-runner.sh macos-universal)" dist
tools/smoke-gasm-bundle.sh dist/opennfspu-gasm-0.0.0-dev-macos-universal.zip
OPENNFSPU_SMOKE_DATA=game tools/smoke-gasm-bundle.sh dist/opennfspu-gasm-0.0.0-dev-macos-universal.zip

```

Without data the smoke test checks the files (and on macOS the signature, bundle id, icon and the universal
runner), that `gasm-run` stops with OpenNFSPU's "game data not found" message and exit code 1, and the
launcher's `--help`, `--dry-run` and `--forget-data` against fake folders: an installed game, and folders
and files it must refuse (no `FEData`, no `Porsche.exe`, a wrong-size `Porsche.exe`, a `.cue` sheet), and an `.iso` that must go to the game as `--asset cd=`. It uses
a scratch `HOME` and `OPENNFSPU_GAME`, so it never opens a dialog or touches your saved location. With
`OPENNFSPU_SMOKE_DATA=<folder or .iso>[:<another>...]` it also runs the game headless through the launcher (`--headless 400`) and checks that
the hash line equals a direct `gasm-run` run of the same module (`SMOKE_GASM_RUN=<path>` compares with
another runner); with several sources, `game:NFS_PU.iso`, they must all hash the same.

Linux bundles can be tested on a Mac in Apple's `container`:

```sh
container run --rm -v "$PWD":/w -w /w debian:trixie-slim bash -c \
  'apt-get update -qq && apt-get install -y -qq libasound2t64 &&
   OPENNFSPU_SMOKE_DATA=game tools/smoke-gasm-bundle.sh dist/opennfspu-gasm-0.0.0-dev-linux-arm64.tar.gz'
# x86_64: add --arch amd64 --rosetta and use the linux-x86_64 bundle
```

The Windows launcher is exercised by the release workflow's `gasm-smoke-windows` job.
