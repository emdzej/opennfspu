/* EA real library: fonts (\real\cmn\fontattr.c, \real\patch3\cmn\fontdraw.c, \real\cmn\fontdtex.c;
   0x53c430-0x53ec50, 0x571940-0x5723e0, the palettes 0x594730-0x594a40).

   A font file (.ffn 'FNTF', .pfn) is used in place: +0x08 a version, +0x0a the glyph count, +0x0c flags
   (bits 16-17 the string coding, bit 1 / bit 2 the palette kind), +0x10..+0x13 four signed bytes (pen x
   and y offsets, ascent, descent), +0x14 the glyph table's offset, +0x1c the glyph image's (a shape: its
   type, 4-bit 0x7a / 1 or 8-bit 0x7d / 5, width +4, height +6, pixels from +0x10). A glyph is 11 bytes:
   +0 its character (u16), +2 width, +3 height, +4 x and +6 y in the image (u16), +8 the advance, +9 the
   x and +10 the y offset (signed). Text becomes quads of four THRASH vertices (x y z rhw diffuse
   specular u v, 32 bytes), drawn by the font driver: here the DTEX driver (0x5dfb60), which keeps the
   glyph image as square DTEX pages in the palette the font's colours make. */
#pragma once
#include "real/dtex.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct FontDtex FontDtex;
typedef struct FontSoft FontSoft;

/* The font drivers (0x53c640 makes one current; a font takes the current one at its first draw and keeps
   it, 0x5717c0): the DTEX driver (0x5dfb60, current from FONT_init 0x5365e0 on) and the software one
   (0x5dec18, \real\pc\fontpc.c) that draws the glyphs' pixels into the current window. */
typedef enum { FONT_DRIVER_DTEX, FONT_DRIVER_WINDOW } FontDriver;
/* Makes d current; the one current before. 0x53c640 */
FontDriver FONT_setdriver(FontDriver d);

/* A font (0xec bytes, FONT_open 0x53c430). */
typedef struct Font {
    int32_t penx, peny;          /* +0x04, +0x08: the file's +0x10, +0x11 */
    int32_t ascent, descent;     /* +0x0c, +0x10: from the capitals' height when the font has them */
    int32_t lineheight;          /* +0x14 (state 4) */
    int32_t lineheight0;         /* +0x18: the file's */
    int32_t nglyphs;             /* +0x1c */
    int32_t f20;                 /* +0x20: 100 */
    const uint8_t *file;         /* +0x24 */
    const uint8_t *glyphs;       /* +0x28 */
    const uint8_t *image;        /* +0x2c: the shape */
    uint32_t (*next)(const uint8_t **s);   /* +0x30: the string's next character */
    int32_t nkern;               /* +0x34 */
    const uint8_t *kern;         /* +0x38: 8-byte pairs, sorted (none in this game's fonts) */
    int32_t ntabs;               /* +0x3c */
    const int32_t *tabs;         /* +0x40: tab stops */
    FontDtex *tex;               /* +0x44: the driver's texture */
    uint32_t corner[4];          /* +0x4c: the quads' four corner colours (states 0xc..0x10) */
    uint32_t colour[4];          /* +0x5c: the palette's colours (states 8..0xb) */
    uint32_t palette[16];        /* +0x6c */
    uint8_t dirty;               /* +0xac: the palette changed */
    float slant;                 /* +0xb0: state 1 */
    float sx, sy;                /* +0xb4, +0xb8: states 2, 3 */
    uint32_t flags;              /* +0xbc: states 5 (1), 6 (2: clip), 7 (4: kern) */
    float clip[4];               /* +0xc0 x0, +0xc4 x1, +0xc8 y0, +0xcc y1 */
    float du, dv, dx, dy;        /* +0xd0 .. +0xdc: states 0x12 .. 0x15 */
    float z, rhw;                /* +0xe0, +0xe4: states 0x16, 0x17 */
    FontDriver driver;           /* +0xe8: the driver it took at its first draw */
    FontSoft *soft;              /* (+0x44 for the software driver) */
} Font;

/* A text mesh (0x20 bytes, 0x53e0d0). */
typedef struct {
    Font *font;          /* +0x00 */
    int32_t nverts;      /* +0x04 */
    float *verts;        /* +0x08: nverts x 8 floats */
    float x, y, z;       /* +0x0c, +0x10, +0x14 */
    float endx, minx;    /* +0x18, +0x1c: the pen's end and its least x */
} FontMesh;

Font *FONT_open(const void *file);                                 /* 0x53c430 */
void FONT_close(Font *f);
void FONT_setstate(Font *f, int state, uint32_t v);                /* 0x53e330 */
void FONT_setstatef(Font *f, int state, float v);                  /* 0x53e600 */
uint32_t FONT_getstate(const Font *f, int state);                  /* 0x53e730 */
float FONT_getstatef(const Font *f, int state);                    /* 0x53e940 */
void FONT_setclip(Font *f, float x, float y, float w, float h);    /* 0x53d510 (and clipping on) */
/* The glyph of character c, NULL if none. 0x53d690 */
const uint8_t *FONT_glyph(const Font *f, uint32_t c);
/* Builds the quads of s at (x, y) into out; the vertex count. 0x53d820 */
int FONT_build(Font *f, float *out, float x, float y, const char *s, float *endx, float *minx);
/* Draws s at (x, y). 0x53d730 */
void FONT_print(Font *f, float x, float y, const char *s);
FontMesh *FONT_makemesh(Font *f, float x, float y, const char *s); /* 0x53e0d0 */
void FONT_drawmesh(const FontMesh *m);                             /* 0x53e230 */
void FONT_freemesh(FontMesh *m);                                   /* 0x53e2b0 */
/* The text's bounds: left, top, width, height (0 for empty text). 0x53ea50 */
void FONT_extent(Font *f, const char *s, float *x, float *y, float *w, float *h);
/* Several draws with one texture: the driver's texture is set once (0x53e300 begins, 0x53e320 ends). */
int FONT_pages(const Font *f);   /* port: the DTEX pages of its glyph image (0 before its first draw) */
void FONT_begin(void);
void FONT_end(void);
