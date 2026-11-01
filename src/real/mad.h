/* EA's MAD video codec, as the front end's movies use it (FEData\Movies\*.mad: EArts-av, inPC-av, the
   chronicle's ch1-6 and the eras' er1-4pc-av). The decoder (0x56c800 .. 0x56d020 and its assembly at
   0x592c10 .. 0x5933f0) and the frame display (0x56a6d0, its tables 0x56ab20 and the row routines
   0x5923c0 .. 0x5926c8).

   A movie is a chunk stream (tag, little-endian size counting the header): 'MADk' key frames, 'MADm'
   frames predicted from the previous one, 'MADe' predicted frames that aren't kept as the reference, and
   the 'SC..' audio chunks the sound library plays. A video chunk's header: +0xc the frame time in 16.16
   ms, +0x10 and +0x12 the width and height (shorts), +0x15 the quality; the bit stream from +0x18.

   Frames are 7-bit YUV 4:2:2 words of two pixels: byte 0 V, 1 U, 2 the second pixel's Y, 3 the first's
   (each 0..127), width x height / 2 words, rows of width / 2. */
#pragma once
#include <stdint.h>

/* Starts a frame's bit stream (bits after the chunk's 0x18 header bytes, 16-bit little-endian words up
   to end): predicted or not, at a quality. The tables are built on the first call. 0x56c800 */
void MAD_begin(const uint8_t *bits, const uint8_t *end, int inter, int quality);
/* One 16 x 16 macroblock into frame words dest from the reference ref (the same word of the previous
   frame) of a frame width pixels wide. 0x56ca90 */
void MAD_macroblock(const uint8_t *ref, uint8_t *dest, int width);
/* A whole video chunk (the header first) into dest from ref (NULL for none), as showmad decodes one
   (0x4dc300: the macroblocks in rows of 16). */
void MAD_decode(const uint8_t *chunk, uint32_t size, const uint8_t *ref, uint8_t *dest);

/* A frame onto the current window at (x, y): mode 0 as it is, 3 at twice the size (rows and columns
   interpolated); clipped to the window's clip. 0 when drawn, -1 for a mode not ported (1, 2), -2 for a
   window depth other than 15, 16 or 32 (32 only in mode 0). 0x56a6d0 */
int MAD_blit(int x, int y, const uint8_t *frame, int w, int h, int mode);
