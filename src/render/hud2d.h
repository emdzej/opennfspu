/* The HUD's 2D drawing (0x419d70-0x41a530, the HUD's view 0x4670c0, the text drawer 0x5367d0): the
   visuals queue quads (plain, or textured into twenty texture buckets), texts and lines during a pass, and
   the pass's end draws them (0x41a350): the quads through an EAGL material of coordinates, colours,
   texture coordinates, a texture and a render-state record, the texts through their font drawers.
   Coordinates are screen pixels (the view 0x4670c0 maps them). */
#pragma once
#include "eagl/eagl.h"
#include "real/font.h"
#include "real/texture.h"
#include "render/render.h"
#include <stdbool.h>
#include <stdint.h>

/* A font drawer (0x536770's object): the font, its texture states and its last depth. */
typedef struct {
    Font *font;              /* +0x04 */
    TextureStates states;    /* +0x08 */
    int32_t rs[6];           /* the render-state record it applies (0x537460) */
    float z;                 /* +0x28 */
} HudFont;

extern bool g_hud_opaque;    /* 0x60c820: every queued colour fully opaque */

void Hud2D_Init(void);                                                        /* 0x419d70 */
/* An untextured quad: four corners (x, y; 4 floats a corner, the last two ignored) and their colours. 0x419fb0 */
void Hud2D_Quad(const float corners[16], const uint32_t colours[4]);
/* A quad of texture t in bucket b (0..19, a texture each a pass): four corners (x, y, z, w), colours,
   texture coordinates. 0x41a1a0 */
void Hud2D_TexQuad(int b, Texture *t, const uint32_t colours[4], const float corners[16], const float uv[8]);
/* A text: colour, position, its drawer and scale. 0x419e30 */
void Hud2D_Text(const char *s, uint32_t colour, float x, float y, HudFont *f, float sx, float sy);
/* A line (two points x y z w, two colours). 0x419eb0 */
void Hud2D_Line(const float points[8], const uint32_t colours[2]);
/* Drawn at once rather than queued: a line (0x465f10), n vertices of lines in one colour (0x465ea0), a
   flat quad (0x465f60), a flat triangle (0x465fc0), a textured quad (an art's own material, 0x59ee20). */
void Hud2D_LineNow(const float points[8], const uint32_t colours[2]);
void Hud2D_LinesNow(int n, const float *points, uint32_t colour);
void Hud2D_QuadNow(const float corners[16], uint32_t colour);
void Hud2D_TriNow(const float corners[12], uint32_t colour);
void Hud2D_TexQuadNow(Texture *t, const uint32_t colours[4], const float corners[16], const float uv[8]);
/* The pass's queued quads, texts and lines drawn, the queues emptied. 0x41a350 */
void Hud2D_Flush(void);
/* The render system's current camera as the HUD's: orthographic over the screen's pixels. 0x4670c0 */
void Hud2D_View(RenderSystem *r);

HudFont *HudFont_new(Font *f);
/* Text at (x, y, z) projected through the view (clipped to its viewport). 0x5367d0 */
void HudFont_print(HudFont *h, const EaglView *v, float x, float y, float z, const char *s);
