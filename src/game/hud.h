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
    void (*destroy)(HudVisual *v);   /* slot 0 */
    void (*draw)(HudVisual *v);      /* slot 1 */
} HudVisualVtbl;

struct HudVisual {
    const HudVisualVtbl *vt;
    uint8_t flag;          /* +0x08 */
    int32_t state;         /* +0x0c (slots 5 and 6: 0x537790, 0x42a360) */
};

/* The HUD for the race (the visuals ported so far). 0x424420 */
void HUD_Init(void);
void HUD_Free(void);
/* The HUD over camera c (the overlay view). 0x425910 */
void HUD_Draw(RenderSystem *r, RCamera *c);
