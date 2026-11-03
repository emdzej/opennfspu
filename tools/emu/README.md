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
