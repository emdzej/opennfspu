/* EA real library: dynamic textures (\real\patch3\cmn\dyntexture.c, 0x553d20-0x555530).

   A DTEX texture is the game's copy of a texture in the format the card takes: created from a shape's
   pixels and palette, converted on creation (TEXTURE_converttype), and uploaded to the driver
   (THRASH talloc + tupdate) later, all at once. Textures are grouped in types, one per (size, format,
   palette format, mip levels) combination (at most 32, 0x6a33f8): a type's textures share the size of
   their card memory, so when the card runs out a texture can take over the card memory of the type's
   least recently used one (DTEX_touch, on DTEX_settexture). Each type has three upload priorities (one
   per pass of DTEX_uploadall) that the game sets per texture class (DTEX_setprio).

   Only square power-of-two textures are DTEX textures. Formats are THRASH texture formats, chosen per
   texture by scoring the driver's formats against the source format (DTEX_choose). */
#pragma once
#include "render/thrash.h"
#include <stdbool.h>
#include <stdint.h>

/* A texture (0x38 bytes, from the DTEX memory pool). */
typedef struct DtexObject {
    struct DtexObject *next;     /* +0x00: the type's list of textures (newest first) */
    int32_t type;                /* +0x04 */
    struct DtexObject *older;    /* +0x08: the type's upload order (least recent at the tail) */
    struct DtexObject *newer;    /* +0x0c */
    ThrashTexture handle;        /* +0x10: the driver's texture while it has card memory, else 0 */
    uint8_t *shape;              /* +0x14: the converted pixels as a shape record (header + pixels) */
    uint8_t *pixels;             /* +0x18: shape + 0x10 */
    uint8_t *clut;               /* +0x1c: 0x400 bytes for palettised formats, else NULL */
    int32_t serial;              /* +0x20: number within the type */
    uint32_t srcformat;          /* +0x24: the format it was made from */
    uint32_t srcclut;            /* +0x28 */
    uint32_t flags;              /* +0x2c: DTEX_F_* */
    char name[8];                /* +0x30: not terminated when 8 long */
} DtexObject;

enum {
    DTEX_F_LOADED = 1,           /* pixels are in shape */
    DTEX_F_ONCARD = 2,           /* the card's copy is current */
    DTEX_F_GLOBALCLUT = 4,       /* the driver has one palette for all textures (THRASH_ABOUT_GLOBALCLUT) */
    DTEX_F_CLUT = 8,             /* clut holds a palette */
};

/* A texture type (0x30 bytes). */
typedef struct {
    int32_t count;               /* +0x00: textures of the type */
    uint32_t flags;              /* +0x04: low byte = mip levels */
    uint32_t format;             /* +0x08 */
    uint32_t clutformat;         /* +0x0c */
    int32_t size;                /* +0x10: width = height */
    int32_t bytes;               /* +0x14: pixel bytes with all mip levels */
    int32_t prio[3];             /* +0x18: upload priority per pass */
    DtexObject *objects;         /* +0x24 */
    DtexObject *newest;          /* +0x28: the upload order's head */
    DtexObject *oldest;          /* +0x2c */
} DtexType;

/* DTEX_info's answer (0x554b40): serial, shape, pixels, palette (if loaded), format, source format. */
typedef struct { int32_t serial; uint8_t *shape, *pixels, *clut; uint32_t format, srcformat; } DtexInfo;

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(DtexObject) == 0x38 && sizeof(DtexType) == 0x30, "the original's layouts");
#endif

extern DtexType DTEX_types[32];   /* 0x6a33f8 */
extern int DTEX_ntypes;           /* 0x6a3a44 */
extern int DTEX_nobjects;         /* 0x6a3a40 */
extern bool DTEX_uploaded;        /* 0x6a3a48: DTEX_uploadall has run (textures are on the card) */
extern uintptr_t DTEX_current;    /* 0x6a3a58: the last DTEX_settexture */
extern int DTEX_destroycount;     /* 0x6a3a64: DTEX_destroyall calls */

/* The texture memory pool (VirtualAlloc'd in the original, 12 MiB first, 256 KiB less per failure down to
   512 KiB): the port allocates DTEX memory from the heap and frees it all when the pool goes. 0x553d20,
   0x5548b0, 0x555160 */
int DTEX_initpool(uint32_t size);
void DTEX_freepool(void);

/* The type of (size, format, palette format, flags), made if new ("too many different texture types"
   ends the program after 32). 0x554930 */
int DTEX_create(int size, uint32_t format, uint32_t clutformat, uint32_t flags);
/* The driver format (and palette format) a texture of *format / *clutformat becomes: the lowest score of
   DTEX_score over the driver's formats; both updated. 0x554580 */
uint32_t DTEX_choose(uint32_t *format, uint32_t *clutformat);
/* How badly a driver format fits a source format: the position in the source format's preference list,
   +20 for a missing entry or alpha that would be lost, +20 if the card lacks the format, +10 for leaving
   a palette behind; 10000 = impossible. The default of the scoring hook (0x5df2c8). 0x554070 */
int DTEX_score(uint32_t srcformat, uint32_t srcclut, uint32_t format, uint32_t clutformat);
/* The preference list of a source format (0-terminated driver formats; 0x5def98[format]), replaceable
   (0x5546b0: the game sets 565 and 8888 per texture class); 0x554690 picks whether 8-bit textures prefer
   staying 8-bit (1) or 565 (0). */
void DTEX_setprefs(uint32_t format, const uint32_t *list);
void DTEX_setpalettepref(int keep8bit);

/* A new texture of width x height (square, a power of two; else NULL) made from source format srcformat
   and palette format srcclut, with flags (low byte: mip levels) and a name (8 characters kept). 0x5546c0 */
DtexObject *DTEX_new(int width, int height, uint32_t srcformat, uint32_t srcclut, uint32_t flags, const char *name);
void DTEX_free(DtexObject *t);                                     /* 0x554a20 (DTEX_destroy) */
/* Converts source pixels (and palette) into the texture; it needs (re)uploading after. 0x554ad0 */
void DTEX_load(DtexObject *t, const void *pixels, const void *clut);
int DTEX_info(const DtexObject *t, DtexInfo *out);                 /* 0x554b40: 0 if t is NULL */
void DTEX_invalidate(DtexObject *t);                               /* 0x554b90 */
/* Gives one texture card memory (talloc) and its pixels (tupdate); the texture or NULL. DTEX_upload
   0x554cc0 */
DtexObject *DTEX_upload(DtexObject *t);
/* Uploads every texture, type by type, in passes of rising priority: the number of passes. -1 if done
   already. 0x554e40 */
int DTEX_uploadall(void);
void DTEX_resetall(void);                                          /* 0x555090: the driver's textures go */
void DTEX_destroyall(void);                                        /* 0x5550f0 */
/* Selects a texture on the driver: a DtexObject (made current, uploaded on demand), or a settexture code
   (0..0x10). 0x5551a0 */
void DTEX_settexture(uintptr_t t);
void DTEX_touch(DtexObject *t);                                    /* 0x555210 */
/* The priority of pass `pass` (0..2) for textures of (size, format, palette format, flags). 0x5553a0 */
void DTEX_setprio(int pass, int prio, int width, int height, uint32_t format, uint32_t clutformat, uint32_t flags);
int DTEX_prio(const DtexObject *t, int pass);                      /* 0x555000: 1 past the third pass */
/* Called before every upload with the texture going up and the one losing its card memory (debugging). */
typedef struct { int32_t size; uint32_t srcformat; const char *name; const DtexObject *t; } DtexHookInfo;
void DTEX_sethook(void (*hook)(const DtexHookInfo *up, const DtexHookInfo *down));   /* 0x555400 */
/* Converts n bytes of pixels between formats (fallbacks for the 8-bit-luminance and same-format cases). 0x555410 */
void DTEX_convert(uint32_t srcformat, uint32_t srcclut, uint32_t format, uint32_t clutformat, const void *src,
                  void *dst, const void *srcclutdata, void *clutdata, int bytes);
/* A half-size copy of a shape (every other pixel and row), for textures the card can't take at full size.
   0x555530 */
uint8_t *DTEX_halve(const uint8_t *shape);
