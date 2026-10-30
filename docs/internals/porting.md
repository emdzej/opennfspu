# Porting conventions

How a function of `Porsche.exe` becomes C in `src/`. The rules in `AGENTS.md` (fidelity, no data from the
exe, no pasted decompiler output, cite the original) come first; this page is about the shape of the code.

## Files and names

- One C file per original module, under the directory of its library: `src/real/` (EA's real library,
  `\real\cmn\*.c`, `\real\pc\*.c`), `src/eagl/` (EAGL2), `src/front/` (front end, FERT), `src/game/`
  (simulation, AI, world, track), `src/render/` (the game's renderer and THRASH). Name the file after the
  original source file when a string gives it (`locatbig.c`, `loadshp.c`), else after the module
  (`track.c`, `world.c`).
- Function names are the Ghidra names (`tools/ghidra/names.tsv`). A function you port gets a real name in
  `names.tsv` (and in the Ghidra project, `ghidra function rename`): no `FUN_xxxxxx` in `src/`.
- Every ported function is preceded or followed by its address: `/* BIG_findentry 0x565a30 */` (in the
  header next to the declaration, or above the definition).
- C++ methods: `Class_Method(Class *self, ...)`. Constructors `Class_ctor`, destructors `Class_dtor`.

## Data layout

- wasm32 is a 32-bit target like the original, so pointers and `int` have the original's sizes: structs
  that the original lays out in memory (and that files or the exe's tables are read into) keep the
  original's layout and offsets. Comment each field with its offset (`/* +0x1c */`) and check the size
  under `#if UINTPTR_MAX == 0xffffffffu` with `_Static_assert` (the native test build is 64-bit, where
  only the file-format structs, which have no pointers, keep their size).
- File formats are parsed with explicit byte reads (`BIG_getnum`-style helpers, little or big endian as
  the format is), never by casting a pointer to a struct, so the native build reads them too.
- Floats are `float` (the original is x87 single precision for stored values). Keep the original's
  order of operations in arithmetic; don't "simplify" formulas, the results must match.
- C++ classes with virtual methods: a `const XxxVtbl *vt` first member and a `static const XxxVtbl` per
  class, slots in the original's order (`re/vtables.tsv`), each slot commented with its address.

## Globals and tables from the exe

- The original's globals become `static` variables of the module (or `extern` in its header if other
  modules use them), named as in Ghidra (`g_` prefix for data that has a name, else the module's
  convention); comment the original address: `static int track_count;   /* 0x6b2a40 */`.
- Initialised data the game needs from the exe (tables, defaults, strings) is read at runtime through
  `exe.h` (`exe_data`, `exe_u32`, `exe_str` by virtual address), copied into the module at init. Never
  type the values into the source.

## Not yet ported

- A call into code nobody has ported yet goes to a function with the original's name, declared in the
  header of the module it belongs to and defined in that directory's `pending.c` (`src/game/pending.c`,
  ...) with `PENDING_NOTE(0x4xxxxx, "Name")` from `src/pending.h` (logs "pending: Name 0x4xxxxx" once)
  and a neutral return. Never a silent stub; `grep -r PENDING_NOTE src` lists what's left. When you port
  the function, delete its pending definition.
- Windows, DirectX, DirectInput, DirectSound, DirectPlay, Win32 file and thread calls are not ported:
  they map onto the platform contract (`platform.h`), the file layer (`vfs.h`) and THRASH. Describe the
  mapping where it happens.
- The original's file system is asynchronous (a device thread, `FILE_queueop`); the port completes an
  operation at once and runs its callback from the same place the original's completion would be seen,
  so the order of effects stays the original's.

## Verification

- Every module gets a headless test (`tests/<module>_test.c`) that runs the ported code on the real data
  (skips without it) and checks what can be checked: counts, bounds, checksums of decoded output against
  `tools/nfsfmt.py`, known values from the exe.
- Anything visible: a headless gasm-run screenshot (`--headless N --screenshot`), never a window.
