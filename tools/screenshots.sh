#!/usr/bin/env bash
# Regenerate the documentation screenshots (docs/public/screenshots/*.jpg) from opennfspu.wasm, run headless
# by the released gasm-run on your own game data. They are renders of our port, nothing else: never put
# the game's own files in docs/public/. Never opens a window (and never uses macOS screencapture).
#
#   tools/screenshots.sh [game folder] [opennfspu.wasm]
# Defaults: ./game (the installed folder: GameData/, FEData/, Porsche.exe), build-gasm/opennfspu.wasm
# (build it first: AGENTS.md "gasm module"). Needs sips (macOS) to convert to JPEG. gasm:gl frames are
# drawn by ANGLE for --screenshot (the hashes don't depend on it).
set -euo pipefail
cd "$(dirname "$0")/.."
DATA=${1:-game}
WASM=${2:-build-gasm/opennfspu.wasm}
OUT=docs/public/screenshots
[ -f "$WASM" ] || { echo "no $WASM: build opennfspu.wasm first" >&2; exit 1; }
command -v sips >/dev/null || { echo "sips not found (macOS); convert $OUT/*.png yourself" >&2; exit 1; }
RUN="$(tools/fetch-gasm-runner.sh macos-universal)/gasm-run"
mkdir -p "$OUT"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cp "$WASM" "$tmp/opennfspu.wasm"   # a snapshot: other builds may replace build-gasm/opennfspu.wasm meanwhile

shot() {   # name frames [gasm-run options...]
  local name=$1 frames=$2; shift 2
  "$RUN" "$tmp/opennfspu.wasm" --asset-dir "$DATA" --headless "$frames" --screenshot "$tmp/$name.png" "$@" \
    2>/dev/null | tr '\n' ' '
  sips -s format jpeg -s formatOptions 80 "$tmp/$name.png" --out "$OUT/$name.jpg" >/dev/null
  echo "-> $OUT/$name.jpg ($(($(wc -c < "$OUT/$name.jpg") / 1024)) KB)"
}

# Add a shot per game state as the port reaches it (frontend, showroom, tracks, replays).
shot start 60
