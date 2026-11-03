/* The HUD's art (HGraphicVisual, 0x10c bytes, vtable 0x5b2bcc; HGraphicVisual_LoadArt 0x422e10): one image
   of a DashHud .fsh drawn as a quad. A file's images are packed once into one square texture page ("HUD:"
   and its name; the page's side the power of two past the images' extent), kept by name for the next art
   of the file, and drawn through the HUD's 2D layer in a bucket of their own (or at once, with the art's
   own material). The image's place in the page gives the texture coordinates, in four sets (as is and
   mirrored in x, y or both). */
#pragma once
#include "real/texture.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int32_t x, y, w, h;          /* +0x04 .. +0x10: the HVisual's rectangle (0x436670) */
    const uint8_t *file;         /* +0x14: the .fsh */
    const uint8_t *shape;        /* +0x18: the image */
    float corner[16];            /* +0x1c: four corners x y z w */
    uint32_t colour[4];          /* +0x5c: white unless set (0x423d90) */
    float uv[4][8];              /* +0x6c */
    uint8_t flipy, flipx;        /* +0xec, +0xed */
    uint8_t coloured;            /* +0xee */
    uint8_t now;                 /* +0xf4: drawn at once rather than queued */
    Texture *texture;            /* +0xf8 */
    int32_t bucket;              /* +0x100 */
    int32_t size;                /* +0x104: the page's side */
    uint8_t centred;             /* +0x108 */
    uint8_t inset;               /* +0x109: half a texel in from the mirrored edges */
} HudArt;

extern const char *g_hud_dir;   /* 0x65b310: GameData\DashHud\ */

/* Image `index` of DashHud\<name>.fsh at (x, y), w x h. single: the page holds that image only. 0x422e10 */
HudArt *HudArt_load(const char *name, int index, int x, int y, int w, int h, bool now, bool centred, bool single);
void HudArt_free(HudArt *a);
void HudArt_place(HudArt *a, int x, int y);                         /* 0x423b10 */
void HudArt_setuv(HudArt *a, float u, float v, float du, float dv); /* 0x423910 */
void HudArt_setcolours(HudArt *a, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3);   /* 0x423d90 */
void HudArt_setcorners(HudArt *a, const float xyz[12]);             /* 0x423c20: four corners x y z */
void HudArt_draw(HudArt *a);
/* The art's page drawn again from one image: a size x size window of the depth, black, the image at its
   corner (its centre set to none, in the image), into the art's texture. 0x423e00 */
void HudArt_redraw(HudArt *a, uint8_t *shape, int size, int depth);                                        /* 0x423a30 */
