/* EA real library: pixel and palette format conversion (\real\cmn\depthconv.c and the pixel kernels of
   0x56fe80-0x570f10, 0x572ce0-0x572db0).

   Two numberings meet here: shape types (the FSH record type: 0x7a P4, 0x7b P8, 0x7e 1555, 0x78 565,
   0x7f 888, 0x7d 8888, 0x6d 4444, 0x42 15-bit BGR) and THRASH texture formats (1 P4, 2 P8, 3 1555, 4 565,
   5 888, 6 8888, 7 4444, 11 555, 12 DXT1, 13 DXT3; render/thrash.h). Both index one 8 x 8 table of
   conversion kernels (two variants, 0x5de3fc and 0x5de2fc) and its table of intermediate types (0x5de4fc):
   a conversion steps from type to type until it reaches the target, at most 10 steps. The tables are
   read from the exe; the kernels are the port's own (one C function per kernel address).

   Palettes ("cluts"): shape palette records (0x22-0x24, 0x29, 0x2a, 0x2c-0x2e) are expanded into a
   256-entry work buffer (0x6bd960) in the layout a kernel wants; palettised-to-direct kernels read it
   (16-bit) or the current palette pointer (0x5de2f8, 32-bit). */
#pragma once
#include <stdint.h>

/* Shape record header (16 bytes, docs/formats.md): type and next-record offset, width, height, the
   centre (+8, +0xa) and the position word (+0xc: x in bits 0-11, y in 16-27, mip levels in 28-31). */
enum { SHAPE_HEADER = 0x10 };

/* The 8-bit index tables of the kernels: shape type -> 0..7 (0x534350; -1 = none), THRASH format ->
   0..7 (0x5350b0, table 0x5de574), index -> THRASH format (0x5350d0, table 0x5de5ac). */
int DEPTH_shapeindex(uint32_t type);
int DEPTH_texindex(uint32_t format);
uint32_t DEPTH_indextex(int index);
/* A window/shape bit depth -> its shape type (1 0x79, 4 0x7a, 8 0x7b, 15 0x7e, 16 0x78, 24 0x7f, 32 0x7d;
   DirectDraw-style masks 0x613/0x22b/0x235/0x378/0x115c/0x22b8 too). 0x5350f0 */
uint32_t DEPTH_bpptotype(int bpp);

/* Expands a shape's palette for 32-bit ARGB use: a pointer to 256 entries, either the record's own data
   (type 0x2a) or buf (256 entries) filled from it; the system palette if the shape has none. getclutptr
   0x534440, getclut16ptr 0x5345a0 (565), getclut16aptr 0x534750 (8888 with alpha, clut type 0x2c). */
const uint32_t *getclutptr(const uint8_t *shape, uint32_t *buf);
const uint16_t *getclut16ptr(const uint8_t *shape, uint16_t *buf);
const uint32_t *getclut16aptr(const uint8_t *shape, uint32_t *buf);

/* A copy of shape converted to shape type target, in a new MEM block (MEM_purge it); packed shapes are
   unpacked first. palette = 1: palettised sources expand through the identity remap of the system palette
   instead of their own. variant selects the kernel table (0 = 0x5de3fc, else 0x5de2fc). SHAPE_converttype
   0x534ab0 */
uint8_t *SHAPE_converttype(const uint8_t *shape, uint32_t target, int palette, int variant);

/* Converts n pixels from THRASH format srcfmt (palette format srcclutfmt, palette srcclut) to dstfmt into
   dst (and the palette into dstclut when the target is palettised): 1 if it could. TEXTURE_converttype
   0x534e50 */
int TEXTURE_converttype(uint32_t srcfmt, uint32_t srcclutfmt, uint32_t dstfmt, uint32_t dstclutfmt,
                        const void *src, void *dst, const void *srcclut, void *dstclut, int n);

/* Bytes per row of a shape (packed shapes with type '{' store it directly). 0x5710a0 */
int SHAPE_rowbytes(const uint8_t *shape);
/* An unpacked copy of a packed shape record (type bit 0x80) in a new block, or shape itself. 0x5710e0 */
uint8_t *SHAPE_unpack(const uint8_t *shape);

/* The palette work buffer (0x6bd960), the current 32-bit palette (0x5de2f8, initially the system palette
   0x6b4a60), the transparent colour of the 16-bit-to-32-bit kernels (0x6a6368). */
extern uint32_t DEPTH_clutbuf[256];
extern const uint32_t *DEPTH_clut;
extern uint32_t DEPTH_colourkey;
extern uint32_t REAL_syspalette[256];

/* A kernel: n pixels from src to dst. Exposed for the tests. */
typedef void (*DepthKernel)(void *dst, const void *src, int n);
DepthKernel DEPTH_kernel(uint32_t address);

/* A new, empty shape of width x height at bpp bits (1, 4, 8, 15, 16, 24, 32; 0 = the display's), header
   cleared, pixels not; NULL for bad sizes or depths. createshape 0x574a20 (\real\cmn\creates.c) */
uint8_t *SHAPE_create(int width, int height, int bpp, unsigned memflags);
