# Status

**Early: infrastructure and format decoders; not playable yet.**

| Part | State |
|---|---|
| Build: `opennfspu.wasm` on gasm 0.16.0, native build for tools and tests | in place |
| Bundles for macOS, Linux and Windows, the browser player | in place (they start the module; there is no game yet) |
| File layer: the installed game folder, case-insensitive names | in place |
| The original program read at run time (`Porsche.exe` 3.5, size and CRC-32 checked) | in place |
| Reverse engineering: function inventory of `Porsche.exe` | in progress |
| Data formats: archives, textures, car and track models, sound banks, movies | in progress |
| Renderer (the original's Direct3D 7 path on OpenGL ES 3.0) | not started |
| Frontend (menus, showroom, Evolution, Factory Driver) | not started |
| Simulation (handling, damage, AI, police) | not started |
| Sound, music, speech | not started |
| Multiplayer | out of scope for now |

Nothing on this page is a promise of dates. Progress is tracked in the
[repository](https://github.com/emdzej/opennfspu).
