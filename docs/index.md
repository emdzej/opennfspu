---
layout: home

hero:
  name: OpenNFSPU
  text: Porsche Unleashed, reimplemented
  tagline: "A faithful reimplementation of Need for Speed: Porsche Unleashed (Electronic Arts, 2000) in C11, ported function by function from the Windows game. It runs the game from your own copy, as one WebAssembly module for the gasm runtime, natively and in the browser. Single player is playable; multiplayer is still to come."
  actions:
    - theme: brand
      text: Get started
      link: /guide/
    - theme: alt
      text: Play in the browser
      link: /play/
      target: _self
    - theme: alt
      text: Status
      link: /guide/status

features:
  - title: Faithful
    details: Not an emulator and not a remake. The goal is the original's behaviour, its handling model, AI and quirks included, ported from Porsche.exe with the same arithmetic.
  - title: Your own data
    details: Reads the installed game folder (GameData, FEData, Porsche.exe). Even the original's tables are read from its executable at run time. Nothing from the game is in OpenNFSPU.
    link: /guide/game-data
    linkText: Game data
  - title: Runs on gasm
    details: One opennfspu.wasm for macOS, Linux, Windows and the browser on the gasm WebAssembly game runtime. The original's Direct3D 7 rendering goes to OpenGL ES 3.0 (WebGL 2 in the browser).
    link: /guide/gasm
    linkText: Running on gasm
  - title: In the browser
    details: Choose your game folder once and run it in the browser. Your data stays on your device.
    link: /play/
    linkText: Play
    target: _self
  - title: In progress, and honest about it
    details: Single player runs from the menus to the race and back (Quick Race, the knockouts, the Evolution career, Factory Driver). Multiplayer is not ported. Expect rough edges.
    link: /guide/status
    linkText: Status
---

<div class="vp-doc" style="max-width: 1152px; margin: 48px auto 0; padding: 0 24px;">

## Screenshots

Renders of OpenNFSPU itself (headless runs of `opennfspu.wasm` on the game's own data, made by
`tools/screenshots.sh`).

<div class="shots">
  <figure><img src="/screenshots/menu.jpg" alt="The main menu" loading="lazy"><figcaption>The main menu</figcaption></figure>
  <figure><img src="/screenshots/quickrace.jpg" alt="Quick Race with the car in the garage" loading="lazy"><figcaption>Quick Race</figcaption></figure>
  <figure><img src="/screenshots/alps.jpg" alt="A race in the Alps against three opponents" loading="lazy"><figcaption>The Alps, against three AI opponents</figcaption></figure>
  <figure><img src="/screenshots/castle.jpg" alt="The castle track from the chase camera" loading="lazy"><figcaption>The castle track, from the chase camera</figcaption></figure>
  <figure><img src="/screenshots/canyon.jpg" alt="The canyon track" loading="lazy"><figcaption>The canyon track</figcaption></figure>
  <figure><img src="/screenshots/cockpit.jpg" alt="The driver's seat camera on the Autobahn" loading="lazy"><figcaption>The driver's seat on the Autobahn, in the rain</figcaption></figure>
</div>

## Built for gasm

<p><a class="gasm-badge" href="https://gasm.emdzej.pl"><img class="gasm-badge-light" src="https://gasm.emdzej.pl/badge/built-for-gasm-light.svg" alt="Built for gasm" width="120" height="44"><img class="gasm-badge-dark" src="https://gasm.emdzej.pl/badge/built-for-gasm-dark.svg" alt="Built for gasm" width="120" height="44"></a></p>

OpenNFSPU is one WebAssembly module, `opennfspu.wasm`, running on [gasm](https://gasm.emdzej.pl), a
portable game runtime: the same module natively (`gasm-run`) and in the browser. See
[Running on gasm](/guide/gasm), or [play in the browser](/play/){target="_self"}.

## Status

In progress: single player is playable (Quick Race, the knockouts, the Evolution career, Factory Driver); multiplayer is not ported. Details in
[Status](/guide/status).

## Sibling projects

OpenNFSPU follows the conventions of [OpenRF](https://openrf.emdzej.pl) (Return Fire) and
[OpenGTA](https://opengta.emdzej.pl) (Grand Theft Auto): a portable C11 core, ported from the original
executable, on gasm.

</div>
