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
