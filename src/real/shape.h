/* EA real library shapes: FSH files (\real\cmn\loadshp.c) and their directory (\real\pc\shpclut.c).

   A shape file: 'SHPI' (or 'SHPS'), the file size, the entry count (+8, little-endian), a 4-byte family
   tag, then count entries of a 4-character name and the entry's offset (+0x10 + 8 * i). An entry is a
   chain of records, each a 32-bit header (type in the low byte, the offset to the next record in the
   upper 24 bits, 0 = last) followed by its data: the image first (width, height, centre and position as
   16-bit values at +4..+0xe, then the pixels), then attachments such as the palette (docs/formats.md). */
#pragma once
#include <stdint.h>

/* Loads name (".fsh" added without an extension: the exe's default, read at 0x5e663c) into a block with
   memflags, unpacked if packed (FILE_loadpackfile 0x56e190); NULL if it can't be read or isn't a shape
   file (abort: REAL_abort instead). SHAPE_loadfile 0x5aeb10 */
void *SHAPE_loadfile(const char *name, unsigned memflags, int abort);

uint32_t SHAPE_count(const void *shp);                             /* 0x537ca0 */
/* The name of entry i as stored (4 bytes, read as a little-endian u32), 0 past the end. 0x537cd0 */
uint32_t SHAPE_entryname(const void *shp, int i);
/* Entry i's first record (inline in the original: the offset at +0x14 + 8 * i). */
const uint8_t *SHAPE_entry(const void *shp, int i);

/* Whether a record type is a shape type the library knows (the table at 0x5de890, read from the exe). 0x537d00 */
int SHAPE_typevalid(const uint8_t *rec);
/* The palette record of a shape (types 0x22-0x24, 0x29, 0x2a, 0x2c-0x2e) in its chain, NULL if none; an
   unknown type at the start aborts. SHAPE_clut 0x537d20 */
const uint8_t *SHAPE_clut(const uint8_t *rec);
/* The name a shape's 'o' record carries in its chain (its text at +8), NULL if none. 0x56a6a0 */
const char *SHAPE_name(const uint8_t *rec);
/* Mirrors an unpacked shape of 8 bits and up left to right in place, its x centre too (packed or
   smaller ones abort). SHAPE_horizflip 0x5627a0 (\real\cmn\hflip.c) */
void SHAPE_horizflip(uint8_t *rec);
