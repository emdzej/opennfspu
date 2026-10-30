#!/usr/bin/env bash
# Check a gasm bundle from tools/package-gasm.sh without game data (CI runs it on each bundle's own OS):
#   tools/smoke-gasm-bundle.sh <opennfspu-gasm-...-macos-universal.zip | ...-linux-<arch>.tar.gz>
# Unpacks it, checks the files, runs the bundled gasm-run without data (it must stop with OpenNFSPU's "game
# data not found" message and exit code 1, not crash), and the launcher's --help and --dry-run paths against
# fake data (an installed folder with GameData/, FEData/ and a 2,129,965-byte Porsche.exe, and folders and
# files that must be refused, and an .iso that goes to the game as asset cd; no dialogs: OPENNFSPU_GAME,
# --dry-run, a scratch HOME).
# With OPENNFSPU_SMOKE_DATA=<installed game folder or an .iso of it> it also runs the game through the
# launcher (headless) and checks that its hash line equals a direct gasm-run on the same module and
# parameters (the bundled gasm-run, or SMOKE_GASM_RUN=<another gasm-run>). Several, separated by ':', must
# all give the same hashes: OPENNFSPU_SMOKE_DATA=game:/path/NFS_PU.iso checks the image against the folder.
set -euo pipefail
ARCHIVE=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
case "$ARCHIVE" in
  *.zip) (cd "$T" && unzip -q "$ARCHIVE") ;;
  *.tar.gz) tar xzf "$ARCHIVE" -C "$T" ;;
  *) fail "unknown archive $ARCHIVE" ;;
esac
DIR=$(find "$T" -mindepth 1 -maxdepth 1 -type d -name 'opennfspu-gasm-*' | head -n 1)
[ -n "$DIR" ] || fail "no opennfspu-gasm-* folder in the archive"
for f in README.txt LICENSE LICENSE-gasm; do [ -s "$DIR/$f" ] || fail "missing $f"; done
if [ -d "$DIR/Porsche Unleashed (gasm).app" ]; then
  APP="$DIR/Porsche Unleashed (gasm).app"
  RUN="$APP/Contents/MacOS/gasm-run"; WASM="$APP/Contents/Resources/opennfspu.wasm"; L="$APP/Contents/MacOS/OpenNFSPU"
  codesign --verify --strict "$APP" || fail "code signature"
  [ "$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$APP/Contents/Info.plist")" = pl.emdzej.opennfspu.gasm ] ||
    fail "bundle id"
  [ -s "$APP/Contents/Resources/opennfspu.icns" ] || fail "icon"
  lipo -info "$RUN"
else
  RUN="$DIR/gasm-run"; WASM="$DIR/opennfspu.wasm"; L="$DIR/opennfspu.sh"
  [ -s "$DIR/opennfspu.png" ] || fail "icon"
  [ -s "$DIR/opennfspu-gasm.desktop" ] || fail "desktop entry"
fi
if [ ! -x "$RUN" ] || [ ! -s "$WASM" ] || [ ! -x "$L" ]; then fail "gasm-run, opennfspu.wasm or the launcher missing"; fi
grep -q "gasm-run [0-9]" "$DIR/README.txt" || fail "README does not name the gasm version"
grep -q "version 3.5" "$DIR/README.txt" || fail "README does not name the game version"
if grep -qi "freeware" "$DIR/README.txt"; then fail "README calls the game freeware"; fi

# 1. No data: a clean error from the game, not a crash.
set +e
out=$(cd "$T" && "$RUN" "$WASM" --headless 5 --mute 2>&1); rc=$?
set -e
echo "$out"
[ "$rc" = 1 ] || fail "gasm-run without data: exit $rc, expected 1"
echo "$out" | grep -q "game data not found" || fail "no 'game data not found' message"

# 2. Launcher: help, and the command it builds (no dialogs, scratch HOME so nothing real is touched).
REAL_HOME=$HOME
export HOME="$T/home" XDG_CONFIG_HOME="$T/home/.config" XDG_STATE_HOME="$T/home/.local/state"
mkdir -p "$HOME"
"$L" --help | grep -q "OpenNFSPU" || fail "--help"
# fakes: the installed game (any letter case, a space in the path), and wrong ones
G="$T/My Porsche Unleashed"; mkdir -p "$G/gamedata" "$G/FEDATA"
dd if=/dev/zero of="$G/porsche.EXE" bs=2129965 count=1 2>/dev/null
mkdir -p "$T/empty" "$T/wrongexe/GameData" "$T/wrongexe/FEData" "$T/noexe/GameData" "$T/noexe/FEData" "$T/nofe/GameData"
printf 'MZ' >"$T/wrongexe/Porsche.exe"
cp "$G/porsche.EXE" "$T/nofe/Porsche.exe"
: >"$T/NFS PU.iso"; : >"$T/NFS_PU.cue"; : >"$T/NFS_PU.mdf"
OPENNFSPU_GAME="$G" "$L" --dry-run --mute | tee "$T/cmd"
{ grep -q -- "--asset-dir" "$T/cmd" && grep -q "My" "$T/cmd" && grep -q -- "--mute" "$T/cmd"; } || fail "dry run with an installed folder"
for bad in empty wrongexe noexe nofe NFS_PU.cue NFS_PU.mdf missing; do
  set +e
  OPENNFSPU_GAME="$T/$bad" "$L" --dry-run >/dev/null 2>"$T/err"; rc=$?
  set -e
  [ "$rc" = 2 ] || fail "$bad accepted (exit $rc, expected 2)"
  echo "refused $bad: $(tr '\n' ' ' <"$T/err")"
done
grep -q "2,129,965" <(OPENNFSPU_GAME="$T/wrongexe" "$L" --dry-run 2>&1 || true) || fail "no exe size message"
grep -q "\.bin file" <(OPENNFSPU_GAME="$T/NFS_PU.cue" "$L" --dry-run 2>&1 || true) || fail "no .cue message"
# an image goes to the game as asset cd (what is inside is the game's to check)
OPENNFSPU_GAME="$T/NFS PU.iso" "$L" --dry-run | tee "$T/cmd-iso"
{ grep -q -- "--asset" "$T/cmd-iso" && grep -q "cd=" "$T/cmd-iso" && ! grep -q -- "--asset-dir" "$T/cmd-iso"; } || fail "dry run with an .iso"
# the data as an argument (saved, but not by a dry run), then --forget-data
"$L" --dry-run "$G" --mute | grep -q -- "--mute" || fail "dry run with the data as an argument"
if "$L" --dry-run </dev/null >/dev/null 2>&1; then fail "no data, dry run: should exit non-zero"; fi
LOCS=("$XDG_CONFIG_HOME/opennfspu/data-location" "$HOME/Library/Application Support/OpenNFSPU/data-location")
for f in "${LOCS[@]}"; do [ ! -e "$f" ] || fail "a dry run saved the data location"; done
"$L" --forget-data | grep -q "forgot" || fail "--forget-data"

# 3. Optional: the real game with the user's data, through the launcher, against a direct gasm-run.
if [ -n "${OPENNFSPU_SMOKE_DATA:-}" ]; then
  REF=${SMOKE_GASM_RUN:-$RUN}
  case "$REF" in /*) ;; *) REF="$PWD/$REF" ;; esac
  ARGS=(--headless 400 --mute)
  first=""
  IFS=: read -r -a SOURCES <<<"$OPENNFSPU_SMOKE_DATA"
  for src in "${SOURCES[@]}"; do
    if [ -d "$src" ]; then DATA=$(cd "$src" && pwd); SRC=(--asset-dir "$DATA")
    else DATA=$(cd "$(dirname "$src")" && pwd)/$(basename "$src"); SRC=(--asset "cd=$DATA"); fi
    rm -f "$HOME/Library/Logs/OpenNFSPU/gasm.log" "$XDG_STATE_HOME/opennfspu/gasm.log"
    OPENNFSPU_GAME="$DATA" "$L" "${ARGS[@]}" >"$T/launch.out" 2>&1 || fail "the launcher's run on $DATA failed: $(tail -n 3 "$T/launch.out")"
    # the launcher logs when there is no terminal (always on macOS)
    cat "$T/launch.out" "$HOME/Library/Logs/OpenNFSPU/gasm.log" "$XDG_STATE_HOME/opennfspu/gasm.log" 2>/dev/null >"$T/launch.all" || true
    HOME=$REAL_HOME "$REF" "$WASM" "${SRC[@]}" "${ARGS[@]}" >"$T/direct.out" 2>&1 || fail "the direct run on $DATA failed"
    grep -q "game data from" "$T/launch.all" || fail "the game did not find the data in $DATA"
    a=$(grep -E "^video_fnv32=" "$T/launch.all" | tail -n 1); b=$(grep -E "^video_fnv32=" "$T/direct.out" | tail -n 1)
    echo "$(basename "$DATA"): launcher: $a"; echo "$(basename "$DATA"): direct:   $b"
    grep -q "frames=400 " "$T/launch.all" || fail "the game did not run 400 frames"
    if [ -z "$a" ] || [ "$a" != "$b" ]; then fail "the launcher's hashes differ from gasm-run's ($DATA)"; fi
    if [ -z "$first" ]; then first=$a; elif [ "$a" != "$first" ]; then fail "$DATA hashes differently from ${SOURCES[0]}"; fi
  done
fi
echo "PASS $(basename "$ARCHIVE")"
