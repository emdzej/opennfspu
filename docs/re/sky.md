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
