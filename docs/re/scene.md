# Scene elements (.scn)

The scene module (0x469000-0x471000, `src/game/scene.c`) reads the text files next to each track in
`GameData/Track/`:

| File | Read by | Holds |
|---|---|---|
| `<track>_cameras.scn` | `Scene_LoadStarts` 0x46cc00 | replay/TV cameras and the camera-switch triggers (type 6) |
| `<track>_audio.scn` | `Scene_LoadStarts` | positional sounds |
| other `<track>*.scn` | `Scene_LoadStarts` | anything else whose name starts with the track's |
| `<track>_FStart.scn` / `_BStart.scn` | `Scene_LoadStartScenes` 0x46d550 | the start (type 2) and end (type 1) lines and their banners, forward / reverse |
| `<track>_ST<n>.scn` | `Scene_LoadSkillTest` 0x46d280 | a skill test's lines ("Start", "End") and cones |

`Track_LoadWorld` (0x487650) loads them after the world: `Scene_LoadStarts`, then the skill test (game
type 4, test number 0x657c80) or the start file of the race's direction (reverse flag 0x657430).
`Scene_LoadStarts` lists `<dir>*.scn`, upper-cases each name and the track's, and parses every file that
starts with the track's name unless it goes on with `_FSTART`, `_BSTART` or `_ST`. The Monaco tracks
and the skid pad have no start files; the original builds its "Failed to load Start/End File" message
and doesn't show it.

## File format

Text, opened in text mode (CR LF reads as LF). `Scene_ParseElements` (0x46c170) reads a word with
`fscanf("%s")` until the stream's end flag is set; a known keyword creates the element and calls its
load (vtable slot 1), any other word is skipped. Each load reads its version with `%d` and then that
version's fields with `fscanf` straight into the object, and most versions end with two `fgets` of the
name field (the rest of the numbers' line, then the name line) whose last character (the newline) is
cut. The port keeps every element as its object's byte image and reads with the exe's own format
strings (`src/game/scene_crt.c`), so the runtime's behaviour carries over, notably: `%d` stores four
bytes, so an int read into a 16-bit field spills its upper half into the next field until that one is
read; whitespace in a format (a newline too) skips any amount of input whitespace.

| Keyword | Class (vtable) | Size | Versions read | Versions in the data |
|---|---|---|---|---|
| `AUDIO_ELEMENT` | CAudioElem (0x5b4b58) | 0xa4 | 1-4 (1 without a name) | 4 |
| `GEOM_ELEMENT` | CGeomElem (0x5b4280) | 0xc4 | 1-5 (1 without a name) | 4, 5 |
| `TRIGGER_ELEMENT` | CTriggerElem (0x5b4554) | 0xf0 | 1-5 (1 without a name) | 3, 4, 5 |
| `CAMERA_ELEMENT` | CCameraElem (0x5b4228) | 0xc4 | 1-4 | 4 |
| `PARTICLE_ELEMENT` | CParticleElem (0x5b44ac) | 0x7c | 1 | none |

Quirks kept: camera version 2 has no `break` and reads a whole version-3 element after its own; trigger
version 2 jumps into version 4's tail (so it reads a name, version 1 doesn't); geometry version 4 reads an
eighth number into a local that is dropped; the trigger constructor computes the line's end points from a
field it hasn't set yet (every version then reads them, so the port leaves them zero).

## Layouts

Common (CSceneElem, ctor 0x46bb30): `+0x04` active byte, `+0x08` position, `+0x14` orientation (3x3,
rows right / up / forward), `+0x38` name (64). Slots 12-19 are the getters and setters of these
(0x46bdd0 ... 0x46be40).

- **Trigger**: `+0x7c` type (0 ?, 1 end line, 2 start line, 6 camera switch, others events), `+0x80`,
  `+0x84` (1.0), `+0x88` width (3.0), `+0x8c` (0.25), `+0x90`, `+0x94` (200), `+0x9c`, `+0xa0`, the line's
  end points `+0xa4` / `+0xb0`, two direction vectors with flags (`+0xe4` + `+0xbc`, `+0xe8` + `+0xc8`),
  `+0xd4` id, `+0xd6`, three links `+0xd8` `+0xdc` `+0xde` (element indices; `Scene_ParseElements` adds
  the list's size before the file), `+0xe0`, `+0xec`.
- **Camera**: `+0x78` (12), `+0x7c` (1), `+0x80`, `+0x84`, `+0x88` (1), `+0x8c` (1), `+0x90`, `+0x94` (3.0),
  `+0x98`, `+0x9c` field of view (37.5; the data has 45), `+0xa0` (0.6), `+0xb4` target, `+0xc0` type
  (1 forward race, 2 reverse).
- **Geometry**: `+0x78`, `+0x7c` object type (four characters: `FNSH`, `CONE`, ...), `+0x80`, `+0x84`,
  `+0x88`, `+0x8c`, `+0x90`, `+0x94` scale (percent, 3 floats), `+0xa0` the orientation times the scale
  (0x46af60: `Mat3_Mul(orientation, diag(scale) * 0.01)`).
- **Audio**: `+0x78` byte, `+0x7c` vector, `+0x88`..`+0xa0` 16-bit fields and two floats (`+0x8c` radius
  100, `+0x90`), `+0xa2` byte. The constructor also fills a table of circle points from the radius
  (0x4abf60, the editor's display; pending).
- **Particle**: `+0x78` int (-1).

## Lists

After a load the scene is one list (0x628168) and four built from it:

| List | Built by | Getter |
|---|---|---|
| cameras (0x628178) | 0x46ca50 | `Scene_CameraCount` 0x46d8b0, `Scene_Camera` 0x46d8d0, `Scene_FirstCamera` 0x46d7e0 |
| camera-switch triggers, type 6 (0x628174) | 0x46ca50 | 0x46d7d0 |
| event triggers, types other than 0, 1, 2, 6 (0x62816c) | 0x46c5d0 | 0x46d7c0 |
| start / end lines, types 0, 1, 2 (0x628170) | 0x46c820 | `Scene_FindTriggerByType` 0x46d1e0, `Scene_FindTriggerById` 0x46d230 |

`Scene_CountGeom` (0x46d910) counts the geometry elements: the world's scene bundle gets that many
instance slots. `Scene_PlaceGeometry` (0x46d120) puts them into the bundle (type, position, scaled
orientation); the bundle's calls (0x482af0, 0x482f70, 0x482fc0) are pending in the world module.
`Scene_Clear` (0x46cf60) frees everything (the camera-switch list only together with the camera list).

`Scene_StartLine` (port addition) gives the start line's position and orientation, the type-2 trigger,
which the race code will find the same way.
