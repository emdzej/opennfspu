#!/bin/sh
# OpenNFSPU @VERSION@ (opennfspu.wasm) on the bundled gasm-run @GASM_VERSION@: Linux launcher.
#   ./opennfspu.sh [options] [DATA] [gasm-run options...]          (./opennfspu.sh --help)
# The game data location comes from the argument (then saved), else $XDG_CONFIG_HOME/opennfspu/data-location,
# else a zenity or kdialog chooser (when a display is available), else the usage text. Test hooks (no
# dialogs): OPENNFSPU_GAME=<folder|image> uses that data without saving it (and turns missing data into exit 2),
# OPENNFSPU_DRY_RUN=1 or --dry-run prints the gasm-run command instead of running it.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
CONF="${XDG_CONFIG_HOME:-$HOME/.config}/opennfspu"
LOCFILE="$CONF/data-location"
LOGDIR="${XDG_STATE_HOME:-$HOME/.local/state}/opennfspu"
LOG="$LOGDIR/gasm.log"
TITLE="Porsche Unleashed (gasm)"
EXE_SIZE=2129965   # Porsche.exe of the 3.5 update (CRC-32 fd2f5573)
DRY=${OPENNFSPU_DRY_RUN:-0}
CHANGE=0

usage() {
  cat <<EOF
OpenNFSPU @VERSION@ on gasm-run @GASM_VERSION@

  $0 [options] [DATA] [gasm-run options...]

DATA is your installed copy of Need for Speed: Porsche Unleashed (version 3.5): the folder with
GameData/, FEData/ and Porsche.exe, or an .iso image of that folder (or a raw .bin). It is saved in
  $LOCFILE
so later runs need no argument. Without one, a chooser opens (zenity or kdialog).

Options:
  --change-data      ask for the game data even if a location is saved
  --forget-data      delete the saved location and exit
  --install-desktop  add a menu entry (~/.local/share/applications/opennfspu-gasm.desktop) and exit
  --dry-run          print the gasm-run command instead of running it (also OPENNFSPU_DRY_RUN=1)
  --help             this text
Anything after DATA goes to gasm-run, e.g. --window 1280x960, --fullscreen, --mute.
OPENNFSPU_GAME=<DATA> uses that data for one run without saving it.

More: https://opennfspu.emdzej.pl/guide/install
EOF
}

# ci_find <dir> <name> <f|d>: the entry of <dir> called <name> in any letter case
ci_find() { find "$1" -mindepth 1 -maxdepth 1 -iname "$2" -type "$3" -print 2>/dev/null | head -n 1; }

# check_data <path>: exit 0 if usable; otherwise prints why
check_data() {
  if [ -f "$1" ]; then
    case "$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')" in
      *.iso | *.bin) return 0 ;;   # the game itself checks what is inside (exit 1 and its message if it isn't the game)
      *.cue) printf 'A .cue sheet only names the image: use the .bin file next to it.\n%s\n' "$1" ;;
      *.mdf | *.nrg) printf 'OpenNFSPU reads .iso and raw .bin images only: convert it, or use the game folder.\n%s\n' "$1" ;;
      *) printf 'This is a file: use the game folder (the one with GameData, FEData and Porsche.exe) or an .iso of it.\n%s\n' "$1" ;;
    esac
    return 1
  fi
  if [ ! -d "$1" ]; then
    printf 'The game data was not found at:\n%s\nChoose it again.\n' "$1"
    return 1
  fi
  if [ -z "$(ci_find "$1" GameData d)" ] || [ -z "$(ci_find "$1" FEData d)" ]; then
    printf 'This folder is not the installed Porsche Unleashed (it needs GameData and FEData):\n%s\n' "$1"; return 1
  fi
  exe=$(ci_find "$1" Porsche.exe f)
  if [ -z "$exe" ]; then printf 'This game folder has no Porsche.exe:\n%s\n' "$1"; return 1; fi
  size=$(wc -c <"$exe" | tr -d ' ')
  if [ "$size" != "$EXE_SIZE" ]; then
    printf 'Porsche.exe is %s bytes, not 2,129,965: OpenNFSPU needs the game updated to version 3.5.\n%s\n' "$size" "$1"
    return 1
  fi
  return 0
}

absolute() { # absolute path without a trailing slash
  p=$1
  case "$p" in /*) ;; *) p="$PWD/$p" ;; esac
  while [ "${#p}" -gt 1 ] && [ "${p%/}" != "$p" ]; do p=${p%/}; done
  printf '%s' "$p"
}

have_gui() { [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] && { command -v zenity >/dev/null 2>&1 || command -v kdialog >/dev/null 2>&1; }; }

# ask <message>: prints the chosen folder or image (empty on Quit)
ask() {
  pick="Choose the installed Porsche Unleashed (with GameData, FEData and Porsche.exe)"
  if command -v zenity >/dev/null 2>&1; then
    choice=$(zenity --question --title "$TITLE" --text "$1" --ok-label "Choose your game folder" \
      --cancel-label Quit --extra-button "Choose disc image" 2>/dev/null)
    rc=$?
    if [ "$rc" = 0 ]; then
      zenity --file-selection --directory --title "$pick" 2>/dev/null
    elif [ "$choice" = "Choose disc image" ]; then
      zenity --file-selection --title "Choose an .iso image of the installed game" \
        --file-filter "Disc images | *.iso *.ISO *.bin *.BIN" --file-filter "All files | *" 2>/dev/null
    fi
  else
    kdialog --title "$TITLE" --yesnocancel "$1" --yes-label "Choose your game folder" \
      --no-label "Choose disc image" --cancel-label Quit 2>/dev/null
    case $? in
      0) kdialog --title "$pick" --getexistingdirectory "$HOME" 2>/dev/null ;;
      1) kdialog --title "Choose an .iso image of the installed game" --getopenfilename "$HOME" "*.iso *.ISO *.bin *.BIN" 2>/dev/null ;;
    esac
  fi
}

error_box() {
  if command -v zenity >/dev/null 2>&1; then zenity --error --title "$TITLE" --text "$1" 2>/dev/null
  elif command -v kdialog >/dev/null 2>&1; then kdialog --title "$TITLE" --error "$1" 2>/dev/null; fi
}

install_desktop() {
  dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
  mkdir -p "$dir"
  sed -e "s|@DIR@|$HERE|g" "$HERE/opennfspu-gasm.desktop" >"$dir/opennfspu-gasm.desktop"
  echo "installed $dir/opennfspu-gasm.desktop"
}

INTRO="OpenNFSPU needs your copy of Need for Speed: Porsche Unleashed (it is not included), version 3.5.

Choose the folder of the installed game (it has GameData, FEData and Porsche.exe), or an .iso image of that folder.

Your choice is remembered; run opennfspu.sh --change-data to pick another."

while [ $# -gt 0 ]; do
  case "$1" in
    --help | -h) usage; exit 0 ;;
    --dry-run) DRY=1; shift ;;
    --change-data) CHANGE=1; shift ;;
    --forget-data) rm -f "$LOCFILE"; echo "forgot the game data location ($LOCFILE)"; exit 0 ;;
    --install-desktop) install_desktop; exit 0 ;;
    *) break ;;
  esac
done
DATAP="" SAVE=0
if [ $# -gt 0 ] && [ "${1#-}" = "$1" ]; then DATAP=$(absolute "$1"); SAVE=1; shift; fi
if [ -z "$DATAP" ] && [ -n "${OPENNFSPU_GAME:-}" ]; then DATAP=$(absolute "$OPENNFSPU_GAME"); fi
if [ -z "$DATAP" ] && [ "$CHANGE" = 0 ] && [ -f "$LOCFILE" ]; then DATAP=$(head -n 1 "$LOCFILE"); fi

MSG=$INTRO
[ -n "$DATAP" ] && { MSG=$(check_data "$DATAP") || true; }
while [ -z "$DATAP" ] || ! check_data "$DATAP" >/dev/null; do
  if [ "$DRY" = 1 ] || [ -n "${OPENNFSPU_GAME:-}" ] || ! have_gui; then
    [ -n "$DATAP" ] && printf '%s\n\n' "$MSG" >&2
    [ "$DRY" = 1 ] || [ -n "${OPENNFSPU_GAME:-}" ] || usage >&2
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

if [ -d "$DATAP" ]; then set -- --asset-dir "$DATAP" "$@"; else set -- --asset "cd=$DATAP" "$@"; fi
set -- "$HERE/gasm-run" "$HERE/opennfspu.wasm" "$@"
if [ "$DRY" = 1 ]; then
  for a in "$@"; do printf "'%s' " "$(printf '%s' "$a" | sed "s/'/'\\\\''/g")"; done
  echo
  exit 0
fi

if [ -t 1 ] || [ -t 2 ]; then exec "$@"; fi
# Started from a menu or file manager: keep the output in a log and show errors in a dialog.
mkdir -p "$LOGDIR"
{ echo "--- $(date '+%Y-%m-%d %H:%M:%S') OpenNFSPU @VERSION@, gasm-run @GASM_VERSION@"; echo "$*"; } >>"$LOG"
"$@" >>"$LOG" 2>&1
rc=$?
[ "$rc" = 0 ] || [ -n "${OPENNFSPU_GAME:-}" ] || error_box "OpenNFSPU stopped with an error:

$(tail -n 4 "$LOG")

$([ -f "$DATAP" ] && printf '\nA disc image must hold the installed game (GameData, FEData, Porsche.exe), not the setup disc.\n')
Full log: $LOG"
exit "$rc"
