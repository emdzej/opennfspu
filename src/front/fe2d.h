/* The front end's 2D drawing (0x4d88c0 .. 0x4da7e0): quads (flat, gouraud, free, images) and texts go
   into one queue sorted by depth (farther first), then by their order among the translucent ones, then
   by texture; the flush (0x4da0a0) draws runs of up to 50 quads of one texture and opacity through one
   EAGL material, and the texts through their font drawers. Coordinates are screen pixels; each queued
   item takes the current depth (0x65b954), which the front end steps down as it draws its layers. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "real/texture.h"
#include "render/hud2d.h"

/* An image of a shape (the front end's: its texture and texture rectangle, its size). */
typedef struct {
    Texture *tex;          /* +0x00 */
    float u0, u1, v0, v1;  /* +0x04 */
    int32_t w, h;          /* +0x14 */
    int32_t cx, cy;        /* +0x1c: the shape's centre, negated */
    char name[0x6c];       /* +0x24 (0x90 bytes in all) */
} FeImage;

extern float g_fe_depth;   /* 0x65b954 */

void FE2D_Init(void);      /* 0x4d88c0 */
void FE2D_Free(void);      /* 0x4d8bc0 */
/* A flat rectangle (0x4d8d30) and its one-pixel frame (0x4d8cb0). */
void FE2D_Rect(float x, float y, float w, float h, uint32_t colour);
void FE2D_Frame(float x, float y, float w, float h, uint32_t colour);
/* A rectangle of four colours: top left, top right, bottom left, bottom right. 0x4d9160 */
void FE2D_Gouraud(float x, float y, float w, float h, uint32_t tl, uint32_t tr, uint32_t bl, uint32_t br);
/* A quad of four corners (x, y) and their colours, clockwise from the first. 0x4d9380 */
void FE2D_Quad(const float xy[8], const uint32_t colours[4]);
/* An image at (x, y) of size (w, h), its corners on whole pixels, flipped by flags (1 across, 2 down);
   opaque draws it without blending. 0x4d95e0 */
void FE2D_Image(const FeImage *img, float x, float y, float w, float h, uint32_t colour, uint32_t flags, bool opaque);
/* A text through a font drawer at (x, y), aligned by align (0 as is, 1 centred, 2 right), scaled; none
   without a colour. The string must live until the flush. 0x4d9e00 */
void FE2D_Text(HudFont *f, float x, float y, int align, uint32_t colour, const char *s, float sx, float sy);
/* The queue drawn and emptied. 0x4da0a0 */
void FE2D_Flush(void);
/* The screen cleared to black (and the z-buffer, when there is one) at the farthest depth. 0x4da340 */
void FE2D_Clear(void);
/* The depth one step nearer: 0.05 while above 0.75, 0.01 while above 0, else 0.001. 0x4da400 */
void FE2D_DepthStep(void);
/* A fixed depth for a while (0x4da460) and back (0x4da480); two steps nearer, kept (0x4da4c0). */
void FE2D_DepthPush(float z);
void FE2D_DepthPop(void);
void FE2D_DepthPushStep(void);
/* Drawing over what is there (0x4da570: the queue flushed, the z test 6, a step back) and its end
   (0x4da5b0). */
void FE2D_OverlayBegin(void);
void FE2D_OverlayEnd(void);
bool FE2D_InOverlay(void);   /* 0x65b990 */
/* n bytes of the frame's text arena (20000 bytes, emptied by the flush). 0x4da4a0 */
char *FE2D_Alloc(int n);
