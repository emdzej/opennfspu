OpenNFSPU needs your own copy of Need for Speed: Porsche Unleashed, installed and updated to version 3.5 (the
folder with `GameData`, `FEData` and `Porsche.exe`). No game data is included. OpenNFSPU is early work: see the
[status](https://opennfspu.emdzej.pl/guide/status).

- **macOS**: unzip and open `Porsche Unleashed (gasm).app` (ad hoc signed, not notarized: the first time, right-click, Open).
- **Linux** (x86_64, arm64): unpack and run `./opennfspu.sh` (`--install-desktop` adds a menu entry).
- **Windows** (x86_64): unzip and run `OpenNFSPU.cmd`.
- **Any system**: `gasm-run opennfspu-<version>.wasm --asset-dir <your game folder>` with gasm 0.16.1 or newer.

The launchers ask for the game folder once and remember it (`--change-data` picks another). Every file has a
`.sha256` next to it. Install guide: https://opennfspu.emdzej.pl/guide/install

Or play in the browser: https://opennfspu.emdzej.pl/play/
