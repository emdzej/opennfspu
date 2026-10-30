# EAGL2: from meshes to THRASH

EAGL2 is EA's graphics layer of the time (`..\..\library\eagl2\include\eaglmaterial.h` in the exe's
strings). The game's renderers (cars, track, sky, HUD 3D) never call THRASH for geometry: they fill EAGL
streams and run EAGL materials, and EAGL transforms, clips, projects and hands the result to THRASH. The
port is `src/eagl/` (`eagl.h` is the API); addresses are Porsche.exe's.

## Precision

Direct3D 7 sets the x87 to 24-bit precision when the game creates its device, so the game's float code runs
with every operation rounded to single precision. C `float` arithmetic in the original's operation order
reproduces it; the port keeps that order term by term. The hand-written PACODE transformer sets 24-bit
precision itself. Transcendentals (the projection's `fptan`, `fsin`) are taken as the correctly rounded
float of the double result.

## Matrices (0x539600..0x539fe0)

Row vectors, `v' = v * M`, translation in elements 12..14, D3D style. Product `0x539620` (`out = a * b`),
identity, scale, translation, rotation + translation in and out, orthonormal inverse (`0x539b20`: transpose
the 3x3, rotate -t), point and array transforms. A matrix stack (`0x539bc0`, 0x1c bytes: base, top, index,
capacity, serial, seen, dirty) counts every change in `serial`; views compare it with `seen`.

## The transformer (0x537ed0..0x5385d0, PACODE 0x5b05c0..0x5b0960)

EA's real library has one vertex transformer with module state: matrix (`0x5dea58`), near/far (`0x5dea5c`,
default 1.0 / 65535), clip-code output (`0x69e0d0`), guard-band shift (`0x5dea60`, as an exponent increment:
`|z| * 2^shift`), source/destination/code strides. `EAGL_Transform(flags, n, src, dst)` (`0x538320`) picks a
routine from the flags (cached until a setter resets it):

| Flags | Routine |
|---|---|
| `& 0xffff == 1` / `3` | 4x4 / xyz with w = 1, no codes (`0x538470`, `0x538430`) |
| `& 0xff == 0x11` / `0x13` | xyz out with w = 1 and codes (`0x538540`, `0x5384b0`) |
| matrix + 1/z, not bit 7, code routine 1..3 | PACODE `0x5b0730` |
| otherwise | the C routine `0x5385d0` |

Flag bits: 1 matrix, 2 the input has 3 components, 8 `out[3] = 1/z`, 0x10 `out[3] = 1`, bits 8..10 the
clip-code routine. Without a code output the bits 8..15 are dropped; without a guard band, code routine 2 is
replaced by 1. A draw transforms with `0x20b` (perspective: PACODE, code routine 2) or `0x613`
(orthographic: the C routine, code routine 6).

The PACODE routine sums `x*m0 + y*m4 + z*m8 + m12` (and so on), ignores the fourth column and stores
`1/z` as the fourth component: the projection puts the depth into z.

**Clip codes** come from the IEEE bit patterns, without float compares: the sign bits of x, y, z and the signs
of `|z| - |x|`, `|z| - |y|` (integer subtraction of the magnitudes: for non-negative floats the integer order
is the float order) make a 5-bit index into a 32-word table (the exe's `0x5dfb90` / `0x5dfc10` for PACODE,
`0x5de94c` / `0x5de9cc` for the C routines, read at runtime); near and far are the same trick against the
near/far bits. With a guard band the low byte is the guard band (`|z|` scaled by `2^shift`) and the second
byte the view frustum itself:

| Bits | Meaning |
|---|---|
| 0x01 / 0x02 | nearer than near (or z negative) / farther than far |
| 0x04..0x20 | outside the guard band: one bit per side plane |
| 0x0400..0x2000 | outside the view frustum (same planes) |

A primitive is **rejected** when the AND of its vertex codes `& 0x3c03` is set (all outside one frustum
plane or near/far) and **clipped** when the OR `& 0x3f` is (outside the guard band or near/far). Triangles
crossing the screen edge but inside the guard band are drawn unclipped (the driver's guard band clips
them).

## Views (0x538a80..0x539470)

`EaglView` (0x224 bytes): camera stack (+0x00), object stack (+0x5c), background colour (+0xb8), near / far
/ fov / aspect (+0xcc..), `zscale = far / (far - near)`, `zoffset = -zscale * near`, ortho parameters, the
sphere-cull tan/cos pairs (+0xf8..), the combined matrix (+0x108 = model * view * projection), the projection
(+0x148), model * view (+0x188), the owner device (+0x1c8), the projection serial (+0x1d0) and the viewport
(+0x1d8..0x21c), projection kind (+0x220).

- **Projection** (`0x538f20`): identity, then `m0 = cot(fov/2) * sx`, `m5 = -cot(fov/2) * sy * aspect`,
  `m8, m9` = the off-centre offsets, `m10 = -1` (depth = -z). The cull values are `tan(fov/2)`,
  `sin(pi/2 - fov/2)` and the same for `fov/2 * aspect`.
- **Viewport** (`0x538b10`, arguments w, h, x, y, minz, maxz): clamped to the display; centre and half sizes
  kept; a clamped rectangle gets the projection scale/offset that keeps the picture where the unclamped one
  was. `minz` / `maxz` are the near/far distances the sphere test uses.
- **Begin** (`0x538e40`): current view of the device and the context, the projection rebuilt, `THRASH_clip`
  to the viewport (truncated). **End** (`0x538ef0`): `THRASH_flushwindow`, no current view.
- **Update** (`0x539110`): model * view recombined when either stack's serial moved, times the projection
  when that or the projection serial moved.
- **Sphere test** (`0x5391f0`): the centre through the camera stack; near/far and the four side planes by
  distance (`> r` culls), or the viewport box for ortho views.

## The context (0x1182c bytes, `g_eagl` = 0x69e0d4)

One per device, allocated by the device object (`0x5387a0`). The layout matters: the game's own layers
(R3D, RCustomLayers, Headlights classes in 0x437700..0x458000) read and write it by offset.

| Offset | What |
|---|---|
| +0x0000 | current view |
| +0x0004 | vertex count of the draw |
| +0x0008..0x10 | stamps: diffuse / specular / uv stream already copied in this draw (layer position) |
| +0x0018 | positions (LayerCoords) |
| +0x001c | stage-0 texture of the draw |
| +0x0020 | primitive type: 1 strip, 2 lines, 3 triangle list, 4 quads |
| +0x0024..0x40 | near, far, viewport scale x/y/z, offset x/y/z (`0x540580`) |
| +0x0044 | clip code per vertex (252) |
| +0x0434 | work vertices: `ThrashVertex` (32 bytes) or `ThrashVertex2` (40); room for 2016 |
| +0x10034 | u16 indices (0x5e8: 504 triangles) |
| +0x10c04 | triangle count (non-DX7 path) |
| +0x10c08, +0x10c0c | the vertex list some layers walk (terminated by a negative index) |
| +0x11000 | per primitive `{AND & 0x3c03, OR & 0x3f}` (252) |
| +0x117e4 | flags: 1 perspective, 2 ortho, 8 transformed this draw, 0x10/0x20/0x40/0x80 diffuse/specular/uv/uv2 set by a layer, 0x100 all inside, 0x200 all rejected, 0x10000 guard band, 0x20000 DX7 driver |
| +0x117e8 | guard band scale (2 with one) |
| +0x117ec | sort this draw |
| +0x117f0 | the `ThrashPrimitive` of the draw (setstate 0x1b3) |
| +0x11808 / +0x1180a | vertex / index count of the result |
| +0x1180c | depth bias (2^-15 units, added by the kernels on DX7) |
| +0x11810 | current vertex format (setstate 0x2a) |
| +0x11814..0x11820 | two-stage draw: uv sets and textures |
| +0x11824 / +0x11828 | stage 0 / 1 blend mode as last set (0x29 / 0x10029) |

`EAGL_ContextInit` (`0x548f80`, the mode index as argument) asks THRASH for its guard band (setstate 0x3f:
left, right, top, bottom) and takes one when the mode's width and height times 1.5 fit inside; it sets the
DX7 flag when `THRASH_about`'s name starts with "DX7". dx7z.dll and the port's THRASH say "DX7 3rash" with a
+-2048 guard band: every draw goes the DX7 way with a guard band of 2.

## Materials and layers (0x53eed0..0x53f6f0)

A **material** (`EAGL::Material`, vtable 0x5b3c94: the game's destructor, `EAGL_MaterialDraw` 0x53ef20) is a
chain of **layers**. `EAGL_MaterialDraw(streams, prim, n)` stores n and the primitive type, clears the
stamps and the low flag word, and runs each layer's slot 0 with its two stream slots; a layer returns the
next one (NULL stops the chain, as the coords layer does for a wholly rejected draw).

Layer (`EAGL::Layer`, 0x30 bytes): vtable, prev, next, type (+0xc), id (+0x10), param (+0x14), stream slots
(+0x18, +0x1c), material (+0x20), dependency positions (+0x24, int16) and layers (+0x28), dependency count
(+0x2c). Slots: 0 execute, 1 destroy, 2 set param, 3 dependencies (`{type, id}` pairs), 4 reset, 5 / 6 wants
stream 0 / 1, 7 what it provides (4 colours, 8 uv, 0x10 coords), 8 (always 0 in EAGL's layers).

`EAGL_MaterialBind` (`0x53f010`) numbers the stream slots in chain order (a layer that wants only stream 0
reads it as both) and resolves each layer's dependencies: the nearest previous layer of that type and id (id
0 = any), with its chain position as the stamp.

**Factories** (`0x53efa0`): a chain, newest first; EAGL's own (`0x53f6f0`) is last. The game registers its
own at `0x449a60` for its 0x8000+ types.

| Factory type | Layer type | What (vtable) |
|---|---|---|
| 0 | 0 | draw (0x5bbc60, `0x53f390`): dependencies colours 5, specular 0x10, uv 2, texture 3 |
| 1 / 2 | 1 | coords (LayerCoords 0x5b6a34 / 0x5bbe10): positions, transformed here, 32 / 40-byte vertices |
| 3 | 2 | uv (LayerUV) |
| 4 | 3 | texture (LayerTexture) |
| 5 | 1 | coords data (0x5bbdec, passive) |
| 6 | 5 | colours (LayerColours) |
| 7 | 6 | (0x5bbdc8, passive, provides colours) |
| 8 | 7 | render states (LayerState: six states, only changes sent, `0x5403c0`) |
| 9 | 8 | sort (0x810 bytes: the draw's triangles sorted by depth) |
| 10, 0xb, 0xe | 9, 10, 0xc | passive data layers |
| 0xc | 5 | environment colour (`0x5741f0`, pending) |
| 0xd | 1 | normals through the camera rotation (`0x574770`, pending) |
| 0xf | 2 | indexed uv through the vertex list (`0x574880`, pending) |
| 0x10 | 0xe | index coords (`0x53f2a0`): pre-transformed positions and codes picked by a byte list |
| 0x12 | 0 | two-stage draw (0x5bbca8, `0x53f510`): stage modes from stream 0, dependencies 5, 0x10, 2/0xc, 2/0xd, 3/0xe, 3/0xf |
| 0x13 | 0x10 | specular (passive) |

What the renderers feed: positions as float4 (stride 16, xyz used), colours as u32 `0xAARRGGBB` per vertex,
uv as float pairs, a texture handle as the texture layer's stream, and the primitive type from the CRP `pr`
item's type (`0x44a190`; 3 for nearly every primitive in the data, a few 4 and 1). Vertex count per draw is at
most 252 (the CRP index lists are bytes).

## The kernels

Draw layers run the kernel of the primitive type: `0x540a40` strip, `0x548120` lines, `0x543840` triangle
list, `0x545830` quads; the two-stage layer `0x542090` / `0x544780` / `0x546bb0`. Each has two paths: the
DX7 path (flag 0x20000), which builds u16 indices and draws with one setstate(0x1b3), and an older one for
drivers that draw through `drawtrimesh` with int indices. Only the DX7 path is ported (the other is never
taken with dx7z.dll).

The triangle-list kernel, step by step:

1. **Stamps**: a stream is used only if its layer position is newer than the one this draw already copied.
2. **Streams into the vertices**: colour (+0x10), specular (+0x14), uv (+0x18) of the triangles that aren't
   rejected (all vertices when every vertex is inside).
3. **Triangles**: inside ones (both codes 0) are projected in place when the positions are new
   (`x = x * sx * rhw + ox`, `y` alike, `z = (z * sz + oz) * rhw + bias`) and indexed; ones that need clipping
   go through the clipper, whose polygon is projected after the input vertices and fanned (`v0, v0+1+k,
   v0+2+k`).
4. **Draw**: the stage blends reset if a two-set format was on, the texture selected (`0x535fb0`, pending),
   sorted if asked, then one indexed triangle list; with every vertex inside, the vertices as they are (no
   indices).

A second draw layer in the same material finds nothing new (stamps, no new transform) and draws the same
result again: multi-pass. The strip kernel works the same way with a twist: a strip vertex used by a
triangle that needs clipping gets a projected copy after the input (the clipper needs the clip-space
original), and `remap` (`0x6bd060`) says which copy each triangle uses; odd triangles reverse their
winding; vertex n is scratch for where the copies end between passes.

Quirks kept: the stream loops of the list kernel round the triangle count up; with everything inside the
sort still runs on the index list (stale) although the draw has no indices; a strip vertex's "is it used by
a visible triangle" history still holds the triangle two back for the last two vertices; the clipper writes
a second texture coordinate set past its 32-byte vertex.

## The perspective clipper (0x54bd90)

Sutherland-Hodgman over the planes whose OR bit is set, in order near, far, +y, -y, -x, +x; the guard band
scale multiplies z for the side planes. Each pass walks the edges from the last vertex; a vertex exactly on
the plane counts as inside. Near/far intersections put z on the plane and `rhw = 1/z`; side intersections
interpolate z and put the clipped axis on the plane (`+-gb * z`). Colours interpolate per channel with the
float-to-int trick: `delta * t + 1.5 * 2^k` (k = 23, 31, 39 for the channel's position) stored as a float,
whose low bits are the rounded scaled delta. Two adjacent 10-vertex buffers (`0x6bc340`, `0x6bc480`) ping
pong. The orthographic one (`0x549610`) and the 40-byte versions (`0x54e620`, `0x550db0`) are pending.

## The depth sort (0x5490e0)

Records of 12 bytes: the smallest screen z of the triangle and its three indices, sorted ascending with MSVC
6's `std::sort` (introsort: median of three, unguarded partition, depth limit 2 log2 n, heap sort fallback,
insertion sort below 17; `0x553630..0x553ca0`), then written back. Ascending means nearest first. Porting
the STL code itself keeps equal keys in the original's order.

## Not EAGL: REALGRAPH (0x575e40..0x57e930)

The seven large float routines at 0x575e40..0x57b8f0 are the clip modes (0, 2..7 by `0x6abd70`) of
REALGRAPH, the real library's own polygon path ("[REALGRAPH] unsupported clip mode selected", dispatcher
`0x57e930`), with integer screen-rectangle outcodes (`0x57d7a0`) and `THRASH_drawtrimesh_glue` (`0x57d530`).
They serve the library's 2D and font drawing, not EAGL.

## Pending

Kernels for lines, quads and the two stages; the orthographic and 40-byte clippers; the layers 0xc, 0xd,
0xf; `TEXTURE_select` (`0x535fb0`, the real library's texture state); the non-DX7 kernel paths. Tests:
`tests/eagl_test.c` (against the recording THRASH of `tests/eagl_fake_thrash.h`); `EAGL_selftest()` draws a
scene for headless screenshots.
