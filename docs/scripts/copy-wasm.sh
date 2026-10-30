#!/usr/bin/env bash
# Put opennfspu.wasm into the site's play page (docs/public/play/opennfspu.wasm, git-ignored).
# CI runs it after building the module; locally it copies build-gasm/opennfspu.wasm.
#   docs/scripts/copy-wasm.sh [path/to/opennfspu.wasm]
# The module is the engine only: the game data always comes from the player's own copy of the game.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
src="${1:-$root/build-gasm/opennfspu.wasm}"
[ -f "$src" ] || { echo "no $src: build opennfspu.wasm first (docs/howto/build-from-source.md)" >&2; exit 1; }
magic="$(head -c 4 "$src" | od -An -tx1 | tr -d ' \n')"
[ "$magic" = 0061736d ] || { echo "$src is not a WebAssembly module" >&2; exit 1; }
cp "$src" "$root/docs/public/play/opennfspu.wasm"
echo "copied $(wc -c < "$src" | tr -d ' ') bytes -> docs/public/play/opennfspu.wasm"
