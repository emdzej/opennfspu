/* The race HUD (HUD_Init 0x424420, HUD_Draw 0x425910): a list of visuals (HOverlay and its classes, vtable
   0x5b2d0c: +0x8 a flag, +0xc the state, 0 hidden .. 2 shown, 3 its own), drawn each frame in passes over
   the overlay camera with the HUD's 2D layer (render/hud2d.h): the front visuals once the race has
   started, the others always, then the flagged ones. The visuals are ported one by one; the first is
   the start's countdown (H321Go). */
#pragma once
#include "game/race.h"
#include "render/render.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct HudVisual HudVisual;
typedef struct {
    void (*destroy)(HudVisual *v);                /* slot 0 */
    void (*draw)(HudVisual *v);                   /* slot 1 */
    void (*reset)(HudVisual *v);                  /* slot 2: the layout again (0x426760) */
    void (*setstate)(HudVisual *v, int32_t s);    /* slot 5: 0x537790, the trays' 0x4343f0 */
    void (*setpos)(HudVisual *v, int x, int y);   /* slot 12 */
    int (*progress)(HudVisual *v);                /* slot 14: of the fade, 0..100 (0x434950) */
    bool (*done)(HudVisual *v);                   /* slot 3: a modal visual finished (it leaves the modal list) */
} HudVisualVtbl;

struct HudVisual {
    const HudVisualVtbl *vt;
    uint8_t flag;          /* +0x08 */
    int32_t state;         /* +0x0c (slots 5 and 6: 0x537790, 0x42a360) */
};

/* The HUD for the race (the visuals ported so far). 0x424420 */
void HUD_Init(void);
void HUD_Free(void);
/* The race over: the results and the other race-over screens in turn (0x426a40 with 1). */
void HUD_RaceOver(void);
/* Every visual hidden, the modal list emptied (the replay's start). 0x425740 */
void HUD_HideAll(void);
/* A time of ticks as mm:ss.hh, signed if asked. 0x435640 */
void HTime_Format(int t, char *out, bool sign);
/* The HUD over camera c (the overlay view). 0x425910 */
void HUD_Draw(RenderSystem *r, RCamera *c);
