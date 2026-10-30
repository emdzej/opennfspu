/* The sky's textures (the object at 0x627e4c, 0x238 bytes; ctor 0x460910): the shapes of Sky\<name>.fsh
   that the sky's pieces asked for by name, and their textures, made on first use. */
#pragma once
#include "real/texture.h"
#include <stdint.h>

enum { SKYTEX_MAX = 40 };

typedef struct {
    Texture *blank;                   /* +0x000: an empty texture (0x535c40) */
    const uint8_t *fsh;               /* +0x004 */
    const uint8_t *shape[SKYTEX_MAX]; /* +0x008 */
    char name[SKYTEX_MAX][6];         /* +0x0a8 */
    Texture *tex[SKYTEX_MAX];         /* +0x198 */
} SkyTextures;

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(SkyTextures) == 0x238, "the original's layout");
#endif

extern SkyTextures *g_skytex;         /* 0x627e4c */

SkyTextures *SkyTextures_ctor(SkyTextures *t, const uint8_t *fsh);   /* 0x460910 */
/* A new file for the same names (shapes looked up again, textures reloaded). 0x460b90 */
void SkyTextures_reload(SkyTextures *t, const uint8_t *fsh);
/* The slot of a shape name: the one that has it, else the shape found in the file in the first free slot;
   -1 if the file hasn't it, 0 if every slot is taken. 0x460a10 (0x460b20) */
int SkyTextures_find(SkyTextures *t, const char *name);
static inline const uint8_t *SkyTextures_shape(SkyTextures *t, int i) { return t->shape[i]; }   /* 0x460b30 */
/* The slot's texture (RTex_make with 'SKY ', class 1, on first use). 0x460b40 */
Texture *SkyTextures_texture(SkyTextures *t, int i);
