# Installing

OpenNFSPU ships as one WebAssembly module, `opennfspu.wasm`, plus ready-to-run **bundles** of that module
with gasm's native runner `gasm-run` for macOS, Linux and Windows. None of them contains any of the game:
you need your own copy, installed and updated to version 3.5 (see [Game data](/guide/game-data)).
Single player is playable and multiplayer is not ported yet: see [Status](/guide/status).

## Download a bundle

Each [release](https://github.com/emdzej/opennfspu/releases) has:

| System | File | Start |
|---|---|---|
| macOS 11 or later (Apple Silicon and Intel) | `opennfspu-gasm-<version>-macos-universal.zip` | `Porsche Unleashed (gasm).app` |
| Linux x86_64 / arm64 | `opennfspu-gasm-<version>-linux-<arch>.tar.gz` | `./opennfspu.sh` |
| Windows x86_64 | `opennfspu-gasm-<version>-windows-x86_64.zip` | `OpenNFSPU.cmd` |
| any system with gasm-run 0.16.1 or newer | `opennfspu-<version>.wasm` | `gasm-run opennfspu-<version>.wasm --asset-dir <your game folder>` |

Every file has a `.sha256` next to it:

```sh
shasum -a 256 -c opennfspu-gasm-0.1.0-macos-universal.zip.sha256     # macOS
sha256sum -c opennfspu-gasm-0.1.0-linux-x86_64.tar.gz.sha256          # Linux
```

On Windows: `Get-FileHash opennfspu-gasm-0.1.0-windows-x86_64.zip` in PowerShell, and compare with the
`.sha256` file. Unpack the archive anywhere; each bundle has a `README.txt` with these instructions.

## First run: choose your game folder

The first time, the launcher asks where the game is: choose the installed folder, with `GameData`,
`FEData` and `Porsche.exe`, or an `.iso` (or raw `.bin`) image of it (see
[Game data](/guide/game-data#or-an-iso-image-of-the-folder)). The launcher checks a folder and says what is
missing or wrong (no `FEData`, a `Porsche.exe` that isn't version 3.5, a `.cue` sheet); an image goes to the
game as it is (`--asset cd=<image>`), and the game says "game data not found" if the installed game isn't
in it. Your choice is remembered.

| | macOS | Linux | Windows |
|---|---|---|---|
| How it asks | a dialog, then a folder chooser | zenity or kdialog, if installed; otherwise pass the folder: `./opennfspu.sh /path/to/game` | a dialog, then a folder chooser |
| Saved in | `~/Library/Application Support/OpenNFSPU/data-location` | `~/.config/opennfspu/data-location` | `%APPDATA%\OpenNFSPU\data-location` |
| Choose again | hold <kbd>Option</kbd> while opening the app | `./opennfspu.sh --change-data` | `OpenNFSPU.cmd --change-data` |
| Log | `~/Library/Logs/OpenNFSPU/gasm.log` | the terminal, or `~/.local/state/opennfspu/gasm.log` when started from a menu | the console window and `%LOCALAPPDATA%\OpenNFSPU\gasm.log` |
| Saves | `~/Library/Application Support/gasm/opennfspu/` | `~/.local/share/gasm/opennfspu/` | `%APPDATA%\gasm\opennfspu\` |

The saves are gasm's storage for `opennfspu`, the same namespace the browser player uses.

## Launcher options

```sh
opennfspu.sh [options] [DATA] [gasm-run options...]
```

| Option | What |
|---|---|
| `DATA` | the game folder or image for this run; it is saved for the next ones |
| `--change-data` | ask for the game folder even if one is saved |
| `--forget-data` | delete the saved location and exit |
| `--dry-run` | print the `gasm-run` command instead of running it |
| `--install-desktop` | (Linux) add a menu entry, `~/.local/share/applications/opennfspu-gasm.desktop` |
| `--help` | the options and where things are kept |

Anything after the data goes to `gasm-run`, such as `--window 1280x960`, `--fullscreen` or `--mute` (see
[Running on gasm](/guide/gasm)). `OPENNFSPU_GAME=<folder|image>` uses that folder or image for one run without saving it,
and makes a missing or wrong folder an error (exit code 2) instead of a dialog: that is how the bundles are
tested.

## macOS: Gatekeeper

The app is signed ad hoc, not notarized, so the first start is blocked as from an "unidentified
developer". Right-click `Porsche Unleashed (gasm).app` and choose **Open**; on macOS 15 and later, open it
once, then **System Settings > Privacy & Security > Open Anyway**. Or:

```sh
xattr -dr com.apple.quarantine "Porsche Unleashed (gasm).app"
```

## Linux: libraries

`gasm-run` needs ALSA for sound (`libasound2`, or `libasound2t64` on Debian 13, Ubuntu 24.04 and newer) and
a Vulkan capable graphics driver. For the folder chooser, install `zenity` or `kdialog`.

## Windows: SmartScreen

`gasm-run.exe` and the launcher are not signed. If SmartScreen stops them, click **More info**, then **Run
anyway**. `OpenNFSPU.cmd` runs `opennfspu.ps1` with the built-in PowerShell.

## In the browser

Nothing to install: open the [player](/play/){target="_self"}. See [Playing in the browser](/guide/playing).

## From source

See [Build from source](/howto/build-from-source).
