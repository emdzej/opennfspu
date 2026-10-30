# Cars: R3DCar

The car side of the 3D renderer (0x437700..0x458000, `d:\Nfs\Nfs5\game\Common\Render`): how a car's model
is loaded, split into levels of detail and parts, and drawn through EAGL. Port: `src/render/r3dcar.c`,
`car_ini.c`, `car_pending.c` (what isn't ported yet), `car_selftest.c` (a port addition). Test:
`tests/car_load_test.c`.

## Classes

| Class | Vtable | Port |
|---|---|---|
| `R3DObject` (base of cars and other 3D objects) | 0x5b37f0 (22 slots) | the slots cars use, in `R3DCar` |
| `R3DCar` | 0x5b3620 (25 slots) | `R3DCar` |
| `R3DLOD` / `R3DCarLOD` | 0x5b36cc / 0x5b36ac (7 slots) | `R3DCarLOD` |

`R3DCar_ctor` (0x437850) runs `R3DObject_ctor` (0x4413f0); the object is 0x708 bytes and points at the
simulation's car state (+0xf4), from which the renderer reads the position (+0x330), rotation (+0x364),
style (+0x568), the 120 option ints (+0x56c) and the car's specification (+0x558). The port's structs keep
only the fields the ported code reads, each commented with its original offset.

## Loading (`R3DCar_LoadCrp` 0x441940)

1. Slot 2 (`R3DObject_Unload` 0x442320) frees a previous load.
2. `<name>.crp` (".crp" is appended unless the name has it) comes from a cache shared by name with a
   reference count (`R3DCrp_Acquire` 0x43ff80): one chunk tree (`UDataFile`, 0x59f630..). Without it the
   original loads a stand-in car and still returns success.
3. `<name>.tpg` (`R3DCar_LoadTpg` 0x437780) is an INI text file (below), kept at +4. From `[header]`:
   `drivercampos` (default -0.36, 0.4, -0.25), `drivercamrot`, `dooraxis` (default 0, 1, 0), `noroof`,
   `hoodextent` (0.15), `trunkextent` (-0.15), `headlightextent`, `movespoilerextent`, and the style's own
   `drivercamposstyleN` / `drivercamrotstyleN`. In the race with the mirror setting (0x65742c) the camera's
   x is negated.
4. The root's `sf` items (shapes) go into a vector sized by `numfiles`, by their index.
5. With textures on: `<name>d.fsh` and `<name>l.fsh` (which ones depends on the detail byte +0xaa and the
   front end), then the option decals (0x447b80, pending).
6. `R3DObject_BuildLODs` (0x4424e0): slot 19 (`R3DCar_PreLOD` 0x43ad80: `R3DCar_LoadStyles`, then the
   spec's forced options +0x32c where they are not -1), then 7 LODs (slot 16 returns 7, slot 17 makes a
   `R3DCarLOD` for LOD number 1..7, slot 1 of it builds it), slot 20, then the size (0x444fc0).
7. `R3DObject_LoadHeader` (0x4426c0): texture pages (pending) and the material set (0x44a6b0, partly).
8. The `PdnB` and `odnW` point lists (records `{option, value, points}`, kept when the option has the
   value; `PdnB` lowered by the half height).

### Styles (`R3DCar_LoadStyles` 0x437a30)

The 120 options are cleared, then `[style<N+1>]` gives `count` entries: `geometry<i>` is an option index,
or else `texture<i>` + 0x3c; `type<i>` is its value. The rest of the function eases the hood, trunk and
headlight extents over time (GetTickCount) and takes the stripe colour: pending with the car's animation.

## INI files (0x5234c0..0x523ae0)

The text is copied and split in place. Parsing finds `[name]` lines (up to 700 sections); selecting a
section splits its `key=value` lines (up to 100; a value ends at the line break or `;`; both are trimmed of
blanks and tabs). Names compare case-sensitively. Getters: int (atoi), bool (int != 0), float (atof),
vectors (`sscanf` with the exe's `"%f,%f,%f"`), each with a default for a missing key. Files are read through
the real library's buffered stdio wrapper (0x56d510 open, 0x56d670 read, 0x56d640 close).

Quirk: a key line without `=` makes the original's section scan start over at the same place forever; no
file of the game has one. The port moves on to the next line.

## Parts and levels of detail (`R3DCarLOD_Build` 0x43c0e0)

Every `Arti` article has a `Base` item. Bytes from +0x44 (the part info):

| Offset | Meaning |
|---|---|
| +0x44 / +0x45 | option index / value: the part is used only when the car's option has this value (body variants `Body` / `Body1` / `Body2` are option 1 = 0 / 1 / 2; cabrio tops 0x25; windows 0x23; lights 0x14; decals 0x1f; handles 0x28) |
| +0x46 | sort key (signed): the draw order within an LOD |
| +0x48 | wheel slot (-1 none): the LOD records the article in its slot table, and the draw adds the slot's offset (+0x6d0) |
| +0x4b | hinge kind: 6 / 7 doors (about `dooraxis`, x negated for 7), others (wheels 1..4, hood, trunk, shadows 0x12..0x15) three angles about x, y, z |
| +0x4d | animation slot: nonzero = animated vertices (`vt` index + 0x10 per frame; cabrio tops) |
| +0x4e | view mask (bits 0x10..0x80) and 8 = deforms with damage |
| +0x51 / +0x52 | drawn in the cockpit view / exterior only |
| Base +0 bit 0 | has translucent primitives (the second pass) |

Then a table of 12-byte entries from Base + 0x64 (first index at +8, count at +12): byte 0 = has primitives,
u16 +4 / +6 = the range of `pr` indices, u16 +8 = the LOD number, which is also the index of the part's `vt`,
`nm`, `uv` and `tr` items. An LOD takes every article whose option matches and which has an entry with its
number. The parts are sorted by the key with MSVC 6's `std::sort` (introsort: median of three, Hoare
partition, a heap fallback past 2 log2 n levels, then insertion sort), which the port reproduces so equal
keys keep the original's order. The entry compared runs from the table's start while the index recorded runs
from `first`, which is the same thing because `first` is 0 in every car.

A part (0x54 bytes) keeps a cache of one lookup per item kind (`vt nm uv df Base pr ef`, the exe's table
0x5e5084). Animated parts copy frame 0 and count their frames into the car's animation slot. Deformable parts
copy their vertices when the detail level (0x6197c4) allows damage.

`R3DCarLOD_Bounds` (0x43ddc0) takes the min / max of the parts' vertices through their `tr`, starting from
the origin. The object's half size is half that box from LOD list index 4 (index 0 with state flag 0x10), and
the radius is the box's half diagonal with the depth added a second time. The traffic cars (`eas*`, `eden*`,
`postship`) have no LOD 5, so theirs is 0.

## The draw

`R3DObject_Draw` (0x4433c0) pushes the object matrix: scale (-1, 1, -1), the car's rotation, scale
(1, -1, 1), then the translation to the position minus the centre (`R3DCar_Centre` 0x43a340: the car's up
times its half height). `R3DObject_DrawLODs` (0x444d30) picks LODs and view masks:

| Case | Draws |
|---|---|
| front end (0x65b928) | LOD `cur` with mask 0x50, then LOD 0 with 0x20 |
| race, exterior | LOD `table 0x5cdb28[cur]` with 0x80 (if cur < 4), LOD `cur` with 0x50, then 0x20 from `0x5cdb44[quality * 7 + cur]` (or `0x5cdb98[cur]` with flag 0x10) |
| interior (+0x471) | LOD +0x2c8 with 0x30 |
| player's cockpit camera | LOD 1 with 0x10, LOD 0 with 0x40 (+ 0x80 from 0x5cdb1c): pending with the cameras |

`R3DCarLOD_Draw` (0x43c760) sets THRASH state 0x24 to 10 and runs two passes (opaque, then the parts with
translucent primitives). For each part that passes the view checks it pushes the part's `tr` (`UData_Transform`
0x59efd0) pre-multiplied onto the model matrix, then the wheel slot's offset, then the hinge rotation. It walks
the `pr` items of the entry's range: a primitive in this pass (bit 16 of its type word) draws through
material `pr[1] & 0xffff` of the car's material set.

## Materials

`R3DMaterialSet_Build` (0x44a6b0) reads the root's `RMth` render methods (named layer chains: `CarExtEnv`,
`CarWindow`, `CarWheel`, ...), the `sf` / `sn` shapes and the `mt` materials. An `mt` record has its
texture-reference count in the low half and its stream count in the high half of +8, its method key at
+0xc / +0x10, then 0x2c-byte texture references from +0x20. A material (0x1c bytes) keeps its first texture's
id (`XETN` = none), its method and per-draw stream overrides. A draw binds the `pr`'s streams into the method's
slots by stream id (`R3DMaterial_Bind` 0x44a190): kinds below 0x100 are the part's items by kind and index
plus the stream's offset, others a named item, and the index lists (`Iv`, `Iu` + n) point into the `pr`'s
byte area. It then calls the method's EAGL material (slot 1) with the primitive type (the `pr`'s type word)
and the element count.

The port builds the `mt` records. Render methods, texture references and texture pages are pending, so
`R3DMaterial_Draw` resolves the streams the way 0x44a190 does and draws them with a **stand-in** EAGL
material: coordinates, then grey from the normals against a fixed light, translucent primitives tinted blue,
untextured. The stand-in is the port's, not the game's look.

## Not ported yet

- Render methods (`RMth`, `RMethod_Find` 0x44ba40), texture references (`RTex_Clone`), texture pages
  (`R3DCar_LoadDetails` 0x443e90, the `[tpageN.details]` / `[fileN.details]` sections), option decals
  (0x447b80), environment maps (0x440900), the shared textures (`SPC1..3`, `FEDU`, `FE`, `FESH`).
- The game's layer classes (`RCustomLayers::*`, `Headlights::*`, `R3DSpecularLayer`, ...) for those methods.
- The car's animation (`R3DCar_Animate` 0x438370 fills the hinge angles from the simulation; the timed
  extents of `LoadStyles`), the post-LOD hook (0x43b040), headlights, particles, damage deformation.
- LOD selection by screen size (`R3DCar_v13` 0x439ec0) and the cockpit camera's draw.
- `EAGL_MatAxisAngle` (0x539940) lives in `r3dcar.c` for now. Its angle is a fraction of a turn
  (degrees / 360, whole turns dropped), and at 0 it is the identity (table 0x5bb2d0).
