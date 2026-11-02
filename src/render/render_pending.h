/* Calls of the render system into modules that aren't ported yet (docs/internals/porting.md): each logs
   once (src/render/pending.c). */
#pragma once
#include "render/render.h"

/* Port: the cars of the world draw, set by the game (0x43f610 draws them in the original). */
extern void (*g_render_cars)(RCamera *c);
void Render_PendingWorld(RCamera *c);          /* 0x468a20: the world, cars and objects of the view */
void Render_PendingAfterViews(void);           /* 0x44fd70 */
