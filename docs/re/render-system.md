# The render system

The game's render system (0x464000-0x469000) between the game and EAGL/THRASH: the driver choice, the
graphics device, render states, cameras and views, and the race frame. Port: `src/render/render.c`,
`render.h`; the render options in `render_options.c`. The world draw is `rworld.c`.

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

## The cars' light

Each car carries a light block, 0x194 bytes at R3DObject +0x2dc. The car's draw copies it and makes it
the layers' light environment, 0x6115cc (0x4433c0). R3DCar_v15 (0x43a490) fills it from these sources:

- **The track's environment map** (0x6115d8, R3DCar_LoadEnvMap 0x440900):
  - `<track>.map`'s header colours: tint, ambient, interior, interior diffuse, light decal.
  - Its road slices (`section%d`): a node range, three reflection textures open and three under a roof,
    texture size and aspect.
  - `<track>.env`, the slices' textures, in a texture bank of class 2.
- **The light under the car** (0x474600 → 0x474660): the colours of the road polygon's vertices (5:5:5),
  each channel interpolated as a plane at the car. Of a quad, the triangle holding the car. This is how
  cars darken under trees and in tunnels.
- **Ambients** 0 to 2, for the exterior, interior and light decals: that light times the map's ambient,
  then times interior or light decal.
- **Diffuses** 0 to 2: the light times the tint, the interior diffuse, or the light decal.
- **The light direction**: away from the sun, whose position is 0x460f50 from the sky configuration
  (+0x3c, +0x44, +0x48, +0xcdc, +0xce0, +0xce4). Under a roof (a surface above with flags 0xc000) it
  points straight down.
- **The slice's reflection textures** (by level, open or covered) when the chrome option is 2
  (0x6197cb), the road's axes transposed for their projection, and the car's place across and along
  the road at its node, in sixths.
- **The nearest dynamic light** (0x456030), with the chrome option at 2 (0x6197ca). Its record goes
  to +0x14c and its texture to +0x18c; without one the texture is cleared and the record stays as it was.
  The lights are the article effects of type 4 (0x455f60, with the extras flag 0x6197e6). Each record
  (0x40 bytes: place, reach, the texture's id and name in the track's bank, two projection rows of
  five) goes into a map (0x627dfc) when its article is first drawn; the map is never emptied during
  the race. The nearest is within 400 and its reach + 2.5, by max(|dx|, |dz|) + min / 4.

## Reflections

With the chrome option at 2, car materials draw extra passes after the base. Each pass is a texture layer
that writes its own texture coordinates into the work vertices (set 1, or set 2 for id 0xf, flagging
EAGL_CF_UV / UV2), then a render state and a draw. A layer without a texture skips its pass: it returns
the layer after the next draw layer, unless the stride is 0x20 and the id 0xe.

| Layer | Texture | Coordinates |
|---|---|---|
| R3DSpecularTextureLayer (0x445450) | the car's sphere maps SPC1..3 (light block +0xdc, 0x440870), by shininess (stream 0: below 0.05, 0.95, above), none with +0x190 | the normals through the model and view rotations: n.xy · 0.5 / √(\|n.z\| + 1) + 0.5, with the original's coarse root (0x5f400000, one step with 1.47 / 0.47) |
| R3DSkyMapTextureLayer (0x445a40) | the road slice's (+0xd0), by the same levels | the position, pushed along the normal by +0, through the model matrix less the car's position into the road's axes (+0x104): across × 1/size + along × 0.1 + 0.5, up × aspect/size + 0.5 |
| R3DShadowMapTextureLayer (0x446060) | the nearest dynamic light's (+0x18c; without one the pass is skipped) | the world positions through the light's two rows (+0x164, +0x178) |

The two-stage draw layer's kernels (0x542090, 0x544780, 0x546bb0) are the one-stage ones run with
40-byte vertices: the second coordinate set, both textures (0x535fb0), the two-set format (setstate 0x2a
1) and each stage's blend (0x29, 0x10029).

## Headlights

Night tracks (industrial and the Monaco set) are those whose materials make the headlights' vertex
layers: the layer factory sets the night flag (0x628a28) when it makes 0x8002, 0x8003 or 0x8011. That
flag also picks the HUD's night dials and the night car pages.
- Headlights_Load (0x452460) reads `GameData\Render\night.psh`, an SHPP file; its `nght` entry is the
  beam, a 128 × 128 byte table.
- Each frame (0x452590) builds the viewed car's headlight space: the world into the car's frame.
- A point's light (0x4527e0) comes from the table at its depth × 1.28 and side × 3.62, less 32 ×
  |height|, divided by 256.
- Headlights::LayerVertexLight (0x452fe0; its _Opaque twin is the same code) adds the headlights' colour
  (0xff808080) times that light to each vertex colour, a channel at most 255. Its dependencies (0x5b3d70)
  are the colours, the index list and the positions.

Not ported yet:
- the cars' light switches (R3DObject +0x2d4), taken as on;
- the cop cars' red and blue light (0x452920);
- the UV and ambient variants (0x453b70, 0x453490), which no track uses at the default options.

## Sprites, the sun and the lens flare

The render module's art comes from two class 4 texture banks: `GameData\Render\common.fsh` and
`particle.fsh` (0x613e04, `Render_LoadCommonFsh` 0x448a40). Its screen sprites are the effects of
`GameData\Render\flare.dat` (0x452180, the bank 0x61bcc0):
- 0x8c-byte records sorted by name, looked up by binary search (0x452130). Each has two layers: a texture
  name (`null` for none), a colour and a size. It also has flags (4 turns the sprite with its screen
  position) and a blink: phase, on, off, in race ticks.
- A layer's texture (0x451ff0) is one of the sky's shapes, by the name's bytes the other way round, else
  common.fsh's: `SUNA`, `FLR0`, `BLUR`, `GLAR`. `lens` is in neither file, so the flare elements that ask
  for it draw nothing.
- One quad mesh draws them all (0x451360), through a blend record (0x537760) whose transparency slot
  is set to 1 (0x5377c0, which Ghidra names FERT_EventPump_v2 because the code is shared): source
  alpha plus destination. The textures are black-backed (SUNA, GLAR, SPIK are opaque black-to-white
  gradients), so black adds nothing. The sun's records are set the same way.
- The screen draw (0x4515e0) places each layer as a quad of
  size × 0.1 / 640 of the screen's width and / 480 of its height, turned by (x + y) / 1440 turns. Below
  alpha 1 the colours' alpha is scaled; below 0.05 the layer is skipped.

The sun (0x460640, 0x1a0 bytes at 0x627e5c):
- Its place is `Sky_SunPosition` (0x460f50). It only exists when the configuration's offsets (+0xcdc,
  +0xce0) put it off the zenith by more than 0.02. Autobahn, canyon, forest, foothills and Monaco 1 have
  it at the zenith: no sun sprite, no flare.
- Among the sky's levels, after those flagged 0x400 (0x460150), the "anus" sprite is drawn at its
  projected place in a pixel view (0x466f70: orthographic, x right, y down). This only happens when the
  sun is on the screen and in front.
- The first time, the 4 × 4 pixels round it are read back (`THRASH_readrect`) as the reference. This
  needs the driver to modulate alpha (0x6197aa, Render_DetectCard's setstate 0x1c3 = the caps word 0x13).
  The reference also tells 16-bit from 32-bit pixels: the last word stays 0 in 16-bit modes.
- After the world (0x45d060 → 0x460320), with the LENSFLARE option (0x657d7c) and the configuration's
  +0xdd8, the pixels are read again every 200 ms (GetTickCount). The count of unchanged ones / 16 is
  the flare's alpha.
- The flare is thirteen sprites, "bnus" to "nnus", on the line from the sun through the screen's centre,
  at `flarepos.dat`'s distances (defaults 1, 0.75, -0.75, 0.5, ...).
- If reading back takes over 5 ms on average after 10 reads, the flare stops for good.

The track's glows are the article effects of type 2 (`'ef'` +8: x, y, z, w, a normal, the sprite's
name, a phase; 0x451810, from the effects list after the opaque one):
- The sprite blinks as above. With its flag 0x10, the effect's phase is added; the remainder is unsigned.
- A depth bias (record slot 4) of depthfactor × -250000 / distance² pulls it towards the eye.
- Fog and z writes are off while it draws; the fog's density and colour are put back after.
- Each layer is a billboard (RBillBoard_CreateQuads 0x44f5b0): corners along the current view's axes,
  from its model-view inverted by cofactors (0x53a1c0), turned by an angle in degrees.
- Its alpha and size depend on the flags (0x4514c0). 8 flickers: at most 190, plus up to 64 at random.
  1 and 2 fade it by its facing; 0x20 and 0x40 scale its size the same ways. Under fog it fades over
  300 units.
- The facing is the dot product (0x556080) of the normal through the model matrix with the camera's
  back row. The normal's w is 0 and the row's 1, and the function scales by their ratio, so the facing
  is always 0. Glows with the flags 1, 2, 0x20 or 0x40 never draw, as in the original.
- The angle (facing × 120 + distance × 3.6) only counts with the flag 4, or when fog was on at the
  previous glow.

In the port, GetTickCount is the frames stepped at 60 Hz (`app_ticks_ms`), so runs repeat. gasm-run 0.16
returns pixels from `read_pixels` only when it runs with `--screenshot`. Without it, the reads are zeros:
the reference and every read match, and the flare shows at full strength even behind the world.
