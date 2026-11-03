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

`forcesdump.patch` adds `--param forcesdump=<tick>[:<step>:<last>]`. The task runs on even ticks. Apply
one dump patch at a time (both add to `sim_rand.c`):

```sh
git apply tools/emu/forcesdump.patch && cmake --build build-gasm -j
PY=/path/to/venv/bin/python tools/emu/sweep_forces.sh 450:20:6600 7000
git apply -R tools/emu/forcesdump.patch
```

What the comparisons showed: the game runs the x87 at single precision, so every add, multiply, divide
and root rounds to a float. The runtime's `asin` (0x5a27c0) keeps that precision for its root, and
`fpatan` is exact. The port computes these steps in floats, in the original's order.
