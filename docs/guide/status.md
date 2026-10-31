# Status

**Early: a car can be driven on a track (one car, no race yet).**

| Part | State |
|---|---|
| Build: `opennfspu.wasm` on gasm 0.16.0, native build for tools and tests | in place |
| Bundles for macOS, Linux and Windows, the browser player | in place (they start the module; there is no game yet) |
| File layer: the installed game folder or an ISO image of it, case-insensitive names | in place |
| The original program read at run time (`Porsche.exe` 3.5, size and CRC-32 checked) | in place |
| The 3D driver (THRASH, the original's Direct3D 7 path) on OpenGL ES 3.0, EAGL2 | in place |
| Textures, car models with their texture pages and colours | in place (no environment maps, decals or licence plates yet) |
| Tracks: loading, scenes and start grids, the world draw, the sky's horizon | in progress (all objects tested instead of the visibility grid; no clouds yet) |
| Track road and collision: road nodes, the collision tree of polygons and road edges, the ground under a point | in progress (no roadside collision objects yet) |
| Cameras | in progress (fixed viewer cameras; the race cameras aren't ported) |
| Frontend (menus, showroom, Evolution, Factory Driver) | not started |
| Simulation (handling, damage, AI, police) | in progress (one car drives: ground contact, suspension, engine, gearbox, tyres; no collisions with walls or cars, no damage, AI or police yet) |
| Sound, music, speech | not started |
| Multiplayer | out of scope for now |

The viewers, with `gasm-run opennfspu.wasm --asset-dir <game folder>` (or `--asset cd=<image>`):

- `--param car=<name>` shows a car (`964`, `959`, `356a`, `boxster`, ...), `colour=<rrggbb>` its body colour;
- `--param track=<name>` shows a track from its start line (`alps`, `autobahn`, `canyon`, `monaco1`, ...),
  `yaw=<degrees>` turns the camera;
- `--param drive=<track>` drives a car there (`car=<name>`, default `964`): arrow keys or WASD, space the
  handbrake; on a pad left/right, A throttle, B brake, X handbrake. `camdist=<metres>` moves the chase camera.

Nothing on this page is a promise of dates. Progress is tracked in the
[repository](https://github.com/emdzej/opennfspu).
