# Drawing the track

How the race frame turns the loaded world ([track.md](track.md)) into draws. Port: `src/render/rworld.c`,
`rdraw.c`, `rlists.c`, `src/game/world.c` (`World_VisitObject`), `src/game/track_draw.c`.

## The world draw (0x468a20)

Called by `Render_MainViews` (0x468ae0) between `View_Begin` and `View_End` of the main camera. In order:
0x452590 and 0x451f70 (not ported), the sky (0x45d690) when the camera's +0x12c is set, the world pass
(0x47fb50) when the camera's flag +0x11a is set, two render states (0x5375e0: state 0x39 = 1; 0x5375a0:
state 0x28 = 3), the render lists flushed (0x4671d0 -> 0x466520), particles (0x455130), the cars (0x43f610),
the sky's second pass (0x45d6b0) when +0x12c is 2, and 0x458ef0.

`Render_MainViews` gives the main camera its flags (+0x118 = 1, +0x119 = 4, +0x11a..0x11c = 1), +0x12c = 2,
the range 400 (+0x120) and the view distance from the options (0x6197cc, `viewdistance` in `fe.txt`: 400)
at +0x124, squared at +0x128.

## The world pass (0x47fb50)

The world's per-frame update (0x488c30), the visibility state from the camera's frame and view distance
(0x47f560: the eye, the camera's orientation, the view's ground area and the grid cells around it), then
0x47f8a0: for every cell in view, every object of the cell once. Per object:

1. skipped if its draw object already has it marked this frame (the draw object's byte per entry, +0x08);
2. its bounding sphere (the draw object's slot 5, the world bundle's slot 3) must pass the view's sphere
   test (`EAGL_ViewSphereVisible`) and not be behind a visibility wall (`WVis_blocked`, 0x488ac0) from the eye;
3. the distance: objects whose entry flags have 0x400020 compare the squared distance of the centre with
   700 m squared (0x5b47ac), or 600 m squared (0x5b47a8) with 0x400000; the others estimate the distance
   with a square root made by halving the float's bits and adding 0x1fc00000, subtract the radius (at
   least 0, 0x5b23f8) and compare its square with the view distance squared;
4. handed to the draw object's slot 1 with that squared distance.

The port tests every object of the world in place of the cells (the cell walk 0x47ba50, 0x47f410 isn't
ported): the same objects pass, in the world's order instead of the cells'.

## Draw objects (RDrawArticleBundle, vtable 0x5b3cf0)

Made per article bundle (0x450b10 on the base 0x44fca0: the bundle, one visited byte per entry (100 when
the count is 10000 or more), linked into the list at 0x61bc4c). Slots: 0 destructor 0x450b30, 1 queue
0x450cb0, 2 draw 0x450b70, 3 effects 0x450be0, 4 0x44fe50 (nothing), 5 sphere 0x451170, 6 flags 0x4511b0.

**Queue (slot 1).** From the article's `Base`: the LOD count at +0x0c and 12-byte LOD entries from +0x64
(+0 has 'pr' lists, +1 has effects, +4/+6 the 'pr' index range, +0x0a the LOD's distance). The first LOD
whose distance squared is above the object's squared distance (truncated to an integer, as `__ftol`); LOD 0
is skipped when 0x6197e5 is set and the article has more than one and no 0x10000 flag (every quality level
clears 0x6197e5 in this build). Each 'pr' list of the range goes to a render list by its flags word, keyed
on the squared distance from the camera's point at +0x34 to the list's centre (+0x08), rounded:

| 'pr' flags | List | Key |
|---|---|---|
| 0x20000 | +0x04 (ascending) | the list's priority (+0x04, high half) plus the distance |
| 0x10000 | +0x0c (descending) | distance |
| 0x100000 | +0x08 (descending) | distance |
| 0x40000, with 0x6197e6 | +0x0c | distance |

Effects ('ef' items) go to +0x10, keyed on the object's squared distance. The camera's +0x34 is kept by the
camera behaviours, which aren't ported; the port measures from the eye until they are.

**Draw (slot 2).** The list's material is entry `pr[1] & 0xffff` of the material array of the bundle's
world context (+0x2c: the track's material set, built by `Track_LoadWorld` with 0x44a520/0x44a6b0); the
key is stored at 0x61975c for the layers; the material binds the list and its method draws it; the
material's +0x0c is cleared.

## The render lists (0x466240)

Eight MSVC `std::multimap`s of 16-byte entries (the queued item's record, the draw object, a byte, the
object's entry, the key), keyed on the last word. The flush (0x466520) draws +0x04 in ascending key order
with alpha reference (state 0x24) 10, +0x08 in descending order (farthest first) with 0xa0, +0x0c in
descending order with 10, then hands +0x10 to slot 3, and empties those and +0x18, +0x1c. The port keeps
each list as an array kept sorted with the multimap's rule (a new entry after equal keys), which gives the
same in-order walk.

## The viewer

`--param track=<name>` (`TRACK_selftest`, a port addition) loads the track through `Track_Load`, makes the
race views (`RaceViews_ctor` 0x468da0) and runs `Render_Frame` (0x468900) each frame with the main camera
placed behind the start line (the scene's type-2 trigger; its first matrix row is the race's direction,
the second up: the world's up is +y) or at the first scene camera. `yaw=<degrees>` turns it.
