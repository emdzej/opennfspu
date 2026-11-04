#!/bin/bash
# wholerace.sh <first> <last> [chunk] [jobs]: the free run (freerun.sh, every racer, CAR=-2 by default)
# over ticks first..last in chunks of `chunk` ticks (default 200), `jobs` at a time (default 6). The
# chunks start from the port's state: when every chunk ends with nothing differing, nothing carries
# from one to the next and together they are one free run of the whole stretch. ARGS= the race (default
# the replay), PY= the Python with unicorn. Prints each chunk's line, then the totals.
cd "$(dirname "$0")/../.."
F=$1 L=$2 C=${3:-200} J=${4:-6}
export CAR=${CAR:--2}
seq "$F" "$C" "$L" | xargs -P "$J" -I{} bash -c '
  a={}; b=$((a + '"$C"' - 1)); [ $b -gt '"$L"' ] && b='"$L"'
  r=$(tools/emu/freerun.sh $a:1:$b $((2 * b + 400)) 2>&1 | tail -1)
  echo "$a-$b $r"' | sort -n | tee /tmp/wholerace.txt
awk '{c += $3; d += $5} END {print "total calls", c, "differing", d}' /tmp/wholerace.txt
