# Sound

The game's sound is EA's SND library, version 4.1g (its init carries the string "SNDAUTHOR: Dave Mercier,
Friday 03:06PM Mar 03, 2000, V4.1g"). The game side of audio (banks, engines, collisions, track sounds,
music, speech) is at 0x4a4000-0x4b0000; the library's interface at 0x55e000-0x569000; its voices, software
mixer and DirectSound device at 0x588000-0x591000 and 0x595000-0x59d000. The library is plain C: its
objects are structures with function pointers, so there is no RTTI to name them.

The port (src/snd/) keeps the library's structures and arithmetic. Its device, DirectSound buffers that a
thread fills ahead of the play cursor, becomes the mixer pulled by the platform's audio callback.

## Banks and patches

A bank starts with `BNKl`, a version (4 in this game's files), a 16-bit patch count and the size of its
header part. Version 4 banks list the patches' offsets from +0x14; each offset counts from its own word
(0x58ff20).

A patch starts with `PT` and a flag byte (bit 1: an 8-byte header instead of 4), then tags. The tag reader
(0x599510) skips 0xfc padding; 0xff ends the patch, 0xfe separates layers and 0xfd marks the sample part;
any other tag has a length byte (0xff: a 4-byte length follows) and, for lengths up to four, a big-endian
value. The parser (0x598eb0) files each tag's value and data under its number (0x00-0x25 and 0x80-0xa1)
after setting defaults, then copies the fields the library uses into a 0xa4-byte layer description:

| Tag | Field | Default |
|---|---|---|
| 0x01, 0x02 | key range | 0, 127 |
| 0x03, 0x04 | velocity range | 0, 127 |
| 0x07 | root key | 60 |
| 0x0c | pan | 64 |
| 0x0e | volume | 127 |
| 0x19 | envelope: {ticks, level} pairs (data plus value) | the exe's {forever, 127} at 0x5e3498 |
| 0x1c | the envelope's starting level | 127 |
| 0x82 | channels | 1 |
| 0x84 | sample rate | 22050 |
| 0x85 | samples | 0 |
| 0x86, 0x87 | loop start and length | -1 |
| 0x88, 0x89, 0x94, 0x95 | each channel's data, from the bank's start | 0 |
| 0xa0 | codec | 10 |

Every patch in the game's `.bnk` banks is codec 10, EA-XA ADPCM (15 bytes for 28 samples a channel).

## Playing

`SNDplay` (0x5652f0) takes a bank, a patch and play parameters (their defaults 0x561710: pitch 0x1000,
volume 127, pan 64). It walks the patch's layers (0x590560) and starts a voice for each whose key and
velocity ranges hold the request (0x5901d0): a voice of the 0x70-byte table at 0x6b4940, its pitch from
the key against the root key, its volume and pan from the patch's and the request's, its envelope and
modulators. The voice's source is set up for the codec (0x598cd0, 0x589d90, 0x596910, 0x598280).

The library has two outputs: its own software mixer (flag 4 of the output configuration) and one
DirectSound buffer per voice (flag 0x10, 3D with flag 1). The game's default configuration (0x24, at
0x4a6480) uses the software mixer, which is what the port has.

## The software mixer

A source (0x50 bytes, the table at 0x6b1710) holds its state, its gain and pitch, its current and target
master and per-channel levels (floats; level changes ramp over 32 samples, 0x597e70) and a chain of nodes
sorted by type (+0x34; 0x596a80): the decoder (type 4), an optional per-channel effect (type 3, 0x1818
bytes, 0x599e30) and a resampler made when the pitch is first set (0x5995e0). The mixer renders each
playing source's chain into a float buffer and adds it to each output channel by its level (0x598510),
then each output channel's converter writes the device's format.

The decoders are picked from a table by codec and by whether the sample loops or streams (0x598280,
registered by 0x59b650-0x59b750). EA-XA's (0x597b70 one-shot, 0x59c7a0 looping, 0x59c3c0 streaming)
decode in floats: each 15-byte frame's first byte gives a pair of prediction coefficients (0x5e3b28,
0x5e3b38) and a shift; its 28 samples come from 14 bytes, high nibble first, each the nibble scaled by the
shift (the table 0x59b680 builds) plus the two samples before weighted by the coefficients.

The device's fill (0x58f970) alternates the library's 100 Hz tick (0x5892d0: envelopes, volume ramps,
pitch slides, modulators) with mixing runs (0x5986f0) of up to 256 samples.

## The cars near the listener

Each odd tick 0x4a7b90 keeps a list of the race cars nearest the listener (four records of 0x38 bytes at
0x655d60; 4, 2 or 1 of them at sound quality 0, 1, 2), within 300 units (50 with car flag 0x10), leaving
out the viewport's own car (in split screen the humans). Cars within 400 have their `lden` bank (stream
type 1, index `car*2+1`, 0x4b02a0) asked for in advance. A record whose car is no longer near is let go
and its slots stopped; a new car takes a free record. (When a record changes cars, the check that would
stop its engine slots tests the new car against null, so it never does.)

0x4a6e50 plays record i's car like the player's: Doppler `340 / (340 + closing speed)`, the distance,
pan and facing. 0x4aa740 plays the engine as two streamed banks in slots i+10 (`lden`) and i+14
(`ldex`), crossfaded by the facing through the fade bytes around 0x5d17a4 (only `lden` at the low
qualities), pitched by rpm and Doppler, attenuated by `128 / (2 + distance)` (camera 0xb: `256 / (1 +
distance)`); the road noise (patch 0x10, slot i+6) by the forward speed up to 40, scaled by 0x655f34.
That value is 0x80, less each near car's shortfall from 128 units (0xc0 base, clamped to 0..0x80); the
player's engine snapshot uses it as its spread. The car's events go through the impact player with the
record's slot i+0x22 for slotted ones (stop events included, unlike the player's). A human car's horn
(car+0xd86) plays patch 10 (11 for type 0x30) in slot i+0x27; the others' horn countdowns are never set
by the exe. Sirens (0x4aac60, 0x4aad80) are pending.

A patch played from a streamed bank (-2 track, -3 cars, -4 speech) resolves through the cache
(0x4a8340): if the bank isn't cached yet, it reserves an entry and queues a load-only request (at most
six waiting) and stays silent until a later tick finds it. Eviction takes a free entry, else the least
recently used one older than 16 ticks, else the last one of a lower type (0x4a81b0).

## The track's sound elements

The scene's `CAudioElem`s carry a record at +0x7c: the position (copied from the element's by 0x4abf40),
+0x88 kind, +0x8a the sound (an entry number of `track.viv`), +0x8c radius, +0x90 inner radius, +0x94
period and +0x96 its random part (both in units of 8 ticks), +0x98 repeats and +0x9a their random part,
+0x9c volume in percent, +0x9e the time it plays next, +0xa0 its voice, +0xa2 a trigger flag.

Sixteen voices (0x656ac0, 16 bytes each: the element, its play, its sound, plays left) play in slots
0x39..0x48, which take their banks from the streamed track archive. On each odd tick, for the player's
car, 0x4aed00 goes through a quarter of the scene's elements, picked by bits 1-2 of the tick:
- An element with a period gets its next time once the old one has passed, if it has no voice.
- Elements within their radius + 100 (a rough ground distance: the larger axis difference plus a quarter
  of the smaller) are passed to 0x4ae890, and so are elements that still have a voice.

0x4ae890 gives a free voice to an element whose time has come; if none is free, it takes the farthest
finished voice, provided that voice is farther away than the element. It plays the sound over its
repeats and sets pan and volume by kind:
- Pan: kinds 3 and 4 sit at the car's left and right, kind 2 in the middle, the others by direction.
- Volume: kinds 1, 6 and 7 fall with the squared distance over the radius; the others are full inside the
  inner radius and fall from there to the radius.
- Kind 1 is a one-shot: it plays when a scene trigger sets its flag (Scene_Tick 0x470b10, 0x4703c0,
  0x4af210), and until then only asks for its sound.

The elements draw from the game's shared random numbers (0x537db0), so the simulation's later random
values depend on them, as in the original.

Before the race 0x4a8670 reads the opponents' engine banks and the rain's, then Audio_LoadTrackBanks
(0x4af390) the sounds of elements near the road's first node, each with a time limit of 0x280 timer
ticks. The 0x4aed00 loop over the world's animated objects, which have sound lists of their own, is
pending.

## The listener and the triggers

0x465360 puts the listener at the camera's eye. Its velocity (0x655e44) is the velocity of the car the
camera follows, except under the TV cameras (modes 0xc and 0xe), where it is null. Every Doppler factor
subtracts it from the source's velocity, so in the car and chase views the player's own engine is not
shifted. With a listener velocity, the player's speed term passed to 0x4a9e20 (0x655f5c) is zero, and a
near car's speed is relative to the viewport's car.

Scene_Tick (0x470b10) tests the first race car against each event trigger, on every odd tick. The car
has to pass all four tests:
- Position (0x470620). A line trigger needs one of the car's wheel diagonals to cross it; a sphere
  trigger needs the car's centre within the radius.
- Facing (0x470a30): above 0.5 against the car's forward axis.
- Direction (0x470920): above 0.5 against its direction of motion.
- Speed window (0x470ac0).

What a hit does:
- Type 4 sets the linked kind-1 sound element's trigger flag.
- Type 7 moves the linked animated object (0x482fa0, pending).
- Type 3 runs the tests over the racer count but indexes the race car list, and does nothing either way.

A trigger with +0x9c set fires only once. Only castle and forest have sound triggers (five in all).

Sim_Step ramps the effects volume (0x5d1744) from zero over ticks 0x11 to 0x50, keeping the option's
value in 0x657ca4.

## Streams and music

SND's stream players (0x566320 .. 0x566d40, 16 of them at 0x6b7680) play a queue of EA streams one after
another through one voice:
- A stream is a sequence of chunks: `SCHl` holds a PT header (channels, rate, length; EA-XA by default),
  `SCCl` a count, `SCDl` the data blocks, `SCEl` the end.
- A data block starts with its sample count, then one offset per channel, past that table. Each
  channel's data opens with two 16-bit predictor samples (the previous block's last two), followed by
  15-byte EA-XA frames of 28 samples.
- The voice's sources pull the blocks from a ring, the library's stream bank (0x560fb0 .. 0x5616a0), with
  as many slots as the player's buffer has kilobytes. The streaming decoder (0x59c3c0, render 0x59c170)
  decodes whole frames while they last and carries a block's partial last frame over through its
  leftovers. With no block left it plays silence.
- The lead channel's progress counts against the head entry. An entry played out leaves the queue
  (0x5662c0, 0x566200).
- A header in a different format waits until the ring has drained, then the voice is restarted (state 2).

The original reads the files on its file thread (STREAM_*, 0x591000 onwards), waits for a second of
prebuffer, and has the player take ten chunks a pass. The port reads the chunks from the data files
itself, when the player's service (0x5666f0, run on the library's ticks) takes them.

The music (0x4ad2a0 .. 0x4ae780, globals at 0x656ab4) plays the songs of `GameData\Music\zzzymus.viv`.
Entries are named `<kind><n>-<title>-<artist>`, with kinds game, menu, win, lose and vic.
- Audio_Init calls Music_Init (0x4aded0) with a 256 KB buffer: one stream player and a 25 Hz task
  (0x4ad790).
- The playlist (0x4ae240) holds entry numbers, played in order or shuffled. For the shuffle, the next
  song is any but the current one, picked by the shared random numbers plus GetTickCount; the port uses
  the library's ticks in milliseconds for GetTickCount.
- 0x4ae2a0 picks a song by a wildcard pattern (0x4a5d60: `*`, `?`, `#` and the operators `& | ! ~ ^`).
  Mode 1 plays one matching song once, from just past the playlist's end, then goes back to the list.
  Mode 2 plays one and empties the list.
- A change fades the song playing out over 500 ms. A newly queued song starts at volume 0 and is set
  to the music's volume once the player reports it. The end-of-race kinds (lose, win, vic) play at the
  options' music volume.
- A queued song whose read-ahead drops under 0.55 s while more is still to come fades out over what
  is left.
- The race start fades the music out over 1.5 s (0x4a8670), sets the race's playlist, and 0x4aafd0 sets
  the volume (0x5d14b0 times 0x5d1740 over 128) and starts playing. Without the front end, the port's
  playlist is every song of the front end's "game*" list (Music_LoadSongList 0x4adbf0).

## The announcer

The speech archive `GameData\Speech\zzzx<language>.viv` lists its 166 lines in `oneshots.txt`: the
countdown, eras, places, the gap to the next car (seconds ahead and behind), laps, best laps, records,
knockout and tournament results. Each line plays as a type-2 streamed one-shot (0x4b0370), through
small functions that pick an index, some of them at random (0x4a5f70 .. 0x4a6450).

During a race, 0x427790 runs when the local player finishes a lap (called from the progress code,
0x416330). It does nothing on the last lap, in a knockout's eliminated places, or in a replay.
Otherwise it says:
1. The lap's number.
2. With two racers, the lead (0x4a5f70): the most seconds the other car is ahead (0x4353c0, from the
   road-node gap over the speeds), passed in negated, so positive means the player leads.
3. With more racers, the place, then the seconds behind the car ahead (1 to 12, then "way behind").
4. A best lap, compared with the earlier laps; the first lap sets the reference.
5. "Final lap" when one lap is left.

The gap lines need the player faster than 13.39. The end-of-race lines belong to the result screens
(HCongrats, HTrackRecords, HTournamentStandings), which aren't ported yet.

## Reverb

SND's software reverb (0x598ac0 .. 0x598c30) is switched on by 0x5650f0, which routes through 0x590000
to 0x58ff60. With bit 0x20, the mode picks the preset: 1 means group 0's own, anything else the default
at 0x5e25b0.

A preset (`"FX"`) gives the filter count of each of three stages at +4, a gain byte at +7, and from +8
four bytes per filter:
- the delay in ms (big-endian 16 bits), which becomes the next prime number of samples at the rate;
- a, the allpass coefficient, in 127ths;
- b, the damping, in 127ths.

Each filter is an allpass: the output is the delayed sample less a times the input, and the line takes
back the input plus a times the output. That feedback goes through a one-pole lowpass with weights
b·g and (1 - b)·g, where g = (gain byte + 100) / 350. Stage 0 runs in series on the send bus. Stages 1
and 2 run from it into two more buses (odd output channels take stage 2's when stage 1 has filters).
The default preset has four filters in stage 0 only: 100, 70, 49 and 34 ms, with a of 64 or 76 and b
of 0 or 95, all at g = 1. Every output channel gets its output.

Each source sends its decoded samples times its level (0x598840): the master volume times the voice's
group send times 1.5. The reverb runs after the sources (0x598890) and goes quiet 2000 blocks after
the last send.

A voice's send is its patch's tag 0x13 times its play's +0x0a times its group's level (0x589b20), each
over 127. 0x565150 sets the group levels (0x20 the software's, 0x80 the hardware's) and recomputes the
send of every voice playing. The levels start at zero.

The game:
- Switches the reverb on at the high qualities (0x4ab240; with no sends yet).
- Each tick, tests the player's car for a roof above it (a surface with negative flags, 0x473290).
  Under a roof the send goes straight to 100, the hardware's to 50. In the open it drops by 0x20 a tick
  to 8, the hardware's to half of that. At the low qualities it is reset each tick first.
- Makes the collision bank's patches above 0x2f quieter by (150 - send) / 150 (0x4a96b0).
- Resets the send at the race start.

The hardware reverb (DirectSound 3D, 0x58e530) isn't ported.
