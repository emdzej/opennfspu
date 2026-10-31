/* The race HUD (see hud.h). */
#include "game/hud.h"
#include "exe.h"
#include "game/car.h"
#include "game/car_drive.h"
#include "game/hud_config.h"
#include "game/race_car.h"
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

/* ---- the times (HTime 0x4315d0, 0xe8 bytes, vtable 0x5b31a0; HMultiTime 0x42a0f0, 0xe0 bytes, vtable
   0x5b2ccc) ---- */

/* A time of ticks (64 a second) as mm:ss.hh ("59:59.99" past an hour, 0x5cd1e8), signed ("+" 0x5cd200,
   "-" 0x5cd1fc) if asked. 0x435640 */
void HTime_Format(int t, char *out, bool sign)
{
    out[0] = 0;
    if (sign) strcat(out, exe_str(t < 0 ? 0x5cd1fc : 0x5cd200));
    int a = 0 < t ? t : -t;
    int secs = (int)((float)a * exe_f32(0x5b2828));
    int min = secs / 0x3c;
    int hund = (int)((float)((a & 0x3f) * 100) * exe_f32(0x5b2828));
    if (0x3c < min) {
        strcat(out, exe_str(0x5cd1e8));
        return;
    }
    size_t n;
    if (min < 10) strcat(out, exe_str(0x5ccad0));
    n = strlen(out);
    snprintf(out + n, 16, "%d", min);
    strcat(out, exe_str(0x5cd1f8));   /* ":" */
    if (secs % 0x3c < 10) strcat(out, exe_str(0x5ccad0));
    n = strlen(out);
    snprintf(out + n, 16, "%d", secs % 0x3c);
    strcat(out, exe_str(0x5cd1f4));   /* "." */
    if (hund < 10) strcat(out, exe_str(0x5ccad0));
    n = strlen(out);
    snprintf(out + n, 16, "%d", hund);
}

typedef struct {
    HCombo combo;
    char text[12];     /* +0xd4 */
    int32_t flash;     /* +0xe0: ticks the value stays red */
    int32_t penalties; /* +0xe4 */
} HTime;

static void value_plain(HCombo *c) { if (c->value) c->value->colour = HudConfig_scheme(); }      /* 0x41ca90 */

/* The race's time (from the start, tick 0x1c0; the finish's once finished, +0xb50 2), with the
   penalties' (0x657830 each, not ported: 0); "time" (hud.loc 0x12). 0x431650 */
static void time_draw(HudVisual *v)
{
    HTime *t = (HTime *)v;
    Car *car = local_car();
    if (always && v->state != 0 && car) {
        PENDING_NOTE(0x434f00, "the pursuit's red time (game type 9, 0x434f00; red 0x41ca80)");
        if (t->flash == 0) value_plain(&t->combo);
        int now = *CAR_I(car, 0xb50) == 2 ? *CAR_I(car, 0xb24) : g_sim_tick - 0x1c0;
        if (now < 0) now = 0;
        int pen = t->penalties * 0;   /* (0x657830) */
        HTime_Format((int)((float)pen * exe_f32(0x5b24e4) + (float)now), t->text, false);
        const char *s = LOC_string(HudConfig_loc(), 0x12);
        combo_draw(&t->combo, s ? s : "", t->text);
    }
    if (t->flash < 1) t->flash = 0;
    else if (--t->flash == 0) value_plain(&t->combo);
}

/* The record or the best or last lap by the split times option (0x657dd8: 1 the record 0x6574b8, 2 the
   best lap of +0xb28.., 3 the last); a point-to-point race's record (0x657498). Shown by the second
   layout's +0x4c. 0x42a160 */
static void multitime_draw(HudVisual *v)
{
    HTime *t = (HTime *)v;
    Car *car = local_car();
    if (HudConfig_choicei32(0x4c) == 0 || !always || v->state == 0 || !car) return;   /* 0x41e870 */
    int label, n = 0;
    PENDING_NOTE(0x433790, "the track records (0x657498, 0x6574b8: HTrackRecords)");
    if (g_race_linear) label = 0x15, n = 0;
    else if (g_hud_splits == 1) label = 0x16, n = 0;
    else if (g_hud_splits == 2) {
        label = 0x17;
        int laps = *CAR_I(car, 0xb20);
        n = 359999;
        if (laps == 0) n = 0;
        for (int i = 0; i < laps; i++)
            if (*CAR_I(car, 0xb28 + 4 * i) < n) n = *CAR_I(car, 0xb28 + 4 * i);
    } else if (g_hud_splits == 3) {
        label = 0x18;
        int laps = *CAR_I(car, 0xb20);
        n = laps ? *CAR_I(car, 0xb24 + 4 * laps) : 0;
    } else
        return;   /* (the original formats its own pointer as a time and draws its text) */
    HTime_Format(n, t->text, false);
    const char *s = LOC_string(HudConfig_loc(), label);
    combo_draw(&t->combo, s ? s : "", t->text);
}

static void time_reset(HudVisual *v) { figures_reset(v, 0xa4, false); }        /* 0x431760 */
static void multitime_reset(HudVisual *v) { figures_reset(v, 0x0, true); }     /* 0x42a290 */

static const HudVisualVtbl time_vt = {combo_destroy, time_draw, time_reset, tray_setstate, combo_setpos, tray_progress};
static const HudVisualVtbl multitime_vt = {combo_destroy, multitime_draw, multitime_reset, tray_setstate, combo_setpos, tray_progress};

static HudVisual *time_new(const HudVisualVtbl *vt, int place, int w, int h)
{
    HTime *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    int32_t x, y;
    HudConfig_pos(place, &x, &y);
    combo_init(&t->combo, vt, x, y, w, h, place, true);
    return &t->combo.tray.base;
}

/* ---- the standings (HPosition, 600 bytes, vtable 0x5b2dbc; 0x42a6f0) ---- */

int32_t g_hud_opponentlist = 1;   /* 0x657dd4: OPPONENTLIST (1 the gap in nodes, 2 in time) */

enum { POS_MAX = CAR_MAX };

/* A row a racer by place: the car's colour, the driver's name, the gap to the player; below them
   "position" and the player's place of all. */
typedef struct {
    HTray tray;
    float swatch[16];             /* +0xc4: the colour square's corners, the first row's */
    HText *name[POS_MAX];         /* +0x104: by row */
    HText *gap[POS_MAX];          /* +0x108 */
    HText *label, *place, *of, *extra;   /* +0x10c, +0x110, +0x114, +0x118 */
    char placebuf[8], ofbuf[8];   /* +0x11c, +0x120 */
    char gapbuf[POS_MAX][12];     /* +0x123: by car */
    char namebuf[POS_MAX][25];    /* +0x183: by car */
    int32_t count;                /* +0x254 */
} HPosition;

/* The rows, the label and the place's texts, the colour square, at (x, y) through the places (4).
   0x42b5b0 */
static void position_setpos(HudVisual *v, int x, int y)
{
    HPosition *p = (HPosition *)v;
    int32_t px, py;
    HudConfig_setpos(HUDPOS_POSITION, x, y);
    HudConfig_pos(HUDPOS_POSITION, &px, &py);
    tray_place(&p->tray, px, py);
    int rw = HudConfig_i32(0xb4), rh = HudConfig_i32(0xb8), sw = HudConfig_i32(0x114), sh = HudConfig_i32(0x118);
    int in = HudConfig_i32(0x34), top = HudConfig_i32(0x38);
    for (int i = 0; i < p->count; i++) {
        if (p->name[i]) p->name[i]->x = in + px + in + sw, p->name[i]->y = top + rh * i + py;
        if (p->gap[i]) p->gap[i]->x = px + rw - in - 8, p->gap[i]->y = top + rh * i + py;
    }
    int w = p->tray.w, h = p->tray.h;
    float a, b, c, ah;
    if (!p->label || !p->place || !p->of || !p->label->font || !p->place->font || !p->of->font) return;
    /* "position" at the bottom left (measured on the label's font for the alphabet's height). */
    text_extent(p->label, exe_str(0x5ccdd0), &a, &b, &c, &ah);
    p->label->x = px + in;
    p->label->y = py + (h - (int)((float)top + ah));
    /* The place ("4/") and of all ("4") as wide as samples, ending at the right inset. */
    float slash_w, slash_h, four_w, four_h;
    text_extent(p->place, exe_str(0x5ccddc), &a, &b, &slash_w, &slash_h);
    int right = (int)((float)(w - in) - slash_w + (float)px);
    text_extent(p->of, exe_str(0x5ccc38), &a, &b, &four_w, &four_h);
    int at = (int)((float)right - four_w) - 3;
    p->place->x = at;
    p->place->y = py + (h - (int)((float)top + slash_h));
    p->of->x = (int)((float)at + slash_w);
    p->of->y = py + (h - (int)((float)top + four_h));
    /* The colour square: centred on the first row. */
    float zh;
    text_extent(p->label, exe_str(0x5ccad0), &a, &b, &c, &zh);
    int x0 = in + px, y0 = (int)(zh * 0.5f - (float)(sh / 2) + (float)(py + top));
    float q[16] = {(float)x0, (float)y0, 0, 1, (float)(x0 + sw), (float)y0, 0, 1,
                   (float)(x0 + sw), (float)(y0 + sh), 0, 1, (float)x0, (float)(y0 + sh), 0, 1};
    memcpy(p->swatch, q, sizeof q);
}

/* Its size for the count, place and colours again. 0x42bac0 */
static void position_reset(HudVisual *v)
{
    HPosition *p = (HPosition *)v;
    int rw = HudConfig_i32(0xb4), rh = HudConfig_i32(0xb8), top = HudConfig_i32(0x38);
    tray_setsize(&p->tray, rw, (p->count + 1) * rh + top * 2, top + rh * p->count);
    int32_t x, y;
    HudConfig_pos(HUDPOS_POSITION, &x, &y);
    v->vt->setpos(v, x, y);
    tray_colours(&p->tray);
    uint32_t c14 = (uint32_t)HudConfig_i32(0x14);
    for (int i = 0; i < p->count; i++) {
        if (p->name[i]) p->name[i]->colour = (uint32_t)HudConfig_i32(0x1c), p->name[i]->colour2 = c14;
        if (p->gap[i]) p->gap[i]->colour = HudConfig_scheme(), p->gap[i]->colour2 = c14;
    }
    if (p->label) p->label->colour = (uint32_t)HudConfig_i32(0x18), p->label->colour2 = c14;
    if (p->extra) p->extra->colour = HudConfig_scheme(), p->extra->colour2 = c14;
    if (p->place) p->place->colour = HudConfig_scheme(), p->place->colour2 = c14;
    if (p->of) p->of->colour = (uint32_t)HudConfig_i32(0x18), p->of->colour2 = c14;
}

static bool out_of_race(Car *c) { return (*CAR_I(c, 0xb50) == 3 || *CAR_I(c, 0xb50) == 1) && g_race_mode == 1; }

/* 0x42af60 */
static void position_draw(HudVisual *v)
{
    HPosition *p = (HPosition *)v;
    if (g_race_view == 2 || g_race_view == 3 || p->count < 2) return;
    int was = p->count;
    p->count = g_racers.n;
    for (int i = 0; i < g_racers.n; i++)
        if (out_of_race(g_racers.c[i])) p->count--;
    if (was != p->count) v->vt->reset(v);
    for (int i = 0; i < p->count; i++) {
        Car *c = g_racers.c[i];
        if (!out_of_race(c) && g_race_mode == 1 && p->count < *CAR_I(c, 0xb48) - 1) return;
    }
    if (HudConfig_choicei32(0x48) == 0 || !always || v->state == 0 || p->count < 1) return;   /* 0x41e8a0 */
    tray_draw(&p->tray);
    int rh = HudConfig_i32(0xb8);
    Car *me = local_car();
    int mine = me ? *CAR_I(me, 0xb54) : 0;
    for (int i = 0; i < g_racers.n && i < POS_MAX; i++) {
        Car *c = g_racers.c[i];
        if (out_of_race(c)) continue;
        int row = *CAR_I(c, 0xb48) - 1;
        if (row < 1) row = 0;
        if (POS_MAX <= row) continue;
        float q[16];
        memcpy(q, p->swatch, sizeof q);
        for (int k = 0; k < 4; k++) q[4 * k + 1] += (float)(rh * row);
        uint32_t col;
        memcpy(&col, CAR_DESCB(c) + 0x218, 4);
        if (g_hud_opaque) col |= 0xff000000u;   /* 0x419f70 */
        uint32_t cs[4] = {col, col, col, col};
        Hud2D_Quad(q, cs);
        text_print(p->name[row], p->namebuf[i], 1.0f, 1.0f);
        char *g = p->gapbuf[i];
        const char *s = g;
        if (g_race_knockout == 1) {
            PENDING_NOTE(0x42af60, "the knockout's standings (its distances)");
            s = exe_str(0x5ccde4);
        } else if (g_race_mode == 3) {
            PENDING_NOTE(0x48d720, "the network race's standings");
            s = exe_str(0x5ccde4);
        } else if (*CAR_I(c, 0xb50) == 2) {
            HTime_Format(*CAR_I(c, 0xb24), g, false);
        } else {
            int d = *CAR_I(c, 0xb54) - mine;
            if (g_hud_opponentlist == 1) {
                if (d == 0) s = exe_str(0x5ccde4);   /* "-----" */
                else snprintf(g, 12, "%d", d);
            } else if (g_hud_opponentlist == 2) {
                float a = me ? *CAR_F(me, 0x35c) : 0, b = *CAR_F(c, 0x35c), f;
                if (d < 1) {
                    f = a * exe_f32(0x5b23f0);
                    if (f < b) f = b;
                } else {
                    f = b * exe_f32(0x5b23f0);
                    if (b * exe_f32(0x5b23f0) < a) f = a;
                }
                if (exe_f32(0x5b2df8) < a && d != 0) HTime_Format((int)lrintf((float)(d * 0x40) / f), g, true);
                else s = exe_str(0x5ccde4);
            } else {
                g[0] = 0;
            }
        }
        text_print(p->gap[row], s, 1.0f, 1.0f);
    }
    const char *label = LOC_string(HudConfig_loc(), 0x11);
    text_print(p->label, label ? label : "", 1.0f, 1.0f);
    if (!g_race_knockout && me) {
        snprintf(p->placebuf, sizeof p->placebuf, "%d%s", *CAR_I(me, 0xb48), exe_str(0x5ccde0));
        text_print(p->place, p->placebuf, 1.0f, 1.0f);
        snprintf(p->ofbuf, sizeof p->ofbuf, "%d", p->count);
        text_print(p->of, p->ofbuf, 1.0f, 1.0f);
    }
}

static void position_destroy(HudVisual *v)   /* 0x42ad40 */
{
    HPosition *p = (HPosition *)v;
    for (int i = 0; i < POS_MAX; i++) text_free(p->name[i]), text_free(p->gap[i]);
    text_free(p->label);
    text_free(p->place);
    text_free(p->of);
    text_free(p->extra);
    free(p);
}

static const HudVisualVtbl position_vt = {position_destroy, position_draw, position_reset, tray_setstate, position_setpos, tray_progress};

static HudVisual *position_new(void)
{
    HPosition *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    int32_t x, y;
    HudConfig_pos(HUDPOS_POSITION, &x, &y);
    tray_init(&p->tray, &position_vt, x, y, 0, 0, 0, false, 0, false, false);
    p->count = g_racers.n < POS_MAX ? g_racers.n : POS_MAX;
    int rw = HudConfig_i32(0xb4), rh = HudConfig_i32(0xb8), top = HudConfig_i32(0x38);
    tray_setsize(&p->tray, rw, (p->count + 1) * rh + top * 2, top + rh * p->count);
    int nw = (int)((double)rw * exe_f64(0x5b2db0)), gw = (int)((double)rw * exe_f64(0x5b2da8));
    uint32_t c14 = (uint32_t)HudConfig_i32(0x14);
    for (int i = 0; i < p->count; i++) {
        p->name[i] = text_new(HudConfig_font(0x64), (uint32_t)HudConfig_i32(0x1c), c14, 0, 0, nw, rh, 0, false);
        p->gap[i] = text_new(HudConfig_font(0x7c), HudConfig_scheme(), c14, 0, 0, gw, rh, 1, false);
    }
    p->label = text_new(HudConfig_font(0x4c), (uint32_t)HudConfig_i32(0x18), c14, 0, 0, rw, rh, 0, false);
    p->extra = text_new(HudConfig_font(0x64), HudConfig_scheme(), c14, 0, 0, rw, rh, 1, false);
    p->place = text_new(HudConfig_font(0x4c), HudConfig_scheme(), c14, 0, 0, rw, rh, 0, false);
    p->of = text_new(HudConfig_font(0x64), (uint32_t)HudConfig_i32(0x18), c14, 0, 0, rw, rh, 0, false);
    PENDING_NOTE(0x42a6f0, "the standings' ping meter (network)");
    HudConfig_pos(HUDPOS_POSITION, &x, &y);
    position_setpos(&p->tray.base, x, y);
    /* The drivers' names, cut to fit 55 pixels (" " when none). */
    for (int i = 0; i < g_racers.n && i < POS_MAX; i++) {
        const char *src = (const char *)CAR_DESCB(g_racers.c[i]) + 0x17c;
        int n = (int)strlen(src);
        if (24 < n) n = 24;
        bool fits = false;
        while (0 < n && !fits) {
            memcpy(p->namebuf[i], src, (size_t)n);
            p->namebuf[i][n] = 0;
            float a, b, w, h;
            if (p->name[0] && p->name[0]->font && p->name[0]->font->font) text_extent(p->name[0], p->namebuf[i], &a, &b, &w, &h);
            else w = 0;
            if (exe_f32(0x5b2da4) < w) n--;
            else fits = true;
        }
        if (n == 0) snprintf(p->namebuf[i], sizeof p->namebuf[i], "%s", exe_str(0x5ccdc0));
    }
    return &p->tray.base;
}

/* ---- the damage, drawn (HGraphicDamage, 0x1d0 bytes, vtable 0x5b2b84; 0x421e20) ---- */

/* A getter returning a pair as instructions (mov eax, imm; mov edx, imm or mov edx, eax): its values read
   from the code. */
static void code_pair(uint32_t fn, int32_t *x, int32_t *y)
{
    *x = exe_i32(fn + 1);
    *y = exe_data(fn + 5, 1)[0] == 0xba ? exe_i32(fn + 6) : *x;
}

/* The car seen from above in six parts of car.fsh (colour by their zones' damage, five of them drawn
   again mirrored below) inside its outline, a green-to-red scale between "0%" and "100%", "damage". */
typedef struct {
    HTray tray;
    HText *right, *left, *title;  /* +0xc4 "100%", +0xc8 "0%", +0xcc */
    HudArt *outline;              /* +0xd0 */
    HudArt *part[6];              /* +0xd4 */
    int32_t mirror[6];            /* +0xec: the mirrored copy's offset (0: below by its height) */
    uint32_t colour[12][4];       /* +0x104: the scale's lines, then a row a damage level 1 .. 11 */
    float th;                     /* +0x1cc: the alphabet's height in the title's font */
} HGraphicDamage;

/* The part getters (sizes 0x420a70.., places 0x420ad0..) and the outline's size (0x420920). */
static const uint32_t part_size[6] = {0x420a70, 0x420a80, 0x420a90, 0x420aa0, 0x420ab0, 0x420ac0};
static const uint32_t part_at[6] = {0x420ad0, 0x420ae0, 0x420af0, 0x420b00, 0x420b10, 0x420b20};
static const int part_image[6] = {3, 0, 4, 5, 2, 1};

/* 0x4229f0 */
static void gdamage_setpos(HudVisual *v, int x, int y)
{
    HGraphicDamage *g = (HGraphicDamage *)v;
    int32_t px, py, ax, ay, ow, oh;
    HudConfig_setpos(HUDPOS_GDAMAGE, x, y);
    HudConfig_pos(HUDPOS_GDAMAGE, &px, &py);
    tray_place(&g->tray, px, py);
    int w = HudConfig_i32(0xbc), in = HudConfig_i32(0x34), top = HudConfig_i32(0x38);
    ax = HudConfig_i32(0x104), ay = HudConfig_i32(0x108);
    code_pair(0x420920, &ow, &oh);
    int y1 = py + top + oh * 2 + ay;
    if (g->left) g->left->x = px + in, g->left->y = y1;
    const char *t = LOC_string(HudConfig_loc(), 0xc);
    float a, b, c, d;
    if (g->left && g->left->font && g->left->font->font) text_extent(g->left, t ? t : "", &a, &b, &c, &g->th);
    if (g->right) g->right->x = px + w - 6, g->right->y = y1;
    if (g->title && g->title->font && g->title->font->font) {
        text_extent(g->title, t ? t : "", &a, &b, &c, &d);
        g->title->x = px + in;
        g->title->y = (int)((float)(HudConfig_i32(0xc0) - top) - d + (float)py);
    }
    PENDING_NOTE(0x493c30, "the damage's percentage text (made, never drawn)");
    int bx = px + ax, by = py + ay;
    if (g->outline) HudArt_place(g->outline, bx, by);
    for (int i = 0; i < 6; i++) {
        int32_t ox, oy;
        code_pair(part_at[i], &ox, &oy);
        if (g->part[i]) HudArt_place(g->part[i], bx + ox, by + oy);
    }
}

/* The art at its place, mirrored (in y, one pixel up) below by off or its height. 0x423c90 */
static void art_mirror(HudArt *a, bool flip, int off)
{
    a->flipy = flip;
    int x = a->x, y = a->y;
    if (!a->centred && flip) y += off ? off : a->h;
    if (flip) y -= exe_i32(0x5ccc98);
    float c[12] = {(float)x, (float)y, 0, (float)(a->w + x), (float)y, 0, (float)(a->w + x), (float)(a->h + y), 0, (float)x, (float)(a->h + y), 0};
    HudArt_setcorners(a, c);
}

static void level_colours(HGraphicDamage *g, HudArt *a, int level)
{
    if (level == 0) {
        HudArt_setcolours(a, 0x55058228u, 0x55058228u, 0x55058228u, 0x55058228u);
        return;
    }
    if (11 < level) level = 11;   /* (port: the original reads past its rows) */
    const uint32_t *r = g->colour[level];
    HudArt_setcolours(a, r[0], r[1], r[2], r[3]);
}

/* Shown by the second layout's +0x3c 1. 0x422490 */
static void gdamage_draw(HudVisual *v)
{
    HGraphicDamage *g = (HGraphicDamage *)v;
    Car *car = local_car();
    if (HudConfig_choicei32(0x3c) != 1 || v->state == 0 || !car) return;   /* 0x41e8d0 */
    tray_draw(&g->tray);
    /* Each part's two zones' damage in levels (zone x 4 x 0.01). */
    static const int zones[12] = {2, 0, 1, 1, 3, 7, 4, 6, 5, 5, 8, 8};
    int lv[12];
    for (int k = 0; k < 12; k++) lv[k] = (int)((double)(*CAR_F(car, 0x4e4 + 4 * zones[k]) * exe_f32(0x5b241c)) * exe_f64(0x5b2bc0));
    for (int i = 0; i < 6; i++) {
        HudArt *a = g->part[i];
        if (!a) continue;
        level_colours(g, a, lv[2 * i]);
        art_mirror(a, false, 0);
        HudArt_draw(a);
        if (i != 1 && i != 4 && i != 5) {
            level_colours(g, a, lv[2 * i + 1]);
            art_mirror(a, true, g->mirror[i]);
            HudArt_draw(a);
        }
    }
    if (g->outline) {
        art_mirror(g->outline, false, 0);
        HudArt_draw(g->outline);
        art_mirror(g->outline, true, 0);
        HudArt_draw(g->outline);
    }
    /* The scale: two lines (green to yellow, yellow to red), as thick as the layout's +0x40. */
    int32_t px, py, ow, oh;
    HudConfig_pos(HUDPOS_GDAMAGE, &px, &py);
    code_pair(0x420920, &ow, &oh);
    int w = HudConfig_i32(0xbc), in = HudConfig_i32(0x34), top = HudConfig_i32(0x38);
    float ly = (float)(top * 3) + (float)(py + oh * 2 + HudConfig_i32(0x108)) + g->th;
    float x0 = (float)(in + px), x1 = (float)(in + w / 2 + px), x2 = (float)((w + px) - in);
    for (int k = 0; k < HudConfig_i32(0x40); k++) {
        float l0[8] = {x0, ly, 0, 1, x1, ly, 0, 1}, l1[8] = {x1, ly, 0, 1, x2, ly, 0, 1};
        Hud2D_Line(l0, g->colour[0]);
        Hud2D_Line(l1, g->colour[0] + 2);
        ly += 1.0f;
    }
    text_print(g->left, exe_str(0x5ccc8c), 1.0f, 1.0f);
    text_print(g->right, exe_str(0x5ccc84), 1.0f, 1.0f);
    PENDING_NOTE(0x434f00, "the pursuit's red damage title (game type 9)");
    if (g->title) g->title->colour = (uint32_t)HudConfig_i32(0x18);
    const char *t = LOC_string(HudConfig_loc(), 0xc);
    text_print(g->title, t ? t : "", 1.0f, 1.0f);
}

static void gdamage_destroy(HudVisual *v)   /* 0x4223b0 */
{
    HGraphicDamage *g = (HGraphicDamage *)v;
    text_free(g->right);
    text_free(g->left);
    text_free(g->title);
    HudArt_free(g->outline);
    for (int i = 0; i < 6; i++) HudArt_free(g->part[i]);
    free(g);
}

/* Its colours again (the texts'). 0x422cc0 */
static void gdamage_reset(HudVisual *v)
{
    HGraphicDamage *g = (HGraphicDamage *)v;
    PENDING_NOTE(0x422cc0, "HGraphicDamage's reset (the layout changed)");
    tray_colours(&g->tray);
}

static const HudVisualVtbl gdamage_vt = {gdamage_destroy, gdamage_draw, gdamage_reset, tray_setstate, gdamage_setpos, tray_progress};

static HudVisual *gdamage_new(void)
{
    HGraphicDamage *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    int32_t x, y;
    HudConfig_pos(HUDPOS_GDAMAGE, &x, &y);
    int w = HudConfig_i32(0xbc), h = HudConfig_i32(0xc0);
    tray_init(&g->tray, &gdamage_vt, x, y, w, h, 0, false, 0, false, false);
    uint32_t c14 = (uint32_t)HudConfig_i32(0x14);
    g->left = text_new(HudConfig_font(0x7c), HudConfig_scheme(), c14, 0, 0, 10, 0x14, 0, false);
    g->right = text_new(HudConfig_font(0x7c), HudConfig_scheme(), c14, 0, 0, 10, 0x14, 1, false);
    g->title = text_new(HudConfig_font(0x4c), (uint32_t)HudConfig_i32(0x18), c14, 0, 0, 10, 0x14, 0, false);
    for (int i = 0; i < 6; i++) {
        int32_t pw, ph;
        code_pair(part_size[i], &pw, &ph);
        g->part[i] = HudArt_load(exe_str(0x5ccc80), part_image[i], 0, 0, pw, ph, false, false, false);   /* "car" */
    }
    int32_t ow, oh;
    code_pair(0x420920, &ow, &oh);
    g->outline = HudArt_load(exe_str(0x5ccc80), 6, 0, 0, ow, oh, false, false, false);
    if (g->outline) HudArt_setcolours(g->outline, 0xcc000000u, 0xcc000000u, 0xcc000000u, 0xcc000000u);
    static const int mirrored[3] = {0, 2, 3};
    for (int k = 0; k < 3; k++) {
        int i = mirrored[k];
        int32_t pw, ph, ox, oy;
        code_pair(part_size[i], &pw, &ph);
        code_pair(part_at[i], &ox, &oy);
        g->mirror[i] = (oh - oy) * 2 - ph;
    }
    for (int i = 0; i < 6; i++)
        if (g->part[i]) HudArt_setcolours(g->part[i], 0, 0, 0, 0);
    /* The colours (immediates of the constructor's stores, 0x4221ac .. 0x4222b2). */
    uint32_t c0 = exe_u32(0x4221b1 + 6), cy = exe_u32(0x4221ac + 1), c3 = exe_u32(0x4221cc + 6);
    uint32_t row[4] = {c0, cy, cy, c3};
    memcpy(g->colour[0], row, sizeof row);
    static const uint32_t rows[10] = {0x4221c7, 0x4221dc, 0x4221e7, 0x422204, 0x422221, 0x42223e, 0x42225b, 0x422278, 0x422295, 0x4222b2};
    for (int l = 1; l <= 11; l++) {
        uint32_t c = exe_u32(rows[l <= 10 ? l - 1 : 9] + 1);
        for (int k = 0; k < 4; k++) g->colour[l][k] = c;
    }
    gdamage_setpos(&g->tray.base, x, y);
    /* The tray's divider above the title. */
    const char *t = LOC_string(HudConfig_loc(), 0xc);
    float a, b, c, d = 0;
    if (g->title && g->title->font && g->title->font->font) text_extent(g->title, t ? t : "", &a, &b, &c, &d);
    float lo = (float)(g->tray.h - HudConfig_i32(0x38)) - d;
    tray_setsize(&g->tray, g->tray.w, g->tray.h, (int)(lo - (float)(HudConfig_i32(0x38) / 2)));
    return &g->tray.base;
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

/* ---- the wrong way (HWrongWay, 0x48 bytes, vtable 0x5b350c; 0x436dd0) ---- */

/* "wrong way" (hud.loc 0x46ef) at the screen's centre in brackets of lines five pixels long, blinking
   (shown 64 ticks, hidden 32). */
typedef struct {
    HudVisual base;
    HText *text;          /* +0x10 */
    int32_t acc, last;    /* +0x14, +0x18: the ticks counted, the last tick */
    int32_t x, y;         /* +0x1c, +0x20: the text's place */
    uint8_t shown;        /* +0x24 */
    int32_t box[8];       /* +0x28: the brackets' corners (left top, left bottom, right top, right bottom) */
} HWrongWay;

/* Its place: centred on the screen, the brackets five pixels past the text's width and the alphabet's
   height. 0x437290 */
static void wrongway_layout(HWrongWay *w)
{
    w->x = g_display.width / 2;
    w->y = g_display.height / 2;
    if (!w->text || !w->text->font || !w->text->font->font) return;
    const char *s = LOC_string(HudConfig_loc(), 0x46ef);
    float ex, ey, ew, eh;
    text_extent(w->text, s ? s : "", &ex, &ey, &ew, &eh);
    float cx = (float)w->x, hw = ew * exe_f32(0x5b23f0), t = cx - hw;
    w->box[0] = w->box[2] = (int)(t - exe_f32(0x5b2400));
    w->box[4] = w->box[6] = (int)(hw + cx + exe_f32(0x5b2400));
    int top = w->y - 5, bottom = (int)((float)w->y + eh + exe_f32(0x5b2400));
    w->box[1] = w->box[5] = top;
    w->box[3] = w->box[7] = bottom;
    w->x = (int)t;
    w->text->x = w->x, w->text->y = w->y;
}

static void wrongway_line(int x0, int y0, int x1, int y1)
{
    float p[8] = {(float)x0, (float)y0, 0, 1, (float)x1, (float)y1, 0, 1};
    uint32_t c[2] = {0xfffab400u, 0xfffab400u};
    Hud2D_Line(p, c);
}

/* 0x436ea0 */
static void wrongway_draw(HudVisual *v)
{
    HWrongWay *w = (HWrongWay *)v;
    int now = g_sim_tick;
    if (g_race_knockout) return;
    w->acc += now - w->last;
    if (w->shown ? 0x41 <= w->acc : 0x21 <= w->acc) {
        w->acc = 0;
        w->shown = !w->shown;
    }
    if (w->shown) {
        const char *s = LOC_string(HudConfig_loc(), 0x46ef);
        text_print(w->text, s ? s : "", 1.0f, 1.0f);
        int *b = w->box;
        wrongway_line(b[0], b[1], b[0] + 5, b[1]);
        wrongway_line(b[0], b[1], b[0], b[1] + 5);
        wrongway_line(b[2], b[3], b[2] + 5, b[3]);
        wrongway_line(b[2], b[3], b[2], b[3] - 5);
        wrongway_line(b[4], b[5], b[4] - 5, b[5]);
        wrongway_line(b[4], b[5], b[4], b[5] + 5);
        wrongway_line(b[6], b[7], b[6] - 5, b[7]);
        wrongway_line(b[6], b[7], b[6], b[7] - 5);
    }
    w->last = now;
}

static void wrongway_destroy(HudVisual *v)   /* 0x436e50 */
{
    text_free(((HWrongWay *)v)->text);
    free(v);
}

static const HudVisualVtbl wrongway_vt = {wrongway_destroy, wrongway_draw, NULL, NULL, NULL, NULL};

static HudVisual *wrongway;   /* 0x60ca0c */

static HudVisual *wrongway_new(void)
{
    HWrongWay *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    w->base.vt = &wrongway_vt;
    w->shown = 1;
    w->text = text_new(HudConfig_font(0x4c), 0xfffab400u, (uint32_t)HudConfig_i32(0x14), 0, 0, 0, 0, 0, false);
    wrongway_layout(w);
    return &w->base;
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
    add(time_new(&time_vt, HUDPOS_TIME, HudConfig_i32(0xa4), HudConfig_i32(0xa8)), true, false);           /* 0x4315d0 */
    add(time_new(&multitime_vt, HUDPOS_MULTITIME, HudConfig_choicei32(0), HudConfig_choicei32(4)), true, false);   /* 0x42a0f0 */
    PENDING_NOTE(0x42f880, "HUD_Init's text damage (HTextDamage 0x42f880, the second layout's +0x3c 2 or 3)");
    add(position_new(), true, false);
    add(gdamage_new(), true, false);   /* 0x421e20 */
    add(speedometer_new(), true, false);
    add(tach_new(), true, false);
    add(tachdigital_new(), true, false);
    PENDING_NOTE(0x424420, "HUD_Init's other visuals (gauges, lap, position, time, map, messages)");
    add(countdown_new(), false, false);
    wrongway = wrongway_new();   /* 0x436dd0 (drawn on its own) */
    Hud2D_Init();   /* 0x419d70 */
    HudConfig_trays(true);   /* (0x657de4, the trays option: on) */
}

void HUD_Free(void)
{
    if (wrongway) wrongway->vt->destroy(wrongway);
    wrongway = NULL;
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
    PENDING_NOTE(0x425910, "the pursuit's time trays (game type 9: 0x60ca1c, 0x60ca40)");
    Car *me = local_car();
    if (wrongway && me && *CAR_I(me, 0xd30) != 0 && g_race_mode != 9 && g_race_view != 2 && 0x1c0 < g_sim_tick) wrongway->vt->draw(wrongway);
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
