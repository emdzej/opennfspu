# Track and world

How a track is loaded (0x471000-0x474000, 0x474000-0x48a000) and what the port has of it
(`src/game/track.c`, `src/game/world.c`).

## Files

A track `<name>` lives in the tracks' directory (`GameData\Track\` in the installed game; the exe reads the
directory through the pointer at 0x65b2b8, which is set at startup by code not located yet: the exe contains
no such string). `Track_Load` 0x471ad0 joins the directory and the race's track name (0x657444) and hands the
path to `Track_LoadWorld` 0x487650, which reads, in order:

| File | Read by | Port |
|---|---|---|
| `<name>.crp` | the chunk-data loader (`UDataFile_load` 0x59f6a0) | yes |
| camera animations (root `CmAn` items) | 0x471660, 0x4717b0 | yes |
| the track renderer (root materials, render methods) | 0x44a520, 0x44a6b0 | pending (3D renderer) |
| `<name>_fstart.scn`, `_bstart.scn`, `_st<n>.scn`, other `*.scn` | `Scene_LoadStarts` 0x46cc00, `Scene_LoadStartScenes` 0x46d550, `Scene_LoadSkillTest` 0x46d280 | pending |
| `animdefs.txt` | 0x47ed00, `WMidGround_ParseObjectDefs` 0x47ea60 | pending |
| the articles (root `Arti` articles) | `World_BuildBundles` 0x487940 | yes (the article bundles, object list) |
| `<name>.edg` | `Track_LoadEdges` 0x488000 (from 0x487940) | pending |
| visibility walls (root `Vis ` item) | 0x4889d0 | yes |
| simulation data (root `SimD`, `SimT`) | 0x47fbd0, 0x47ff20 | pending |
| `<name>.jnc` | 0x480f20 | pending |

## The track's chunk tree

The root is a `Trak` record. Its items: `CmAn` (5 camera animations), `Desc` (descriptions), `RMth` (render
methods), `SimD`/`SimT`, `Vis ` (visibility walls), `mt` + index (materials; +0x28 is the texture, the name of
an entry of `<name>.fsh` read as a little-endian u32, or 0) and `sn`. Its articles are all `Arti`, one per piece
of the track, named like `RD1136C (1 PRIMARY)` (road) or `CNK0000L (1 BUSHCLIFF_BACK1_SECONDARY CHP1)` (scenery
chunk). An article's items: `Name`, `Base`, the geometry (`vt`, `nm`, `uv`, `df`, `pr` lists, see
`docs/formats.md`), and for placed objects `tr` (a 4x4 matrix), `Anim`, `Anqt`.

The `Base` item (0x70, 0x7c or 0x88 bytes in the data; the world reads its first 0x48):

| Offset | What |
|---|---|
| +0x00 | flags: 0x8000 scene prototype, 0x6000 mid-ground (0x2000 solid), others not read here |
| +0x14, +0x20 | bounding box min, max |
| +0x2c, +0x3c | bounding sphere centre (+0x38 is w = 1), radius |
| +0x44 | object type (four characters as stored, e.g. `DFLT`), for prototypes and instances |

## Bundles

The world (`BWorld`, 0x3c bytes, global 0x628960) keeps the tree and splits the articles into bundles,
`R3DTypes::W3DObjectBundle` (0x30 bytes, ctor 0x489090): vtable, two flags, the drawing object (+0x8), the
collision object (+0xc), the entry count (+0x18) and entries (+0x1c, 8 bytes each: flags and the article), the
kind (`Arti`), a serial number (counter 0x5d0608) and the world's context (the track renderer, BWorld +0xc).
`WArticleBundle` (0x34) adds a 64-byte lookup cache per entry (+0x30, 0x477ca0/0x477cd0): the article, the end
of its items and seven {record, id} pairs for the ids of the table at 0x5e5084 (`vt nm uv df Base pr ef`), so
repeated lookups of the same item don't search again (0x43c620, 0x43c650).

`World_BuildBundles` gives the array of all articles to each bundle's load (vtable slot 1) in this order; a
load takes the articles it is for by clearing them from the array:

| Bundle | Load | Takes | Notes |
|---|---|---|---|
| `WSceneBundle` (0x4c) | 0x482680 | nothing (copies) | prototypes: `tr` + Base 0x8000; then 64 instance slots (or as many as the scene has geometry elements, 0x46d910, when that isn't 0), each first a `DFLT` placeholder at y = 9999 (0x482bb0) |
| `WInstanceBundle` (0x44) | 0x478130 | `tr` without 0x8000 | each instance's record (0x38: position, 3x3 rotation, type, a byte of its object definition) from its `tr` matrix (0x539ab0); with a running `Anim` item the record stays at the origin; keeps a copy of the prototypes like the scene |
| `WAnimBundle` (0x38) | 0x477780 | an `Anqt` item | an animation per entry (0x4713f0) |
| `WMidGroundBundle` (0x34) | 0x47cba0 | Base & 0x6000 | entry flag 0x400000 without 0x2000 |
| `WStaticBundle` (0x34) | 0x4833a0 | everything left | |
| `WLineBundle` | 0x479890 | (the line set, not articles) | pending |

The prototypes stay in the array (the instance bundle doesn't take them), so the anim bundle takes the animated
ones and the static bundle the rest. Every track has the same 70 prototypes, 29 of them animated.

Then every entry of every bundle becomes an object of the world's object list (8 bytes: bundle, a flag, the
entry index; 0x489760, 0x488d20): the list the renderer and the queries walk. The port keeps it in
`BWorld.objects`.

Per track (articles: scene slots / instances / animated / mid-ground / static, visibility walls):

| Track | Articles | Inst | Anim | Mid | Static | Walls |
|---|---:|---:|---:|---:|---:|---:|
| alps | 1752 | 111 | 32 | 151 | 1458 | 275 |
| autobahn | 1312 | 40 | 35 | 147 | 1090 | 62 |
| canyon | 1580 | 51 | 29 | 13 | 1487 | 185 |
| castle | 2671 | 101 | 32 | 41 | 2497 | 403 |
| coastal | 768 | 21 | 31 | 68 | 648 | 48 |
| farmland | 1139 | 72 | 30 | 25 | 1012 | 80 |
| foothills | 1332 | 48 | 29 | 27 | 1228 | 112 |
| forest | 1172 | 68 | 29 | 21 | 1054 | 175 |
| industrial | 1815 | 47 | 37 | 50 | 1681 | 160 |
| monaco1-5 | 507-669 | 8-22 | 29 | 24-29 | 439-594 | 97 |
| skidpad | 276 | 0 | 29 | 3 | 244 | 0 |

## Visibility walls

`Vis ` holds 0x18-byte records, two points each. 0x4872a0 turns a record into a wall in the ground plane: a
flag (the first point's y isn't 0), the first point's x and z, the delta to the second, the length squared and
its inverse (1.0 at 0x5b24a8 over it). The walls are line-of-sight blockers, not a renderer's visibility list:
0x488ac0 answers whether the line from a point to another crosses an enabled wall (per-wall disable flags at
BWorld +0x30), in one mode any wall, in the other only raised ones; the crossing test is 0x489bf0 (both
parameters in [0, 1], parallel lines never cross). Its caller is 0x47f8a0. Drawing visibility is the
renderer's sphere test against the view (EAGL, `docs/re/eagl.md`), on each object's bounding sphere.

## What a renderer uses

`BWorld.objects[i]` is `{bundle, flag, index}`; `bundle->entries[index].article` is the article, whose items
are the geometry (`UData_find(article, 'vt  ')` and friends) and `WArticleBundle_base` /
`WArticleBundle_sphere` give the bounds. Materials are the root's `mt` items; their texture is an entry of
`<name>.fsh` (loaded with `UTIL_loadshape`, converted by `RTex_ConvertShape` 0x44d6b0). Instances carry their
placement in `WInstanceBundle.placed[index]` (position and rotation), scene slots in `WSceneBundle.placed`.
