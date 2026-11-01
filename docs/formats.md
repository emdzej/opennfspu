# Data formats

What the game's files contain, as far as we know. `tools/nfsfmt.py` (Python, standard library only) is the
reference decoder for every format marked **decoded**; the C port must agree with it. The authority is
`Porsche.exe` (v3.5, 2,129,965 bytes, CRC-32 `fd2f5573`). Community notes on EA's formats gave the first
guesses; everything below is checked against the data, and where an address is given, against the code.

```sh
python3 tools/nfsfmt.py scan   game                                  # magic / compression inventory
python3 tools/nfsfmt.py viv    game/GameData/Sounds/sd1a.viv [dir]   # list (and extract) an archive
python3 tools/nfsfmt.py fsh    game/FEData/Art/E1.fsh out/fsh/E1     # every image as PNG
python3 tools/nfsfmt.py sheet  game/FEData/Art/E1.fsh out/e1.png     # images composed at their x, y
python3 tools/nfsfmt.py crp    game/GameData/CarModel/996.crp out/996.obj
python3 tools/nfsfmt.py render game/GameData/Track/alps.crp out/alps.png top
python3 tools/nfsfmt.py render game/GameData/CarModel/996.crp out/996.png side '(Body|Roof|Bottom|.*Out|Light[123])'
```

Byte order is little-endian unless stated. "4CC" ids are compared by the code as little-endian u32s
(`'Arti'` is the immediate `0x41727469`), so in the file they appear byte-reversed (`itrA`).

## Status

| Format | Files | Status |
|---|---|---|
| RefPack compression | `*.crp`, some `*.fsh`, FSH pixel blocks | decoded |
| VIV archives (BIGF, C0FB) | `*.viv` | decoded |
| FSH / SHPI images | `*.fsh`, `*.env`, `*.psh` (SHPP) | decoded (all pixel formats in the data); SHPP partial |
| CRP models (cars, tracks) | `*.crp` | geometry decoded (vertices, normals, UVs, triangles, LODs, damage shapes); materials, `Base`, part placement partial |
| MAD video | `FEData/Movies/*.mad` | identified: EA Madcow video + EA-XA R1 audio in ASF chunks |
| ASF audio | `GameData/Music/zzzymus.viv` entries, movie audio | identified: EA SCHl/SCCl/SCDl/SCEl, EA-XA R1 ADPCM |
| BNK sound banks | `*.bnk` (in `Sounds/*.viv`, `Speech/zzzxeng.viv`) | header decoded; samples EA-XA (4-bit) |
| INI-style text | `*.lay`, `*.tpg`, `*.clr`, `*.map`, `*.ini`, `*.scr` | readable text (`[section]` / `key=value`) |
| Everything else | see the inventory | unknown / to do |

## RefPack (EA "0x10FB") compression

Loader dispatch: `0x571150` reads byte 1 (`0xfb`, or `0x32`) and `byte 0 & 0xfe` and jumps to one of EA's
codecs: `0x10` RefPack (`0x594510`), `0x18` (`0x5941b0`), `0x30/0x32/0x34` (`0x5935b0`), `0x46`
(`0x5933f0`). The shipped data uses only `0x10`.

Header: flags byte (`0x10`, `| 0x01` = a compressed-size field precedes the size, `| 0x80` = sizes are 4
bytes instead of 3), `0xfb`, then the uncompressed size, big-endian. The stream is LZ77 with four command
forms, chosen by the first byte:

| First byte | Bytes | Literals | Match length | Match offset |
|---|---|---|---|---|
| `0x00-0x7f` | 2 | `b0 & 3` | `((b0 >> 2) & 7) + 3` | `((b0 & 0x60) << 3) + b1 + 1` |
| `0x80-0xbf` | 3 | `b1 >> 6` | `(b0 & 0x3f) + 4` | `((b1 & 0x3f) << 8) + b2 + 1` |
| `0xc0-0xdf` | 4 | `b0 & 3` | `((b0 & 0x0c) << 6) + b3 + 5` | `((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1` |
| `0xe0-0xfb` | 1 | `((b0 & 0x1f) << 2) + 4` | none | |
| `0xfc-0xff` | 1 | `b0 & 3`, then end | | |

Literals are copied first, then the match (which may overlap its own output). All 66 compressed CRPs and 15
compressed FSHs decode to exactly the stated size. `928.crp` is stored uncompressed.

## VIV archives

Type detection: `0x5659a0` (returns 1 for C0FB, 2 for BIGF; `0x56e040` reads big-endian integers).

**BIGF** (45 files): everything big-endian. `'BIGF'`, u32 file size, u32 entry count, u32 header size, then
per entry u32 offset, u32 size, NUL-terminated name. Offsets are absolute.

**C0FB** (10 files, `Track/Data/tr*a.viv`): the compact form. u16 `0xc0fb`, u16 header size, u16 count,
then per entry 24-bit offset, 24-bit size (big-endian), NUL-terminated name.

Contents: music (`Music/zzzymus.viv`, 28 ASF streams named `gameNN-Title-Artist`), sound effect banks
(`Sounds/sd*.viv`: `.bnk` plus `CRDl` curve tables `.cct/.clt/.ect/.elt`), track ambience
(`Sounds/track.viv`, 593 `.bnk`), speech (`Speech/zzzxeng.viv`, 161 `.bnk`), track cameras
(`Track/Data/camera.viv`: `.rho`; `tr*a.viv`: `.can`).

## FSH (SHPI) images

Loader: the `'SHPI'` check is in `0x5aeb10`.

File: `'SHPI'`, u32 total size, u32 entry count, 4-char directory id (`GIMX` everywhere), then per entry a
4-char name and a u32 offset. Names double as ids: `.map` files refer to track textures by the name read
as a u32 (`texture1=542722899` is `'SKY '`). A whole file may be RefPack-compressed.

Each entry is a chain of records. A record starts with 16 bytes: u32 code, u16 width, u16 height, i16
centre x, i16 centre y, u16 x, u16 y. The code's low byte is the record type; its upper 24 bits are the
distance to the next record of the entry (0 = last; the last record of the file runs to its end). x, y is
the position on the artist's source sheet (front-end screens are cut into 128x128 tiles placed this way;
`nfsfmt.py sheet` reassembles them). The centre is the hotspot for sprites; for palettes centre x is the
colour count. Pixel rows are not padded; a record's data is padded to 16 bytes. No mipmaps were found.

| Type | Meaning | Count in data |
|---|---|---|
| `0x7b` | 8-bit indexed (palette in a following record) | 6058 |
| `0x7d` | 32-bit, bytes B G R A | 2044 |
| `0x7e` | 16-bit A1 R5 G5 B5 | 1437 |
| `0x6d` | 16-bit A4 R4 G4 B4 | 671 |
| `0x78` | 16-bit R5 G6 B5 | 90 |
| `0x7f` | 24-bit, bytes B G R | 1 |
| `0x80` flag | the pixel block is RefPack-compressed: `0xf8` (565), `0xfb` (8-bit), `0xed` (4444) | 222 |
| `0x2a` | palette, 32-bit BGRA | attachment |
| `0x29` | palette, 16-bit 565 (checked visually: the chronicle art) | attachment |
| `0x2d` | palette, 16-bit 1555 | attachment |
| `0x22`, `0x24` | palette, 6-bit RGB / 24-bit BGR (handled, not seen) | |
| `0x6f` | text (e.g. `"cloud layer"`), width = length | attachment |
| `0x7c` | hotspot / region list (count in width) | attachment |

A few `0xed` records decompress to more than w*h*2 bytes (8 of 12); not yet understood.

`.env` files are plain SHPI (environment maps). `.psh` (`'SHPP'`, 3 files) has the same directory layout
(`GIMX`, 4-char names); record layout still to check.

## CRP models

Cars (`GameData/CarModel/*.crp`) and tracks (`GameData/Track/*.crp`). A CRP is (usually RefPack-compressed)
data in EA's generic **chunk** layout. Helpers in the exe (these names are ours):

| Address | What it does |
|---|---|
| `0x59f620` | first child entry of a directory (`off * 16 + entry` if relative) |
| `0x59f280` | count child directories with an id |
| `0x59f2e0` | find a child directory by id (and index) |
| `0x59f440` | count items with an id |
| `0x59f4b0` | find an item by id (and index) |
| `0x59f570` | find an item by exact id (binary search when sorted) |
| `0x59f390` | binary search over 16-byte entries by u32 id |
| `0x59f020` | the `'Name'` item of an article |
| `0x59f050` | the `'dz'` item with a given index |
| `0x59f0a0`, `0x59f150`, `0x59f1f0` | copy / create / resize an item (runtime-owned data, bit 2) |
| `0x43c0e0` | walks a car's articles (`'Arti'`) and their `'Base'` records |

Every entry is 16 bytes: u32 id, u32 info, u32 count, u32 offset.

- info bit 0: the id is a 2-character tag in its high half and a 16-bit index in its low half; lookups
  compare `id & 0xffff2020 | 0x2020` (so `'pr' + 0x1000` matches `'pr  '`).
- info bit 1: the offset is relative to the entry itself: in 16-byte units for directories, in bytes for
  items.
- info bit 3: the entries are sorted by id (lookups binary-search).
- Directory: `info >> 5` child directories, followed by `count` items.
- Item: `info >> 8` is the data size in bytes; `count` the element count.

The file header is the root directory entry: id `'Car '` or `'Trak'`, children = **articles** (`'Arti'`,
one per object), then the shared items. The children start right after the header.

**Shared items** (cars): `'mt'` materials (52 x 6 bytes in 356A, still to decode), `'RMth'` render methods
(named: `CarInt`, `CarIntShd`, `CarWheel`, `CarExtEnv`, `CarWindow`, ...), `'sf'` (large: surfaces, likely
the texture pages), `'PdnB'` / `'odnW'` bounding data.

**Article items**:

| Tag | Element | Meaning |
|---|---|---|
| `Name` | text | article name (`Body`, `Roof`, `DoorOut`, `WheelFront`, `Interior`, `LeftArm2`, ...) |
| `Base` | 124+ bytes | per-article record: a LOD/part count at +12, ids and flags at +68.., a table of 12-byte part descriptors at the end (`1fff1000 ffff0001`). No transform found: car vertices are already in car space |
| `vt` | 4 x f32 (x, y, z, 1) | vertex positions. Index: low nibble = part / LOD number, `0x8000` set = the damaged shape (same count) |
| `nm` | 4 x f32 (x, y, z, 0) | vertex normals (cars) |
| `uv` | 2 x f32 | texture coordinates |
| `df` | 4 bytes | vertex colours (tracks) |
| `$n` | per triangle | per-face data |
| `dz` | | damage zones |
| `tr` | | (cars) to decode |
| `ef` | | (tracks) effects, to decode |
| `An` | | (tracks) animation, to decode |
| `pr` | primitive list | below |

**Primitive list (`pr`)**. Header (48 bytes): u32 type (low byte 3 = triangle list; other bits are flags),
u32 `0x80000000 | material`, f32 bounding sphere (x, y, z, radius), 16 zero bytes, u32 stream count, u32
index-list count. Then the streams, 16 bytes each: u32 stream id, u32 0, u16 size of the referenced part,
u16 kind (0 = `vt`, 1 = `nm`, 2 = `uv`, 3 = `df`, `'$n'` = per triangle), u16 index of the referenced
part, u16 index list number or `0xffff` (element k uses entry k). Then the index lists: u32 id (`'Iv'` or
`'Iu'` + index), u32 offset into the u8 index area that follows. The pr's item count is the number of
elements; every three make a triangle. All 3,301 lists of `356a.crp` and `monaco5.crp` reference only
existing vertices and UVs.

The pr index's high nibble selects the part (car: `pr[N000]` uses `vt[000N]`; track: `pr[0000]`,
`pr[1000]`, `pr[2000]` are three detail levels of the same article, `vt[0]` with the most vertices); the low
nibble a variant (`pr[1001]`, e.g. another material or lights on).

**Coordinates**: metres. Cars: x = width, y = up, z = length (356A body spans 3.7 m). Wheels are modelled
at the origin and placed by the simulation; doors, hood and trunk are in hinge-local frames (not placed by
the reference renderer yet). Cars carry alternatives in one file (`Body`, `Body1`, `Body2`, cabrio tops up
and down, interior, the driver), so a preview needs a name filter. Track articles are in world space (the
Alps renders as the full point-to-point road from above).

To do: materials (`mt`, `RMth`, how `pr` material numbers pick textures from the car's `.fsh` files and the
`.tpg` pages), `sf`, `Base`, `$n`, `dz`, `tr`, track `ef`/`An`.

## Audio and video

Identified with FFmpeg's EA demuxer, which reads these files as they are (`ffprobe` output in brackets):

- **MAD** (`FEData/Movies/*.mad`, 12 files): EA chunk stream; video chunks `MADk` (key frame) and `MADm`
  (other frames), audio `SCHl` (header), `SCCl` (count), `SCDl` (data), `SCEl` (end). Video is EA
  "Madcow" (`eamad`: an MPEG-1-like intra/inter DCT codec), 15.15 fps; 192x144 or 208x144 (chronicle
  clips), 320x240 (`EArts-av.mad`), 320x176 (`inPC-av.mad`). Audio: EA-XA R1 ADPCM, 22050 Hz stereo. The
  exe checks `'MADk'` in `0x4dc300` and `0x4e1xxx`. The exe's decoder (0x56c800..0x56d020, its assembly at
  0x592c10..0x5933f0) also takes `MADe`: a predicted frame that isn't kept as the next one's reference. A
  video chunk: +0xc the frame time (16.16 ms), +0x10/+0x12 width and height, +0x15 the quality, the bit
  stream of 16-bit words from +0x18; 16 x 16 macroblocks of 7-bit YUV 4:2:2 (port: `src/real/mad.c`).
- **ASF** (music, movie audio): `SCHl` starts with a `'PT\0\0'` patch header of tagged values (tag, length,
  big-endian value; `0xfd` opens the sub-header, `0xff` ends it): `0x80` version, `0x82` channels, `0x84`
  sample rate, `0x85` sample count; no `0x83` codec tag, so the default applies (EA-XA R1). Music: 32000 Hz
  stereo, e.g. 2:40 for `game13`. The exe checks `'SCHl'` in `0x5665f0`.
- **BNK** (`'BNKl'`): u16 version (4), u16 slot count, u32 header size (= start of sample data), u32 data
  size, u32 `0xffffffff`, then one u32 per slot: offset of the slot's `PT` header relative to the slot
  itself, 0 = empty. PT tags: `0x85` samples, `0x84` rate, `0x86`/`0x87` loop start/end, `0x88` absolute
  data offset, `0x8a`. Sample data is EA-XA 4-bit ADPCM (5056 bytes for 9424 mono samples = 15 bytes per 28
  samples). The `*16.viv` banks (e.g. `sd1a16.viv`) are the higher-quality set; their codec is to check.
  The exe checks `'BNKl'` in `0x4af6d0`.
- **CRDl** (`.cct/.clt/.ect/.elt` next to the banks): curve tables (engine sound by rpm/load?), to decode.

## Inventory of the rest

| Ext | Count | Magic / kind | What it seems to be |
|---|---|---|---|
| `lay` | 98 | text `[section.N]` | front-end screen layouts: controls, positions, callbacks, accessors (`Game.Damage`) |
| `ctr` | 78 | text (`;;;` comments) | front-end control definitions |
| `scr` | 1 | text | screen definition |
| `loc` | 101 | `'LOCH'` + `'LOCI'` | localised string tables (per language) |
| `tpg` | 52 | text | car texture pages: files (`exterior.fsh`, `interior.fsh`, `shadow.fsh`, `driver.fsh`), page sizes and bpp, damage zone extents |
| `clr` | 28 | text | car colour sets (HSV per colour, stripe package) |
| `map` | 15 | text | track texture / lighting map per slice range: tint, ambient, texture ids (FSH names as u32) |
| `sim` | 86 | binary, 328 bytes, starts with the name (`1953 356 cabrio S 1.5L`) | car simulation parameters |
| `ais` | 22 | binary, 304 bytes, name first | AI car parameters |
| `spl` | 86 | binary, 52 bytes | engine sound parameters per car |
| `scn` | 78 | text: `TRIG`, `AUDI`, `CAME`, `GEOM` ... elements | track scene scripts: triggers, audio, cameras, start grids |
| `key` | 28 | binary: u32 count, (u32, f32) pairs | keyframes along the track (forward / backward) |
| `lsp` | 30 | binary: u32, f32 x, z pairs | racing lines (`Track/racer/work`) |
| `edg` | 15 | binary, large | track edges / road boundaries |
| `jnc` | 15 | binary, f32 | junctions |
| `dtx` | 15 | 11 bytes | per-track detail flags |
| `env` | 15 | `'SHPI'` | environment map textures |
| `rho`, `can` | 20+ | binary (in `camera.viv`, `tr*a.viv`) | replay/TV camera data |
| `ffn`, `pfn` | 21 | `'FNTF'`, `'FNTP'` | fonts |
| `psh` | 3 | `'SHPP'` | images (license plates, night, HUD) |
| `cop` | 10 | binary | police settings per track |
| `bin` | 25 | binary | sky geometry (`Track/Sky`) and others |
| `csv` | 9 | text | AI personalities, scripts, spreadsheets |
| `dat` | 5 | binary | particles, flares, car part tables (`Render/`) |
| `car`, `prt`, `trn`, `per`, `fac` | 1 each | binary (`FEData/Data/nfs5.*`) | front-end databases: cars, parts, tracks, persons, factory |
| `sav`, `trk`, `rpl`, `lst`, `hud` | | binary (`SaveData/`) | saves, records, replay, players, HUD config |
| `eng` | 1 | binary | dash text strings |

Developer leftovers to ignore: `vssver.scc` (SourceSafe), `FEData/Backup/V3.2.zip`, `*.xls`, `*.h`.
