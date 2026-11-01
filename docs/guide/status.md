# Status

**Early: the game starts in the front end; Quick Race lets you pick the track, the car and the opponents and starts the race; Escape pauses it, and its menu goes back to the menus.**

| Part | State |
|---|---|
| Build: `opennfspu.wasm` on gasm 0.16.0, native build for tools and tests | in place |
| Bundles for macOS, Linux and Windows, the browser player | in place (they start the module; there is no game yet) |
| File layer: the installed game folder or an ISO image of it, case-insensitive names | in place |
| The original program read at run time (`Porsche.exe` 3.5, size and CRC-32 checked) | in place |
| The 3D driver (THRASH, the original's Direct3D 7 path) on OpenGL ES 3.0, EAGL2 | in place |
| Textures, car models with their texture pages and colours | in place (no environment maps, decals or licence plates yet) |
| Tracks: loading, scenes and start grids, the world draw, the sky's horizon | in progress (all objects tested instead of the visibility grid; the sky's dome with its clouds, the weather's blend along the road, fog and lightning; the animated objects (the autobahn's train, signs, saws, fish) on their keyframed paths; the scene's placed objects (the finish lines, the objects the triggers raise); the sun and its lens flare (with the LENSFLARE option), the lamps' glows; no knockable objects yet) |
| Track road and collision: road nodes, the collision tree of polygons, road edges and roadside objects (cylinders, boxes), the ground under a point | in progress (no collisions with animated objects yet) |
| Cameras | in progress (the race cameras: in the car, on the hood, the near and far chase cameras, the fixed and overhead ones, the crash camera, kept out of walls and under ceilings; no TV or replay cameras, no dashboard yet) |
| Race HUD: fonts, the HUD's 2D layer and art, its layouts (`SaveData\XXhud1XX.hud`, `XXhud2XX.hud`), localised strings | in progress (the start countdown, the gear, the speedometer and tachometer dials and their digital forms, the race time and lap times, the wrong-way warning, the standings and the place, the damage drawing, the track map with the laps, the race's results when it ends, then its replay (from the recorded controls); no text damage or congratulations yet, no replay cameras, no track records) |
| Frontend (menus, showroom, Evolution, Factory Driver) | in progress (FERT: the control templates, layouts and their expressions through the original's interpreter; the front end's 2D drawing, fonts, strings and images; the screens stack, the mouse, the GoTo buttons (their gradients, bevels and stems) and the bottom bar; the garage room behind the menus with the chosen car on its lift and the screens' ovals over it; the options screen's tabs and panels with the audio, display and graphics settings (kept in fe.txt: the race takes the sound levels and the HUD's split times, opponent list and speed units, and the render options); Quick Race's track, car and opponents through the accessors (their combo boxes step through the values: the drop-down lists aren't ported), and in the race Escape opens the pause menu (continue, race again, quit: view replay, exit to windows, exit to game menu, cancel); the menus' music; the player's save (the default player's), the account, the dealership (buying a car, its colour), the owned cars in the garage and in Quick Race (their value, the repairs, the car setup's settings, which the race takes), selling a car on the used car market and the buyers' offers, and the career's tournaments: their races run with the player's car pay the place's prize into the save; no player selection, buying used cars, the parts and paint shops, controller setup, the factory and victory rooms, movies or saves yet) |
| Simulation (handling, damage, AI, police) | in progress (one car drives from its grid slot behind the start line: ground contact, suspension, engine, gearbox, tyres, the road's walls, roadside posts, trees and boxes, the body against the ground when the car is over; body damage in nine zones from hard hits; opponents race the racing line (`opponents=<n>`), the cars collide with each other; no visible dents, police or traffic yet) |
| Sound, music, speech | in progress (EA's SND library: banks, voices, the software mixer with its EA-XA and PCM decoders and resampler; the player's engine from its layered samples by rpm, throttle and view; the collisions, landings and dents, the tyres' skids and the body's scrapes, the wind, the gearbox's whine and the gear changes, muffled from inside the car; the countdown's voice; the nearest opponents' engines, road noise, impacts and skids, panned and Doppler-shifted; the track's positional sounds (its scene's sound elements); the one-shots the track's triggers set off; the animated objects' (the train's), the music (the race's songs streamed and shuffled); the announcer's lap calls (lap, place, gaps, best and final lap); the cop cars' sirens, the reverb under roofs; no end-of-race speech or the other speech yet) |
| Multiplayer | out of scope for now |

Without parameters, `gasm-run opennfspu.wasm --asset-dir <game folder>` (or `--asset cd=<image>`) starts in the front end, as the game does. The viewers:

- `--param screen=<layout>` shows a front-end screen (`GameSetup.lay`, `QuickRace.lay`, ...), `fe=<screen>` starts the front end there;
- `--param car=<name>` shows a car (`964`, `959`, `356a`, `boxster`, ...), `colour=<rrggbb>` its body colour;
- `--param track=<name>` shows a track from its start line (`alps`, `autobahn`, `canyon`, `monaco1`, ...),
  `yaw=<degrees>` turns the camera, `at=<x>,<y>,<z>` puts it elsewhere;
- `--param drive=<track>` drives a car from the race's grid there (`car=<name>`, default `964`): arrow keys or WASD, space the
  handbrake, R puts the car back on the road; on a pad left/right, A throttle, B brake, X handbrake, Y back on the road. `cam=<mode>` picks the camera (0 in the car, 1 hood, 2-4 near chase, 5-7 far chase, 8, 9, 13), C changes it and B looks back, `opponents=<n>` adds AI opponents, `follow=<n>` puts the camera behind car n, `laps=<n>` sets the laps, `aidrive=1` lets the AI drive your car, `finish=<tick>` finishes you there (a test aid).

Nothing on this page is a promise of dates. Progress is tracked in the
[repository](https://github.com/emdzej/opennfspu).
