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
  # (the 4:3 picture without the frame's side bars; WIDE=1: a 16:9 one, the whole frame)
  [ "${WIDE:-0}" = 1 ] || sips -c 720 960 "$tmp/$name.png" >/dev/null
  sips -s format jpeg -s formatOptions 80 "$tmp/$name.png" --out "$OUT/$name.jpg" >/dev/null
  echo "-> $OUT/$name.jpg ($(($(wc -c < "$OUT/$name.jpg") / 1024)) KB)"
}

# A shot per game state (add them as the port reaches more). Races: start=384 begins straight in the
# camera's mode, cam=3 a near chase camera.
shot menu 200 --param intro=0
shot quickrace 120 --param intro=0 --param fe=quickrace
shot castle 500 --param start=384 --param drive=castle --param cam=3
shot alps 900 --param start=384 --param drive=alps --param cam=3 --param opponents=3
shot canyon 700 --param start=384 --param drive=canyon --param cam=3
shot cockpit 500 --param start=384 --param drive=autobahn --param cam=1
WIDE=1 shot alps-wide 900 --param start=384 --param drive=alps --param cam=3 --param opponents=3 --param aspect=16:9 --param res=1280x720
