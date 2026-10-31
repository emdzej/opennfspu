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
| Track road and collision: road nodes, the collision tree of polygons, road edges and roadside objects (cylinders, boxes), the ground under a point | in progress (no animated objects yet) |
| Cameras | in progress (fixed viewer cameras; the race cameras aren't ported) |
| Race HUD: fonts, the HUD's 2D layer and art, its layouts (`SaveData\XXhud1XX.hud`, `XXhud2XX.hud`), localised strings | in progress (the start countdown, the gear, the speedometer and tachometer dials and their digital forms, the race time and lap times; no position, damage or map yet, no track records) |
| Frontend (menus, showroom, Evolution, Factory Driver) | not started |
| Simulation (handling, damage, AI, police) | in progress (one car drives from its grid slot behind the start line: ground contact, suspension, engine, gearbox, tyres, the road's walls, roadside posts, trees and boxes, the body against the ground when the car is over; body damage in nine zones from hard hits; opponents race the racing line (`opponents=<n>`); no collisions between cars, no visible dents, police or traffic yet) |
| Sound, music, speech | not started |
| Multiplayer | out of scope for now |

The viewers, with `gasm-run opennfspu.wasm --asset-dir <game folder>` (or `--asset cd=<image>`):

- `--param car=<name>` shows a car (`964`, `959`, `356a`, `boxster`, ...), `colour=<rrggbb>` its body colour;
- `--param track=<name>` shows a track from its start line (`alps`, `autobahn`, `canyon`, `monaco1`, ...),
  `yaw=<degrees>` turns the camera;
- `--param drive=<track>` drives a car from the race's grid there (`car=<name>`, default `964`): arrow keys or WASD, space the
  handbrake; on a pad left/right, A throttle, B brake, X handbrake. `camdist=<metres>` moves the chase camera, `opponents=<n>` adds AI opponents, `follow=<n>` puts the camera behind car n.

Nothing on this page is a promise of dates. Progress is tracked in the
[repository](https://github.com/emdzej/opennfspu).
