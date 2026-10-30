# Architecture of Porsche.exe

How the original program is put together, from the entry point to a race frame. It is the map the port
follows. Addresses are virtual addresses in `Porsche.exe` (v3.5, 2,129,965 bytes, CRC-32 `fd2f5573`,
image base 0x400000). Names are the ones in `tools/ghidra/names.tsv`. The per-range module list is in
[inventory.md](inventory.md), and the porting order is in [port-plan.md](port-plan.md).

## The program in one paragraph

Porsche Unleashed is a C++ program (MSVC 6, RTTI on, about 360 polymorphic classes) on top of three EA
libraries: the "real" library, EAGL2 and FERT. The "real" library (`\real\cmn\*.c`, `\real\pc\*.c`,
`\real\patch3\...`) provides files, BIG/VIV archives, FSH shapes, streams, timers, input, fonts, networking
and sound. EAGL2 (`..\..\library\eagl2`) is the 3D engine: materials as stacks of layers, plus a software
transform/clip/light pipeline. FERT is the front-end runtime: windows, controls, parts, attributes and
an event pump, driven by `.lay`/`.ctr` files. The game code (`d:\Nfs\Nfs5\game\...`,
`d:\Nfs\Nfs5\Frontend\...`) sits on top of these. Rendering goes through **THRASH**, EA's driver
interface. `THRASH_opendll` 0x574d50 loads a driver DLL (`Drivers/dx7z.dll` for Direct3D 7,
`voodoo2z.dll` for Glide) and binds its 36 `_THRASH_*` entry points into the function-pointer block
0x6bd850-0x6bd8f8. Everything the game draws reaches the GPU through those pointers.

## Startup

1. `entry` 0x5a2aca is the MSVC CRT start-up. It calls `WinMain` 0x4b6500.
2. `WinMain` splits the command line at spaces into an argv (at most 32 entries, with argv[0] = "AppName")
   and calls `GameMain` 0x4b6840.
3. `GameMain` does the following, in order:
   - Reserves a 128 MB address range with `MEM_reserve` 0x59eaf0 as a swap-space probe ("Insufficient
     space in the swap file" on failure) and releases it again.
   - `CD_CheckAutorun` 0x4a5a00 runs.
   - `Sys_Init` 0x4a51e0 runs. It installs the fatal-error handler (pointer at 0x5debf0; the failing
     source file and line are stored in 0x5deb74/0x5deb78 before each call) and calls `MEM_init` 0x59e780
     (the UMemory object heap; pool sizes 24 MB and 4 MB). It then calls FastBlock pools,
     `TIMER_init(128)`, constructs the network session object (global 0x628c70), and runs DirectInput
     keyboard, joystick and mouse init, SYNCTASK, and async file I/O (`initasync` 0x562f50: 30
     operations, 8 KB).
   - Creates the save directory (path in 0x65b360) and sets the paths from the module file name
     (`Sys_InitPaths` 0x59d400).
   - Clears the game-settings block 0x6573e8 (0x3e24 bytes). `Options_Read` 0x4b6450 reads the option
     stream from `fe.txt`, and `Option_Apply` 0x4b4950 applies each record (`Option_Size` 0x4b4aa0 steps
     through the list). These options are how the front end and the race talk: the front end writes a
     race description, and the race reads it.
   - `Audio_Init` 0x4a6610 sets up a 256 KB audio heap.
   - `Render_Init` 0x4673b0 reads the driver and resolution chosen in 3D Setup from the registry (or
     fails with "Run 3D Setup before running the game.") and opens the THRASH DLL.
   - If the game was started by a network lobby, it joins or hosts (`Net_JoinGame` 0x48e0d0,
     `Net_HostGame` 0x48dbe0) and spins until the session is ready.

The Windows side is in `\real\pc\startup.c`. `STARTUP_init` 0x53a990 registers the window class,
`STARTUP_createwindow` 0x53b870 creates the window, and a low-level keyboard hook (`KeyboardHookProc`
0x53aec0) traps the system keys. A GetMessage/DispatchMessage pump runs around 0x53b744 (in code the
auto-analysis missed; `FillGaps.java` creates the functions).

## The outer loop (GameMain)

`GameMain` loops until the player quits:

```
options = Options_Read()
if front end enabled (0x65b298):
    FE_IntroMovies() once            # eaarts-av.mad, loadscrn.fsh
    FE_Init(); result = FEApplication_Run("gamesetup", options); FE_Shutdown()
if result != quit:
    apply options; Render_SetVideoMode(w, h, bpp)     # 0x657a48/4c/50
    Race_Configure(options); LoadingScreen(0)
    GameStartup()                    # 0x4b65a0: track, cars, HUD, sounds
    while restart requested (0x606a88):
        per-race resets (AI, HUD, world, sounds, camera, net sync, ...)
        RaceLoop()                   # 0x415300
    GameShutdown()                   # 0x4b66e0
else: tear down (audio, render, memory) and return
```

The game mode is 0x6573e8: 3 is a network race, and 2, 6, 7 and 8 are other race types. The race state
is 0x657424 (2 = running, 3 = ending).

## Front end

`FEApplication_Run` 0x4d2e70 is one large function, 8.6 KB with 134 callees. It does the following:

- Sets 640x480x16.
- Shows loading steps with `FE_LoadingScreen` 0x4dd4b0 and loads fonts (`univers*.ffn`) and the
  persistent data (`nfs5.per`, `nfs5.car`, `nfs5.trk`, `nfs5.trn`, `nfs5.fac`, the player saves).
- Builds the "data areas" (`FEda*` classes: audio, cameras, display, graphics, game options, user list,
  car, track, tournament, chronicle, comms, controllers).
- Sets up the 3D rooms (`FE3DRoom` and its subclasses: dealership, factory, garage, track select,
  victory; they draw with EAGL), pre-parses every `.lay` file (`FE_PreparseLayouts` 0x4d2700) and opens
  the start screen.
- Runs screens until a screen command starts a race or quits (`FECallBackList_Dispatch` 0x50b7f0:
  PLAYGAME, GOBACK, ...; `FEMainScreen_Command` 0x50f820: quick race, knockout, ...).
- On the way out, writes the race description back into the option records.

The FERT runtime (0x524000-0x52c000) parses layouts (`.lay`) and control templates (`.ctr`, `.scr`).
It provides `FERT::Window`, `Control`, `Part` (rect, shape, text, data) and typed `Attribute`s, and its
`EventPump` delivers mouse, key and timer events (`FERT::*Event` classes). Controls such as `FEListBox`,
`FESlider` or `FECar3DControl` bind to data through `FE*Accessor` objects. The accessors use dotted paths
into the data areas, such as `Game.GameType`, `OwnedCars.Car` or `Track1.Laps`.

During a race, the pause menu re-enters the same runner with the "options" screen (`RaceLoop`, around
0x415300+0x1a0).

Movies: `showmad` 0x4dc300 plays EA MAD files (`*.mad`: video chunks plus audio) through the stream
layer, into frame buffers set up by `initrender` 0x4dbc30 (`FEIntro.cpp`).

## The race: timing model

The timing is fixed-rate, driven by the multimedia timer rather than by frames:

- `TIMER_init(128)` 0x5644b0 runs a 128 Hz timer (`\real\pc\inittmr.c`).
- `Timer_Tick` 0x4a43b0, registered by `Timer_Start` 0x4a44e0, keeps three counters:
  - 0x6559e8 counts at 128 Hz.
  - 0x6559ec counts at **64 Hz**: the simulation clock.
  - 0x6559f0 counts at **32 Hz**: input sampling. On each 32 Hz tick, the timer thread itself calls
    `Input_Sample` 0x412e90 and `Input_QueuePacket` 0x413820, unless 0x606aa4 is set, in which case the
    race loop samples instead.

  In a network race the timer also paces itself against the host: 1000-unit steps, with a stall when it
  is ahead and a double tick when it is behind.
- **Input is lockstep-shaped.** Each sample becomes a 4-byte control packet per player in a 128-entry ring
  (`Input_Enqueue` 0x415ea0, rings at 0x606af0, valid flags at 0x607af0). 0x5cc818 counts the packets.
  The sim target is twice the packet count (0x606a9c = 2 * 0x5cc818): two sim ticks per input packet.
  This is how a network race stays deterministic, and replays use the same packets.
- `RaceLoop` 0x415300 loops until 0x606a8c is set (race over). Each iteration does the following:
  - Samples the front-end/HUD actions into a 32-entry ring of action words (0x5e954c, indexed by
    0x606ac8). It handles the pause menu (`HUD_HandleInput` 0x425eb0 returns the chosen item), in-race
    options, screenshots (`Unleashed%02d.tga`, action 0xd) and camera/view keys (`Input_Action`
    0x4138f0: held or pressed this frame).
  - Runs **sim ticks** from 0x606aa0 up to the target 0x606a9c. Each tick is `Sim_Step` 0x415010 (or,
    in replay or spectate modes, a camera-only step `Camera_Update` 0x463af0). On even ticks it first
    calls `Sim_HalfStep` 0x415f10.
  - Draws one frame with `Render_Frame` 0x468900, unless nothing advanced.
  - Checks for quit (`Race_QuitRequested` 0x413960) and the end of the race (`Race_End` 0x416ae0).

### Sim_Step (one 64 Hz tick)

The tick counter is 0x606ac4. Before the start (ticks 0 to 0x1c0, 7 seconds) it plays the countdown
(`Audio_Countdown` 0x4a6010 at the ticks listed in 0x5cc7c4) and ramps a value in 0x5d1744 up between
ticks 16 and 80. After that:

- **Even ticks**: `Sim_ResetCollisionSlots`, `Sim_ClearContacts` 0x498190, `AI_TickAll` 0x4067c0 (every
  car's AI controller, a virtual call), the even task list (`TaskList_Run` 0x414d50 on 0x606ad0) and
  `Race_TickEven`.
- **Every tick**: the per-tick task list 0x606acc. Car physics registers itself here.
- **Odd ticks**: `Race_TickOdd`, `Race_UpdatePositions` 0x415fe0, `Audio_Tick` 0x4a7ef0, `World_Tick`
  0x479e20, `R3DCar_TickAll` 0x443570, `R3DCarParticle_Tick`, `Scene_Tick` 0x470b10.
- Then `Net_Tick` 0x48ed20.

So most subsystems effectively run at 32 Hz, interleaved over two 64 Hz ticks. The cars are in
0x5e8b34 (count 0x5e8b58) and the traffic in 0x5e8c24 (count 0x5e8b5c). Each car has a 0x1110+ byte
record (contact accumulator at +0x110c). The AI controllers are 0x5e6774[i], one `AIHigh_*` object per
car.

### Render_Frame

In order, `Render_Frame` 0x468900 does the following:

- Device begin (`Render_BeginFrame` 0x468010; with vsync on, it also calls `THRASH_sync`).
- Memory statistics.
- Particle catch-up at 64 Hz against `TIMER_ticks` (`Particles_Tick` 0x4550b0, at most 6 steps).
- Sky/lighting setup (0x44fb80, 0x44f070, 0x44eb50).
- The race views (`Render_MainViews` 0x468ae0, each view between `View_Begin` 0x467170 and `View_End`
  0x4671e0).
- HUD (`HUD_Draw` 0x425910).
- Overlays (`HUD_DrawOverlay` 0x4257d0).
- Present (`Render_EndFrame` 0x468030, the device's page flip).

The render device object holds a driver interface at +0x70 (begin at vtable +8, present at +0xc).

## Rendering stack

```
game render (R3DCar, R3DObject, W*Bundle, Sky*, particles, HUD)      0x418000-0x488000
   -> EAGL2: EAGL::Material = stack of EAGL::Layer* (coords, UV, texture, state, colours) plus
      game-defined layers (RCustomLayers::*, Headlights::*, R3D*Layer); scene and view API
      0x538000-0x53fab0, geometry kernels 0x53fab0-0x554000
   -> textures: TEXTURE_* (0x534000+), DTEX dynamic textures 0x554930-0x554e40 (dyntexture.c)
   -> THRASH function pointers 0x6bd850.. -> dx7z.dll (Direct3D 7)
```

DirectDraw is also used directly (`DDRAW_init` 0x5757a0, `\real\pc\ddraw.c`) to enumerate devices and
display modes. The front end draws its 2D parts (FSH shapes, fonts) and its 3D rooms through the same
path. Text is `\real\pc\fontpc.c` / `fontdraw.c` / `fontdtex.c` (fonts are `.ffn`, drawn as textured
quads).

## Files

- **Paths** are relative to the game folder (`GameData\...`, `FEData\...`). Folder enumeration is
  `UFileFind` (FindFirstFile; 0x59fc60). The port's file layer covers this with `vfs_list`.
- **Synchronous loads**: `FILE_load` 0x569ea0 and `FILE_loadat`/`FILE_size` (`\real\cmn\hlsfile.c`),
  and `FILE_save` 0x56a550.
- **Asynchronous I/O**: `\real\pc\nfile.c` runs a device thread (`FILE_devicethread` 0x5679b0) over a
  queue of operations (`FILE_queueop`, `FILE_allocateop`, `FILESYS_*`).
- **Streams**: `\real\cmn\stream.c` (0x56ada0-0x56c640) splits a file into typed chunks with "taps"
  (filters) and is used for movies and music.
- **BIG/VIV archives**: `FILE_loadbigheader` 0x56a2c0 reads a BIGF header, and `BIG_locateentry` 0x565b70
  (`locatbig.c`) finds a name in it.
- **Packed files**: `FILE_loadpack` 0x56e250 (`loadpack.c`) loads and unpacks (EA RefPack).
- **FSH shapes**: `SHAPE_loadfile` 0x5aeb10 (`loadshp.c`). Pixel-format conversion is in
  `\real\cmn\depthconv.c` (`getclutptr`, `SHAPE_converttype`, `TEXTURE_converttype`, 0x534440-0x534e50).
- **Memory-mapped files**: `FILE_mapview` 0x591750.

Formats are described in [../formats.md](../formats.md).

## Memory

The UMemory object heap (`d:\Nfs\Support\Utility\UMemory.cpp`) is set up by `MEM_init` 0x59e780. Its
allocator object is pointed to by 0x5e4fe8, with alloc at vtable slot 1 and free at slot 2:

- `operator_new` 0x59ed40, `operator_new_tag` 0x59ed70 and `operator_delete` 0x59ee00 go through it.
- The real library has its own block heap: `MEM_purge` 0x531d00 frees a block and marks it 'FB'.
- STL containers (MSVC 6 STL) use `FastAlloc_alloc` 0x4211f0 / `FastAlloc_free` 0x4237f0, which keep
  per-size free lists for blocks up to 256 bytes under a critical section.

The port can use malloc throughout. Nothing in the game logic depends on allocator addresses, except
that the heap sizes give the limits the original ran under.

## Threads and timers

There are several threads:

- **Multimedia timer** (`TIMER_setevent` 0x564330, timeSetEvent). It runs the 128 Hz tick, and with it
  input sampling.
- **Secondary timer thread** (`wininittimer` 0x53bdd0, `lowtimer.c`).
- **File device threads** (`THREAD_create` 0x55f1d0).
- **SYNCTASK** (`systask.c`): synchronous tasks run from the main loop (0x536450 is called each race
  iteration).
- **Sound library** (DirectSound, 0x58c000-0x59d000): mixes on its own.

The port is single-threaded and frame-driven (gasm). The 128 Hz timer becomes a counter advanced from
the frame clock. Because the original samples input from the timer thread, input sampling moves into
the frame step. The order of operations is kept: samples, then sim ticks, then the frame.

## Input

`\real\patch3\pc\key.c` (`KEY_init` 0x55faa0) and `mouse.c` (`MOUSE_init` 0x536bc0) use DirectInput,
`INPUT_init` 0x5328f0 is the generic layer, and joysticks are read with `joyGetPosEx` (`JOY_read`
0x563f30). The race reads controls into per-player 4-byte packets (above). The front end gets
mouse/key events from the FERT event pump. Controller mapping is configured in the front end
(`FEControllers_Map` 0x51ca50: Accel, Brake, HandBrake, ...).

## Audio

The game side is in 0x4a6000-0x4b0000:

- `AudioCmn_AddBank` 0x4a6990 and `AudioCmn_LoadBank` 0x4a6a60 handle sound banks (`.bnk` inside
  `.viv`).
- Engine sounds come from `EngineAudio_Load` 0x4ac9e0 (`<car>16.viv`).
- Track sounds come from `Audio_LoadTrackSounds` 0x4afda0.
- Streamed music: `Music_Init` 0x4aded0, `zzzymus.viv`.
- Speech comes from `Audio_LoadSpeech` 0x4a8260.

The mixing library underneath is EA's SND library on DirectSound (0x58c000-0x59d000; DirectSoundCreate
users 0x58e400, 0x58e990, 0x597d20).

## Networking (stubbed in the port)

The networking code covers:

- The online lobby (`fsNet`, porsche-entry.earacing.com) and a client/server game layer
  (0x488000-0x494000: `CDataPacket`, `CConHost`/`CConClient`).
- Results written as HTML.
- The real library's TCP, TAPI modem, RAS and packet layers (0x580000-0x58c000, 0x594000-0x596000).

The network session object at 0x628c70 is tested all over the main loop (+8 connected, +0xbe/+0xbf/+0xdc
roles). With it absent (0) the game runs single-player. As OpenGTA does with its `stubs.c`, the port
keeps the null-session behaviour. The lockstep input design leaves room for an online mode later.

## Conventions seen everywhere

- Fatal errors set the file/line globals and call through 0x5debf0 (`NFS_ErrorHandler`); `REAL_assert`
  0x53eda0 and `REAL_abort` 0x5647c0 are the library's own.
- `__ftol` 0x5a0d48 appears after every float-to-int conversion. The port uses C truncation toward zero,
  which matches MSVC's default rounding mode for `__ftol`.
- `rand` 0x5a1579 is MSVC's LCG (214013, 2531011). It is used by the race setup, so the port needs the
  same generator to keep deterministic behaviour.
- `sprintf` 0x5a0d6f builds every path (`%s%s.fsh` with a directory prefix).
- Classes: game objects use single inheritance with virtual methods. In `names.tsv`, vtable slots are
  named `Class_vN` until their purpose is known; slot 0 is usually, but not always, the destructor.
