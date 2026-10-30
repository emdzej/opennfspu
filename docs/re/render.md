# Textures and the game renderer

How a shape in an FSH file becomes a texture on the card, and the parts of the game's renderer around it
(render methods, materials, the render system). Ported pieces cite their files; the rest is a map for the
next steps.

## From a shape to the card

```
FSH shape ──RTex_convertshape──> shape for the class ──Texture_fromshape──> TEXTURE ──DTEX_new/_load──> DTEX
 (game, render/rtex.c)            (halved, mip-shrunk,      (real/texture.c)          (converted to the driver's
                                   copied per quality)                                 format, real/dtex.c)
                         TexManager_begin ──DTEX_uploadall──> THRASH talloc + tupdate (card memory)
                         TEXTURE_select ───DTEX_settexture──> THRASH settexture (+ the 7 stage states)
```

### Pixel formats (`src/real/depthconv.c`, `\real\cmn\depthconv.c`)

Two numberings: shape types (FSH record types: 0x7a P4, 0x7b P8, 0x7e 1555, 0x78 565, 0x7f 888, 0x7d 8888,
0x6d 4444, 0x42 15-bit BGR) and THRASH texture formats (1 P4, 2 P8, 3 1555, 4 565, 5 888, 6 8888, 7 4444,
11 555, 12 DXT1, 13 DXT3). Both map to an index 0..7 (shape types: 0x534350; formats: table 0x5de574) into
one 8 x 8 table of conversion kernels (variants 0x5de3fc and 0x5de2fc) and a table of the intermediate
type of each step (0x5de4fc). A conversion repeats steps until it reaches the target (at most 10):
`SHAPE_converttype` 0x534ab0 for shapes (allocating `"%dto%d"` blocks with a `CSHP` sentinel),
`TEXTURE_converttype` 0x534e50 for texture pixels (formats; a palettised source has its palette converted
into the work buffer 0x6bd960 first). The kernels are 28 small functions at 0x56fe80-0x570830 plus the
per-pixel 0x572ce0 (8888 to 565) and 0x572d0b (8888 to 1555, 0 stays 0). Rules worth knowing:

- 16-bit to 32-bit kernels turn the colour key (0x6a6368, 0) into 0: transparent black.
- 8888 to 565 for palettes never yields 0 for an opaque colour (black becomes 1).
- 8-bit to 8888 reads the 32-bit palette *pointer* (0x5de2f8), not the work buffer: after a
  `TEXTURE_converttype` palette expansion it would read a stale palette. DTEX never picks that path with
  the GL driver (8-bit sources become 4444 through the 16-bit buffer).

Palette records: 0x2a (8888) and 0x29 (565) are the data's (3009 and 3090 images), 0x2d (1555) has 4;
`getclutptr` 0x534440 / `getclut16ptr` 0x5345a0 / `getclut16aptr` 0x534750 expand any of them.

### DTEX (`src/real/dtex.c`, `\real\patch3\cmn\dyntexture.c`)

A texture type is (size, format, palette format, flags with the mip count in the low byte); at most 32
(0x6a33f8, 0x30 bytes each). A texture (0x38 bytes) belongs to a type and keeps its converted pixels as a
shape in DTEX's pool. The driver format comes from scoring every format the driver lists against the
source (`DTEX_score` 0x554070, through the hook 0x5df2c8): the format's position in the source format's
preference list (0x5def98), +20 for alpha the target loses, +20 if the card lacks it, +10 for leaving a
palette behind. The game sets the lists of 565 and 8888 sources per texture class (0x5546b0) and chooses
whether 8-bit sources prefer 8-bit or 565 (0x554690).

`DTEX_uploadall` 0x554e40 runs at `TexManager_begin` (the start of a race or a front-end screen): types
in turn, in passes of rising priority (three per type, 0x5553a0), until every texture is up or the card
is full. When it is full, a texture without card memory takes the least recently used texture's of the
same type when it is selected (`DTEX_touch` 0x555210): the reason types share one size.

With the port's GL driver: no palette formats, so 8-bit textures become ARGB4444, the others keep their
format (8888, 565, 1555, 4444). `tests/tex_conv_test.c` checks all 10,428 FSH images and 4,424 uploads
against the reference decoder.

### Textures (`src/real/texture.c`)

`TEXTURE` (0x54 bytes, vtable 0x5bb6d4): manager, DTEX texture, size, formats, flags (bits 16+ = stage),
a reference count shared by copies, a 20-byte name, seven stage states (+0x38: dither 5, filter 7, 0xc,
mipmap 0xb, LOD bias 8, address U 0x41, V 0x42). The manager (0x14 bytes, vtable 0x5bb6c8: destructor,
create from records, create from a shape) remembers the two stages' textures. The graphics object at
0x69ed0c is { EAGL device, texture manager }; the device's +4 is the render-state object EAGL uses.
Stage states are cached per stage (0x5de6bc) and only changes are sent (setters 0x5de72c: `EAGL_RSSet`,
mipmap 0x5357d0 (3 = trilinear when 0x69dd1d), address 0x535800 (state 0xd when the driver has one mode for
U and V)).

### The game's textures (`src/render/rtex.c`)

Quality level 0..3 (0x619770, from CPU clock, memory and video memory: 0x44e240) selects a row of the
class table 0x5ce59c (6 classes x 4 bytes: depth class, sizes to halve, flags). `RTex_make` 0x44d8e0
prepares the shape (`RTex_convertshape` 0x44d6b0) and makes the texture; names are 4-character codes
(ids below 0x20 become `CAR0`+n). Banks (0x44cf80: an MSVC multimap of (id, name) to textures) hand out
textures by key and states, cloning (`":CLONE"`, shared DTEX) when the states differ (0x44d250). Each
track's `.dtx` (11 bytes: texture counts per size and format) sets the DTEX priorities (0x44dee0); without
one, a table per quality level (0x44de40/50/60; level 3 none).

Packed (RefPack) shape records have no texture format (0x5563f0 knows only DXT1/DXT3 packing): the 222
such records are front-end art drawn in 2D.

## Materials and render methods (not ported yet)

Shared by cars and tracks (`src/render/r3dcar.c` builds the `mt` records and draws through a stand-in):

| Address | What |
|---|---|
| 0x44a520 / 0x44a5d0 | `R3DMaterialSet` constructor / destructor: materials, a texture bank (0x44cf80), the list of sets at 0x619768 |
| 0x44a6b0 | `R3DMaterialSet_Build`: the root's `RMth` records become method templates (0x44b170, a map by name); the `sf` (or `sn`) shapes, inline or as FSH files under the track directory (`"%s%s"` with 0x65b2b8), go into a (code, name) -> shape map (0x44b510); then every `mt` record: its render method (`RMethod_Find` 0x44ba40 by name and the method key 0x6197f8, built from the template on a miss: 0x44bab0), a material (0x1c bytes), and its texture references (0x2c bytes from +0x20: stream id, id, name, 7 stage states): a global texture (0x44d1f0 on the shared bank) with the same states, else `RTexBank_get` (0x44d250) from the shape map; plus the record's constant streams |
| 0x44bab0 / 0x44be40 | Builds a render method from an `RMth` template: an EAGL material (vtable 0x5b3c94), the layer list (0x10-byte entries: factory id, flags, key masks), the stream ids sorted and made unique (0x44ca10, 0x44cc50, 0x44cce0), one EAGL layer per entry (`EAGL_CreateLayer` 0x53efa0) with slots for the streams it reads and `CMethodData` blocks for layers that keep data (0x44bc40) |
| 0x44a160 | A stream id's slot: ids below 0x10000 index the method's table (+0x28), larger ones are looked up (0x44c2c0) |
| 0x44a190 | `R3DMaterial_Bind`: the material's constant streams, the `pr` item's streams (kinds below 0x100 are the part's items via 0x43c700, others chunk records via 0x59f570) and its index lists into the method's slots (+0x2c, 8 bytes per slot); overrides (+0xc, -1 = replaced) during a part's draw (0x44a0e0, 0x44a110) |
| 0x44a090 | The per-draw reset of the scratch streams (4 slots of 0x1000 + 0x400 bytes at 0x613e70 / 0x617e70; counter 0x619764). Called by `View_Begin` 0x467174 and `R3DObject_DrawLODs` |
| 0x467ed0 | The cull state: 3 minus the render system's view's (+0x70, +4, +0x10) cull value, to the render-state setter 0x537600 |

The game's EAGL layer classes (vtables in `re/vtables.tsv`): R3D layers at 0x445430-0x446db0
(`R3DSpecularTextureLayer`, `R3DSkyMapTextureLayer`, `R3DShadowMapTextureLayer`, `R3DLightLayer`,
`R3DSpecularLayer`, `R3DObjectLayer`), `RCustomLayers` at 0x448b60-0x44a020 (`LayerFullAmbient`,
`LayerAmbient`, `LayerSetAlpha`, `LayerUVAnimate`, `LayerTransformCoords`, `LayerUntransformedCoords`,
`LayerTransformIndexCoords`, `LayerIndexedColour`, `LayerIndexedUV`, `LayerFalseSort1/2`) and `Headlights`
at 0x452fc0-0x453b70 (`LayerVertexLight`, `_Opaque`, `_Ambient`, `LayerUVLight`). All have the EAGL layer
vtable shape (9 slots; Headlights 10).
