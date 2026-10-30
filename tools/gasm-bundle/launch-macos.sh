#!/bin/bash
# Launcher of Porsche Unleashed (gasm).app: OpenNFSPU @VERSION@ (opennfspu.wasm) on the bundled gasm-run @GASM_VERSION@.
# Installed as Contents/MacOS/OpenNFSPU by tools/package-gasm.sh. macOS ships bash 3.2: no bash 4 features.
#
# Finds the game data (remembered in ~/Library/Application Support/OpenNFSPU/data-location), asks for it
# with a dialog when it is missing, then runs gasm-run --asset-dir <folder> or --asset cd=<image>; its output goes to
# ~/Library/Logs/OpenNFSPU/gasm.log.
# Test hooks (no dialogs): OPENNFSPU_GAME=<folder|image> uses that data without saving it (and turns missing data into
# exit 2), OPENNFSPU_DRY_RUN=1 or --dry-run prints the gasm-run command instead of running it. --help: the options.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
RES="$(cd "$HERE/../Resources" && pwd)"
CONF="$HOME/Library/Application Support/OpenNFSPU"
LOCFILE="$CONF/data-location"
LOGDIR="$HOME/Library/Logs/OpenNFSPU"
LOG="$LOGDIR/gasm.log"
TITLE="Porsche Unleashed (gasm)"
EXE_SIZE=2129965   # Porsche.exe of the 3.5 update (CRC-32 fd2f5573)
DRY=${OPENNFSPU_DRY_RUN:-0}
CHANGE=0

usage() {
  cat <<EOF
Porsche Unleashed (gasm).app: OpenNFSPU @VERSION@ on gasm-run @GASM_VERSION@

  open "Porsche Unleashed (gasm).app" [--args [options] [DATA] [gasm-run options...]]
  "Porsche Unleashed (gasm).app/Contents/MacOS/OpenNFSPU" [options] [DATA] [gasm-run options...]

DATA is your installed copy of Need for Speed: Porsche Unleashed (version 3.5): the folder with
GameData/, FEData/ and Porsche.exe, or an .iso image of that folder (or a raw .bin). It is remembered in
  $LOCFILE
Without one the app asks for it. Hold Option while opening the app (or pass --change-data) to pick
another; --forget-data deletes the saved location.

Options:
  --change-data  ask for the game data even if a location is saved
  --forget-data  delete the saved location and exit
  --dry-run      print the gasm-run command instead of running it (also OPENNFSPU_DRY_RUN=1)
  --help         this text
Anything after DATA goes to gasm-run, e.g. --window 1280x960, --fullscreen, --mute.
OPENNFSPU_GAME=<DATA> uses that data for one run without saving it.

Log: $LOG
More: https://opennfspu.emdzej.pl/guide/install
EOF
}

# ci_find <dir> <name> <f|d>: the entry of <dir> called <name> in any letter case
ci_find() { find "$1" -mindepth 1 -maxdepth 1 -iname "$2" -type "$3" -print 2>/dev/null | head -n 1; }

# check_data <path>: exit 0 if usable; otherwise prints why
check_data() {
  local p=$1 exe size
  if [ -f "$p" ]; then
    case "$(printf '%s' "$p" | tr '[:upper:]' '[:lower:]')" in
      *.iso | *.bin) return 0 ;;   # the game itself checks what is inside (exit 1 and its message if it isn't the game)
      *.cue) echo "A .cue sheet only names the image: choose the .bin file next to it."; echo "$p" ;;
      *.mdf | *.nrg) echo "OpenNFSPU reads .iso and raw .bin images only: convert it, or choose the game folder."; echo "$p" ;;
      *) echo "This is a file: choose the game folder (the one with GameData, FEData and Porsche.exe) or an .iso of it."; echo "$p" ;;
    esac
    return 1
  fi
  if [ ! -d "$p" ]; then
    echo "The game data was not found at:"
    echo "$p"
    echo "Choose it again."
    return 1
  fi
  if [ -z "$(ci_find "$p" GameData d)" ] || [ -z "$(ci_find "$p" FEData d)" ]; then
    echo "This folder is not the installed Porsche Unleashed (it needs GameData and FEData):"; echo "$p"; return 1
  fi
  exe=$(ci_find "$p" Porsche.exe f)
  if [ -z "$exe" ]; then echo "This game folder has no Porsche.exe:"; echo "$p"; return 1; fi
  size=$(wc -c <"$exe" | tr -d ' ')
  if [ "$size" != "$EXE_SIZE" ]; then
    echo "Porsche.exe is $size bytes, not 2,129,965: OpenNFSPU needs the game updated to version 3.5."
    echo "$p"; return 1
  fi
  return 0
}

# absolute <path>: absolute path without a trailing slash
absolute() {
  local p=$1
  case "$p" in /*) ;; *) p="$PWD/$p" ;; esac
  while [ "${#p}" -gt 1 ] && [ "${p%/}" != "$p" ]; do p=${p%/}; done
  printf '%s' "$p"
}

option_held() { # the Option key is down (NSEvent modifier flag 1 << 19)
  [ "$(osascript -l JavaScript -e 'ObjC.import("AppKit"); ($.NSEvent.modifierFlags & 0x80000) ? "1" : "0"' 2>/dev/null)" = 1 ]
}

# ask <message>: a dialog, then a folder or file chooser; prints the chosen path (empty on Quit)
ask() {
  local choice
  choice=$(osascript -e 'on run argv' \
    -e 'button returned of (display dialog (item 1 of argv) with title (item 2 of argv) buttons {"Quit", "Choose disc image…", "Choose your game folder…"} default button 3 cancel button 1 with icon note)' \
    -e 'end run' "$1" "$TITLE" 2>/dev/null) || return 0
  case "$choice" in
    "Choose your game folder…")
      osascript -e 'POSIX path of (choose folder with prompt "Choose the installed Porsche Unleashed (the folder with GameData, FEData and Porsche.exe):")' 2>/dev/null ;;
    "Choose disc image…")
      osascript -e 'POSIX path of (choose file with prompt "Choose an .iso image of the installed game (or a raw .bin):")' 2>/dev/null ;;
  esac
}

INTRO="OpenNFSPU needs your copy of Need for Speed: Porsche Unleashed (it is not included), version 3.5.

Choose the folder of the installed game (it has GameData, FEData and Porsche.exe), or an .iso image of that folder.

Your choice is remembered. To change it later, hold Option while opening the app."

# Launcher options, then optional game data, then gasm-run's options.
while [ $# -gt 0 ]; do
  case "$1" in
    --help | -h) usage; exit 0 ;;
    --dry-run) DRY=1; shift ;;
    --change-data) CHANGE=1; shift ;;
    --forget-data) rm -f "$LOCFILE"; echo "forgot the game data location ($LOCFILE)"; exit 0 ;;
    -psn_*) shift ;;   # process serial number from older Finder launches
    *) break ;;
  esac
done
DATAP="" SAVE=0
if [ $# -gt 0 ] && [ "${1#-}" = "$1" ]; then DATAP=$(absolute "$1"); SAVE=1; shift; fi
if [ -z "$DATAP" ] && [ -n "${OPENNFSPU_GAME:-}" ]; then DATAP=$(absolute "$OPENNFSPU_GAME"); fi
if [ -z "$DATAP" ] && [ "$CHANGE" = 0 ] && [ -f "$LOCFILE" ]; then
  DATAP=$(head -n 1 "$LOCFILE")
  if [ "$DRY" = 0 ] && option_held; then DATAP=""; fi
fi

MSG=$INTRO
[ -n "$DATAP" ] && { MSG=$(check_data "$DATAP") || true; }
while [ -z "$DATAP" ] || ! check_data "$DATAP" >/dev/null; do
  if [ "$DRY" = 1 ] || [ -n "${OPENNFSPU_GAME:-}" ]; then   # never open dialogs in test runs
    echo "no usable game data${DATAP:+: $MSG}" >&2
    exit 2
  fi
  DATAP=$(ask "$MSG")
  [ -n "$DATAP" ] || exit 0
  DATAP=$(absolute "$DATAP")
  SAVE=1
  MSG=$(check_data "$DATAP") || true
done
if [ "$SAVE" = 1 ] && [ "$DRY" = 0 ]; then
  mkdir -p "$CONF" && printf '%s\n' "$DATAP" >"$LOCFILE"
fi

if [ -d "$DATAP" ]; then DATA=(--asset-dir "$DATAP"); else DATA=(--asset "cd=$DATAP"); fi
# ANGLE (with SwiftShader for --gl-software) is in Resources/angle/ (see package-gasm.sh).
CMD=("$HERE/gasm-run" "$RES/opennfspu.wasm" --gl-lib "$RES/angle" "${DATA[@]}" "$@")
if [ "$DRY" = 1 ]; then printf '%q ' "${CMD[@]}"; echo; exit 0; fi

mkdir -p "$LOGDIR"
{ echo "--- $(date '+%Y-%m-%d %H:%M:%S') OpenNFSPU @VERSION@, gasm-run @GASM_VERSION@"; printf '%q ' "${CMD[@]}"; echo; } >>"$LOG"
"${CMD[@]}" >>"$LOG" 2>&1
rc=$?
IMGHINT=""
[ -f "$DATAP" ] && IMGHINT="
A disc image must hold the installed game (GameData, FEData, Porsche.exe), not the setup disc: hold Option while opening the app to choose another.
"
if [ "$rc" != 0 ] && [ -z "${OPENNFSPU_GAME:-}" ]; then   # no dialog in test runs
  osascript -e 'on run argv' \
    -e 'display alert "OpenNFSPU stopped with an error" message (item 1 of argv) as critical' -e 'end run' \
    "$(tail -n 4 "$LOG")
${IMGHINT}
Full log: $LOG" >/dev/null 2>&1
fi
exit "$rc"
