# THRASH: the 3D driver

Porsche Unleashed draws through **THRASH** ("3rash"), EA's driver interface of the time: the game
(EA's "real" library, `\real\patch3\pc\set3d.c`) loads a driver DLL and calls it through a table of
function pointers. The CD ships two: `Drivers/dx7z.dll` (Direct3D 7, "DX7 3rash", built 2000-03-01,
sources `\3rash\dx7\dx7*.c`) and `Drivers/voodoo2z.dll` (Glide). OpenNFSPU ports `dx7z.dll` onto
OpenGL ES 3.0: `src/render/thrash.c` is the DLL's logic, `src/render/thrash_dev.c` a small Direct3D 7
device on GLES 3 underneath it. Addresses below are dx7z's (image base `0x60000000`) unless marked as
Porsche.exe's.

## Binding (Porsche.exe)

| Address | Name | What |
|---|---|---|
| `0x5753f0` | `THRASH_drivername` | Picks the DLL (`!name` forces it, else the `THRASH_DRIVERNAME` variable wins), closes the old one |
| `0x574d50` | `THRASH_opendll` | `LoadLibrary` (two path patterns), `GetProcAddress` of all 41 exports, aliases getstate/setstate into 4 more slots, sets `setstate(0x1f, 0x6b)` (version), calls `about` and sets the Glide flag `0x5deb70` when the signature is `3DFX`/`2FD3` |
| `0x5753b0` | `THRASH_closedll` | `FreeLibrary` |
| `0x594a80`, `0x594cb0` | `THRASH_selecttable`, `THRASH_savetable` | Switch / save sets of the pointer table (45 words per set from `0x69e5f0`) |
| `0x5755d0` | `THRASH_openwindow` | 640x480 default, creates the window, `setstate(0x12, windowed)`, `0x13` (window hooks `0x5e1960`), `0x19` (hwnd); `setstate(0x25)` (answers 0) |
| `0x556910` | `GLIDE_env` | Glide variables, then driver, window, `selectdisplay`, `setvideomode(mode, 2, 1)` (16-bit) |
| `0x5401e0`, `0x540240`, `0x540200` | `RS_SetThrashState`, `RS_SetThrashStateAlias`, `RS_SetDepthBias` | The renderer's setstate wrappers (the depth-bias one stores the value instead while recording) |
| `0x540320` | `RS_ProbeThrashState` | Sets a state to its own id, restores it if refused |
| `0x44e760` | | Graphics options probe: tries states and restores them (`0x24`, `0x2f`, `6`, `0x15`, `7`, `0xb`, `0x38`, `4`, `0x29`), reads `about` (`flags` bit 6, texture size limits, stages, subtype, texture RAM, device name: "Trident Blade", "Voodoo") |

The pointer table, `0x6bd850`..`0x6bd8f8` (named `p_THRASH_<name>_<argbytes>` in the Ghidra project):

| Slot | Export | Slot | Export | Slot | Export |
|---|---|---|---|---|---|
| `0x6bd874` | about | `0x6bd8a4` | lockwindow | `0x6bd8bc` | setstate (also `0x6bd858`, `0x6bd860`) |
| `0x6bd85c` | clearwindow | `0x6bd86c` | unlockwindow | `0x6bd8c4` | getstate (also `0x6bd8d8`, `0x6bd8e0`) |
| `0x6bd8f4` | clip | `0x6bd888` | pageflip | `0x6bd894` | settexture |
| `0x6bd890` | drawline | `0x6bd8ac` | readrect | `0x6bd898` | setvideomode |
| `0x6bd87c` | drawlinemesh | `0x6bd870` | writerect | `0x6bd8b8` | sync |
| `0x6bd8d4` | drawlinestrip | `0x6bd878` | restore | `0x6bd8a8` | talloc |
| `0x6bd864` | drawpoint | `0x6bd8f8` | selectdisplay | `0x6bd88c` | tfree |
| `0x6bd8a0` | drawpointmesh | `0x6bd8b0` | flushwindow | `0x6bd8c8` | treset |
| `0x6bd8e8` | drawquad | `0x6bd89c` | init | `0x6bd880` | tupdate |
| `0x6bd884` | drawquadmesh | `0x6bd8d0` | is | `0x6bd8f0` | window |
| `0x6bd868` | drawtri | `0x6bd8cc` | drawtrifan | `0x6bd8ec` | drawtrimesh |
| `0x6bd8dc` | drawtristrip | `0x6bd8b4` | drawsprite | `0x6bd850` | drawspritemesh |

The `@8` variants of drawtristrip / drawtrifan / drawlinestrip / drawpointstrip are exported but not
fetched by the game.

How the game uses it, from its call sites:

- **Meshes** (the renderer at `0x540a40`, `0x542090`, `0x543840`, `0x544780`, `0x545830`, `0x546bb0`,
  `0x548120`): it projects and clips itself and builds pre-transformed vertices, then draws them with
  `setstate(0x1b3, &prim)`, a raw DrawIndexedPrimitive: triangle lists (4) and strips (5), FVF `0x1c4`
  (one texture coordinate set) or `0x2c4` (two, after `setstate(0x2a, 1)`). Texture blending per stage
  with `0x29` / `0x10029`.
- **Textures** (`DTEX_upload` `0x554cc0`, `0x572840`, `0x5728f0`): `talloc(w, h, format, clut, mips)` and
  `tupdate(t, pixels, clut)`, with the format chosen by scoring the card's format table against the FSH
  shape type (`0x5541xx`); `THRASH_FormatToShapeType` `0x5565e0` maps THRASH formats to shape types.
- **Lens flare** (`0x460150`): `readrect` of 4x4 pixels at the sun, when `setstate(0x1c3)` said yes.
- **z clear** (`0x4da340`): `window(3)` + `clearwindow` clears only the z-buffer; without a z window it
  draws a far quad instead.
- **2D** (`0x5340d0`, `0x535230`, `0x570b00`): `lockwindow` / `unlockwindow` on the front or back buffer
  (the software font driver, movies).

## Vertices

`ThrashVertex` (FVF `0x1c4`, 32 bytes): `x, y` (screen pixels), `z` (0..1), `rhw`, `diffuse`, `specular`
(D3DCOLOR `0xAARRGGBB`; the specular alpha is the vertex fog factor), `u, v`. FVF `0x2c4` adds `u2, v2`
(40 bytes). setstate `0x2a` switches between them (and the stride the batcher and meshes use; the line
and point index functions always step 32 bytes).

## The batcher (dx7wrap.c)

Single triangles, quads, triangle and quad meshes and lines are collected into one buffer of `0x1fa0`
bytes (253 vertices of 32 bytes) and drawn with DrawIndexedPrimitive when the primitive type changes,
it would overflow, or any device state changes (`DX7_SetRS` / `DX7_SetTSS` flush first). Each copied
vertex gets:

- `z += depth bias` (setstate `0x18`: value * 2^-15; the bias resets at every EndScene);
- `diffuse = 0xffffffff` when shading is flat (setstate `6`, 0);
- with the custom fog table (setstate `0x14`), `specular = table[round(z * 255)] << 24` (its colour lost).

Triangles and quads are culled in software before they are queued: the sign bit of
`(b.x - a.x)(c.y - a.y) - (c.x - a.x)(b.y - a.y)` against the cull state (`0x60031fec`: 1 draws all,
`0x80000000` culls counter-clockwise, 0 clockwise), on top of Direct3D's own culling. Indexed strips and
fans skip all of that and go straight to the device; points are drawn from the caller's vertices, which
are adjusted in place (so a redrawn point gains the bias again). `clearwindow` doesn't flush: triangles
still queued land on top of the cleared area. `setstate(0x1b3)` doesn't flush either: its primitive is
drawn before whatever is still queued.

## setstate

`setstate(state, value)`: the low 16 bits are the id, bits 16-18 a texture stage (only `0x10000` ..
`0x70000` exactly). It answers 0 when unsupported. Most successful calls are recorded for `getstate`
(ids 0-0x46, 0x64-0x6e, 0xc8-0xd7, 0x12c-0x13b, 0x190-0x19f, 0x2bc-0x2cb, 8 stages each; the slot map
adds `hi - lo` per range, so neighbouring ranges share a slot) and passed to the callback set with `0x27`.

| Id | What (values) | Direct3D |
|---|---|---|
| 1 | texture (= settexture) | SetTexture |
| 2 | cull: 0 none, 1 counter-clockwise, 2 clockwise | CULLMODE 1/3/2 + software cull |
| 3 | background colour (clear colour; also the material colour) | SetMaterial |
| 4 | z-buffer: 0 off (ZFUNC always), 1 on, 2 W-buffer (if the card has one, else z and 0) | ZENABLE, ZWRITEENABLE, ZFUNC |
| 5 | dither | DITHERENABLE |
| 6 | shade: 0 flat (white diffuse), 1 Gouraud, 2 Gouraud + specular | SHADEMODE, SPECULARENABLE |
| 7 | filter: 0 point, 1 linear, 2 anisotropic | MAG/MINFILTER |
| 8 | mip LOD bias (float) | MIPMAPLODBIAS |
| 9 | antialias: 0, 1 (off), 2 | ANTIALIAS |
| 10 | alpha blending: 0 off, 2 on (1, 3 refused) | ALPHABLENDENABLE |
| 0xb | mipmapping: 0 none, 1 point, 3 linear (2 refused) | MIPFILTER |
| 0xd | address: 0 clamp, 1 wrap, 2 mirror | ADDRESS |
| 0xe | fog density (float, or n meaning 1/n) | FOGDENSITY |
| 0xf | fog colour | FOGCOLOR |
| 0x12 | windowed (flip becomes a blit) | |
| 0x13 | window hooks (8 words; [5] is called around locks) | |
| 0x14, 0x69 | custom fog table (64 + 1 bytes) | FOGTABLEMODE none, vertex fog from the table |
| 0x15 | fog: 0 off, 1 on, 2 linear, 4 exp, 8 exp2 | FOGENABLE, FOGTABLEMODE, FOGVERTEXMODE, ... |
| 0x18, 0x66 | depth bias (the batcher's) | |
| 0x19 | window handle | clipper |
| 0x1e | sprite mesh stride; 0x1f version (0x68..0x6b, else 0x6b) | |
| 0x23 | surfaces not lost? | IsLost |
| 0x24 | alpha test: 0 off, else on with that reference | ALPHATESTENABLE, ALPHAFUNC, ALPHAREF |
| 0x27 | state-change callback | |
| 0x28 | z compare: 0..7 = never .. always | ZFUNC |
| 0x29 | texture blend (below) | texture stage ops |
| 0x2a | vertex format: 0 one texture coordinate set, 1 two | FVF, TEXCOORDINDEX |
| 0x2e, 0x65 | gamma (float, clamped 0..4): the desktop ramp times it | SetGammaRamp |
| 0x2f, 0x30, 0x34-0x36 | stencil enable / function / fail / z-fail / pass (6 skipped) | STENCIL* |
| 0x38, 0x68 | transparency: 0 alpha, 1 additive (src alpha or one, by card), 2 (zero, inv src alpha), 3 multiply | SRCBLEND, DESTBLEND |
| 0x39, 0x6a | z write | ZWRITEENABLE |
| 0x3c, 0x67 | vsync (Flip flags) | |
| 0x3f | guard band (4 floats: left, right, top, bottom) | |
| 0x40 | alpha compare 0..7 | ALPHAFUNC |
| 0x41, 0x42 | address U / V | ADDRESSU/V |
| 0x43 | clear z (float) | |
| 0x193 | the 47 capability words | |
| 0x194, 0x195 | source / destination blend (0..9) | SRC/DESTBLEND |
| 0x199 | wireframe | FILLMODE |
| 0x19a | texture factor | TEXTUREFACTOR |
| 0x1b3 | draw primitive (see Vertices) | DrawIndexedPrimitive / DrawPrimitive |
| 0x1c3 | "blend modulate alpha" capability (the flare readback's switch) | |
| 0x1a4-0x1c1 | Direct3D transform and lighting (matrices, lights, vertex buffers) | not used by the game: answer 0 in the port |
| other ids | stored only (0x1a-0x1c, 0x20, 0x21, 0x37, 0x3a, 0x3b, 0x3d, 0x3e) or refused | |

Texture blend modes (`0x29`; stage in the high word), as stage ops (colour / alpha):

| Mode | Stage 0 | Later stages |
|---|---|---|
| 0, 8 | modulate texture by diffuse / texture alpha (modulated when the card can) | 0: select texture (disable with one stage); 8: blend by texture alpha / disable |
| 1 | add / add | same |
| 2 | disable | disable |
| 3 | modulate / modulate | same |
| 4 | subtract / select texture | same |
| 5 | select current / add-signed(1 - texture, current) | same |
| 6, 7 | modulate 2x, 4x | same |
| 0x61 | add-signed | same |
| 0x62 | add / blend factor alpha (texture, factor) | same |
| 0x63 | blend factor alpha / select texture | same |

`setvideomode` (`DX7_DefaultStates` `0x6000a630`) starts every mode with: no texture, cull
counter-clockwise, linear filter, Gouraud, blending on, alpha test 16, point mips, background black,
dither, fog off (density 0, colour white), transparency 0, clamp, gamma 1, no bias, one texture
coordinate set, blend mode 0 on both stages, stencil off, clear z 1.0, vsync, z-buffer (W first when
asked for), z compare less-equal (3).

## Textures (dx7tex.c)

`talloc(width, height, format, clut, mipstage)`: `mipstage` low 16 bits = extra mip levels, high 16 bits
the stage (`0x10000` = stage 1). Formats (FSH shape type in brackets): 1 P4 [0x7a], 2 P8 [0x7b],
3 ARGB1555 [0x7e], 4 RGB565 [0x78], 5 RGB888 [0x7f, never available], 6 ARGB8888 [0x7d], 7 ARGB4444
[0x6d], 12 DXT1 [0xf8], 13 DXT3 [0xfd]. Rows are `(bits * width + 7) / 8` bytes, mip levels follow
level 0 in the data, DXT data starts 8 bytes in. `tupdate(t, pixels, clut)`: the staging surface aliases
the caller's pixels for the Blt; the clut is 256 words `0x00RRGGBB` (palettised textures are opaque).

`tfree` walks a list that nothing fills (`0x60021d50`), so it never frees: textures go only with
`treset`. `treset` doesn't clear the recorded state 1, so `settexture` with a new texture at a reused
address is skipped as "already set" (as in the DLL).

## Windows

`window(n)`: 1 front, 2 back (front when there is no back buffer), 3 and 5 the z-buffer, others none.
Drawing always goes to the render target (the back buffer); the window decides what `lockwindow`,
`readrect`, `writerect` and `clearwindow` mean. `lockwindow` answers `{pixels, pitch, format, width,
height}` (`0x600216d8`): format 4 for RGB565 (11 for 555), 6 for 32-bit, 5 for 24-bit, the bit depth
for the z-buffer. `readrect` copies rows at 4, 3 or 2 bytes a pixel by format; `writerect` uses 2 bytes
unless the format is 5 (so in 32-bit modes it copies half of each row: kept).

## THRASH_about (0x600555a0)

Signature `'7D3D'`, size `0xd0`, version, flags (bit 0, bit 5 windowed always; 1 square, 2/3 power-of-two
width/height, 4 software, 6 global clut, 7 trilinear in two passes), texture size limits, the texture
format table (16 words: bit 0 available, 5 = 1555 only as 555) and clut table (4), display modes
(`0x28` bytes each, index 0 unused; 1..5 are 640x480, 800x600, 1024x768, 1280x1024, 1600x1200), stages,
"DX7 3rash", the card subtype (`DX7_CardSubtype` `0x6000a040`: PCI ids to codes, e.g. `0x12` GeForce
256, `0x10` TNT2, 4 TNT), texture RAM, the author string, DirectX version 7 and the device name.

## The port

- **The card.** A fixed profile (`caps_init`): a GeForce 256-class part, which is what the readme's
  advice targets: two stages, stencil, table and vertex fog, specular, anisotropy, trilinear, LOD bias,
  separate U/V address modes, gamma, no W-buffer, no antialiasing; every format but 888; subtype `0x12`;
  32 MB. The DLL's answers that depend on the card follow from it.
- **Surfaces.** Two colour textures (front, back) at the mode's size times an integer scale that fits the
  drawable (sharper 3D at the original layout), RGB565 in 16-bit modes (the original's banding), RGB8 in
  32-bit modes; one depth/stencil buffer that stays with the render target. Rows are stored top first,
  so lock buffers and `glReadPixels` agree. `pageflip` swaps them (a copy when windowed); `THRASH_present`
  (a port addition) draws the front buffer letterboxed onto the drawable, through the gamma value.
- **Vertices.** `x, y` + 0.5 (Direct3D 7 pixel centres on integer coordinates), `w = 1/rhw` for
  perspective-correct interpolation, z to GL's -1..1. The viewport is a scissor rectangle (pre-transformed
  vertices aren't moved by it; clear rectangles are clipped to it).
- **Fixed function.** One shader: two texture stages with Direct3D's ops and arguments (`D3DTOP_*`,
  `D3DTA_*` with complement / alpha-replicate), specular added, then fog (table modes from the pixel's z,
  else the specular alpha), then the alpha test. Flat shading takes each triangle's first vertex colour
  (Direct3D's rule; done by spelling the triangles out).
- **Textures** are expanded to RGBA8 on upload (palettised and DXT included); the pixels are kept so a
  clut-only update can re-expand.
- **Locks** read the surface back (centre texel of each scaled pixel) and write it back on unlock.

Uncertain or approximated:

- A stage without a texture ends the cascade (stage 0: the diffuse colour). The DLL's default blend for
  stage 1 selects the texture and the game draws with stage 1 empty, so on its hardware an empty stage
  can't have contributed; Direct3D's documentation doesn't pin this down.
- P4 nibble order (high nibble left) is assumed; the data has no 4-bit textures.
- DXT mip offsets: the DLL advances by pitch * height, which for compressed surfaces isn't the level
  size; the port uses the block size (the data has no DXT mips that would tell).
- The z-buffer can't be read on GLES 3 / WebGL 2: locking or reading the z window fails (the game
  doesn't).
- The lock hook ([5] of setstate `0x13`) is called with 1 before and 0 after a lock; the DLL pushes no
  argument for the first call.
- Wireframe (`0x199`) is recorded but not drawn (no polygon mode in GLES 3); lines and points are one
  scaled-surface pixel wide; anisotropic filtering is linear.
- Display mode list entries' words 3-6 (4, 1, 1, 1) aren't identified.

## Checking it

`THRASH_selftest()` (port addition) draws through the API: a Gouraud triangle and its culled reverse, a
wrapped palettised texture, a 50% alpha-blended quad, a vertex-fogged strip, a line and a point, then
flips and presents. Headless screenshots of `gasm:gl` games need ANGLE next to `gasm-run` (or
`--gl-lib <dir>`).
