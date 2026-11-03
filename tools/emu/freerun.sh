#!/bin/bash
# freerun.sh <start:step:last> <frames>: every wrapped call of one car (fdfn=1, forcesdump.patch applied)
# run in order under the emulator, each on the state the emulator left (FD_CARRY): a free run of the
# original from the port's state at the start. CAR=<i> (default the player; -2 every racer, each carried on its
# own), ARGS= the run (default the
# replay), PY= the Python with unicorn.
cd "$(dirname "$0")/../.."
.deps/gasm-runner-macos-universal/gasm-run build-gasm/opennfspu.wasm --asset-dir game ${ARGS:---param replay=replay.rpl} --param forcesdump=$1 --param fdfn=1 --param fdcar=${CAR:--1} --headless $2 2>&1 | grep "^\[guest\] FD" | sed 's/^\[guest\] //' > /tmp/fdall.txt
grep '^FD road[a-z]' /tmp/fdall.txt > /tmp/fdroad.txt; export FD_ROAD=/tmp/fdroad.txt
D=$(mktemp -d); cd "$D" && mkdir parts && awk 'BEGIN{n=0} /^FD fn/{n++} {print > ("parts/f" sprintf("%05d",n) ".txt")}' /tmp/fdall.txt
export FD_CARRY="$D/carry.json"
tot=0; bad=0
for f in parts/f*.txt; do
  grep -q "^FD fn" $f || continue
  t=$(grep -o "tick=[0-9]*" $f | head -1); fn=$(grep -o "^FD fn 0 [0-9a-f]*" $f | cut -d' ' -f4)
  # (the packet task reads the replay's file or the controls: the run's input, taken from the port)
  [ "$fn" = 499150 ] && continue
  r=$("${PY:-python3}" "$OLDPWD/tools/emu/emu_forces.py" "$OLDPWD/game/Porsche.exe" $f 2>&1 | grep -E "differing|did not|bad|Error|carried|vtable" | tr "\n" " ")
  tot=$((tot+1))
  if echo "$r" | grep -qE "differing words [1-9]|did not|bad|Error|uncovered [0-9]" || ! echo "$r" | grep -q "differing words"; then bad=$((bad+1)); echo "$t $fn $r"; fi
done
echo "calls $tot differing $bad"
