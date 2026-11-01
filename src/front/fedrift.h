/* The front end's drifting decorations (0x51f1e0 .. 0x5218d0; no source file name): thin lines sliding to
   and fro across a box with a glint now and then running along them, rings opening where two of them
   cross (circles.fsh), and words fading in at random places of a box and spreading out as they fade.
   The menus' movie circle (FEMovieCircle, 0x4cd6c0 / 0x4cd980) draws all three over its movie; the
   showcase's animated pictures draw words. They move by the clock (TIMER_ticks) and pick by rand(). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "render/hud2d.h"

/* A line (0x48 bytes): across a box, sliding along one axis between the box's sides. */
typedef struct {
    int32_t pos, min, max;   /* +0x00 .. +0x08: its place and range on the axis it slides along */
    int32_t div;             /* +0x10: ticks a pixel (5 .. 9) */
    int32_t last;            /* +0x14 */
    bool up;                 /* +0x18: sliding towards max */
    bool across;             /* +0x19: a horizontal line (sliding up and down) */
    int32_t start, len;      /* +0x1c, +0x20: where it starts and its length, across */
    bool glint;              /* +0x24 */
    int32_t gpos, glen;      /* +0x28, +0x2c */
    int32_t gspeed, gdir;    /* +0x30, +0x34 */
    int32_t gtick, gwait;    /* +0x38, +0x3c */
    int32_t from, to;        /* +0x40, +0x44: the line's ends (the glint's way) */
} DriftLine;

/* A crossing (0x18 bytes): a ring opening, five frames of 16 ticks, where a line of each set crosses. */
typedef struct {
    bool on;                   /* +0x00 */
    int32_t frame;             /* +0x04 */
    const DriftLine *a, *b;    /* +0x08, +0x0c: the horizontal line and the vertical one */
    int32_t since, wait;       /* +0x10, +0x14 */
} DriftCross;

/* A word (0x38 bytes): one of the fonts, at a random place of its box, aligned left, right or centred,
   across or down. */
typedef struct {
    bool on;                   /* +0x00 */
    int32_t align;             /* +0x08 */
    bool down;                 /* +0x0c */
    int32_t life;              /* +0x10 */
    int32_t x, y;              /* +0x14, +0x18 */
    int32_t since, wait;       /* +0x1c, +0x20 */
    HudFont *font;             /* +0x24 */
    int32_t box[4];            /* +0x28: x0, y0, x1, y1 */
} DriftWord;

/* The box and kind the next lines are made for (0x51f1e0); a line (0x51f220); its draw (0x51f320). */
void DriftLine_Box(int x0, int y0, int x1, int y1, bool across);
void DriftLine_Init(DriftLine *l);
void DriftLine_Draw(DriftLine *l, uint32_t colour);

/* A crossing (0x51fef0: the rings' shapes loaded with the first), its release (0x51ff90), its draw over
   the two sets of lines (0x51ffc0). */
void DriftCross_Init(DriftCross *c);
void DriftCross_Free(DriftCross *c);
void DriftCross_Draw(DriftCross *c, uint32_t colour, const DriftLine *a, int na, const DriftLine *b, int nb);

/* The box the next words are made for (0x5200e0); a word (0x520110); its draw of text in one of the
   fonts (0x520160). */
void DriftWord_Box(int x0, int y0, int x1, int y1);
void DriftWord_Init(DriftWord *w);
void DriftWord_Draw(DriftWord *w, const char *text, uint32_t colour, HudFont *const *fonts, int nfonts);

/* A text a letter at a time from (x, y), the letters spacing apart, aligned left (0), right (1) or
   centred (2) on it, across or down. 0x5218d0 */
void Drift_Text(HudFont *f, int x, int y, uint32_t colour, const char *s, int spacing, int align, bool down);
