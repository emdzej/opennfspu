/* The renderer's 3D noise (0x44f110, 0x44f1c0): lattice noise over a 4096-entry permutation (0x619c30)
   and a table of 267 random floats (0x619804), each lattice point's value and gradient being the four
   floats from its byte index on (neighbouring points share three of them). */
#pragma once

/* The tables, from the real library's generator seeded with 12345 (then reseeded with a value drawn
   before). Render_RaceInit 0x468ef0 -> 0x44f110 */
void Noise_Init(void);
/* The noise at (x, y, z): each coordinate + 10000, the eight corners' value / 2 plus gradient . offset,
   mixed by the ease 3t^2 - 2t^3. 0x44f1c0 */
float Noise3(float x, float y, float z);
