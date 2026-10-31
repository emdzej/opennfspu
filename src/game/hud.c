/* The race HUD (see hud.h). */
#include "game/hud.h"
#include "exe.h"
#include "game/car.h"
#include "game/car_drive.h"
#include "game/hud_config.h"
#include "game/race_progress.h"
#include "eagl/eagl.h"
#include "pending.h"
#include "render/hud2d.h"
#include "render/hudart.h"
#include "render/r3dcar.h"
#include <math.h>
#include "real/font.h"
#include "real/loc.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The visual list (0x60c9f4): each visual, whether it is drawn only after the start, and the flag of the
   last pass. */
typedef struct { HudVisual *v; uint8_t front, last; } HudEntry;
static HudEntry *list;
static int nlist;
static bool hidden;          /* 0x60ca00 */
static bool always;          /* 0x60c970: draw the front visuals whatever their state */

static void add(HudVisual *v, bool front, bool last)
{
    HudEntry *n = realloc(list, sizeof *n * (size_t)(nlist + 1));
    if (!n) return;
    list = n;
    list[nlist++] = (HudEntry){v, front, last};
}

/* ---- the countdown (H321Go, 0x4f8 bytes; HUD_Countdown 0x417a80) ---- */

/* A keyframe of the countdown's digit (0x14 bytes from +0x204): its scale, centre, colour, and a byte
   the ring uses. */
typedef struct { float scale; int32_t x, y; uint32_t colour; int32_t ring; } Key;

typedef struct {
    HudVisual base;
    Key key[16];
    HudArt *digit[3];        /* +0x48c, +0x488, +0x484: 3, 2, 1 in their boxes */
    HudArt *centre[3];       /* +0x498, +0x494, +0x490: 3, 2, 1 at the centre */
    HudArt *full, *outline;  /* +0x49c, +0x4a0: the rings */
    int32_t w, h, cx, cy;    /* +0x4a4, +0x4a8, +0x4b4, +0x4b8 */
    uint8_t plain;           /* +0x4f4: no fading (the display can't blend so: 0x6197aa != 1) */
} H321Go;

static int32_t exe_i32(uint32_t va) { return (int32_t)exe_u32(va); }
static double exe_f64(uint32_t va)
{
    uint64_t u = (uint64_t)exe_u32(va) | (uint64_t)exe_u32(va + 4) << 32;
    double d;
    memcpy(&d, &u, 8);
    return d;
}
static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static void go_destroy(HudVisual *v)
{
    H321Go *g = (H321Go *)v;
    for (int i = 0; i < 3; i++) {
        HudArt_free(g->digit[i]);
        HudArt_free(g->centre[i]);
    }
    HudArt_free(g->full);
    HudArt_free(g->outline);
    free(g);
}

/* The digit of this step, keyframed (scale over its size about the key's centre), the last step's
   fading out, and the centre's. 0x418690 */
static void go_digits(H321Go *g, int step, int frame)
{
    if (step > 2) return;
    HudArt *d = g->digit[step];
    uint32_t c;
    if (d) {
        const Key *k = &g->key[frame];
        if (!g->plain && 0.0f < k->scale) {
            int32_t w = (int32_t)((float)exe_i32(0x5cc848 - 8u * (unsigned)step) * k->scale);
            int32_t h = (int32_t)((float)exe_i32(0x5cc84c - 8u * (unsigned)step) * k->scale);
            float r[12] = {(float)(k->x - w / 2), (float)(k->y - h / 2), 0, (float)(w / 2 + k->x), (float)(k->y - h / 2), 0,
                           (float)(w / 2 + k->x), (float)(h / 2 + k->y), 0, (float)(k->x - w / 2), (float)(h / 2 + k->y), 0};
            HudArt_setcorners(d, r);
            HudArt_setcolours(d, k->colour, k->colour, k->colour, k->colour);
            HudArt_draw(d);
        }
        if (0 < step && frame <= 4 && !g->plain) {
            HudArt *prev = g->digit[step - 1];
            HudArt_setcolours(prev, 0x80ffffff, 0x80ffffff, 0x80ffffff, 0x80ffffff);
            HudArt_draw(prev);
        }
    }
    c = g->plain ? 0xff000000u : (uint32_t)frame << 29 | 0xffffff;
    HudArt_setcolours(g->centre[step], c, c, c, c);
    HudArt_draw(g->centre[step]);
    if (0 < step && !g->plain) {
        int a = frame ? 0x100 / frame : 0;
        c = (uint32_t)a << 24 | 0xffffff;
        HudArt *prev = g->centre[step - 1];
        HudArt_setcolours(prev, c, c, c, c);
        HudArt_draw(prev);
    }
}

/* From tick 256 to 448 (the start): the rings, the cross through the centre, the digit of the step (3, 2,
   1, a step each 64 ticks), its frame (the tick within the step, in sixteenths). 0x418510 */
static void go_draw(HudVisual *v)
{
    H321Go *g = (H321Go *)v;
    if (g_race_mode == 6 || g_race_mode == 7 || g_race_mode == 8) return;
    PENDING_NOTE(0x426a20, "the HUD's pause check (0x426a20)");
    int32_t t = g_sim_tick;
    if (exe_i32(0x5b27d8) <= t && t <= exe_i32(0x5b27e4) && g_race_view == 0) {
        if (v->state != 3) v->state = 3;   /* 0x4343f0 */
        int step = exe_i32(0x5b27dc) <= t;
        if (exe_i32(0x5b27e0) <= t) step = 2;
        int sub = t % 64;   /* fmod(tick, 64) */
        int frame = (int)((float)((sub * 3 + 3) * 5) * 0.015625f);
        HudArt_setcolours(g->outline, 0x80fab400, 0x80fab400, 0x80fab400, 0x80fab400);
        HudArt_setcolours(g->full, 0x3ffab400, 0x3ffab400, 0x3ffab400, 0x3ffab400);
        HudArt_draw(g->full);
        HudArt_draw(g->outline);
        /* The cross (0x419c70). */
        float hl[8] = {0, (float)g->cy, 0, 1, (float)g->w, (float)g->cy, 0, 1};
        float vl[8] = {(float)g->cx, 0, 0, 1, (float)g->cx, (float)g->h, 0, 1};
        uint32_t lc[2] = {0x80000000u, 0x80000000u};
        Hud2D_Line(hl, lc);
        Hud2D_Line(vl, lc);
        go_digits(g, step, frame);
        PENDING_NOTE(0x418930, "the countdown's ring of segments (0x418930)");
        return;
    }
    if (v->state != 0) v->state = 0;
}

static const HudVisualVtbl go_vt = {go_destroy, go_draw, NULL, NULL, NULL, NULL};   /* 0x5b27ec */

static HudVisual *countdown_new(void)
{
    H321Go *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    g->base.vt = &go_vt;
    g->w = g_display.width;
    g->h = g_display.height;
    g->cx = g->w / 2;
    g->cy = g->h / 2;
    /* The digit's keyframes (all at the centre): none for five frames, then shrinking from eight times
       its size, and their colours. */
    static const float scale[16] = {0, 0, 0, 0, 0, 8, 7, 6, 5, 4, 3, 2, 1, 1, 1, 1};
    static const uint32_t colour[16] = {0x80ffffff, 0x80ffffff, 0x80ffffff, 0x80ffffff, 0x80ffffff, 0x3fffffff, 0x3fffffff,
                                        0x3fffffff, 0x3fffffff, 0x80ffffff, 0x80ffffff, 0x80ffffff, 0xbfffffff, 0xbfffffff,
                                        0x80ffffff, 0x80ffffff};
    static const int32_t ring[16] = {0xff, 0xff, 0x19, 0x80, 0x19, 0x80, 0x19, 0x80, 0x19, 0x80, 0x19, 0x80, 0x19, 0x80, 0x19, 0xff};
    for (int f = 0; f < 16; f++) g->key[f] = (Key){scale[f], g->cx, g->cy, colour[f], ring[f]};
    /* The digits' boxes: 130 pixels square at the centre, each digit centred in it. */
    int bx = (g->w - 0x82) / 2, by = (g->h - 0x82) / 2;
    int s = exe_i32(0x5b27c0), sy = exe_i32(0x5b27c4);
    g->full = HudArt_load("circlefull", 0, g->cx, g->cy, exe_i32(0x5b27c8), exe_i32(0x5b27cc), false, true, false);
    g->outline = HudArt_load("circleoutline", 0, g->cx, g->cy, exe_i32(0x5b27c8), exe_i32(0x5b27cc), false, true, false);
    static const char *files[3] = {"321go", "321go", "321go2"};
    static const int index[3] = {1, 0, 0};
    for (int i = 0; i < 3; i++) {
        uint32_t sz = 0x5cc848 - 8u * (unsigned)i;
        int w = exe_i32(sz), h = exe_i32(sz + 4);
        g->digit[i] = HudArt_load(files[i], index[i], (s - w) / 2 + bx, (sy - h) / 2 + by, w, h, false, false, false);
    }
    static const char *cfiles[3] = {"321go", "321go", "321go2"};
    static const int cindex[3] = {2, 3, 1};
    static const uint32_t csize[3] = {0x5cc860, 0x5cc858, 0x5cc850};
    for (int i = 0; i < 3; i++)
        g->centre[i] = HudArt_load(cfiles[i], cindex[i], g->cx, g->cy, exe_i32(csize[i]), exe_i32(csize[i] + 4), false, true, false);
    g->plain = 1;   /* (0x6197aa, a display capability the port's options don't set: != 1) */
    return &g->base;
}

/* ---- the trays (HTray, vtable 0x5b3318; 0x433ef0): a visual's dark panel, sliding in from the nearest
   side of the screen (or fading) when it shows ---- */

typedef struct {
    HudVisual base;
    int32_t w, h;              /* +0x10, +0x14 */
    int32_t drag, dx, dy;      /* +0x18 .. +0x20: the HUD editor's drag (0x434450) */
    int32_t x, y;              /* +0x24, +0x28 */
    int32_t homex, homey;      /* +0x2c, +0x30 */
    float corner[16];          /* +0x34: four corners x y z w */
    float line[8];             /* +0x74: the divider, two points */
    uint32_t linec[2];         /* +0x94 */
    int32_t lineoff;           /* +0x9c: the divider's height in the tray (none if 0) */
    uint8_t onlyslide;         /* +0xa0: drawn only */
    int32_t since;             /* +0xb4: the tick of the last state */
    uint8_t sliding;           /* +0xb8 */
    int32_t fade;              /* +0xbc: 0 slides, 1 fades */
    uint8_t whileslide;        /* +0xc0: ... while sliding */
    uint8_t shade;             /* +0xc1 */
} HTray;

/* The corners and the divider at (x, y). 0x434270 */
static void tray_corners(HTray *t, int x, int y)
{
    float fx = (float)x, fy = (float)y, x1 = (float)(t->w + x), y1 = (float)(t->h + y);
    float c[16] = {fx, fy, 0, 1, x1, fy, 0, 1, x1, y1, 0, 1, fx, y1, 0, 1};
    memcpy(t->corner, c, sizeof c);
    t->x = x, t->y = y;
    float ly = (float)(t->lineoff + y);
    float l[8] = {fx, ly, 0, 1, (float)(t->w + x), ly, 0, 1};
    memcpy(t->line, l, sizeof l);
}

/* 0x434000 */
static void tray_setsize(HTray *t, int w, int h, int lineoff)
{
    t->w = w, t->h = h;
    t->lineoff = lineoff;
    float ly = (float)(t->y + lineoff);
    float l[8] = {(float)t->x, ly, 0, 1, (float)(w + t->x), ly, 0, 1};
    memcpy(t->line, l, sizeof l);
}

/* Its home (unless it is sliding: then only the corners move). 0x434240 */
static void tray_place(HTray *t, int x, int y)
{
    if (!t->sliding) t->homex = x, t->homey = y;
    tray_corners(t, x, y);
    t->sliding = 0;
}

static void tray_colours(HTray *t)   /* 0x434550 */
{
    uint32_t c = HudConfig_scheme();
    t->linec[0] = (c & 0xffffff) | 0x3f000000u;
    t->linec[1] = (HudConfig_scheme() & 0xffffff) | 0x3f000000u;
}

static void tray_init(HTray *t, const HudVisualVtbl *vt, int x, int y, int w, int h, int lineoff, bool onlyslide, int fade,
                      bool whileslide, bool shade)
{
    t->base.vt = vt;   /* (HOverlay 0x42a2c0: the flag clear, state 0) */
    t->w = w, t->h = h;
    t->lineoff = lineoff;
    t->onlyslide = onlyslide;
    t->x = x, t->y = y;
    t->fade = fade;
    t->whileslide = whileslide;
    t->shade = shade;
    tray_colours(t);
    tray_place(t, x, y);
}

static void tray_setstate(HudVisual *v, int32_t s)   /* 0x4343f0 */
{
    v->state = s;
    ((HTray *)v)->since = g_sim_tick;   /* (0x606ac8, the race loop's tick: the same count) */
}

static int tray_progress(HudVisual *v)   /* 0x434950 */
{
    if (v->state != 1 && v->state != 2) return 100;
    int p = (g_sim_tick - ((HTray *)v)->since) * 100 / (((HTray *)v)->fade ? 0x30 : 0xb);
    return p < 0x65 ? p : 100;
}

/* The screen's part its home is in: 0 .. 2 left, middle, right in the upper half, 3 .. 5 in the lower.
   0x434590 */
static int tray_side(int x, int y, int w)
{
    bool lower = g_display.height / 2 <= y;
    if (g_display.width / 2 <= x) return lower ? 5 : 2;
    if (g_display.width / 2 < w + x) return lower ? 4 : 1;
    return lower ? 3 : 0;
}

/* Its place sliding in (state 1, over eleven ticks from its side; at home it settles, 3) or out (2, to off
   the screen; then hidden, 0). The middle ones slide from the top or the bottom; those at the bottom
   settle at once (the test is the wrong way round). 0x434600 */
static void tray_slide(HTray *t, int state, int32_t out[2])
{
    int side = tray_side(t->homex, t->homey, t->w);
    int e = g_sim_tick - t->since, W = g_display.width, H = g_display.height;
    int x = t->homex, y = t->homey;
    if (state == 1) {
        if (side == 0 || side == 3) {
            x = e * (t->homex + t->w) / 0xb - t->w;
            if (t->homex <= x) t->base.state = 3, x = t->homex;
        }
        if (side == 2 || side == 5) {
            x = W - e * (W - t->homex) / 0xb;
            if (x <= t->homex) t->base.state = 3, x = t->homex;
        }
        if (side == 1) y = e * (t->homey + t->h) / 0xb - t->h;
        else if (side == 4) y = H - e * (H - t->homey) / 0xb;
        else goto done;
        x = t->homex;
        if (t->homey <= y) {
            t->base.state = 3;
            y = t->homey;
        }
    } else if (state == 2) {
        if (side == 0 || side == 3) {
            x = t->homex - e * (t->homex + t->w) / 0xb;
            if (x <= -t->w) t->base.state = 0, x = -t->w;
        }
        if (side == 2 || side == 5) {
            x = e * (W - t->homex) / 0xb + t->homex;
            if (W <= x) t->base.state = 0, x = W;
        }
        if (side == 1) {
            x = t->homex;
            y = t->homey - e * (t->homey + t->h) / 0xb;
            if (y <= -t->h) t->base.state = 0, y = -t->h;
        } else if (side == 4) {
            x = t->homex;
            y = e * ((t->h - t->homey) + H) / 0xb + t->homey;
            if (H <= y) t->base.state = 0, y = H;
        }
    }
done:
    out[0] = x;
    out[1] = y;
}

/* Its place (sliding, or at home), then the panel: black, the top corners a quarter opaque, the bottom
   ones fully, scaled by the fade; the divider once it has settled. 0x434070 */
static void tray_draw(HTray *t)
{
    HudVisual *v = &t->base;
    if ((v->state == 1 || v->state == 2) && t->fade == 0) {
        int32_t p[2];
        tray_slide(t, v->state, p);
        t->sliding = 1;
        v->vt->setpos(v, p[0], p[1]);
    } else {
        v->vt->setpos(v, t->homex, t->homey);
        tray_corners(t, t->homex, t->homey);
    }
    if (t->onlyslide && (!t->whileslide || (v->state != 1 && v->state != 2))) return;
    if (HudConfig_notrays()) return;
    int a = 100;
    if (t->fade == 1) {
        if (v->state == 1) a = v->vt->progress(v);
        if (v->state == 2) a = 100 - v->vt->progress(v);
    }
    uint32_t c[4];
    for (int k = 0; k < 4; k++)
        c[k] = g_hud_opaque ? exe_u32(0x5b32f4 + 4u * (unsigned)k)
                            : ((uint32_t)exe_u32(0x5b3304 + 4u * (unsigned)k) * (uint32_t)a / 100u) << 24 | exe_u32(0x5b32e4 + 4u * (unsigned)k);
    Hud2D_Quad(t->corner, c);
    if (0 < t->lineoff && v->state == 3) Hud2D_Line(t->line, t->linec);
    if (t->shade) PENDING_NOTE(0x435d00, "the trays' shade (0x435d00)");
}

/* ---- the texts (HTextVisual, 0x34 bytes, vtable 0x5b3184; 0x430e50) ---- */

/* A font of the HUD's, opened once by name (the map 0x60cb68): the file, the font and its drawer. */
typedef struct TextFont {
    struct TextFont *next;
    char name[32];
    uint8_t *file;
    Font *font;
    HudFont *drawer;
} TextFont;
static TextFont *fonts;
static int ntexts;   /* 0x60cb6c */

typedef struct {
    int32_t x, y, w, h;       /* +0x04 .. +0x10 (HVisual 0x436670) */
    TextFont *font;           /* +0x14 */
    int32_t align;            /* +0x18: 1 right (x its right edge) */
    uint32_t colour, colour2; /* +0x24, +0x28 */
    float width;              /* +0x2c: the text's, when right aligned */
    uint8_t keep;             /* +0x30: measured once */
} HText;

static TextFont *text_font(const char *name)
{
    for (TextFont *f = fonts; f; f = f->next)
        if (!strcmp(f->name, name)) return f;
    TextFont *f = calloc(1, sizeof *f);
    if (!f) return NULL;
    snprintf(f->name, sizeof f->name, "%s", name);
    char path[300];
    snprintf(path, sizeof path, "%s%s", g_hud_dir, name);
    size_t n = 0;
    f->file = vfs_read_all(path, &n);
    if (f->file) f->font = FONT_open(f->file);       /* 0x536540: the drawer over the font */
    if (f->font) f->drawer = HudFont_new(f->font);
    f->next = fonts;
    fonts = f;
    return f;
}

static HText *text_new(const char *font, uint32_t colour, uint32_t colour2, int x, int y, int w, int h, int align, bool keep)
{
    HText *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->x = x, t->y = y, t->w = w, t->h = h;
    t->align = align;
    t->colour = colour, t->colour2 = colour2;
    t->keep = keep;
    ntexts++;
    t->font = text_font(font);
    return t;
}

static void text_free(HText *t)   /* 0x4310f0 */
{
    if (!t) return;
    free(t);
    if (--ntexts == 0)
        while (fonts) {
            TextFont *f = fonts;
            fonts = f->next;
            free(f->drawer);
            FONT_close(f->font);
            free(f->file);
            free(f);
        }
}

/* s at its place (right aligned: ending there), queued in the HUD's 2D layer. 0x431230 */
static void text_print(HText *t, const char *s, float sx, float sy)
{
    if (!t || !t->font || !t->font->font) return;
    int x = t->x, y = t->y;
    if (t->align == 1) {
        if (t->width == 0.0f || !t->keep) {
            float a, b, h;
            FONT_extent(t->font->font, s, &a, &b, &t->width, &h);
        }
        x = (int)((float)x - t->width);
    }
    /* (drawn at once instead with the second argument: the HUD editor's, not ported) */
    Hud2D_Text(s, t->colour, (float)x, (float)y, t->font->drawer, sx, sy);
}

/* s's extent, the height the alphabet's (0x5cd0d4). 0x431360 */
static void text_extent(HText *t, const char *s, float *x, float *y, float *w, float *h)
{
    float d;
    FONT_extent(t->font->font, s, x, y, w, h);
    FONT_extent(t->font->font, exe_str(0x5cd0d4), &d, &d, &d, h);
}

/* ---- the label-and-value trays (HComboTextVisual, vtable 0x5b297c; 0x41c760) ---- */

typedef struct {
    HTray tray;
    HText *label;    /* +0xc4: at the bottom left */
    HText *value;    /* +0xc8: right aligned at the top right */
    int place;       /* +0xcc, +0xd0: its setter and getter of the places' table */
} HCombo;

/* At (x, y), through the places' table (written, read back), its texts inset by the layout's. 0x41c930 */
static void combo_setpos(HudVisual *v, int x, int y)
{
    HCombo *c = (HCombo *)v;
    int32_t px, py;
    HudConfig_setpos(c->place, x, y);
    HudConfig_pos(c->place, &px, &py);
    tray_place(&c->tray, px, py);
    int w = c->tray.w, h = c->tray.h;
    if (c->value) c->value->x = px + (w - HudConfig_i32(0x34)), c->value->y = py + HudConfig_i32(0x38);
    if (c->label && c->label->font && c->label->font->font) {
        float a, b, d, th;
        text_extent(c->label, exe_str(0x5ccad0), &a, &b, &d, &th);   /* "0" */
        c->label->x = px + HudConfig_i32(0x34);
        c->label->y = py + h - (int)((float)HudConfig_i32(0x38) + th);
    }
}

static void combo_destroy(HudVisual *v)   /* 0x41c8a0 */
{
    HCombo *c = (HCombo *)v;
    text_free(c->value);
    text_free(c->label);
    free(c);
}

/* The tray and both texts, unless hidden. 0x41c8e0 */
static void combo_draw(HCombo *c, const char *label, const char *value)
{
    if (c->tray.base.state == 0) return;   /* 0x42a310 */
    tray_draw(&c->tray);
    text_print(c->label, label, 1.0f, 1.0f);
    text_print(c->value, value, 1.0f, 1.0f);
}

static void combo_init(HCombo *c, const HudVisualVtbl *vt, int x, int y, int w, int h, int place, bool keep)
{
    tray_init(&c->tray, vt, x, y, w, h, h / 2, false, 0, false, false);
    c->place = place;
    c->value = text_new(HudConfig_font(0x4c), HudConfig_scheme(), (uint32_t)HudConfig_i32(0x14), 0, 0, w, h / 2, 1, keep);
    c->label = text_new(HudConfig_font(0x4c), (uint32_t)HudConfig_i32(0x18), (uint32_t)HudConfig_i32(0x14), 0, 0, w, h / 2, 0, false);
    combo_setpos(&c->tray.base, x, y);
}

static Car *local_car(void) { return g_race_local < g_racers.n ? g_racers.c[g_race_local] : NULL; }

/* ---- the gear (HGear, 0xd4 bytes, vtable 0x5b2b44; 0x421c60) ---- */

/* "GEAR" (hud.loc 10) and the gear: R and N (0x46e1, 0x46e2), the digits' strings (0x5ccc10) from first.
   Shown by the second layout's +0x64. 0x421cd0 */
static void gear_draw(HudVisual *v)
{
    if (HudConfig_choicei32(0x64) != 1 || v->state == 0) return;   /* 0x41e920, 0x42a310 */
    Car *car = local_car();
    if (!car) return;
    const uint8_t *loc = HudConfig_loc();
    int g = (int8_t)car->b[0xd82];
    const char *s;
    if (g == 0) s = LOC_string(loc, 0x46e1);
    else if (2 <= g && g <= 8) s = exe_str(exe_u32(0x5ccc10 + 4u * (unsigned)(g - 2)));
    else s = LOC_string(loc, 0x46e2);
    const char *label = LOC_string(loc, 10);
    combo_draw((HCombo *)v, label ? label : "", s ? s : "");
}

/* The texts' colours again. 0x41ca30 */
static void combo_reset(HCombo *c)
{
    tray_colours(&c->tray);
    if (c->value) c->value->colour = HudConfig_scheme(), c->value->colour2 = (uint32_t)HudConfig_i32(0x14);
    if (c->label) c->label->colour = (uint32_t)HudConfig_i32(0x18), c->label->colour2 = (uint32_t)HudConfig_i32(0x14);
}

/* Its size (0x41ca10: the divider halfway), place and colours again. 0x421db0 */
static void gear_reset(HudVisual *v)
{
    HCombo *c = (HCombo *)v;
    tray_setsize(&c->tray, HudConfig_i32(0x94), HudConfig_i32(0x98), HudConfig_i32(0x98) / 2);
    int32_t x, y;
    HudConfig_pos(HUDPOS_GEAR, &x, &y);
    combo_setpos(v, x, y);
    combo_reset(c);
}

static const HudVisualVtbl gear_vt = {combo_destroy, gear_draw, gear_reset, tray_setstate, combo_setpos, tray_progress};

static HudVisual *gear_new(void)
{
    HCombo *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    int32_t x, y;
    HudConfig_pos(HUDPOS_GEAR, &x, &y);
    combo_init(c, &gear_vt, x, y, HudConfig_i32(0x94), HudConfig_i32(0x98), HUDPOS_GEAR, false);
    return &c->tray.base;
}

/* ---- the speed and the revs in figures (HSpeed 0x42e490, 0xd8 bytes, vtable 0x5b2fd8; HTachDigital
   0x42f660, 0xdc bytes, vtable 0x5b30e0) ---- */

typedef struct {
    HCombo combo;
    char text[8];   /* +0xd4 */
} HFigures;

/* n in decimal, zero-padded to `digits` (the pads 0x5ccad0 "0", 0x5ccef4 "00", 0x5ccef0 "000"); the
   zeros alone below `least`. */
static void figures(char *out, int n, int digits)
{
    static const uint32_t pad[4] = {0, 0x5ccad0, 0x5ccef4, 0x5ccef0};
    int off = 0;
    for (int p = digits - 1, lim = 10; p > 0; p--, lim *= 10)
        if (n < lim) {
            off = p;
            break;
        }
    if (off) memcpy(out, exe_str(pad[off]), (size_t)off + 1);
    snprintf(out + off, 8 - (size_t)off, "%d", n);   /* _itoa 0x5a175c */
}

/* The speed in three figures, km/h (+0xec4 m/s x 3.6, 0x5b3018) or mph (x 0x5b3014); "000" below 1 m/s
   (0x5ccef0). Shown by the second layout's +0x30 1. 0x42e4d0 */
static void speed_draw(HudVisual *v)
{
    HFigures *f = (HFigures *)v;
    Car *car = local_car();
    if (HudConfig_choicei32(0x30) != 1 || v->state == 0 || !car) return;   /* 0x41e940 */
    float ms = *CAR_F(car, 0xec4);
    int n = g_hud_imperial ? (int)(ms * exe_f32(0x5b3014)) : (int)(ms * exe_f32(0x5b3018));
    int label = g_hud_imperial ? 0x14 : 0xb;
    /* (the pads chosen by the speed before the slow test: 10 and 100) */
    int off = n < 10 ? 2 : n < 100 ? 1 : 0;
    if (off) memcpy(f->text, exe_str(off == 2 ? 0x5ccef4 : 0x5ccad0), (size_t)off + 1);
    if (ms < 1.0f) memcpy(f->text, exe_str(0x5ccef0), 4);   /* (0x5b24a8) */
    else snprintf(f->text + off, sizeof f->text - (size_t)off, "%d", n);
    const char *s = LOC_string(HudConfig_loc(), label);
    combo_draw(&f->combo, s ? s : "", f->text);
}

/* The revs (+0xdb4) in four figures, "0000" below 1 (0x5cd05c); "rpm" (0x46e0). Shown by +0x38 1.
   0x42f6c0 */
static void tachdigital_draw(HudVisual *v)
{
    HFigures *f = (HFigures *)v;
    Car *car = local_car();
    if (HudConfig_choicei32(0x38) != 1 || v->state == 0 || !car) return;   /* 0x41e9a0 */
    int n = (int)*CAR_F(car, 0xdb4);
    if (n < 1) memcpy(f->text, exe_str(0x5cd05c), 5);
    else figures(f->text, n, 4);
    const char *s = LOC_string(HudConfig_loc(), 0x46e0);
    combo_draw(&f->combo, s ? s : "", f->text);
}

/* Their sizes, places and colours again (0x42e640, 0x42f810). */
static void figures_reset(HudVisual *v, int size_off, bool choice)
{
    HCombo *c = (HCombo *)v;
    int w = choice ? HudConfig_choicei32(size_off) : HudConfig_i32(size_off);
    int h = choice ? HudConfig_choicei32(size_off + 4) : HudConfig_i32(size_off + 4);
    tray_setsize(&c->tray, w, h, h / 2);
    int32_t x, y;
    HudConfig_pos(c->place, &x, &y);
    combo_setpos(v, x, y);
    combo_reset(c);
}
static void speed_reset(HudVisual *v) { figures_reset(v, 0x9c, false); }
static void tachdigital_reset(HudVisual *v) { figures_reset(v, 0x14, true); }

static const HudVisualVtbl speed_vt = {combo_destroy, speed_draw, speed_reset, tray_setstate, combo_setpos, tray_progress};
static const HudVisualVtbl tachdigital_vt = {combo_destroy, tachdigital_draw, tachdigital_reset, tray_setstate, combo_setpos, tray_progress};

static HudVisual *figures_new(const HudVisualVtbl *vt, int place, int w, int h, bool keep)
{
    HFigures *f = calloc(1, sizeof *f);
    if (!f) return NULL;
    int32_t x, y;
    HudConfig_pos(place, &x, &y);
    combo_init(&f->combo, vt, x, y, w, h, place, keep);
    return &f->combo.tray.base;
}

/* 0x42e490: the record's +0x9c size, place 1, measured once */
static HudVisual *speed_new(void) { return figures_new(&speed_vt, HUDPOS_SPEED, HudConfig_i32(0x9c), HudConfig_i32(0xa0), true); }
/* 0x42f660: the second record's +0x14 size, place 12 */
static HudVisual *tachdigital_new(void)
{
    return figures_new(&tachdigital_vt, HUDPOS_TACH, HudConfig_choicei32(0x14), HudConfig_choicei32(0x18), true);
}

/* ---- the dials (HDial, vtable 0x5b2aac; 0x4212c0): a face over a dark disc, the needle a quad turned to
   the value from tables made once, a cap over its pivot ---- */

enum { DIAL_STEPS = 900 };   /* the tables' room (0x2a30 bytes each) */

typedef struct {
    HTray tray;
    HudArt *face, *back, *needle, *cap;   /* +0xc4, +0xc8, +0xcc, +0xd0 */
    int32_t w, h;                          /* +0xd4, +0xd8 */
    float corner[4][DIAL_STEPS][3];        /* +0xdc: the needle's corners at each unit, from the centre */
    int place;                             /* +0xa99c, +0xa9a0: its setter and getter of the places */
    int32_t units;                         /* +0xa9a4 */
    int32_t hx, hy, cx, cy;                /* +0xa9b0 .. +0xa9bc: half its size, its centre */
    int32_t style;                         /* +0xa9c0: the needle's (0: with a cap) */
    float redline;                         /* HTach +0xa9d0: the share of the revs below the red line */
} HDial;

/* At (x, y) through the places' table; the disc and face at its corner, the cap centred. 0x421790 */
static void dial_setpos(HudVisual *v, int x, int y)
{
    HDial *d = (HDial *)v;
    int32_t px, py;
    HudConfig_setpos(d->place, x, y);
    HudConfig_pos(d->place, &px, &py);
    tray_place(&d->tray, px, py);
    if (d->face) HudArt_place(d->face, px, py);
    if (d->back) HudArt_place(d->back, px, py);
    if (d->needle) HudArt_place(d->needle, d->w / 2 + px, d->h / 2 + py);
    if (d->cap) HudArt_place(d->cap, (d->w / 2 - d->cap->w / 2) + px, (d->h / 2 - d->cap->h / 2) + py);
    d->hx = d->w / 2, d->hy = d->h / 2;
    d->cx = d->hx + px, d->cy = d->hy + py;
}

/* The dial at value n (at most its units less one; no lower bound). 0x421670 */
static void dial_draw(HDial *d, int n)
{
    if (d->tray.base.state == 0) return;
    tray_draw(&d->tray);
    if (!HudConfig_notrays()) HudArt_draw(d->back);
    if (d->units <= n) n = d->units - 1;
    if (n < 0 || DIAL_STEPS <= n) return;   /* (port: the original reads outside its tables) */
    float cx = (float)d->cx, cy = (float)d->cy, q[12];
    static const int order[4] = {2, 3, 1, 0};
    for (int k = 0; k < 4; k++) {
        const float *c = d->corner[order[k]][n];
        q[3 * k] = cx + c[0];
        q[3 * k + 1] = cy - c[1];
        q[3 * k + 2] = 0;
    }
    HudArt_draw(d->face);
    if (d->needle) {
        HudArt_setcorners(d->needle, q);
        HudArt_draw(d->needle);
    }
    if (d->style == 0) HudArt_draw(d->cap);
}

static void dial_destroy(HudVisual *v)   /* 0x4215a0 */
{
    HDial *d = (HDial *)v;
    HudArt_free(d->face);
    HudArt_free(d->back);
    HudArt_free(d->needle);
    HudArt_free(d->cap);
    free(d);
}

/* Its place and the divider's colours again. 0x4218a0 */
static void dial_reset(HudVisual *v)
{
    int32_t x, y;
    HudConfig_pos(((HDial *)v)->place, &x, &y);
    v->vt->setpos(v, x, y);
    tray_colours(&((HDial *)v)->tray);
}

/* back: the disc's image (circletach 0); face, needle, cap: images of their files; place: its entry of
   the places; w, h: its size; nw, nh: the needle's; pivot: the needle's pivot from its end; start: the
   angle of unit 0 in degrees, step: degrees a unit; units: the face's. The tables turn from start by
   step a unit with the constants the original has (3.14, 1/180, 1.57). 0x4212c0 */
static void dial_init(HDial *d, const HudVisualVtbl *vt, const char *back, int backi, const char *face, int facei, const char *cap,
                      int capi, const char *needle, int style, int place, int w, int h, int nw, int nh, int pivot, float start, float step,
                      int units)
{
    int32_t x, y;
    HudConfig_pos(place, &x, &y);
    tray_init(&d->tray, vt, x, y, w, h, 0, true, 0, false, false);
    d->w = w, d->h = h;
    d->place = place;
    d->units = units;
    d->style = style;
    d->back = HudArt_load(back, backi, x, y, w, h, false, false, false);
    if (d->back) HudArt_setcolours(d->back, 0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u);
    d->face = HudArt_load(face, facei, x, y, w, h, false, false, true);
    d->needle = HudArt_load(needle, style, x, y, nw, nh, false, false, false);
    d->cap = HudArt_load(cap, capi, x, y, 0x14, 0x14, false, false, false);
    HudConfig_pos(place, &x, &y);
    dial_setpos(&d->tray.base, x, y);
    double pi = exe_f64(0x5b2aa0), deg = exe_f64(0x5b2a98), quarter = exe_f64(0x5b2a90);
    double a = (double)start * pi * deg;   /* (x87: extended precision) */
    float da = step * (float)pi * (float)deg;
    float half = (float)((nw + 1) / 2);
    for (int i = units - 1; 0 <= i; i--) {
        float px = (float)(cos(a - quarter) * half), py = (float)(sin(a - quarter) * half);
        double c = cos(a), s = sin(a);
        if (i < DIAL_STEPS) {
            float *t0 = d->corner[0][i], *t1 = d->corner[1][i], *t2 = d->corner[2][i], *t3 = d->corner[3][i];
            t0[0] = (float)(-pivot * c - px), t0[1] = (float)(-pivot * s - py), t0[2] = 0;
            t1[0] = (float)(px - pivot * c), t1[1] = (float)(py - pivot * s), t1[2] = 0;
            double lc = (double)(nh - pivot) * c;
            float ls = (float)((double)(nh - pivot) * s);
            t2[0] = (float)(lc - px), t2[1] = ls - py, t2[2] = 0;
            t3[0] = (float)(px + lc), t3[1] = py + ls, t3[2] = 0;
        }
        a = (double)da + a;
    }
}

/* ---- the speedometer (HSpeedometer, vtable 0x5b3020; 0x42e670) and the tachometer (HTach, 0xa9d4
   bytes, vtable 0x5b3098; 0x42f300) ---- */

/* km/h or mph, a unit each. Shown by the second layout's +0x30 2. 0x42e7a0 */
static void speedometer_draw(HudVisual *v)
{
    Car *car = local_car();
    if (HudConfig_choicei32(0x30) != 2 || v->state == 0 || !car) return;   /* 0x41e960 */
    float ms = *CAR_F(car, 0xec4);
    dial_draw((HDial *)v, g_hud_imperial ? (int)(ms * exe_f32(0x5b3014)) : (int)(ms * exe_f32(0x5b3018)));
}

/* The revs scaled to the red line (+0xee0), in tens. Shown by +0x38 2. 0x42f5c0 */
static void tach_draw(HudVisual *v)
{
    Car *car = local_car();
    if (HudConfig_choicei32(0x38) != 2 || !car) return;   /* 0x41e980 */
    int n = (int)(*CAR_F(car, 0xdb4) * ((HDial *)v)->redline + exe_f32(0x5b30d8));
    dial_draw((HDial *)v, n / 10);
}

/* Its size (the record's +0xc4, no divider), place (6), then the dial's. 0x42f4f0 */
static void tach_setpos(HudVisual *v, int x, int y)
{
    HDial *d = (HDial *)v;
    tray_setsize(&d->tray, HudConfig_i32(0xc4), HudConfig_i32(0xc8), 0);
    int32_t px, py;
    HudConfig_setpos(HUDPOS_TACHDIAL, x, y);
    HudConfig_pos(HUDPOS_TACHDIAL, &px, &py);
    dial_setpos(v, px, py);
    PENDING_NOTE(0x42f4f0, "the tachometer's two texts (+0xa9c4, +0xa9c8; placed, never drawn)");
}

static const HudVisualVtbl speedometer_vt = {dial_destroy, speedometer_draw, dial_reset, tray_setstate, dial_setpos, tray_progress};
static const HudVisualVtbl tach_vt = {dial_destroy, tach_draw, dial_reset, tray_setstate, tach_setpos, tray_progress};

static HudVisual *speedometer_new(void)
{
    HDial *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    const HudDials *o = &g_hud_dials;
    int night = g_r3d_night != 0, st = o->needle;
    const char *face = exe_str(exe_u32(0x5ccefc + 4u * (unsigned)(night + g_hud_imperial * 2)));
    const char *needle = exe_str(exe_u32(0x5ccf0c + 4u * (unsigned)night));
    dial_init(d, &speedometer_vt, exe_str(0x5ccf94), 0, face, g_hud_imperial ? o->speedo_mph : o->speedo_kmh, needle, 5, needle, st,
              HUDPOS_SPEEDO, HudConfig_i32(0xd4), HudConfig_i32(0xd8), exe_i32(0x5ccf18 + 8u * (unsigned)st),
              exe_i32(0x5ccf1c + 8u * (unsigned)st), o->pivot, (float)o->speedo_start, (float)o->speedo_deg * exe_f32(0x5b3060),
              o->speedo_units);
    return &d->tray.base;
}

static HudVisual *tach_new(void)
{
    HDial *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    const HudDials *o = &g_hud_dials;
    int night = g_r3d_night != 0, st = o->needle;
    const char *needle = exe_str(exe_u32(0x5ccfd4 + 4u * (unsigned)night));
    dial_init(d, &tach_vt, exe_str(0x5ccf94), 0, exe_str(exe_u32(0x5ccfcc + 4u * (unsigned)night)), o->tach, needle, 5, needle, st,
              HUDPOS_TACHDIAL, HudConfig_i32(0xc4), HudConfig_i32(0xc8), exe_i32(0x5ccfe0 + 8u * (unsigned)st),
              exe_i32(0x5ccfe4 + 8u * (unsigned)st), o->pivot, (float)o->tach_start, (float)o->tach_deg * exe_f32(0x5b3060), o->tach_units);
    PENDING_NOTE(0x42f300, "the tachometer's texts (0xa9c4, 0xa9c8)");
    int32_t x, y;
    HudConfig_pos(HUDPOS_TACHDIAL, &x, &y);
    tach_setpos(&d->tray.base, x, y);
    Car *car = local_car();
    int red = car ? *CAR_I(car, 0xee0) : 0;
    d->redline = red ? (float)(red - 0x2ee) / (float)red : 0;
    return &d->tray.base;
}

/* ---- the HUD ---- */

void HUD_Init(void)
{
    HUD_Free();
    /* The player's name (the race description's +0x558 +0x17c), the layout chosen (0x657db8), the
       screen. */
    HudConfig_Load("", 0, g_display.width, g_display.height);
    always = true;  /* (HUD_LoadStrings 0x41e190 sets 0x60c970) */
    add(gear_new(), true, false);
    add(speed_new(), true, false);
    PENDING_NOTE(0x4315d0, "HUD_Init's lap and time visuals (0x4315d0, 0x42a0f0)");
    PENDING_NOTE(0x42a6f0, "HUD_Init's damage, position and lap visuals (0x42f880, 0x42a6f0, 0x421e20)");
    add(speedometer_new(), true, false);
    add(tach_new(), true, false);
    add(tachdigital_new(), true, false);
    PENDING_NOTE(0x424420, "HUD_Init's other visuals (gauges, lap, position, time, map, messages)");
    add(countdown_new(), false, false);
    Hud2D_Init();   /* 0x419d70 */
    HudConfig_trays(true);   /* (0x657de4, the trays option: on) */
}

void HUD_Free(void)
{
    for (int i = 0; i < nlist; i++)
        if (list[i].v) list[i].v->vt->destroy(list[i].v);
    free(list);
    list = NULL;
    if (nlist) HudConfig_Free();
    nlist = 0;
}

static void pass(RenderSystem *r, RCamera *c)
{
    View_Begin(r, c);
    EAGL_RSSet(0x28, 7);   /* 0x5375a0(7) */
    Hud2D_View(r);         /* 0x4670c0 */
}

static void end_pass(RenderSystem *r)
{
    Hud2D_Flush();   /* 0x41a350 */
    View_End(r);
}

void HUD_Draw(RenderSystem *r, RCamera *c)
{
    if (hidden || !r || !c) return;
    pass(r, c);
    PENDING_NOTE(0x427ac0, "the HUD's map pass (0x657d5c, 0x427ac0)");
    end_pass(r);
    /* The front visuals, once the race has started. */
    pass(r, c);
    for (int i = 0; i < nlist; i++) {
        HudEntry *e = &list[i];
        if (!e->last && e->front && 0x1c0 < g_sim_tick && (always || e->v->state == 2) && (g_race_view == 0 || g_race_view == 1))
            e->v->vt->draw(e->v);
    }
    end_pass(r);
    /* The others. */
    pass(r, c);
    for (int i = 0; i < nlist; i++)
        if (!list[i].last && !list[i].front) list[i].v->vt->draw(list[i].v);
    end_pass(r);
    for (int i = 0; i < nlist; i++) {
        HudEntry *e = &list[i];
        if (e->last && 0x1c0 < g_sim_tick && (always || e->v->state == 2)) e->v->vt->draw(e->v);
    }
    /* No modal visual (0x60c9c4, not ported): once the race has started the hidden front visuals show,
       sliding in. 0x425de0 */
    if (0x1c0 < g_sim_tick && (g_race_view == 0 || g_race_view == 1))
        for (int i = 0; i < nlist; i++) {
            HudEntry *e = &list[i];
            if (e->front && (always || e->v->state == 2) && e->v->state == 0 && e->v->vt->setstate) e->v->vt->setstate(e->v, 1);
        }
    PENDING_NOTE(0x425910, "HUD_Draw's modal visuals, replay and network parts");
}
