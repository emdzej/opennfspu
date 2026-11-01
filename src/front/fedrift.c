/* The front end's drifting decorations: see fedrift.h. */
#include "front/fedrift.h"
#include "app.h"
#include "front/fe2d.h"
#include "front/fe_parts.h"
#include "real/font.h"
#include "render/render.h"
#include "support/crt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TIMER_ticks (0x53c050), 128 a second, on the port's clock (an int in the original's sums). */
static int32_t ticks(void) { return (int32_t)(uint32_t)((uint64_t)app_ticks_ms() * 128u / 1000u); }

/* The plain look (0x4e2dc0: the display can't modulate alpha) draws none of them. */
static bool plain(void) { return g_render_modalpha == 0; }

/* (0x4e2da0: the rooms' detail off, 0x5d9d38 0; the port's display has it on) */
static bool low_detail(void) { return false; }

static uint32_t fade(uint32_t c, int a) { return a == 0xff ? c : (uint32_t)(((c >> 24) * (uint32_t)a) / 0xff) << 24 | (c & 0xffffffu); }   /* 0x4d82a0 */

/* rand() % 2 and % 128 as the original writes them (a signed remainder; rand's 15 bits are never negative). */
static int rand_odd(void) { return CRT_rand() % 2; }

/* ---------------------------------------------------------------- the lines */

static struct { int x0, y0, x1, y1; bool across; } s_linebox;   /* 0x69ca70 .. 0x69ca7c, 0x69ca68 */

void DriftLine_Box(int x0, int y0, int x1, int y1, bool across)
{
    s_linebox.x0 = x0;
    s_linebox.y0 = y0;
    s_linebox.x1 = x1;
    s_linebox.y1 = y1;
    s_linebox.across = across;
}

void DriftLine_Init(DriftLine *l)
{
    memset(l, 0, sizeof *l);
    if (!s_linebox.across) {   /* upright: sliding along x, from the top down */
        l->min = s_linebox.x0;
        l->max = s_linebox.x1;
        l->start = s_linebox.y0;
        l->len = s_linebox.y1 - s_linebox.y0;
        l->from = s_linebox.y0;
        l->to = s_linebox.y1;
    } else {
        l->min = s_linebox.y0;
        l->max = s_linebox.y1;
        l->across = true;
        l->start = s_linebox.x0;
        l->len = s_linebox.x1 - s_linebox.x0;
        l->from = s_linebox.x0;
        l->to = s_linebox.x1;
    }
    l->pos = CRT_rand() % (l->max - l->min) + l->min;
    l->div = CRT_rand() % 5 + 5;
    l->last = ticks();
    l->glint = false;
    l->up = rand_odd() == 1;
    l->gtick = ticks();
    l->gwait = CRT_rand() % 128;
}

/* A pixel at least each draw (whatever the ticks since), turning at the range's ends; the glint after its
   wait (up to 127 ticks) starts past one end, a fifth of the line long, 3 .. 5 pixels a tick, and once past
   the other end waits again. The line in the colour; the glint white from nothing to 0xdd (by the colour's
   alpha) towards where it runs, cut to the box. */
void DriftLine_Draw(DriftLine *l, uint32_t colour)
{
    if (plain()) return;
    int a = (int)(colour >> 24);
    int step = (ticks() - l->last) / l->div;
    if (step == 0) step = 1;
    l->last = ticks();
    if (!l->up) step = -step;
    l->pos += step;
    if (l->pos < l->min) {
        l->pos = l->min;
        l->up = true;
    }
    if (l->max - 1 < l->pos) {
        l->pos = l->max - 1;
        l->up = false;
    }
    if (!l->glint && l->gwait + l->gtick < ticks()) {
        l->glint = true;
        l->gtick = ticks();
        l->glen = (l->to - l->from) / 5;
        l->gspeed = CRT_rand() % 3 + 3;
        if (rand_odd() == 1) {
            l->gdir = 1;
            l->gpos = l->from - l->glen;
        } else {
            l->gdir = -1;
            l->gpos = l->to + l->glen;
        }
    }
    uint32_t lit = colour, dark = colour;
    if (l->glint) {
        lit = fade(0xddffffffu, a);
        dark = 0xffffffu;
        l->gpos += l->gspeed * (ticks() - l->gtick) * l->gdir;
        l->gtick = ticks();
        if ((l->gdir == 1 && l->to + l->glen < l->gpos) || (l->gdir == -1 && l->gpos < l->from - l->glen)) {
            l->glint = false;
            l->gtick = ticks();
            l->gwait = CRT_rand() % 128;
        }
    }
    if (l->across) {
        FE2D_Rect((float)l->start, (float)l->pos, (float)l->len, 1.0f, colour);   /* 0x4d8d30 */
        if (!l->glint) return;
        if (l->gdir == 1)
            FE2D_GouraudClipped((float)l->gpos, (float)l->pos, (float)l->glen, 1.0f, dark, lit, dark, lit, l->from,
                                l->min, l->to, l->max);   /* 0x4d8f50 */
        else
            FE2D_GouraudClipped((float)l->gpos, (float)l->pos, (float)l->glen, 1.0f, lit, dark, lit, dark, l->from,
                                l->min, l->to, l->max);
    } else {
        FE2D_Rect((float)l->pos, (float)l->start, 1.0f, (float)l->len, colour);
        if (!l->glint) return;
        if (l->gdir == 1)
            FE2D_GouraudClipped((float)l->pos, (float)l->gpos, 1.0f, (float)l->glen, dark, dark, lit, lit, l->min,
                                l->from, l->max, l->to);
        else
            FE2D_GouraudClipped((float)l->pos, (float)l->gpos, 1.0f, (float)l->glen, lit, lit, dark, dark, l->min,
                                l->from, l->max, l->to);
    }
}

/* ---------------------------------------------------------------- the crossings */

static char s_rings_path[200];      /* 0x69ca00 */
static FeImage *s_rings[5];         /* 0x69ca64 */
static int s_rings_refs;            /* 0x69ca80 */

void DriftCross_Init(DriftCross *c)
{
    memset(c, 0, sizeof *c);
    if (low_detail()) return;
    if (s_rings_refs == 0) {
        FE_ResolvePath("circles.fsh", s_rings_path, sizeof s_rings_path);   /* ("%scircles.fsh" 0x5dd7c8) */
        FEShapes_Load(s_rings_path);
        for (int i = 0; i < 5; i++) s_rings[i] = FEShapes_Image(s_rings_path, i);   /* 0x4d74e0 */
    }
    s_rings_refs++;
    c->on = false;
    c->since = ticks();
    c->wait = CRT_rand() % 512;
}

void DriftCross_Free(DriftCross *c)
{
    (void)c;
    if (low_detail() || s_rings_refs <= 0) return;
    if (--s_rings_refs == 0) {
        FEShapes_Release(s_rings_path);   /* 0x4d7270 */
        memset(s_rings, 0, sizeof s_rings);
    }
}

/* After its wait (up to 511 ticks) a random line of each set; the ring's frame by the ticks since (16
   each), centred where they cross; after the fifth a new wait. */
void DriftCross_Draw(DriftCross *c, uint32_t colour, const DriftLine *a, int na, const DriftLine *b, int nb)
{
    if (plain() || low_detail()) return;
    if (!c->on) {
        if (c->wait < ticks() - c->since) {
            c->on = true;
            c->frame = 0;
            c->a = a + CRT_rand() % na;
            c->b = b + CRT_rand() % nb;
            c->since = ticks();
        }
        if (!c->on) return;
    }
    c->frame = (ticks() - c->since) / 16;
    if (c->frame < 5) {
        const FeImage *im = s_rings[c->frame];
        if (im)
            FE2D_Image(im, (float)(c->b->pos - im->w / 2), (float)(c->a->pos - im->h / 2), (float)im->w, (float)im->h,
                       colour, 0, false);   /* 0x4d95a0 */
    } else {
        c->on = false;
        c->since = ticks();
        c->wait = CRT_rand() % 512;
    }
}

/* ---------------------------------------------------------------- the words */

static int32_t s_wordbox[4];   /* 0x69c9f0 .. 0x69c9fc */

void DriftWord_Box(int x0, int y0, int x1, int y1)
{
    s_wordbox[0] = x0;
    s_wordbox[1] = y0;
    s_wordbox[2] = x1;
    s_wordbox[3] = y1;
}

void DriftWord_Init(DriftWord *w)
{
    memset(w, 0, sizeof *w);
    w->since = ticks();
    w->wait = CRT_rand() % 0x180;
    memcpy(w->box, s_wordbox, sizeof w->box);
}

/* After its wait (up to 383 ticks): a font, a life of 256 .. 511 ticks, the next wait, an alignment, across
   or down, and a place: x in the box, y the box's top plus a random number masked (not divided) by the
   box's height, as the original does. For the first fifth of its life it fades in with its letters a pixel
   apart; then it starts at 80% and fades out while its letters spread to 50 pixels apart. */
void DriftWord_Draw(DriftWord *w, const char *text, uint32_t colour, HudFont *const *fonts, int nfonts)
{
    if (plain()) return;
    if (!w->on) {
        if (w->since + w->wait < ticks()) {
            w->font = fonts[CRT_rand() % nfonts];
            w->on = true;
            w->since = ticks();
            w->life = CRT_rand() % 256 + 0x100;
            w->wait = CRT_rand() % 0x180;
            w->align = CRT_rand() % 3;
            w->down = rand_odd() == 1;
            w->x = CRT_rand() % (w->box[2] - w->box[0]) + w->box[0];
            w->y = (CRT_rand() & (w->box[3] - w->box[1])) + w->box[1];
        }
        if (!w->on) return;
    }
    if (w->life + w->since < ticks()) {
        w->on = false;
        w->since = ticks();
        w->wait = CRT_rand() % 0x180;
        return;
    }
    double t = (double)(ticks() - w->since) / (double)w->life;
    int spacing = 1;
    float f;
    if (t < 0.2) {   /* 0x5b4998 */
        f = (float)t * 5.0f;
    } else {
        float u = ((float)t - 0.2f) * 1.25f;
        spacing = 1 - (int)(u * -49.0f);
        float v = u + 0.2f;
        f = 1.0f - (1.0f < v ? 1.0f : v);
    }
    int alpha = (int)(f * 255.0f);
    Drift_Text(w->font, w->x, w->y, fade(colour, alpha), text, spacing, w->align, w->down);
}

void Drift_Text(HudFont *f, int x, int y, uint32_t colour, const char *s, int spacing, int align, bool down)
{
    if (!f || !f->font || !s) return;
    int n = (int)strlen(s);
    float *adv = malloc((size_t)(n > 0 ? n : 1) * sizeof *adv);
    if (!adv) return;
    float total = 0;
    for (int i = 0; i < n; i++) {
        char ch[2] = {s[i], 0};
        float ex = 0, ey = 0, ew = 0, eh = 0;
        FONT_extent(f->font, ch, &ex, &ey, &ew, &eh);   /* 0x5366e0 */
        adv[i] = down ? eh : ew;
        total += adv[i];
    }
    total += (float)(n * spacing);
    int *at = down ? &y : &x;
    if (align == 1) *at = (int)((float)*at - total);
    else if (align == 2) *at = (int)((float)*at - total * 0.5f);   /* 0x5b23f0 */
    for (int i = 0; i < n; i++) {
        char *ch = FE2D_Alloc(2);   /* (the queue draws later) */
        if (ch) {
            ch[0] = s[i];
            ch[1] = 0;
            FE2D_Text(f, (float)x, (float)y, 0, colour, ch, 1.0f, 1.0f);   /* 0x4d9fe0 */
        }
        *at = (int)((float)spacing + adv[i] + (float)*at);
    }
    free(adv);
}
