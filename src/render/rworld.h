/* The world draw of a view (rworld.c). */
#pragma once
#include "render/render.h"

/* Port: the cars queued for the world draw at the view's +0x11b (0x443540), set by the game. */
extern void (*g_render_cars)(RCamera *c);
void Render_World(RCamera *c);                 /* 0x468a20: the world, cars and objects of the view */
