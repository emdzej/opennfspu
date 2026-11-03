#!/bin/bash
# sweep_forces.sh <start:step:last> <frames>: dump the player's ground forces (0x49bcb0) from the replay and
# compare each with the exe (forcesdump.patch applied; FN=<hex> another car task: 49b850 turn, 49b710 move;
# ARGS= the run instead of the replay)
cd "$(dirname "$0")/../.."
.deps/gasm-runner-macos-universal/gasm-run build-gasm/opennfspu.wasm --asset-dir game ${ARGS:---param replay=replay.rpl} --param forcesdump=$1 --param fdfn=${FN:-49bcb0} --param fdcar=${CAR:--1} --headless $2 2>&1 | grep "^\[guest\] FD" | sed 's/^\[guest\] //' > /tmp/fdall.txt
grep '^FD road[a-z]' /tmp/fdall.txt > /tmp/fdroad.txt; export FD_ROAD=/tmp/fdroad.txt
D=$(mktemp -d); cd "$D" && mkdir parts && awk 'BEGIN{n=0} /^FD fn/{n++} {print > ("parts/f" sprintf("%04d",n) ".txt")}' /tmp/fdall.txt
tot=0; bad=0
for f in parts/f*.txt; do
  t=$(grep -o "tick=[0-9]*" $f | head -1)
  r=$("${PY:-python3}" "$OLDPWD/tools/emu/emu_forces.py" "$OLDPWD/game/Porsche.exe" $f 2>&1 | grep -E "differing|did not|bad|Error|order|port changed|unsupported" | tr "\n" " ")
  tot=$((tot+1)); case "$r" in *"differing words 0 ") ;; *) bad=$((bad+1)); echo "$t $r";; esac
done
echo "ticks $tot differing $bad"
