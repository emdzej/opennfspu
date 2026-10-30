/* EA real library packers: the codec dispatch in front of compressed files and RefPack, the one codec
   the game's data uses. A packed block starts with a type byte (bit 0: a 3-byte packed size follows the
   magic) and a magic byte 0xfb (0x32 for some types), then the 3-byte big-endian unpacked size. Types:
   0x10 RefPack, 0x18 and 0x30/0x32/0x34 and 0x46 other EA codecs (none in the data: pending). */
#pragma once
#include <stdint.h>

/* The unpacked size, 0 if the block isn't packed by a known codec. UNPACK_size 0x571240 */
uint32_t UNPACK_size(const uint8_t *src);
/* Unpacks into dst (UNPACK_size bytes); the unpacked size, 0 if not a known codec. UNPACK_dispatch 0x571150 */
uint32_t UNPACK_dispatch(const uint8_t *src, uint8_t *dst);
/* RefPack (type 0x10/0x11); with decode = 0 only the size. REFPACK_decode 0x594510 */
uint32_t REFPACK_decode(const uint8_t *src, uint8_t *dst, int decode);
