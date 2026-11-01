/* The car's texture pages (render module 0x4426c0-0x447b80, R3DCar 0x439b90-0x443ce0).

   A car's materials don't use the shapes of its FSH files directly: R3DObject_LoadHeader composes texture
   pages, one per [tpageN.details] of the .tpg (width, height, bpp 1555 / 4444 / 32), from the shapes of
   the files ([fileN.details] tpage=N: the car's 'sf' files exterior.fsh, interior.fsh, ..., or one of the
   shared files with global=K). Each shape is drawn at its own position on the page if the car's options
   select it ([fileN.CODE]: geometry/texture/type, frontend, testdrive, night), recoloured with the car's
   colours where its palette's alpha says so ("tint"), and mirrored with an offset for mirror=1 files;
   licence plates and race numbers are text drawn on top. A page becomes a texture of the car's material
   set under (0, page index); a page can instead copy another page (sourcetpage) or share one of the two
   global pages (global=K: (0, 0x8000 + K - 1)). The materials then find their textures by those keys.

   The colours (18 slots, +0x1c of the car) come from the car's four chosen colours through a colour
   package (GameData\CarModel\packages.ini: [packageN] band0..band17 = which of the four). */
#pragma once
#include "real/texture.h"
#include "real/window.h"
#include "render/r3dcar.h"
#include <stdbool.h>
#include <stdint.h>

struct R3DMaterialSet;

/* A texture page (0x34 bytes). */
typedef struct CarPage {
    int32_t width, height;          /* +0x00, +0x04: the window's */
    int32_t texwidth, texheight;    /* +0x08, +0x0c: the texture's */
    Window *damage;                 /* +0x10: a CPU copy of the page for damage (damage=1) */
    uint8_t hasdamage;              /* +0x14 */
    uint8_t shared;                 /* +0x15: the texture belongs to another page */
    uint8_t pad16[2];
    Texture *texture;               /* +0x18 */
    int32_t bpp;                    /* +0x1c: 0x613 (1555) by default */
    int32_t texclass;               /* +0x20 */
    uint32_t name;                  /* +0x24: the texture's name in the bank (the page index) */
    Window *window;                 /* +0x28 */
    struct R3DMaterialSet *set;     /* +0x2c: the material set whose bank holds the texture */
    const TextureStates *states;    /* +0x30 */
} CarPage;

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(CarPage) == 0x34, "the page record of R3DObject_LoadHeader");
#endif

void CarPage_init(CarPage *p);      /* the template of 0x4426c0: zero, bpp 0x613, class 2 */
void CarPage_free(CarPage *p);      /* 0x447820 */
/* The page's texture from a window's shape (and its damage copy when damage). 0x447850 */
void CarPage_fromwindow(CarPage *p, Window *w, bool damage);
/* A window over the page's texture's own pixels (+0x28), its damage copy drawn in first when it has one,
   for drawing into the texture (NULL without a texture). 0x4479c0 */
Window *CarPage_Lock(CarPage *p);
/* The window let go and the texture sent again. 0x447a20 */
void CarPage_Unlock(CarPage *p);
/* The page shares a global page's texture (under the global page's name). 0x447a40 */
void CarPage_share(CarPage *p, const CarPage *global);
/* The page shares another page's texture under its own name. 0x447ac0 */
void CarPage_copy(CarPage *p, const CarPage *src, uint32_t name);

/* Draws a shape into a page window at its own position plus (dx, dy), recoloured with colours (18 slots)
   when tint, in the page's format (0x20, 0x613, 0x115c; other shapes and formats through movdfl). 0x444060 */
void R3DCar_BlitShape(Window *w, const uint8_t *shape, bool tint, int bpp, const uint32_t *colours, int dx, int dy);

/* The shared car art (once): carcmn.fsh into the global bank, the shared files (shadow, intglass, suit,
   head, cabrio), the two global pages, the colour packages; the licence and race-number fonts and the
   environment map pend. R3DCar_LoadCommon 0x443690 */
void R3DCar_LoadCommon(void);
void R3DCar_FreeCommon(void);       /* 0x443a80 */

/* The car's 18 colour slots from its specification's four colours through colour package scheme. 0x443ce0 */
void R3DCar_SetColours(R3DCar *car, uint32_t scheme);
/* Hue, saturation and value of a colour (bytes b, g, r as stored). 0x443bd0 */
void R3DCar_HSV(const uint8_t bgr[3], uint8_t hsv[3]);

/* R3DCar's virtuals for the page composition: */
/* slot 10: draws file `file`'s selected shapes into window w (and its mirror). R3DCar_LoadDetails 0x443e90 */
void R3DCar_LoadDetails(R3DCar *car, int file, Window *w, int bpp);
/* slot 11: whether shape `code` of file `file` is drawn for the car's options and the game. 0x439b90 */
bool R3DCar_UseShape(R3DCar *car, int file, uint32_t code);
/* slot 12: whether it's recoloured ("tint"). 0x439e10 */
bool R3DCar_TintShape(R3DCar *car, int file, uint32_t code);
/* The text a shape carries (licence, race number, flyer) drawn on the page. R3DCar_LoadTextures 0x4447b0 */
void R3DCar_LoadTextures(R3DCar *car, Window *w, const uint8_t *shape, int file, uint32_t code);

/* The texture pages of the .tpg, then the material set. R3DObject_LoadHeader 0x4426c0 */
void R3DObject_LoadHeader(R3DCar *car);
/* The pages' textures and damage windows (part of the object's unload 0x442320). */
void R3DCar_FreePages(R3DCar *car);
/* The shared textures of the materials ('SPC1'.. in the race, 'FEDU'.. in the front end) into the car's
   three slots (+0xdc), tiled. 0x440870 for each, from R3DCar_LoadCrp 0x441940 */
void R3DCar_GlobalTextures(R3DCar *car);

/* The shape of an FSH file by its 4-character name (the last with it), NULL if none. 0x53c3b0 */
const uint8_t *SHAPE_find(const uint8_t *fsh, uint32_t code);

/* Port additions for the tests: the colour packages and the shared state of LoadCommon. */
extern uint8_t *g_carschemes;       /* 0x6115f4: 18 bytes per package */
extern uint32_t g_carnschemes;      /* 0x6115f8 */
extern CarPage g_carpages[2];       /* 0x611610 */
extern void *g_carfiles[5];         /* 0x61160c */
