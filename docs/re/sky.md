# Sky

The sky module (0x458000-0x460000, 140 functions; `src/render/sky.c`) draws the sky dome and clouds and
holds the track's light environment. Its data is in `GameData/Track/Sky/`, one set per sky (the track's
name, `default` as the fallback):

| File | Read by | Holds |
|---|---|---|
| `<name>.bin` | `Sky_LoadConfig` 0x459530 (object 0x10c bytes, global 0x627e18, made by 0x4594d0) | the configuration, a 0x71dc-byte block `SkyCnfig`, version 9 |
| `<name>.fsh` | `Sky_LoadFsh` 0x45da90 (global 0x627e50) | the sky textures (the layers' maps by name) |
| `<name>.txt` | `Sky_LoadText` 0x45fa40, from the sky object 0x45d6c0 (100 bytes, global 0x627e48) | the sky layers: level type (`basic`, `MULTINORMAL`, `ADDITIVE`), per weather region the maps, scale, movement, wind and warp |
| `*_lightning.bin` | (version 944, 5216 bytes) | lightning, Autobahn and Industrial |

`Render_RaceInit` (0x468ef0) calls `Sky_LoadFsh(track, 0)` and then 0x458a50 (a 0x10-byte object,
0x458b60). A missing `<name>.bin` or `.fsh` falls back to `default` (the utility's loads under the search
path: 0x59d760 returns nothing when the file is missing, 0x59d690 aborts on a read error).

## Configuration

Version 9 in every track's file; `default.bin` and `Monaco.bin` are version 7 (3,544 bytes).
`SkyConfig_Upgrade` (0x4595d0) makes a new 0x71dc block with the old contents and brings it to 9: words
1-0x12 for version 0 (a default fog: 1000, 0.0004, 75), 0x2cf-0x2d2 for < 3, 0x337-0x338 for < 5, 0x339
(0.1) for < 6, four 15-word records from 0x33a (254s) for < 7, 0x376 (1) for < 8, and for < 9 copies of
older tables into new places (100 words 0x13 -> 0x377, 600 words 0x77 -> 0x697, 100 words 0x2d3 ->
0x1957; very old versions get them reset instead).

## Light environment

Four weather slots of 0x3c bytes from 0xce8. The light environment (global 0x627e1c, 0x459820) points at
the race's weather slot (0x657d9c), or the nearest lower one that is set (its first word non-zero), else
slot 0. The fog / light colour of the frame (0x459940, called by the race frame before 0x466da0, which
stores it in the view) blends two sets of seven floats by the time of day (0x45ce80) unless the slot's
override is set; its time and weather objects (0x45dc40, 0x45d170) are pending.

## Pending

The sky object and its layers (`Sky_LoadText` 0x45fa40, the `SkyLevel_BasicSky` / `_MultiSky` /
`_FreeClouds` classes 0x45a980-0x45c9c0, `SkyMapSource_Texture` / `_Noise`, `Scroller_BasicSky` /
`_FreeClouds` 0x45e780-0x45f730), the horizon (`SkyHorizon_*` 0x459b80-0x45a060), the sky's texture
object (0x460910 / 0x460b90), the frame colour 0x459940 and the draw.


## The sky object and the horizon (ported)

`Sky_LoadFsh` (0x45da90) ends by making the texture object (0x460910, 0x238 bytes at 0x627e4c: the file, 40
shape slots looked up by name with 0x460a10, their names, their textures made on first use with 'SKY ' and
class 1 by 0x460b40; a reload, 0x460b90, looks the names up in the new file) and the sky (0x45d6c0, 100
bytes at 0x627e48: the configuration, the light slot, the keyframe tracks, one level per counted block of
Sky\<name>.txt, the horizon last). Port: `render/sky_tex.c`, `sky_levels.c`, `sky_draw.c`, `sky_text.c`.

The world draw calls 0x45d690 (the update 0x45cef0, then the draw 0x45d3b0) when the camera's +0x12c is
set. The draw sets the camera's near and far planes to 0.15 and 700 (0x467060), turns state 0x39 off and
state 0x28 to 7, takes the heading (the forward row's angle to +z in turns, 0x532850, the other way round for
negative x), puts the frame's eye at the origin (the sky moves with the camera), draws the levels flagged
0x400 (they block the sun), the sun (0x460150, pending), the other levels, and puts everything back.

The horizon (SkyHorizon, 0x459a20) is a band of 16 quads 400 from the eye, from the configuration's +0x14
up by +0x18, starting at its angle +0x04: texture 'hor<level>' or 'horz', colours from 'hzcl' (a 16 x 2
image: per segment the top and bottom colours, rotated by +0x08), texture coordinates from the tile table at
+0x1c (+0x0c columns, +0x10 rows, a flip byte each). It is stored twice, so the quarter turn either side of
the heading (0x45a060) is one contiguous range. It draws through the render system's mesh helper (0x468180,
`render/rmesh.c`) and EAGL's quad kernel (0x545830).

Pending: the sky levels (SkyLevel_BasicSky 0x45acb0, _MultiSky 0x45b460, _FreeClouds 0x45be40, their
scrollers and map sources: the clouds), the keyframe tracks and the weather regions' blend along the track
(0x45a180.., 0x4812a0), the sky colour and fog (0x45cbb0, 0x45d1a0, 0x45d270), the sun and lens flare
(0x4600b0, 0x460150), the after-the-world pass (0x45d060).
