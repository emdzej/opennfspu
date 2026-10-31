/* Calls of the render system into modules that aren't ported yet (docs/internals/porting.md): each logs
   once (src/render/pending.c). */
#pragma once
#include "render/render.h"

void Render_PendingCameraUpdate(RCamera *c);   /* 0x461060 -> 0x464e40: the camera follows its target */
/* Port: the cars of the world draw, set by the game (0x43f610 draws them in the original). */
extern void (*g_render_cars)(RCamera *c);
void Render_PendingWorld(RCamera *c);          /* 0x468a20: the world, cars and objects of the view */
void Render_PendingAfterViews(void);           /* 0x44fd70 */
void Render_PendingParticles(void);            /* Particles_Tick, every 2 timer ticks (0x628148) */
void Render_PendingFrameSetup(void);           /* 0x44fb80, 0x44f070, 0x44eb50 */
void Render_PendingHud(RCamera *c);            /* HUD_Draw, 0x468be0, HUD_DrawOverlay */
