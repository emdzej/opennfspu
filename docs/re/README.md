# Reverse-engineering notes

Notes on `Porsche.exe` (v3.5, 2,129,965 bytes, CRC-32 `fd2f5573`), written in our own words from the
disassembly. They describe layouts, formulas and addresses; they contain no decompiler output and no
data from the game.

| Page | What |
|---|---|
| [architecture.md](architecture.md) | Startup, the main loop, front end to race and back, the race timing model (128/64/32 Hz), rendering stack, files, memory, threads, input, audio, networking |
| [inventory.md](inventory.md) | Address ranges to modules with function counts, imports, the 360 RTTI classes and their vtables, key globals |
| [port-plan.md](port-plan.md) | The porting order toward a first playable race, with sizes and key addresses |
| [thrash.md](thrash.md) | The THRASH driver interface (dx7z.dll) |
| [eagl.md](eagl.md) | EAGL2: matrices, the transformer, views, materials and layers, the draw kernels down to THRASH |
| [../formats.md](../formats.md) | Data file formats |

## Tooling

A Ghidra project `nfspu` holds `porsche.exe` (a copy of `game/Porsche.exe`) and `dx7z.dll`, driven by the
`ghidra` CLI. The scripts are in `tools/ghidra/`:

- `apply-names.sh` creates the functions the auto-analysis missed (`FillGaps.java`), applies
  `names.tsv` (`ApplyNames.java`) and regenerates the grep dumps in `re/` (git-ignored): `all.c`
  (`DumpAllNamed.java`), `funcs.txt` and `meta.tsv` (`DumpMeta.java`: size, callers, callees, imports
  and strings per function).
- `ImportUsers.java` writes `re/imports.tsv` (the functions using each import).
- `rtti.py` writes `re/rtti.tsv` and `re/vtables.tsv` (classes, vtables, methods).
- `Dec.java` decompiles single addresses (creating the function if needed), and `AnalyzeAll.java`
  analyses a program that the CLI imported without analysis (as happened with `dx7z.dll`).

`names.tsv` is the record: `addr, name, module, confidence, description`. The confidence values are:

- high: read and understood
- med: inferred from calls and strings
- low: a guess
- struct: a vtable slot, `Class_vN`; the class is known, the purpose not yet
- range: a placeholder for the CRT or an unidentified library object

Rename in the TSV, not only in Ghidra, then rerun `apply-names.sh`.
