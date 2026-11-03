# Playing in the browser

The [player](/play/){target="_self"} runs `opennfspu.wasm` in your browser with gasm's web runtime, in a
Web Worker. You need:

- the installed game folder, version 3.5, or an `.iso` image of it (see [Game data](/guide/game-data));
- a recent Chrome, Edge or Firefox, or Safari 17 and later: WebAssembly, Web Workers, **WebGL 2** and
  **OffscreenCanvas** (the game draws with WebGL 2 inside the worker);
- about 520 MB of browser storage to import the data once, or none to play without importing.

## Choosing the game

**Choose your game folder** and pick the installed game (or the folder above it), or **Choose a disc
image** and pick an `.iso` (or raw `.bin`) of it. The page checks a folder for `GameData`, `FEData` and a
`Porsche.exe` of version 3.5, and an image for an ISO 9660 volume (the game itself checks what is inside).
Then:

- **Import and play** copies the files the game reads into the site's private browser storage (OPFS). Later
  visits start at once with **Play**. **Remove imported data** deletes the copy.
- **Play without importing** reads the files from the folder or image you picked. You pick it again next time.

Nothing is uploaded: the files are read in your browser. Saves (the players and their careers, the
options, HUD layouts, track records and replays) are kept in the browser's storage for this site
(IndexedDB).

## Your saves

**Export saves (.zip)** downloads them as a zip with one file per save. **Import saves** takes such a zip,
or the save files themselves. Files with the same names are replaced, and the others are kept. Import while
no game runs: the game reads its saves when it starts.

The zip has the same layout as the folder where the desktop version keeps its saves
(`~/Library/Application Support/gasm/opennfspu/` on macOS, `~/.local/share/gasm/opennfspu/` on Linux,
`%APPDATA%\gasm\opennfspu\` on Windows; see [Installing](/guide/install)). So players move both ways: unpack
an export there, or zip that folder's files and import them here.

## While playing

The game draws at the size it is shown: use **Fullscreen** (or double-click) for the full resolution.
Holding <kbd>Esc</kbd> for a second stops the game; in fullscreen the browser takes <kbd>Esc</kbd> to leave
fullscreen first. The line under the game shows the frames per second.

## For developers

`?hashframes=N` runs N frames on virtual time and prints the same hash line as
`gasm-run --headless N` (with `input=` for scripted input, gasm-run's `--input` syntax, and `size=WxH` for
the drawing size; 1280x720 by default, the size `gasm-run --headless` draws at, so the hashes match). Other query parameters are passed to the game. `relay=` and `room=` join a network game's relay room ([Multiplayer](/guide/multiplayer)).
`tools/web-play-test.mjs` drives this page in headless Chrome.
