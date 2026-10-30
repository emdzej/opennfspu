/* EA real library: textures and the texture manager (0x535390-0x536350, 0x537740-0x537790; a C++ class
   pair in the original, no source file name in the exe).

   A texture (0x54 bytes, vtable 0x5bb6d4) owns a DTEX texture (shared with its copies through a reference
   count) and seven texture-stage states (dither, filter, state 0xc, mipmapping, LOD bias, address U, V).
   The manager (0x14 bytes, vtable 0x5bb6c8) makes textures and remembers the ones selected on the two
   stages. The graphics object at 0x69ed0c pairs the EAGL device with the manager (the device's +4 is the
   render-state object EAGL uses). The stage states sent to the driver are cached per stage (0x5de6bc:
   seven (state id, value) pairs per stage) and only changes are sent, through the setters at 0x5de72c. */
#pragma once
#include "real/dtex.h"
#include <stdbool.h>
#include <stdint.h>

enum { TEXTURE_NSTATES = 7 };

/* The seven states (+0x38 of a texture; 0x537740 sets the defaults: 0, 1, 0, 0, 0, 0, 0). */
typedef struct { int32_t v[TEXTURE_NSTATES]; } TextureStates;

struct Texture;
struct TexManager;

typedef struct { void (*destroy)(struct Texture *self, int free_it); } TextureVtbl;     /* 0x5bb6d4: 0x535aa0 */

typedef struct Texture {
    const TextureVtbl *vt;       /* +0x00 */
    struct TexManager *manager;  /* +0x04 */
    DtexObject *dtex;            /* +0x08 */
    int32_t width, height;       /* +0x0c, +0x10 */
    uint32_t format;             /* +0x14: the source format, then the DTEX format */
    uint32_t clutformat;         /* +0x18 */
    uint32_t flags;              /* +0x1c: DTEX flags (low byte mip levels); bits 16+ the stage */
    int32_t *refs;               /* +0x20: shared by copies */
    char name[20];               /* +0x24..+0x37 */
    TextureStates states;        /* +0x38 */
} Texture;

typedef struct {
    void (*destroy)(struct TexManager *self, int free_it);                                     /* 0x535910 */
    Texture *(*create_records)(struct TexManager *self, const uint8_t *pixels, const uint8_t *clut,
                               uint32_t flags, const char *name);                              /* 0x535f50 */
    Texture *(*create)(struct TexManager *self, const uint8_t *shape, uint32_t flags, const char *name); /* 0x535ef0 */
} TexManagerVtbl;                                                                              /* 0x5bb6c8 */

typedef struct TexManager {
    const TexManagerVtbl *vt;    /* +0x00 */
    void *device;                /* +0x04 */
    Texture *stage[2];           /* +0x08, +0x0c: selected textures */
    int32_t count;               /* +0x10: textures made (names "tex%d") */
} TexManager;

/* The graphics object (0x69ed0c points at it). */
typedef struct { void *device; TexManager *textures; } TexGraphics;
extern TexGraphics *g_texgraphics;   /* 0x69ed0c */

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(Texture) == 0x54 && sizeof(TexManager) == 0x14, "the original's layouts");
#endif

extern bool TEXTURE_usedtex;         /* 0x5de6b8: textures go through DTEX (1) */
extern bool TEXTURE_live;            /* 0x69dd1e: between TexManager_Begin and _End */
extern bool TEXTURE_trilinear;       /* 0x69dd1d: mipmapping on means trilinear (3) */
extern bool TEXTURE_jointaddress;    /* 0x69dd1c: the driver has one address mode for U and V */

void TEXTURE_initstates(void);                         /* 0x535710: the stage-state cache's defaults */
void TEXTURE_sendstates(void);                         /* 0x535870: the whole cache to the driver */
void TEXTURE_settrilinear(bool on);                    /* 0x5358b0 */
void TEXTURE_setdtex(bool on);                         /* 0x535e10 */
bool TEXTURE_islive(void);                             /* 0x535e40 */
/* Sends the states of one stage that differ from the cache. 0x535d70 */
void TEXTURE_sendstage(const TextureStates *s, int stage);

void TextureStates_init(TextureStates *s);             /* 0x537740 */
void TextureStates_setmipmap(TextureStates *s, int on);   /* 0x537790 */
/* The other one-state setters of the class (0x5377a0 v[0], 0x5377b0 v[1], 0x5377c0 v[2], 0x5377d0 v[4]);
   which driver state each index drives is the exe's table of (state, setter) pairs (texture.c). */
static inline void TextureStates_set(TextureStates *s, int index, int32_t value) { s->v[index] = value; }
/* 0x5377e0: U and V addressing together (v[5], v[6]). */
static inline void TextureStates_settile(TextureStates *s, int32_t value) { s->v[5] = s->v[6] = value; }

/* The graphics object: registered (0x540100), its device set, which makes a new manager (0x5401a0), and
   gone (0x540180). */
void TexGraphics_register(TexGraphics *g);
void TexGraphics_setdevice(TexGraphics *g, void *device);
void TexGraphics_unregister(TexGraphics *g);

TexManager *TexManager_ctor(TexManager *m, void *device);   /* 0x5358c0 (probes U/V address modes) */
void TexManager_dtor(TexManager *m, int free_it);           /* 0x535910 */
void TexManager_begin(TexManager *m);                       /* 0x535dc0: textures to the card */
void TexManager_end(TexManager *m);                         /* 0x535df0: off the card */
void TexManager_release(TexManager *m, Texture *t);         /* 0x535f90 */
/* Selects the textures of stage 0 and 1 (NULL = none; both NULL = no texturing), with their states. 0x535fb0 */
void TEXTURE_select(TexManager *m, Texture *t0, Texture *t1);
void TexManager_restore(TexManager *m);                     /* 0x536110: states and textures sent again */

/* Textures (made through the manager's vtable in the original: TexManager_create / _create_records). */
Texture *Texture_fromshape(Texture *t, const uint8_t *shape, TexManager *m, uint32_t flags, const char *name); /* 0x535930 */
Texture *Texture_fromrecords(Texture *t, const uint8_t *pixels, const uint8_t *clut, TexManager *m, uint32_t flags,
                             const char *name);                                                               /* 0x535af0 */
Texture *Texture_empty(Texture *t, TexManager *m, uint32_t flags, const char *name);                         /* 0x535c40 */
Texture *Texture_copy(Texture *t, const Texture *src);      /* 0x535e90: shares the DTEX texture */
void Texture_dtor(Texture *t, int free_it);                 /* 0x535aa0 (0x535e50 without the free) */
/* The DTEX format and the converted shape. 0x535d20 */
int Texture_info(const Texture *t, uint32_t *format, uint8_t **shape);
void Texture_invalidate(Texture *t);                        /* 0x535d60 */
void Texture_commit(Texture *t);                            /* 0x5361d0 (DTEX's 0x555390 does nothing) */
void Texture_setstates(Texture *t, const TextureStates *s); /* 0x5361e0 */
void Texture_reload(Texture *t, const uint8_t *shape);      /* 0x536280 */
void Texture_rename(Texture *t, const char *name);          /* 0x536310 */

/* The format code of a shape or palette record (THRASH format; palette records give palette format
   codes: 1 555, 2 565, 4 8888, 6 1555), with 0x80000000 for records flagged 0x2000. 0x5563f0 */
uint32_t TEXTURE_formatof(const uint8_t *rec);
