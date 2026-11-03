#!/bin/bash
# sweep.sh <start:step:last> <frames>: dump the player's drive step from the replay and compare each with the exe
cd "$(dirname "$0")/../.."
.deps/gasm-runner-macos-universal/gasm-run build-gasm/opennfspu.wasm --asset-dir game --param replay=replay.rpl --param drivedump=$1 --headless $2 2>&1 | grep "^\[guest\] DD" | sed 's/^\[guest\] //' > /tmp/ddall.txt
D=$(mktemp -d); cd "$D" && rm -rf parts && mkdir parts && awk 'BEGIN{n=0} /^DD precar/{n++} {print > ("parts/d" sprintf("%04d",n) ".txt")}' /tmp/ddall.txt
tot=0; bad=0
for f in parts/d0*.txt; do
  t=$(grep -o "tick=[0-9]*" $f | head -1)
  r=$("${PY:-python3}" "$OLDPWD/tools/emu/emu_drive.py" "$OLDPWD/game/Porsche.exe" $f 007f 2>&1 | grep -E "differing|did not|bad|Error" | head -1)
  tot=$((tot+1)); case "$r" in *"words 0") ;; *) bad=$((bad+1)); echo "$t $r";; esac
done
echo "ticks $tot differing $bad"
