/* The render module's screen sprites (0x451360 .. 0x452370): the effect bank of GameData\Render\flare.dat
   (the global 0x61bcc0), and the one mesh that draws a sprite's two layers as screen quads (0x61bcb0).
   The sun and its lens flare use them (sky_sun.c); the track's lights' markers and glows to come. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "real/texture.h"

/* An effect (flare.dat's record, 0x8c bytes): two layers, each a texture name ('null': none), a colour
   and a size; the flags (4: turned with the screen position); the blink: phase, on, off (in ticks). */
typedef struct {
    uint32_t name;            /* +0x00 */
    uint32_t texname[2];      /* +0x04 */
    uint8_t pad0c[0x10];
    uint32_t colour[2];       /* +0x1c */
    uint8_t pad24[0x10];
    float size[2];            /* +0x34 (100) */
    uint8_t pad3c[0x10];
    uint32_t flags;           /* +0x4c (4) */
    int32_t phase;            /* +0x50 */
    int32_t on;               /* +0x54 */
    int32_t off;              /* +0x58 */
    uint8_t pad5c[0x30];
} SpriteRec;

_Static_assert(sizeof(SpriteRec) == 0x8c, "flare.dat's record");

/* An effect and its layers' textures (0xc bytes). */
typedef struct {
    SpriteRec *rec;
    Texture *tex[2];
} Sprite;

/* The bank (0x328 bytes): its sorted entries (by name) and the slots they were read into. */
typedef struct {
    Sprite **entries;         /* +0x000 */
    int32_t count;            /* +0x004 */
    Sprite *slot[200];        /* +0x008 */
} SpriteBank;

extern SpriteBank *g_sprites;   /* 0x61bcc0 */

/* The sprite mesh, its blend, uv quad and clock, and the 'BLUR' texture (0x451360, Render_RaceInit);
   their end (0x451480). */
void Sprites_Init(void);
void Sprites_Free(void);
/* The bank from flare.dat (0x452090 -> 0x452180; after Sky_LoadFsh, the sky's textures come first);
   its end (0x4520c0). */
void SpriteBank_Load(void);
void SpriteBank_Free(void);
/* The effect of a name, by binary search (the file is sorted); NULL if it isn't there. 0x452130 */
Sprite *SpriteBank_Find(SpriteBank *b, uint32_t name);
/* Whether neither layer has a texture. 0x451fd0 */
bool Sprite_Empty(const Sprite *s);
/* The effect at screen position pos (x, y) with alpha a (none below 0.05; the colours' alpha scaled
   below 1), through the given blend: when its blink shows it, each layer a quad of its size (a share of
   the screen's width and height), turned by (x + y) / 1440 turns when the flag 4 is set. 0x4515e0 */
void Sprite_Draw(const Sprite *s, const float *pos, float a, void *blend);
