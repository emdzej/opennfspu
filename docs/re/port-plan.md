# Porting plan

The recommended order toward a first playable race, then the whole game. Each step ends with something
that can be checked headlessly with `gasm-run` (a hash or a screenshot) or by a native test against the
user's data. Function counts come from [inventory.md](inventory.md): "fn" is functions, "KB" is code
size, and the size is the better effort estimate (the front end has thousands of tiny accessors). The
architecture behind the steps is in [architecture.md](architecture.md).

Ground rules:

- The rules from OpenRF/OpenGTA apply: port function by function, cite the address, keep the
  original's quirks, load tables from the exe at run time, and write no decompiler output.
- The structure changes in three places:
  - **Threads go.** The 128 Hz timer becomes a counter advanced from the frame clock, input is sampled
    in the frame step, and file I/O is synchronous through the file layer.
  - **Blocking loops become states.** `GameMain`, `FEApplication_Run` and `RaceLoop` become app states
    stepped once per gasm frame.
  - **C++ becomes C.** Classes become structs. Where the original dispatches through a vtable (AI
    controllers, HUD visuals, FE controls, EAGL layers, world bundles), keep an ops table, so the
    dispatch order stays the same.

## 1. Files and formats (about 240 fn, 35 KB)

The file layer exists (`src/vfs.c`, ISO + Joliet). On top of it:

- BIG/VIV: `FILE_loadbigheader` 0x56a2c0, `BIG_locateentry` 0x565b70.
- RefPack unpacking: `FILE_loadpack` 0x56e250.
- FSH shapes: `SHAPE_loadfile` 0x5aeb10, and the pixel/CLUT conversion in `depthconv.c`
  (0x534440-0x534e50).
- The stream chunk reader used by movies and music (`stream.c`, 0x56ada0-0x56c640; ported as a pull
  reader with no device thread).
- The option records (`Options_Read` 0x4b6450, `Option_Apply` 0x4b4950, `Options_Parse` 0x4b0dc0),
  since every later step reads them.

**Check:** a native test decodes every `.viv`, `.fsh` and packed file of the data and compares the
results with `tools/nfsfmt.py`.

## 2. THRASH on GLES 3 (dx7z.dll: 385 fn; exe side about 60 fn, 9 KB)

In progress in `src/render/thrash.c` ([thrash.md](thrash.md)). This step covers:

- The 36 entry points.
- The exe's glue: `THRASH_opendll` 0x574d50 becomes direct calls.
- `Render_Init` 0x4673b0 and `Render_SetVideoMode` 0x467f00 (the registry choice becomes a launch
  parameter).
- The texture managers TEXTURE_* (0x535800-0x536200) and DTEX (0x554930-0x554e40).
- DirectDraw (`DDRAW_init` 0x5757a0) reduces to mode bookkeeping.

**Check:** a frame of clears, a textured quad from an FSH, and gasm's headless hashes.

## 3. EAGL2, then a track and a car on screen (about 1,450 fn, 390 KB)

This is the largest technical step and the one with the most visible progress.

1. EAGL2 API and kernels: scene, view, matrices, material/layer dispatch (0x538000-0x53fab0, 196 fn,
   27 KB), and the geometry kernels (0x53fab0-0x554000, 62 fn, but 79 KB: few, large, mechanical
   transform/clip/light loops). Port one kernel per vertex format, with a shared test that renders the
   same mesh through the original math.
2. A track viewer, as OpenGTA started with its city viewer:
   - `Track_Load` 0x471ad0, `Track_LoadWorld` 0x487650 (`.crp`, `animdefs.txt`, `.jnc`) and
     `Track_LoadEdges` 0x488000.
   - The world bundles and the collision tree (0x474000-0x488000, 329 fn, 77 KB).
   - Sky (140 fn, 30 KB) and scene elements (132 fn, 30 KB).
   - The render system (`Render_Frame` 0x468900, views 0x467170/0x4671e0, 105 fn).

   A free camera (an addition, behind a parameter) flies along the track.
3. Cars: `R3DCar_LoadCrp` 0x441940, `R3DCar_LoadTpg` 0x437780, `R3DCar_LoadCommon` 0x443690, the
   material layers and headlights (the render range 0x437700-0x458000, 507 fn, 125 KB, includes
   particles).

**Check:** headless screenshots of every track at fixed camera keyframes (`Track_LoadKeyframes`
0x44fa10), and of each car in the viewer.

## 4. One car driving: sim, input, the race loop (about 330 fn, 140 KB)

- Car data: `Parts_Load` 0x49c310, `Sim_LoadCarSim` 0x49c630, `Sim_LoadCarAis` 0x49c8b0 and
  `Sim_BuildEngine` 0x49c9d0.
- The physics: the sim range 0x494000-0x4a4000 (100 fn, but dense float code, 62 KB), reached through
  the task lists 0x606acc/0x606ad0 (`TaskList_Run` 0x414d50).
- The race core (0x410000-0x418000, 102 fn): `RaceLoop` 0x415300 as an app state, `Sim_Step` 0x415010
  with its even/odd split, the input packet ring (`Input_Sample` 0x412e90, `Input_QueuePacket`
  0x413820) and the action ring (`Input_Action` 0x4138f0).
- The timer model (`Timer_Tick` 0x4a43b0: 128/64/32 Hz) and the cameras (0x460000-0x464000, 52 fn).

**First playable:** start with launch parameters (car, track) and drive alone, with no HUD, no sound and
no AI. The cars' physics must be deterministic: the same input script must give the same positions
(a native test with scripted packets). The lockstep design of the original makes this natural.

## 5. Front end, minimal path (about 700 of 2,591 fn)

The pieces are:

- FERT (0x524000-0x52c000, 165 fn, 29 KB): layout and control parsing, parts, attributes, the event
  pump (fed from gasm mouse/keys).
- `FEApplication_Run` 0x4d2e70 as a state machine.
- The data areas the quick-race path needs (`FEdaCar`, `FEdaTrack`, `FEdaGameOptions`).
- The main screen (`FEMainScreen_Command` 0x50f820), car and track selection, and the controls they use
  (list box, slider, go-to button, 3D car).
- The 3D rooms for the car and track pick.
- Fonts (0x53c000-0x53f000, 77 fn).
- Saves and persistence (`Save_Load` 0x4de060, `Save_Write` 0x4de850, `FE_LoadPersistent` 0x4e2710)
  through `plat_save_user_file`.

**Check:** a headless input script goes from the title through quick race to the start line.

## 6. Audio and movies (about 510 fn, 105 KB)

- The game side: banks, engine audio, music and speech (0x4a6000-0x4b0000, 137 fn).
- The SND library mixer (0x58c000-0x59d000, about 350 fn including the DirectSound back end, which is
  replaced by the port's mixer, as in OpenRF's `audio.c`).
- Decoders for the formats the formats notes identify (`.bnk`/`.asf`/`.spl`, EA ADPCM).
- MAD movies: `showmad` 0x4dc300, `initrender` 0x4dbc30.

**Check:** `sfx_test`-style WAV output compared before and after refactors, and the movie frame hashes.

## 7. AI, traffic, police and the HUD (about 820 fn, 172 KB)

- AI (0x401000-0x410000, 251 fn, 58 KB): the `AIHigh_*` controllers and `AIState_*` states, opponents,
  traffic (`trafcfg.dat`), police (`.cop`, Pursuit mode), racing lines (`.lsp`), personalities.
- The HUD (0x418000-0x437700, 565 fn, 114 KB): the visuals and the `.hud` layouts.

**Check:** races against AI with scripted input and fixed seeds (MSVC `rand` port), with position logs
compared across runs.

## 8. The whole game

- The rest of the front end (about 1,900 fn): career eras, tournaments, chronicle, factory driver,
  dealership, garage, parts and repairs, paint shop, options, controller setup, credits.
- Replays (`Replay_Load` 0x414710, `Replay_Save` 0x413ff0).
- Skill tests (`Scene_LoadSkillTest` 0x46d280).
- Knockout and the other game types.
- Victory movies.

Networking (net 215 fn plus real-net about 330 fn) is **stubbed** with the null session (0x628c70 = 0),
as OpenGTA stubs DirectPlay; the lockstep input queue is the seam for an online mode later.

## Not ported

- The MSVC runtime (0x5a0b80-0x5aeb10, 0x5af1f0-0x5b0000) and PACODE.
- The Win32 window and message pump (`startup.c`).
- DirectInput and joystick polling, replaced by gasm input.
- The updater and registration (mrbupd, `Registration_Launch`).
- The CD check (`CD_CheckAutorun`).
- `gimme.dll` picture conversion: user driver photos become optional.
- The UMemory and FastAlloc allocators, replaced by malloc. Their limits are documented where they
  change behaviour.

## Rough size

| Step | Functions | Code |
|---|---:|---:|
| 1 Files and formats | ~240 | ~35 KB |
| 2 THRASH (DLL + glue) | ~445 | DLL + 9 KB |
| 3 EAGL, track, car | ~1,450 | ~390 KB |
| 4 Sim, input, race loop | ~330 | ~140 KB |
| 5 Front end, minimal | ~700 | ~150 KB |
| 6 Audio, movies | ~510 | ~105 KB |
| 7 AI, HUD | ~820 | ~172 KB |
| 8 Rest of the front end, replays, modes | ~1,900 | ~290 KB |
| Stubbed / not ported | ~1,300 | ~200 KB |
