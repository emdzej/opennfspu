#!/usr/bin/env bash
# Package opennfspu.wasm with a released gasm-run into a double-clickable bundle (no game data inside).
#   tools/package-gasm.sh <version> <platform> <gasm-run dir> <out dir>
# <platform>: macos-universal, linux-x86_64, linux-arm64 or windows-x86_64. <gasm-run dir> holds gasm-run[.exe]
# and gasm's LICENSE (tools/fetch-gasm-runner.sh <platform> makes one). OPENNFSPU_WASM=<file> picks the module
# (default build-gasm/opennfspu.wasm); it is always bundled as opennfspu.wasm, which is also gasm's storage
# namespace for the saves (the same as the browser player's, "opennfspu"). Produces in <out dir>:
#   macos-universal  Porsche Unleashed (gasm).app, opennfspu-gasm-<version>-macos-universal.zip
#   linux-<arch>     opennfspu-gasm-<version>-linux-<arch>.tar.gz
#   windows-x86_64   opennfspu-gasm-<version>-windows-x86_64.zip
# each archive with a .sha256 next to it. The macOS app is signed ad hoc when codesign is available.
# The launchers are in tools/gasm-bundle/, the icon is drawn by tools/gasm-bundle/icon.py (tools/make-icns.sh on macOS).
# No AOT .cwasm: gasm-run --compile only targets the host it runs on, so it can't be made for the other
# platforms (or both halves of the universal app) at packaging time.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# ANGLE (libEGL, libGLESv2, SwiftShader + its ICD file) for gasm:gl, from the runner dir
# (tools/fetch-gasm-runner.sh), with its notices. gasm-run finds it next to itself or in ../Frameworks (.app).
angle() {
  local dest=$1 f n=0
  for f in "$RUNDIR"/*EGL* "$RUNDIR"/*GLESv2* "$RUNDIR"/*swiftshader* "$RUNDIR"/ANGLE-NOTICES.txt; do
    if [ -f "$f" ]; then cp "$f" "$dest/"; n=$((n + 1)); fi
  done
  if [ "$n" -lt 3 ]; then echo "no ANGLE in $RUNDIR (tools/fetch-gasm-runner.sh fetches it)" >&2; exit 1; fi
}
usage="usage: package-gasm.sh <version> <macos-universal|linux-x86_64|linux-arm64|windows-x86_64> <gasm-run dir> <out dir>"
VERSION=${1:?$usage}; PLATFORM=${2:?$usage}; RUNDIR=${3:?$usage}; OUT=${4:?$usage}
WASM=${OPENNFSPU_WASM:-$ROOT/build-gasm/opennfspu.wasm}
GASM_VERSION=$(cat "$RUNDIR/VERSION" 2>/dev/null || "$ROOT/tools/fetch-gasm-sdk.sh" --version)
SRC="$ROOT/tools/gasm-bundle"
EXE=""; [[ "$PLATFORM" == windows-* ]] && EXE=.exe
case "$PLATFORM" in macos-universal | linux-x86_64 | linux-arm64 | windows-x86_64) ;; *) echo "$usage" >&2; exit 2 ;; esac
for f in "$WASM" "$RUNDIR/gasm-run$EXE" "$RUNDIR/LICENSE" "$ROOT/LICENSE"; do
  [ -f "$f" ] || { echo "missing $f (tools/fetch-gasm-runner.sh $PLATFORM fetches the runner and its LICENSE)" >&2; exit 1; }
done
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
NAME="opennfspu-gasm-$VERSION-$PLATFORM"

# fill <template> <out>: substitute the versions
fill() { sed -e "s|@VERSION@|$VERSION|g" -e "s|@GASM_VERSION@|$GASM_VERSION|g" "$1" >"$2"; }
crlf() { sed -e 's/\r*$/\r/' "$1" >"$1.tmp" && mv "$1.tmp" "$1"; }
sha() { (cd "$OUT" && if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi >"$1.sha256"); }

# readme <out file>: README.txt for this platform (~, $USER, %APPDATA% and backslashes are meant literally)
# shellcheck disable=SC2088,SC2016,SC1003
readme() {
  local start data_how change saves logs lic=""
  [ -n "$EXE" ] && lic=.txt
  case "$PLATFORM" in
    macos-*)
      start='Open "Porsche Unleashed (gasm).app". It is signed ad hoc, not notarized: the first time,
right-click it and choose Open (on macOS 15 and later: open it once, then System Settings, Privacy &
Security, Open Anyway), or run: xattr -dr com.apple.quarantine "Porsche Unleashed (gasm).app"'
      data_how='The first time, the app asks for your game data: "Choose your game folder..." and pick the
installed game (the folder with GameData, FEData and Porsche.exe).'
      change='Hold Option while opening the app to choose another folder, or delete
  ~/Library/Application Support/OpenNFSPU/data-location
From Terminal: "Porsche Unleashed (gasm).app/Contents/MacOS/OpenNFSPU" --help'
      saves='~/Library/Application Support/gasm/opennfspu/'
      logs='~/Library/Logs/OpenNFSPU/gasm.log' ;;
    linux-*)
      start='Run ./opennfspu.sh (from a terminal or your file manager). ./opennfspu.sh --install-desktop adds
a menu entry. gasm-run needs ALSA (libasound2, package libasound2t64 on newer Debian and Ubuntu) and a
Vulkan capable graphics driver.'
      data_how='Give it your game data once: ./opennfspu.sh /path/to/porsche (the installed game, the folder
with GameData, FEData and Porsche.exe). Without an argument it opens a chooser (zenity or kdialog) if
one is installed.'
      change='./opennfspu.sh --change-data, or give it another folder as the argument, or delete
  ${XDG_CONFIG_HOME:-~/.config}/opennfspu/data-location
./opennfspu.sh --help lists the options; anything after the data goes to gasm-run.'
      saves='~/.local/share/gasm/opennfspu/'
      logs='the terminal, or ~/.local/state/opennfspu/gasm.log when started from a menu' ;;
    windows-*)
      start='Double-click OpenNFSPU.cmd. (If Windows SmartScreen warns about gasm-run.exe or the script:
More info, Run anyway.)'
      data_how='The first time, it asks for your game data: pick the installed game (the folder with
GameData, FEData and Porsche.exe). From a command prompt: OpenNFSPU.cmd "C:\Games\Porsche Unleashed"'
      change='OpenNFSPU.cmd --change-data, or delete %APPDATA%\OpenNFSPU\data-location.
OpenNFSPU.cmd --help lists the options; anything after the data goes to gasm-run.'
      saves='%APPDATA%\gasm\opennfspu\'
      logs='the console window, and %LOCALAPPDATA%\OpenNFSPU\gasm.log' ;;
  esac
  cat >"$1" <<TXT
OpenNFSPU $VERSION for gasm ($PLATFORM)
https://opennfspu.emdzej.pl

OpenNFSPU is a faithful, function-by-function reimplementation of Need for Speed: Porsche Unleashed
(Electronic Arts, 2000), ported from the Windows game. This bundle runs it as a WebAssembly module
(opennfspu.wasm) in the gasm runtime: gasm-run $GASM_VERSION is included (https://gasm.emdzej.pl). It is
the same game as the browser player.

OpenNFSPU is early work: see https://opennfspu.emdzej.pl/guide/status for what works.

You need your own copy of Need for Speed: Porsche Unleashed, installed and updated to version 3.5.
None of the original game's files are included.

START
$start

YOUR GAME DATA
$data_how
The choice is remembered (the folder must still be there next time).

CHANGE THE GAME DATA
$change

LAUNCH OPTIONS
Anything after the data goes to gasm-run: --window 1280x960, --fullscreen, --mute.
Full guide: https://opennfspu.emdzej.pl/guide/

SAVES (profiles, options)
$saves

LOG
$logs

LICENSES
OpenNFSPU is GPL-3.0 (LICENSE$lic). gasm-run $GASM_VERSION is MIT (LICENSE-gasm$lic),
https://github.com/emdzej/gasm. ANGLE and SwiftShader (the OpenGL ES libraries) are under their own
licenses (ANGLE-NOTICES.txt). Need for Speed and Porsche Unleashed are (c) 2000 Electronic Arts;
Porsche is a trademark of Dr. Ing. h.c. F. Porsche AG. OpenNFSPU contains no original code or assets
and is not affiliated with them.
TXT
}

case "$PLATFORM" in
macos-*)
  APP="$OUT/Porsche Unleashed (gasm).app"
  rm -rf "$APP"
  mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/Frameworks"
  cp "$RUNDIR/gasm-run" "$APP/Contents/MacOS/gasm-run"
  angle "$APP/Contents/Frameworks"
  fill "$SRC/launch-macos.sh" "$APP/Contents/MacOS/OpenNFSPU"
  chmod +x "$APP/Contents/MacOS/OpenNFSPU" "$APP/Contents/MacOS/gasm-run"
  cp "$WASM" "$APP/Contents/Resources/opennfspu.wasm"
  cp "$ROOT/LICENSE" "$APP/Contents/Resources/LICENSE"
  cp "$RUNDIR/LICENSE" "$APP/Contents/Resources/LICENSE-gasm"
  "$ROOT/tools/make-icns.sh" "$APP/Contents/Resources/opennfspu.icns"
  cat >"$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>Porsche Unleashed (gasm)</string>
  <key>CFBundleDisplayName</key><string>Porsche Unleashed (gasm)</string>
  <key>CFBundleIdentifier</key><string>pl.emdzej.opennfspu.gasm</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleGetInfoString</key><string>OpenNFSPU $VERSION on gasm-run $GASM_VERSION</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleExecutable</key><string>OpenNFSPU</string>
  <key>CFBundleIconFile</key><string>opennfspu</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.racing-games</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSHumanReadableCopyright</key><string>OpenNFSPU contributors, GPL-3.0; gasm-run MIT. Need for Speed: Porsche Unleashed is (c) 2000 Electronic Arts.</string>
</dict></plist>
PLIST
  if command -v codesign >/dev/null; then
    for f in "$APP/Contents/Frameworks"/*.dylib; do codesign --force --sign - "$f"; done
    codesign --force --sign - "$APP/Contents/MacOS/gasm-run"
    codesign --force --sign - "$APP"   # ad hoc (not notarized)
    codesign --verify --strict "$APP"
  fi
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  mkdir "$STAGE/$NAME"
  cp -R "$APP" "$STAGE/$NAME/"
  readme "$STAGE/$NAME/README.txt"
  cp "$ROOT/LICENSE" "$STAGE/$NAME/LICENSE"
  cp "$RUNDIR/LICENSE" "$STAGE/$NAME/LICENSE-gasm"
  rm -f "$OUT/$NAME.zip"
  if command -v ditto >/dev/null; then (cd "$STAGE" && ditto -c -k --norsrc --noextattr --noqtn --noacl --keepParent "$NAME" "$OUT/$NAME.zip")
  else (cd "$STAGE" && zip -qry "$OUT/$NAME.zip" "$NAME"); fi
  sha "$NAME.zip"
  echo "$APP"; echo "$OUT/$NAME.zip" ;;
linux-*)
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  D="$STAGE/$NAME"; mkdir "$D"
  cp "$RUNDIR/gasm-run" "$D/"
  angle "$D"
  cp "$WASM" "$D/opennfspu.wasm"
  fill "$SRC/opennfspu.sh" "$D/opennfspu.sh"
  cp "$SRC/opennfspu-gasm.desktop" "$D/"
  python3 "$ROOT/tools/gasm-bundle/icon.py" 256 "$D/opennfspu.png"
  chmod 755 "$D/gasm-run" "$D/opennfspu.sh"; chmod 644 "$D/opennfspu.wasm" "$D/opennfspu-gasm.desktop" "$D/opennfspu.png"
  readme "$D/README.txt"
  cp "$ROOT/LICENSE" "$D/LICENSE"; cp "$RUNDIR/LICENSE" "$D/LICENSE-gasm"
  if tar --version 2>/dev/null | grep -q GNU; then own=(--owner=0 --group=0 --numeric-owner); else own=(--uid 0 --gid 0 --no-xattrs --no-mac-metadata); fi
  COPYFILE_DISABLE=1 tar "${own[@]}" -C "$STAGE" -czf "$OUT/$NAME.tar.gz" "$NAME"
  sha "$NAME.tar.gz"
  echo "$OUT/$NAME.tar.gz" ;;
windows-*)
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  D="$STAGE/$NAME"; mkdir "$D"
  cp "$RUNDIR/gasm-run.exe" "$D/"
  angle "$D"
  cp "$WASM" "$D/opennfspu.wasm"
  fill "$SRC/OpenNFSPU.cmd" "$D/OpenNFSPU.cmd"; fill "$SRC/opennfspu.ps1" "$D/opennfspu.ps1"
  readme "$D/README.txt"
  cp "$ROOT/LICENSE" "$D/LICENSE.txt"; cp "$RUNDIR/LICENSE" "$D/LICENSE-gasm.txt"
  for f in OpenNFSPU.cmd opennfspu.ps1 README.txt LICENSE.txt LICENSE-gasm.txt; do crlf "$D/$f"; done
  rm -f "$OUT/$NAME.zip"
  (cd "$STAGE" && zip -qr "$OUT/$NAME.zip" "$NAME")
  sha "$NAME.zip"
  echo "$OUT/$NAME.zip" ;;
esac
