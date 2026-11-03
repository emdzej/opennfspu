# Emulator comparisons

These tools run functions of the original `Porsche.exe` under [unicorn](https://www.unicorn-engine.org/)
on a state the port dumps, and compare the original's result with the port's word by word. The exe is read
at run time from the game folder; nothing of it is copied here.

## The drive step (0x49ef50)

`emu_drive.py` maps the exe, rebuilds the player's car in the original's layout from a dump (the car,
its description, specs and parts, the racers, the globals the drive reads and the random generator),
sets the x87 control word, calls 0x49ef50 and lists the words of the car that differ from the port's.
0x473290 (the roof lookup in the world's tree) is answered with 0. The control word defaults to 0x027f;
pass `007f` for single precision, which is what Direct3D 7 leaves the game running at.

`drivedump.patch` adds the dump to the port (`--param drivedump=<tick>[:<step>:<last>]`, the player's
car around the drive step). It is not meant to be committed:

```sh
git apply tools/emu/drivedump.patch && cmake --build build-gasm -j
PY=/path/to/venv/bin/python tools/emu/sweep.sh 450:20:3000 6400   # the game's replay.rpl, every 20th tick
git apply -R tools/emu/drivedump.patch
```

`pip install unicorn` (2.x) in that environment.

## The ground forces (0x49bcb0)

`emu_forces.py` does the same for the force task: the ground contact (0x4999e0), the springs, the
ground's frame, gravity, the body's impulses and the roof bounce. The world tree is answered from the
port's record: each wheel's tracker update (0x473fc0) writes the tracker the port had after it, its
polygon a stand-in object whose normal and vertex methods give the port's, and the roof lookup
(0x473290) returns the port's answer. The comparison skips the trackers' pointers. The control word
defaults to 0x007f.

`forcesdump.patch` adds `--param forcesdump=<tick>[:<step>:<last>]`, `--param fdfn=<hex>` (the function
dumped; `FN=` for `sweep_forces.sh`) and `--param fdcar=<i>` (the car, default the player; `CAR=`):

| fdfn | What |
|---|---|
| 49bcb0 | the forces (the default; even ticks) |
| 49b850, 49b710 | the turn, the move |
| 49fb20 | the drive task (with the drive step 0x49ef50; the racers are in the dump for the draft) |
| 494f90 | the walls (Edges): the edge query 0x474030 answered from the port's record |
| 4083c0 | the AI's drive (opponents not driven by a replay) |
| 49aee0 | the road tracking: the node search 0x4743a0 answered from the port's record, the road's nodes dumped |
| 40b670, 40c000 | the AI's plan and steering (opponents): the whole road and the racing line (.lsp) rebuilt once |
| 4169d0 | the race task (progress, laps, finish): the race's laps, type and linearity in the dump |
| 4102e0 | the hit task: the sparks (0x43f3f0, on the renderer's effects) skipped |
| 40b600 | the traffic's normal state (0x40afd0 the speed target, the lane driver 0x401000, 0x401060, 0x4012b0): run with `ARGS="--param start=384 --param drive=autobahn --param traffic=3"`, the state a stand-in in ecx with the car at +4 |
| 498f60 | the collisions: the walls, the world's objects and the cars checked before this one (0x628cdc), compared after as well. The objects are rebuilt in the original's layout (cylinder 0x38 bytes, box 0x68) with copies of its vtables (0x5b490c, 0x5b4938), so its centre, radius and box test run as they are; the hit kind (slot 8, which asks the bundle) and the RTTI casts are answered from the port |

+0x434 (the car hit) is the original's pointer and the port's index plus one; it compares as equal
when both are set. +0xed4 (the car a traffic car keeps near) is the same but read through, so it is
written as the car's address and compared back to the index. All the race's cars are dumped (by their
index in `g_cars`) with the car lists (`FD list`: the array's and count's addresses, the cars), the
personalities (0x5e67c0; each car's +0xe38 points at its record), and the side and one-way flags 0x40e6d0
sets (0x5e753c, 0x5e7544), the traffic's configuration as 0x406c00 decodes it (0x5e6d08), each car's
squares table (+0x1014) and 0x406900's table of 1 / i (0x5e7550). `ARGS=` replaces the replay with
another run (`--param cops=4 --param aidrive=1` for the police, `--param traffic=3` for the traffic).

The police's states are dumped with the car: 0x40cf90 (the chase), 0x40dcd0 (the roadblock) and
0x40e230 (turning round) run on the state rebuilt in ecx (+0 the original's vtable, +4 the car, the
words the port keeps as a car's index + 1 written as its address) and the state's words are compared
after. `FD_TRACE=<va>,..` prints the lane driver's scores and eax / edi at those addresses, to find
where a function first goes another way.

The whole road is dumped once per run (`FD road*`, its pointers as indices) and rebuilt in the
original's layout (0x47fbd0: the segments, junctions and near lists behind vector pointers);
`sweep_forces.sh` keeps those lines in `/tmp/fdroad.txt` and passes them with `FD_ROAD`.

`emu_replay.py <exe> <replay.rpl> <log>` plays a replay file's packets with the original's reader
(0x4144d0) in the port's order and compares each packet and the assists it sets with `PK` lines
(tick, player, the four bytes, the three assists) logged from the port's `play`.

In a replay only the humans' packets are recorded; the opponents drive again by the AI (its racing
state: the track, AI_Plan, AI_Steer, the drive task). The sweep reports how many words the port changed, so that a pass on
a task that returned early shows as one. Apply
one dump patch at a time (both add to `sim_rand.c`):

```sh
git apply tools/emu/forcesdump.patch && cmake --build build-gasm -j
PY=/path/to/venv/bin/python tools/emu/sweep_forces.sh 450:20:6600 7000
git apply -R tools/emu/forcesdump.patch
```

What the comparisons showed: the game runs the x87 at single precision, so every add, multiply, divide
and root rounds to a float. The runtime's `asin` (0x5a27c0) keeps that precision for its root, and
`fpatan` is exact. The port computes these steps in floats, in the original's order. The x87 keeps its
wide exponent, though: a value that decays through the denormals (the drive's steering filter +0xac8)
stays exact until it is stored, which the port gets in doubles.
