# How Porsche Unleashed works

A theory of operation of Need for Speed: Porsche Unleashed (EA Canada, 2000): how the game is built, from
the timer that drives it to the races, the career and the network game. It comes from porting the Windows
executable, version 3.5, function by function. It is meant for anyone who wants to learn how a racing game
of its time is put together. The details, with the executable's addresses and layouts, are in the
repository's reverse-engineering notes
([`docs/re`](https://github.com/emdzej/opennfspu/tree/main/docs/re)) and its format notes
([`docs/formats.md`](https://github.com/emdzej/opennfspu/blob/main/docs/formats.md)).

## The shape of the program

Porsche Unleashed is one executable of about 2 MB, written in C++ (Microsoft Visual C++ 6, with run-time
type information, about 360 classes with virtual methods). The game sits on three of EA's libraries, and
draws through a fourth piece, a driver loaded at run time:

| Layer | What it is for |
|---|---|
| The "real" library | EA's portable base: files and archives, images, streams, timers, keyboard, mouse and joysticks, fonts, networking, and the SND sound library |
| EAGL2 | The 3D engine: materials as stacks of layers, matrices and views, and a software transform, clip and light pipeline |
| FERT | The front end's runtime: windows, controls, parts and attributes read from text layouts, and an event pump |
| THRASH | EA's driver interface: a small set of entry points (open, set a render state, load a texture, draw, flip) implemented by a DLL, one for Direct3D 7 and one for 3dfx Glide |
| The game | The race (simulation, AI, world, cameras, HUD, race sound), the front end (menus, garage, career) and the network game |

The linker kept the object files in order, so each module of the game is one contiguous stretch of code,
and the source file names left in error messages say which is which. The game's own modules are about two thirds
of the code. The front end alone is the largest module, larger than the simulation, the AI and the 3D
renderer together: much of the game is its menus, its career and its garage.

The program runs as two loops, one after the other. The front end runs first, until the player starts a
race. Its result is written out as a race description, the menus are torn down, the race is loaded and
run, and the race is torn down again before the front end comes back. A restart runs the race loop again
without loading. The pause menu during a race is the front end's runner started again on top of the race,
with one screen.

The front end and the race talk mainly through *option records*: a list of
named values (the track, the laps, the direction, each car's model, colours and driver, the damage
setting, the random seed, the career's points and prizes) that the front end writes and the race reads.
The same records are kept in a text file, `fe.txt`, between runs. This is a clean seam: the race can be
started with only a race description, which is how the network game, the replays and the port's own test
runs start it.

## Time

The race runs on a fixed clock, not on frames. A multimedia timer ticks at 128 Hz, and the game derives two
slower clocks from it: 64 Hz for the simulation and 32 Hz for the input. On each 32 Hz tick the timer
thread samples the controls and queues one *control packet* per player: four bytes holding the steering,
the throttle, the brake and a set of flags (gears, handbrake, horn, assists). The simulation's target is
always two ticks per queued packet, so the input decides how far the race may run.

Each pass of the race loop does three things in this order: it reads the menu and camera keys, it runs
simulation ticks until it reaches the target, and it draws one frame (unless nothing advanced). A slow
machine draws fewer frames but runs the same ticks. Most systems run at 32 Hz, spread over pairs of ticks:

```
every 64 Hz tick:
    even ticks:  clear the contacts, run every car's AI, the "even" task list, the race's even half
    every tick:  the "every tick" task list (the car physics registers its steps here)
    odd ticks:   the race's odd half, the positions, the sound, the world, the cars' render state, the scene
    then:        the network
every frame:     particles caught up at 64 Hz, the sky and light, the views, the HUD, the page flip
```

The cars' simulation is a set of *tasks* each car puts on those two lists when it is made: the drive, the
ground forces, the turn and the move, the walls and collisions, the road tracking, the race progress, the
sound events. A task is a function and a car; the list runs them in the order they were added. This makes
the order of everything explicit, which is what the game's determinism rests on.

The race before the start is the same loop: the first seven seconds of ticks are the countdown, with its
voice and lights at fixed ticks, and the controls take effect only after it.

**Determinism.** Given the same race description and the same control packets, the race plays out the
same. Two details matter for that. The random numbers are the C runtime's own generator (the linear
congruential one, multiplier 214013, increment 2531011), seeded from the race description. And the
arithmetic: Direct3D 7 puts the x87 floating-point unit into single precision, so every addition,
multiplication, division and root in the game rounds to a 32-bit float. A port that computes in doubles,
or in a different order, drifts away from the original within seconds. The port computes in floats in the
original's order, and compares its steps with the original's code run under an x86 emulator (see
`tools/emu` in the repository).

**Replays** use the determinism directly. A replay file is the race description followed by the recorded
control packets, run-length packed in blocks of eight packets per player. There are no car positions in
it. Playing it back runs the whole race again: only the humans' packets are recorded, and the opponents,
the traffic and the police drive again by their AI.

## The data

The game's data is mostly EA's container formats, plus a lot of plain text:

- **VIV archives** (EA's BIG format): a header listing names, offsets and sizes, then the files. Car
  sounds, speech, music and front-end art come in them.
- **CRP files**, the models of cars and tracks: a tree of chunks, each a four-character tag with its
  items. A model's *articles* are its parts, and each article holds its geometry as typed item lists
  (vertices, normals, texture coordinates, primitives, a transform). Chunks point at each other by index,
  and the loader resolves the references.
- **FSH images**: a list of named pictures in several pixel formats, with palettes.
- **RefPack**, EA's LZ-style compression, inside several of those.
- **Sound banks**: patches with layers by key and velocity range, envelopes and loops, the samples in
  EA-XA ADPCM.
- **INI-style text**: the car's description (`.tpg`: camera places, door axes, its styles), the track's
  object definitions, the sky's layers, the scenes (`.scn`: start lines, TV cameras, sound places,
  triggers), the front end's controls and screens.
- **Fixed binary records** for the simulation: the car's specification (engine torque curve, gear ratios,
  masses, tyres), the AI's personalities and traffic configuration, the police scripts, the parts list,
  the career's player save.

Most of what makes a car or a track different is data. A car is its model, its specification and its
sounds; a track is its model, its road, its collision edges, its racing line, its scenes, its sky, its
light map and its police script. The code is the same for all of them. A few things are compiled into
the code instead: some per-track settings (such as each track's speed zones for the AI) are a switch of
literal values in a function, not a file. The port runs that function of the original's code through a
small x86 interpreter rather than copying its numbers.

The front end's screens are data in the same spirit. A control template (`.ctr`) declares a control's
parts, states and attributes, and the attributes are *expressions*: a label's width can be defined as
another part's width times two. The front end expands the attribute names as text and evaluates the
result with a small embedded C interpreter. A layout (`.lay`) places controls on a screen, names its 3D
room, its music and its *callback list*, the C++ class that handles its buttons. Designers could build a
screen without code, as long as its behaviour already existed.

## The world

A track is a closed or open road through scenery, built from a few thousand articles. When the track is
loaded, the articles are sorted into *bundles* by what they are, each bundle taking its articles from the
list: the scene's prototypes (objects that the start and finish place), the placed instances (a
prototype with a transform: signs, cones, barrels), the animated ones (keyframed: the train, the saws,
the fish), the mid-ground scenery and the static rest. Every article of every bundle then becomes an
entry of one object list, which the renderer and the queries walk. Every track has the same seventy
prototypes.

**The road** is a list of *nodes* along the track, each with a position, a direction and lanes, joined at
*junctions* where the road splits or a short cut starts. Everything that asks "where along the track" (a
car's progress, the lap count, the AI, the traffic, the police, the camera's view distance, the light
under the car) asks the road: which node is the car at, how far across it is. The race's length is the
number of nodes along the main path.

**Collisions** use a quadtree over the ground plane, 16,384 units across in fifteen levels: a cell
splits when it holds more than 48 elements. Its elements are the track's solid polygons (triangles and
quads, with surface flags), the road's edges (the walls, from the track's `.edg` file, or built from the
polygons when it is missing) and the roadside objects (cylinders for posts and trees, boxes for crates and
signs). The ground under a wheel is a query of that tree; so are the walls a car's corners touch, and the
objects near a car.

**Visibility** is simpler than one might expect. The renderer tests each object's bounding sphere against
the view, over a grid of the world restricted to the cells under the view's ground area. The track's
*visibility walls* are lines in the ground plane that block lines of sight; they are not a portal system.

**Knockable objects** (cones, signs, petrol pumps, tables) are a nice trick: each is a small rigid body of
the same kind as the simulation's other bodies, asleep at its place until a race car comes within reach.
Then it wakes, is pushed by the car, tumbles and comes to rest on the ground, and goes back to sleep.
Objects that break (barrels, hammers) instead play canned pieces, chosen by the hit's speed. A woken object
that no car touches hangs where it is until one does: the original's behaviour, kept by the port.

## The gameplay

### The cars

A car in the race is one large record shared by the simulation, the AI, the race, the sound and the
renderer: its position and orientation, its velocities, its controls, each wheel's state, the engine and
gearbox, the damage zones, the race progress, the AI's state. Its *description* comes from the race
description (the model, the driver, the colours, the fitted parts); its *specification* from the car's
simulation file and the parts. A car's class says whether it is a human, an opponent, a police car or road
traffic, and that decides which tasks and which controller it gets.

The model is split for drawing into seven levels of detail and many parts, each part used only when the
car's options have a given value. This is how one model holds the body variants, the cabriolet tops, the
lights, the decals and the wheels; the style chosen in the garage sets the options.

### The physics

The car's simulation is the densest floating-point code in the executable, split into a few steps that run
as the car's tasks:

- **The drive** turns the controls into forces. The controls step (with its shift timer and the automatic
  gearbox), the engine (a torque curve sampled every 500 rpm, the gear ratios, the redline), the brakes,
  the split of the torque between the axles, and each wheel's tyre forces from its slip, summed into a
  force and a torque on the body. The engine, the gearbox and the brakes wear with use, and the career keeps
  the wear with the car.
- **The ground forces**: each wheel's contact with the ground under it (from the collision tree), the
  springs and dampers, gravity in the car's frame, and the body touching the ground when the car is upside
  down or over a crest.
- **The turn and the move** integrate the angular and linear velocities over the tick (a 32nd of a
  second, since most steps run every other tick).
- **The walls and collisions**: eight probes at the corners of the car's box against the road's edges,
  then the roadside objects, then the other cars. Two cars test as oriented boxes (separating axes), are
  pushed apart, and exchange an impulse at the contact point.

There are no lookup tables of handling per car beyond the specification: the cars differ by their numbers
(mass, power, gearing, grip, centre of mass), and the same model drives all of them.

### Damage

Hard hits add to the car's damage in nine zones around the body. With damage on, the zones are the car's condition: in the career
they lower its value and are what the repair shop fixes. The renderer shows them two ways: the body's
deformable parts crumple by the zones' damage, and scratches, scuffs and cracked glass are drawn into the
car's own texture pages. Wear and damage are separate: wear grows with distance and abuse of the engine,
gearbox and brakes, damage with impacts.

### The AI

The AI has two layers. A *controller* per car decides who drives it and why: the player, a remote human,
an opponent, a police car, a car being pursued, road traffic or nobody. Each controller runs a *driving
state*: idle, normal driving, racing, chasing, a roadblock, off the road, going to a place, or waiting
off the track. The states are small classes with a tick; a controller switches between them.

The important design point is that the AI drives the same car the player does. A racing opponent's state
plans along the track's *racing line* (a recorded line of positions per node, one file per track and
direction), steers towards it, decides throttle and brake from the speed it wants at the coming nodes,
and writes its decisions into the car's control bytes. Then the same drive step as the player's turns
them into forces. The opponents have no physics of their own and can't cheat on grip; they differ by
*personality*: skills and ranges read from a data file, with the race's difficulty on top.

**Road traffic** uses a lighter lane driver: it picks a lane by scoring the lanes ahead (other cars,
oncoming traffic, the junctions), keeps a target speed from the road's speed zones, and honks. Traffic cars
are few (at most three) and reused: a car far from every viewed car goes to *purgatory*, parked out of the
world for a random time, then comes back on the road near a car that someone is watching, sometimes as
oncoming traffic. A handful of cars feels like a busy road.

**The police** are run by a *director* that reads the track's police script: a list of events at road
nodes (a patrol car joining, a roadblock). As the racers pass the nodes, the director may start an event,
with a chance that depends on the script. A patrol car that sees a speeding car starts a pursuit; the
pursued car keeps a *heat* timer and a list of the cops on it, and the cops take places in a *formation*
around it (behind, beside, ahead to box it in), ram it within a budget, and give up when it gets away.
Traffic pulls over for a pursuit. The roof lights show only at night. Roadblocks are fully ported, but
none of the tracks' police scripts sets one up.

### The race

The race's own tasks follow each car along the road: the distance covered (as progress along the nodes),
the laps, the place (the cars sorted by progress), the wrong way, the end. A race ends when every human
has finished, a short time after the last. The HUD reads the same values.

### The career and Factory Driver

The front end is a large part of the game, because the career is. *Evolution* runs through the eras of
Porsche's history: the player buys a car the era offers, enters tournaments of a few races for points and
prize money, and moves on to later eras as the tournaments are won. Between races there is a garage: the
dealer (new cars of the career's current year), a used car market, a parts shop, a paint shop, repairs
and the car's setup.

The used car market is a small economy simulated in the menus. After each career race the market's
buyers look at every car for sale and give it a score from its price against its value, its mileage for
its age and its damage. The score gives a chance of a sale at the asking price, or an offer near the
value. The market then reprices its cars and adds the year's new used cars, each with random colours,
mileage, wear and a share of the parts that fit it. The player's own cars can be put up for sale the same
way.

*Factory Driver* is a career of missions as a test driver. A mission is a race description with limits:
a time, a damage allowance, cones not to touch, a place to reach. The race checks the limits as it runs and
ends the mission when one is broken. The skill tests are short courses with their own start and end lines
and cones, from the track's skill test scenes.

## Saving

A player's save is a stream of named records, each written and read by the front-end section that owns
it: the player's money, owned cars (each with its fitted parts and wear), the parts in stock, the used car
market, the tournaments, the missions, the track records, the personalities. There is no index and no
length on most records: a reader must read exactly what its writer wrote. The player's HUD layouts are
separate files. The race itself is never saved; the career saves between races.

A quirk of this design: a car put up for sale when the game quits keeps its "for sale" flag, but the market
is rebuilt from its own cars at the next start, so the car is in no list and can't be taken back off the
market. The port keeps it.

## Presentation

**Rendering** is layered. The game's renderer (cars, world bundles, the sky, particles, the HUD) builds
EAGL materials, and a material is a stack of *layers*: coordinates, colours, texture coordinates, a
texture, render state, and the game's own layers (headlight light, specular and sky reflections, shadow
maps, animated texture coordinates). EAGL transforms, clips and lights the vertices on the CPU, with
kernels specialised per vertex format, and hands finished screen-space triangles to THRASH. The Direct3D
driver batches them and sets the device's states. A graphics card at the time did little more than fill
triangles, so the engine does the geometry itself.

The car is lit from the track rather than by a general lighting model: the track's light map gives
colours and reflection textures per stretch of road (with covered stretches different), the colours of the
road polygon under the car give its light (so it darkens under trees and in tunnels), the sun gives the
direction, and the nearest dynamic light (a street lamp) adds its glow. At night the headlights light the
road and the scenery near the car.

The sky is a dome with layers of scrolling clouds, its fog and light blended by the time of day and the
weather of each stretch of the track. Rain and snow fall about the camera; rain runs on the windscreen
in the in-car view.

**The HUD** is a set of small visual classes (tachometer, speedometer, gear, map, place, lap times, the
mirror) laid out by a per-player layout file, which the player can rearrange by dragging. The race's
results, replays, track records and the career's standings are HUD screens too.

**Sound** is EA's SND library mixing in software. The player's engine is several layered streamed
samples, picked and crossfaded by rpm, throttle and whether the view is inside the car. The four nearest
other cars play their engines, road noise, impacts and skids, panned and Doppler-shifted (the factor is
the speed of sound over the speed of sound plus the closing speed). The track has positional sounds
placed in its scenes, reverb under roofs, and one-shot sounds set off by triggers. An announcer calls the
laps, places and gaps from speech banks. Sound banks are loaded on demand into a small cache; a sound
whose bank isn't loaded yet is silent this time and plays once the bank arrives.

**Movies** are EA's MAD format, video chunks with audio, played through the same stream layer as the
music.

## The network game

The network game is layered like the rest: the network providers (IPX, UDP and a TCP stream fallback),
EA's packet and connection layer, a session manager running on its own thread, the game's host and join,
and the front end's screens (create, join, the players list, chat). The race itself has two modes. In the
*positional* mode the cars' states are sent and each machine corrects the others' cars towards them; the
cars pass through each other with collisions off. In the *input exchange* mode the machines exchange the
control packets and every machine runs the same race in step, which is where the packet design of the
single-player race pays off. The Internet game went through EA's lobby servers, which are gone; the port
replaces them with a lobby server run as one more instance of the game.

The port has no sockets: it runs all of this over a relay that stands in for the local network. See
[Multiplayer](/guide/multiplayer).

## Design choices and quirks

Some things the port found that a reader building a similar game may find useful, or amusing:

- **The race doesn't know the menus.** Everything the race needs is in the option records. That made the
  replays, the network game and the port's own unattended test runs simple.
- **Split screen is dead code.** The race has a split-screen mode (a second camera, a second HUD, catch-up
  for the trailing player), but the front end overwrites the mode before every race. The port documents it
  and hasn't turned it on.
- **The wide screen option is a letterbox.** The port's true widescreen is an addition, opt-in, and the
  original's sizes draw exactly as the original.
- **The roadside cylinders all have the same radius** in the collision code, a quarter of a unit, whatever
  their definition says; the definition's radius only places them in the tree.
- **An INI key line without an equals sign would hang the parser**, which starts over on the same line
  forever. No file of the game has one.

Some parts aren't understood yet. A group of large floating-point routines in the real library hasn't been
identified. The simulation steps match the original's bit for bit from the same state, and over free runs
of hundreds of ticks with every racer, but a whole race against the original is still to confirm (see the
[Roadmap](/guide/roadmap)).

## Lessons for building such a game

- **Drive the simulation by a fixed clock and an input queue.** Fixed ticks and recorded inputs give
  replays, lockstep networking and repeatable tests from one mechanism.
- **Make the order of the simulation explicit.** Task lists per tick, run in the order the tasks were
  added, make the frame order a fact of the data, not of the call graph.
- **Let the AI use the player's controls.** One drive model for every car keeps the AI honest and the
  handling consistent; the AI's work is deciding, not moving.
- **Fake the crowd.** A few traffic cars recycled near the camera, and objects asleep until a car comes
  close, cost little and look like a living road.
- **Put the shape of the content in data.** Cars, tracks, police scripts, racing lines and even the menus'
  layouts are files; the code is general.
- **Keep the front end and the game apart.** A plain list of named values between them makes each
  testable on its own.
- **Mind the floating point.** The precision mode the graphics driver sets is part of the game's
  behaviour. A port, or a network game across machines, has to reproduce it.

## Further reading

- [Status](/guide/status): what the port runs today.
- [Beyond the original](/guide/beyond): what the port adds.
- [Multiplayer](/guide/multiplayer): the network game on gasm.
- The reverse-engineering notes:
  [architecture](https://github.com/emdzej/opennfspu/blob/main/docs/re/architecture.md),
  [module inventory](https://github.com/emdzej/opennfspu/blob/main/docs/re/inventory.md),
  [cars](https://github.com/emdzej/opennfspu/blob/main/docs/re/cars.md),
  [track and world](https://github.com/emdzej/opennfspu/blob/main/docs/re/track.md),
  [front end](https://github.com/emdzej/opennfspu/blob/main/docs/re/frontend.md),
  [sound](https://github.com/emdzej/opennfspu/blob/main/docs/re/sound.md),
  [network](https://github.com/emdzej/opennfspu/blob/main/docs/re/network.md),
  [EAGL2](https://github.com/emdzej/opennfspu/blob/main/docs/re/eagl.md),
  [THRASH](https://github.com/emdzej/opennfspu/blob/main/docs/re/thrash.md).
- [Data formats](https://github.com/emdzej/opennfspu/blob/main/docs/formats.md): VIV, FSH, CRP, RefPack,
  banks, replays.
- `tools/emu/README.md` in the repository: how the port's steps are checked against the original's code.
