# The render system

The game's render system (0x464000-0x469000) between the game and EAGL/THRASH: the driver choice, the
graphics device, render states, cameras and views, and the race frame. Port: `src/render/render.c`,
`render.h`; the render options in `render_options.c`. Pending calls are listed in `render_pending.h`.

## Starting up

`Render_Init` 0x4673b0 decides the driver and builds the render system object.

- The option `driver` (fe.txt `driver = registry`) set to `registry` means the 3D Setup's choice from
  `HKLM\Software\Electronic Arts\Need For Speed - Porsche Unleashed`, read by `NFSRegistryManager` (0x118
  bytes, vtable 0x5b3fbc; get string 0x4b7030, get number 0x4b7130). The values are a table in the exe
  (0x5d6f28, 12 bytes each: name, type, default): `Thrash Driver` "dx", `D3D Device` 0, `Thrash Resolution`
  "640x480", `Hardware Acceleration` 1, `Triple Buffer` 0. A missing value is written with its default and
  read back. An empty driver ends the game ("Run 3D Setup before running the game.").
- Driver "dx" becomes "dx" + "7" with the D3D device number as the display, other drivers keep their
  name and display 0; "z" is appended: "dx7z", "voodoo2z". The drivers' folder (*0x65b304) prefixes it.
- Any other `driver` option names the driver directly, with the `width` x `height` options as the mode.

The port has no registry: each value comes from the launch parameter of its name in lower case with `_`
for spaces (`--param thrash_driver=dx`), else the exe's default. The drivers are registered by name
(`THRASH_registerdll`), so there is no folder prefix.

The render system (0x80 bytes, *0x628130; constructor 0x467720) keeps the cameras' near and far planes
(0.15, 700), the CPU type, the graphics object (0x69ed0c, registered by 0x5400f0), the device and the 2D
view. The constructor:

1. creates the device: the full-screen kind (vtable 0x5bc41c; 0x5bc3f0 is the windowed kind, which the
   game never builds: Render_Init passes 0) and opens it (0x556eb0): `GLIDE_env` 0x556910 opens the driver
   (`THRASH_opendll` through `THRASH_drivername`), its window (`init`), the display, and sets mode 1 (640 x
   480 x 16) with two back buffers; then the texture manager (0x5401a0), the driver's about block, the
   render-state object (0x537470), the font (0x5365e0), the EAGL context (0x548f80) and its states;
2. reads the display mode (`Render_DisplayInit` 0x44e190 into 0x619780..: a stamp, width, height, depth),
   the card's quirks (`Render_DetectCard` 0x44e760) and the quality (`Render_SetupQuality` 0x44ebe0);
3. sets a different mode if the options ask for one (0x467f70);
4. makes the 2D view: orthographic, y down (translation by (-1, 1), scale (2/w, -2/h));
5. runs the UV test: an 8 x 8 test texture drawn as a quad and 128 pixels read back (`readrect`) tell
   which texel centre the card samples at (0x44e740). The port's GL driver samples at Direct3D's centres;
   the test is a pending note.

After it, `Render_Init` hands the device to the front end (0x4b6ec0), makes the render heap (0x465cb0) and
registers the game's EAGL layer factory (`RLayers_Register` 0x449a60).

### The quality (`Render_SetupQuality` 0x44ebe0)

From the options (fe.txt / the options screens): gamma (`gammacorrection` in %, at least 0.25, to setstate
0x2e through the alias slot), special effects (`specialfx` 0..3: shadow, particle and smoke counts), car
detail, and the **render method key** (0x6197f8) the materials pick their layer groups with: 8 with
`headlights` = 1, then 1 without `carchrome` (reflections) or 3 with it; with the card's flag 0x6197a8 the
reflection bits become 4. With fe.txt's values the key is 1.

## Render states and the cull

The render-state object (0x38 bytes, 0x537470) holds the states every view starts with (setstate 0x28, 4,
9, 0x15, 0xe, 0x16, 0x17, 0xf, 0x3c, 0x39, 0x43; sent by 0x5374d0) and the **cull** (+0x10: 1 culls
counter-clockwise triangles, 2 clockwise; THRASH maps 2 to `D3DCULL_CW`, 1 to `D3DCULL_CCW`). Setting the
cull (0x537600) only stores it: it would be sent at once only if 0x5ded4c were 1, which it never is. The
material layers' state layer reads it when a material asks for culling (EAGL_RSSetCull).

The race's main view sets 2 (1 when the view is mirrored, 0x65742c). `Render_SetCull` 0x467ed0 swaps 1 and
2, and `R3DObject_Draw` calls it before and after a car: the car's model matrix is mirrored (scales (-1, 1,
-1) and (1, -1, 1) around its rotation), which turns its triangles' winding around.

## Cameras and views

A camera (0x138 bytes, `Render_CreateCamera` 0x467230) has its own EAGL view (0x224 bytes, owned by the
device), its viewport as fractions of the screen, near, far, a clear colour (the view's background; 0 = no
clear), a half field of view (45 degrees) and a **frame** (0x60 bytes at +0xb8): the input orientation
(+0xe8, rows right, up, forward) and eye (+0x10c), and the world-to-camera rotation and translation made
from them (`RFrame_ToView` 0x4661a0: the forward row negated, the orientation transposed, the eye turned by
it and negated; the vector transform is the real library's 0x5327b0, row vectors in single precision).
The game's world has y **down**; EAGL's camera looks along -z.

`RCamera_Setup` 0x466e70 resets the viewport when the display mode's stamp changed, loads the view matrix
and sets the perspective: field of view 2 x the half angle, the aspect of the viewport's share of a 640 x 480
screen. `View_Begin` 0x467170 empties the layers' vertex cache (0x44a090), makes the camera current
(render system +0x10; the coordinate layers' back-face test reads its eye there), sets up the camera, stores
the depth factor (far / (far - near) x near at +0xc), begins the EAGL view, clears it and sets the context's
viewport. `View_End` 0x4671e0 ends the EAGL scene.

The race makes three cameras (0x468da0): the main view (full screen, clear 0xff77bbff, letterboxed 0.15 ..
0.85 with `widescreen`), an overlay (no clear; the HUD's) and a quarter-screen view at (0.5, 0.5).

## The race frame

`Render_Frame` 0x468900: the device's begin (back buffer, `THRASH_sync` with `drawsync`), the particles
ticked every two timer ticks, the frame setup (0x44fb80, 0x44f070, 0x44eb50), the main views
(`Render_MainViews` 0x468ae0: the camera's flags, range 400, the view distance, the camera update 0x461060,
the cull, the sky's light environment 0x459940 / 0x466da0, the world 0x468a20 between View_Begin and
View_End, then 0x44fd70), an empty overlay view, the HUD (`HUD_Draw`, 0x468be0, `HUD_DrawOverlay`), another
overlay view, the page flip. What the world, sky, particles and HUD draw is pending.

## Not ported yet

- The cameras' behaviour (0x460000-0x464000 holds the flare, sky texture lists and camera tracks; the
  per-type camera updates (chase, bumper, cockpit, TV) are reached through 0x461060 -> 0x464e40 and
  `Camera_Update` 0x463af0).
- The sky (0x458000-0x460000: `Sky_LoadConfig` 0x459530 (`Sky\<name>.bin`), `Sky_LoadFsh` 0x45da90,
  `Sky_LoadText` 0x45fa40, the classes SkyHorizon, SkyLevel_BasicSky / _MultiSky / _FreeClouds,
  SkyMapSource_Noise, Scroller_*).
- Render_DetectCard 0x44e760, the UV test 0x44e740, the font 0x5365e0, the 2D window 0x5341f0, the video
  mode change 0x467f70 / `Render_SetVideoMode` 0x467f00, `Render_RaceInit` 0x468ef0.
