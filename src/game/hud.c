/* The race HUD (see hud.h). */
#include "game/hud.h"
#include "real/mouse.h"
#include "exe.h"
#include "game/car.h"
#include "game/camera.h"
#include "game/car_drive.h"
#include "game/audio.h"
#include "game/hud_config.h"
#include "game/race_setup.h"
#include "game/race_car.h"
#include "game/road.h"
#include "game/race_progress.h"
#include "game/mission.h"
#include "eagl/eagl.h"
#include "pending.h"
#include "render/hud2d.h"
#include "render/hudart.h"
#include "render/r3dcar.h"
#include <math.h>
#include "real/font.h"
#include "real/loc.h"
#include "real/random.h"
#include "game/music.h"
#include "game/input.h"
#include "game/replay_file.h"
#include "app.h"
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
/* The modal visuals in turn (0x60c9c4): the first shown, the next when it is done. */
static HudVisual *modal[8];
static int nmodal;
static HudVisual *summary;   /* 0x60ca4c */
static HudArt *s_pointer;           /* 0x60ca7c: the mouse's pointer (DashHud\Pointer.fsh) */
static int32_t s_mousex, s_mousey;  /* 0x60ca80, 0x60ca84: where the mouse was */
static int32_t s_pointer_ticks;     /* 0x60ca88: the ticks left showing it after the mouse moved */
static int32_t s_pointer_last;      /* 0x60ca8c: the tick of the last draw */
static bool s_noscreens;     /* 0x60ca10: the race-over's screens skipped */
static int32_t s_place;        /* 0x60ca6c: the player's place at the race's end */
static bool s_raceover_done;   /* 0x60ca70: the race-over's screens queued once */
static bool s_restarted;       /* 0x60ca78: "race again" chosen in the pause menu */
static HudVisual *mission;   /* 0x60ca50 */
static HudVisual *s_time;    /* 0x60ca1c */
static HudVisual *s_multitime;   /* 0x60ca40 */
static int s_cones_down;     /* 0x60ca04: the traffic objects knocked, last counted */
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

/* The ring (0x418930): the screen darkened (0x33000000) by a clock wipe from the top, a sixteenth of a
   turn per frame of the step: triangles from the centre to the screen's edge at its corners, the edges'
   middles and the points a quarter in from them (the original's table holds the sixteen three times, one
   copy per step; their points are the same). */
static void go_ring(H321Go *g, int frame, int step)
{
    const float cx = (float)g->cx, cy = (float)g->cy, w = (float)g->w, h = (float)g->h;
    const float w3 = (float)(g->w * 3 / 4), h3 = (float)(g->h * 3 / 4), cx2 = (float)(g->cx / 2), cy2 = (float)(g->cy / 2);
    const float edge[17][2] = {{cx, 0}, {w3, 0}, {w, 0}, {w, cy2}, {w, cy}, {w, h3}, {w, h}, {w3, h}, {cx, h},
                               {cx2, h}, {0, h}, {0, h3}, {0, cy}, {0, cy2}, {0, 0}, {cx2, 0}, {cx, 0}};
    int last = frame + step * 16;
    for (int i = step * 16; i <= last; i++) {
        int k = i % 16;
        float t[12] = {cx, cy, 0, 1, edge[k][0], edge[k][1], 0, 1, edge[k + 1][0], edge[k + 1][1], 0, 1};
        Hud2D_TriNow(t, 0x33000000u);
    }
}

/* From tick 256 to 448 (the start): the rings, the cross through the centre, the digit of the step (3, 2,
   1, a step each 64 ticks), its frame (the tick within the step, in sixteenths). 0x418510 */
static void go_draw(HudVisual *v)
{
    H321Go *g = (H321Go *)v;
    if (g_race_mode == 6 || g_race_mode == 7 || g_race_mode == 8 || HUD_ModalShown()) return;   /* 0x426a20 */
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
        go_ring(g, frame, step);
        return;
    }
    if (v->state != 0) v->state = 0;
}

static const HudVisualVtbl go_vt = {go_destroy, go_draw, NULL, NULL, NULL, NULL, NULL, NULL, NULL};   /* 0x5b27ec */

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
    g->plain = g_render_modalpha != 1;   /* 0x6197aa */
    return &g->base;
}

/* A shade over (x, y, w, h): white fading down to the middle, black up from it, alpha percent of 0x7f.
   0x435d00 */
static void tray_shade(int x, int y, int w, int h, int alpha)
{
    if (g_hud_opaque) return;
    uint32_t a = (uint32_t)(alpha * 0x7f / 100) << 24;
    float fx = (float)x, fy = (float)y, x1 = (float)(x + w), ym = (float)(h / 2 + y), y1 = (float)(y + h);
    float q0[16] = {fx, fy, 0, 1, x1, fy, 0, 1, x1, ym, 0, 1, fx, ym, 0, 1};
    float q1[16] = {fx, ym, 0, 1, x1, ym, 0, 1, x1, y1, 0, 1, fx, y1, 0, 1};
    uint32_t c0[4] = {a | 0xffffff, a | 0xffffff, 0xffffff, 0xffffff}, c1[4] = {0, 0, a, a};
    Hud2D_Quad(q0, c0);
    Hud2D_Quad(q1, c1);
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
    if (t->shade) tray_shade(t->x, t->y, t->w, t->h, a);
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

static const HudVisualVtbl gear_vt = {combo_destroy, gear_draw, gear_reset, tray_setstate, combo_setpos, tray_progress, NULL, NULL, NULL};

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

static const HudVisualVtbl speed_vt = {combo_destroy, speed_draw, speed_reset, tray_setstate, combo_setpos, tray_progress, NULL, NULL, NULL};
static const HudVisualVtbl tachdigital_vt = {combo_destroy, tachdigital_draw, tachdigital_reset, tray_setstate, combo_setpos, tray_progress, NULL, NULL, NULL};

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
static void value_red(HCombo *c) { if (c->value) c->value->colour = 0xccff0000u; }               /* 0x41ca80 */

/* The race's time (from the start, tick 0x1c0; the finish's once finished, +0xb50 2), with the
   penalties' (0x657830 each); "time" (hud.loc 0x12), red in a mission over its damage. 0x431650 */
static void time_draw(HudVisual *v)
{
    HTime *t = (HTime *)v;
    Car *car = local_car();
    if (always && v->state != 0 && car) {
        if (g_race_mode == 9 && Mission_DamageOver(true)) value_red(&t->combo);   /* 0x434f00 */
        else if (t->flash == 0) value_plain(&t->combo);
        int now = *CAR_I(car, 0xb50) == 2 ? *CAR_I(car, 0xb24) : g_sim_tick - 0x1c0;
        if (now < 0) now = 0;
        int pen = t->penalties * g_mission_penalty;   /* (-1 without a penalty: the cones' fall takes time off) */
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
    if (g_race_linear) label = 0x15, n = g_track_records[0];
    else if (g_hud_splits == 1) label = 0x16, n = g_lap_records[0];
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

static const HudVisualVtbl time_vt = {combo_destroy, time_draw, time_reset, tray_setstate, combo_setpos, tray_progress, NULL, NULL, NULL};
static const HudVisualVtbl multitime_vt = {combo_destroy, multitime_draw, multitime_reset, tray_setstate, combo_setpos, tray_progress, NULL, NULL, NULL};

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
        if (g_race_derby == 1) {
            /* the derby's: the car's damage, its ten zones (+0x4e4) summed, cut to a whole number at
               each step (the x87's __ftol), over ten */
            int32_t sum = 0;
            for (int k = 0; k < 10; k++) sum = (int32_t)((double)sum + (double)*CAR_F(c, 0x4e4 + 4 * k));
            snprintf(g, 12, "%d", sum / 10);
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
    if (!g_race_derby && me) {
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

static const HudVisualVtbl position_vt = {position_destroy, position_draw, position_reset, tray_setstate, position_setpos, tray_progress, NULL, NULL, NULL};

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

/* The damage trays' title colour: a Factory Driver mission's damage over its limit (0x434f00) turns it
   yellow on the red scheme (0xccff0606), else red; otherwise the layout's +0x18. */
static uint32_t damage_title_colour(void)
{
    if (g_race_mode == 9 && Mission_DamageOver(true)) return HudConfig_scheme() == 0xccff0606u ? 0xccffff00u : 0xccff0000u;
    return (uint32_t)HudConfig_i32(0x18);
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
    if (g->title) g->title->colour = damage_title_colour();
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

static const HudVisualVtbl gdamage_vt = {gdamage_destroy, gdamage_draw, gdamage_reset, tray_setstate, gdamage_setpos, tray_progress, NULL, NULL, NULL};

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

static void flat(const float q[16], uint32_t col);

/* ---- the damage in words (HTextDamage, 0x114 bytes, vtable 0x5b3120; 0x42f880): a tray of the four
   parts' condition (the engine, the tyres' zones, the body, the suspension), each as a percentage or a
   bar, titled "damage" at its foot; shown by the second layout's +0x3c 2 or 3 ---- */

typedef struct {
    HTray tray;
    HText *label[4];   /* +0xc4 .. +0xd0 */
    HText *title;      /* +0xd4 */
    HText *value[4];   /* +0xd8 .. +0xe4: right aligned */
    char buf[4][5];    /* +0xe8: "100%" */
    const char *name[4];   /* +0x104: hud.loc 0xd .. 0x10 */
} HTextDamage;

/* Row k's place: its left (or right, the values') at the tray's margin, a fifth of the height a row;
   the title's (k 4) at the foot. 0x42ff30, 0x430000 */
static void tdamage_row(HTextDamage *d, int k, bool right, int px, int py, int32_t out[2])
{
    int w = HudConfig_i32(0xac), h = HudConfig_i32(0xb0), m = HudConfig_i32(0x34);
    out[0] = right ? w + px - m : m + px;
    if (k == 4) {
        const char *t = LOC_string(HudConfig_loc(), 0xc);
        float a, b, c, th = 0;
        if (d->title && d->title->font && d->title->font->font) text_extent(d->title, t ? t : "", &a, &b, &c, &th);
        out[1] = py + (int)((float)(d->tray.h - HudConfig_i32(0x38)) - th);
    } else {
        out[1] = py + h * k / 5;
    }
}

static void tdamage_setpos(HudVisual *v, int x, int y)   /* HTextDamage_v12 0x4307f0 */
{
    HTextDamage *d = (HTextDamage *)v;
    int32_t px, py, at[2];
    HudConfig_setpos(HUDPOS_TDAMAGE, x, y);   /* 0x420700 */
    HudConfig_pos(HUDPOS_TDAMAGE, &px, &py);
    tray_place(&d->tray, px, py);
    for (int k = 0; k < 4; k++) {
        tdamage_row(d, k, false, px, py, at);
        if (d->label[k]) d->label[k]->x = at[0], d->label[k]->y = at[1];
        tdamage_row(d, k, true, px, py, at);
        if (d->value[k]) d->value[k]->x = at[0], d->value[k]->y = at[1];
    }
    tdamage_row(d, 4, false, px, py, at);
    if (d->title) d->title->x = at[0], d->title->y = at[1];
}

/* The divider above the title, at half the inner margin. */
static void tdamage_divider(HTextDamage *d)
{
    const char *t = LOC_string(HudConfig_loc(), 0xc);
    float a, b, c, th = 0;
    if (d->title && d->title->font && d->title->font->font) text_extent(d->title, t ? t : "", &a, &b, &c, &th);
    float lo = (float)(d->tray.h - HudConfig_i32(0x38)) - th;
    tray_setsize(&d->tray, HudConfig_i32(0xac), HudConfig_i32(0xb0), (int)(lo - (float)(HudConfig_i32(0x38) / 2)));
}

/* The layout again: the size, place and colours. HTextDamage_v2 0x430a00 */
static void tdamage_reset(HudVisual *v)
{
    HTextDamage *d = (HTextDamage *)v;
    tdamage_divider(d);
    int32_t px, py;
    HudConfig_pos(HUDPOS_TDAMAGE, &px, &py);
    tdamage_setpos(v, px, py);
    tray_colours(&d->tray);
    uint32_t c14 = (uint32_t)HudConfig_i32(0x14);
    for (int k = 0; k < 4; k++) {
        if (d->label[k]) d->label[k]->colour = (uint32_t)HudConfig_i32(0x1c), d->label[k]->colour2 = c14;
        if (d->value[k]) d->value[k]->colour = HudConfig_scheme(), d->value[k]->colour2 = c14;
    }
    if (d->title) d->title->colour = (uint32_t)HudConfig_i32(0x18), d->title->colour2 = c14;
}

/* HTextDamage_v1 0x4300d0 */
static void tdamage_draw(HudVisual *v)
{
    HTextDamage *d = (HTextDamage *)v;
    Car *car = local_car();
    int kind = HudConfig_choicei32(0x3c);
    if (!((kind == 2 || kind == 3) && always) || v->state == 0 || !car) return;   /* 0x41e8f0 */
    tray_draw(&d->tray);
    int val[4] = {(int)CarSim_EngineWear(car), (int)((*CAR_F(car, 0x4ec) + *CAR_F(car, 0x4e4)) * exe_f32(0x5b23f0)),
                  (int)CarSim_BodyWear(car), (int)CarSim_SuspensionWear(car)};   /* 0x493810, 0x4938d0, 0x493c00, 0x493b30 */
    for (int k = 0; k < 4; k++) text_print(d->label[k], d->name[k] ? d->name[k] : "", 1.0f, 1.0f);
    if (d->title) d->title->colour = damage_title_colour();
    const char *t = LOC_string(HudConfig_loc(), 0xc);
    text_print(d->title, t ? t : "", 1.0f, 1.0f);
    if (g_hud_damage_display == 3) {
        for (int k = 0; k < 4; k++) {
            const char *s = exe_str(0x5ccc8c);   /* "0%" */
            if (val[k] != 0) {
                snprintf(d->buf[k], sizeof d->buf[k], "%d%s", val[k], exe_str(0x5ccc90));
                s = d->buf[k];
            }
            text_print(d->value[k], s, 1.0f, 1.0f);
        }
        return;
    }
    /* The bars under the labels: the condition's share in the layout's +0x28, the rest in its +0x2c, as
       thick as its +0x3c. */
    int32_t px, py;
    HudConfig_pos(HUDPOS_TDAMAGE, &px, &py);
    int w = HudConfig_i32(0xac), h = HudConfig_i32(0xb0), m = HudConfig_i32(0x34), bar = HudConfig_i32(0x3c);
    int len = w - 2 * m;
    for (int k = 0; k < 4; k++) {
        int x0 = px + m, y = py + h * k / 5;
        float a, b, c, th = 0;
        if (d->label[0] && d->label[0]->font && d->label[0]->font->font) text_extent(d->label[0], d->name[k] ? d->name[k] : "", &a, &b, &c, &th);
        float top = (float)y + exe_f32(0x5b2404) + th, foot = (float)bar + (float)y + th + exe_f32(0x5b2404);
        float split = (float)(x0 + val[k] * len / 100);
        float q[16] = {(float)x0, top, 0, 1, split, top, 0, 1, split, foot, 0, 1, (float)x0, foot, 0, 1};
        flat(q, (uint32_t)HudConfig_i32(0x28));   /* 0x419f70 */
        if (val[k] != 100) {
            float end = (float)(x0 + len);
            float r[16] = {split, top, 0, 1, end, top, 0, 1, end, foot, 0, 1, split, foot, 0, 1};
            flat(r, (uint32_t)HudConfig_i32(0x2c));
        }
    }
}

static void tdamage_destroy(HudVisual *v)   /* 0x42fe80 */
{
    HTextDamage *d = (HTextDamage *)v;
    for (int k = 0; k < 4; k++) text_free(d->label[k]), text_free(d->value[k]);
    text_free(d->title);
    free(d);
}

static const HudVisualVtbl tdamage_vt = {tdamage_destroy, tdamage_draw, tdamage_reset, tray_setstate, tdamage_setpos, tray_progress, NULL, NULL, NULL};

static HudVisual *tdamage_new(void)
{
    HTextDamage *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    int32_t px, py;
    HudConfig_pos(HUDPOS_TDAMAGE, &px, &py);
    int w = HudConfig_i32(0xac), h = HudConfig_i32(0xb0);
    tray_init(&d->tray, &tdamage_vt, px, py, w, h, 0, false, 0, false, false);   /* 0x433ef0 */
    uint32_t c14 = (uint32_t)HudConfig_i32(0x14);
    for (int k = 0; k < 4; k++) {
        d->label[k] = text_new(HudConfig_font(100), (uint32_t)HudConfig_i32(0x1c), c14, 0, 0, 0, h / 5, 0, false);
        d->value[k] = text_new(HudConfig_font(0x7c), HudConfig_scheme(), c14, 0, 0, 0, h / 5, 1, false);
    }
    d->title = text_new(HudConfig_font(0x4c), (uint32_t)HudConfig_i32(0x18), c14, 0, 0, w, h / 5, 0, false);
    tdamage_setpos(&d->tray.base, px, py);
    tdamage_divider(d);
    for (int k = 0; k < 4; k++) d->name[k] = LOC_string(HudConfig_loc(), 0xd + k);
    return &d->tray.base;
}

/* ---- the track map (HTrackMap, 0x150 bytes, vtable 0x5b3248; 0x431e50) ---- */

/* Text drawn at once in the current view (0x431230 with its second argument: 0x536a30, 0x536710). */
static void text_print_now(HText *t, const char *s)
{
    if (!t || !t->font || !t->font->drawer || !g_render || !g_render->camera) return;
    int x = t->x;
    if (t->align == 1) {
        if (t->width == 0.0f || !t->keep) {
            float a, b, h;
            FONT_extent(t->font->font, s, &a, &b, &t->width, &h);
        }
        x = (int)((float)x - t->width);
    }
    FONT_setstate(t->font->font, 0xc, g_hud_opaque ? t->colour | 0xff000000u : t->colour);
    HudFont_print(t->font->drawer, g_render->camera->view, (float)x, (float)t->y, -1.0f, s);
}

enum { MAP_POINTS = 400, MAP_JUNCTIONS = 0xc8 };

static float node_f(int n, int off)
{
    float v;
    memcpy(&v, g_road->nodes + (size_t)n * ROAD_NODE_SIZE + off, 4);
    return v;
}
static float node_x(int n) { return node_f(n, 4); }
static float node_z(int n) { return node_f(n, 0xc); }

/* The road as points along it (0x60e4a0: x, 1, z, its segment), a point each time the road has turned
   by a share of all its turning; the junctions' ends as lines to their centres (0x60cb70). */
typedef struct {
    HTray tray;
    int32_t rx, ry, rw, rh;        /* +0xc4: the view's rectangle (HRenderVisual 0x42c7c0) */
    RCamera *camera;               /* +0xc8: the view */
    uint32_t grid[2];              /* +0xcc: the grid lines' colours (the layout's +0x30) */
    HText *label, *lap, *laps;     /* +0xd4 "lap", +0xd8 "N/", +0xdc "M" */
    HudArt *dot, *blip;            /* +0xe0, +0xe4 */
    int32_t cx, cz, w, d;          /* +0xe8 .. +0xf4: the road's middle and size */
    char lapbuf[4], lapsbuf[4];    /* +0xf8, +0xfc */
    int32_t njunctions, npoints;   /* +0x100, +0x104 */
    uint8_t whole;                 /* +0x14a: zoomed out to the whole track */
    float extent;                  /* +0x14c */
    float point[MAP_POINTS][4];
    float junction[MAP_JUNCTIONS][6];
} HTrackMap;

static int32_t lastpos[2] = {-1, -1};   /* 0x60fda0: the map's place last frame */

static float heading(float dz, float dx) { return (float)(atan2((double)dz, (double)dx) * exe_f64(0x5bb2c8)); }   /* 0x5325d0 */

static int seg_of(int n)   /* 0x481b20 */
{
    for (int s = 0; s < g_road->nseg; s++)
        if (g_road->seg[s].first <= n && n <= g_road->seg[s].last) return s;
    return -1;
}

static const RoadJunction *end_junction(int n)   /* 0x481640 */
{
    int j = g_road->node_end[n];
    return 0 <= j ? &g_road->junctions[j] : NULL;
}

static float turn(float a, float b)
{
    float d = fabsf(a - b);
    if (exe_f32(0x5b23f0) < d) d = exe_f32(0x5b24a8) - d;
    return fabsf(d);
}

static void map_point(HTrackMap *m, float x, float z, int seg)
{
    float *p = m->point[m->npoints++];
    p[0] = x, p[1] = 1.0f, p[2] = z;
    memcpy(&p[3], &seg, 4);
}

static void map_junction(HTrackMap *m, float x, float z, const RoadJunction *j)
{
    if (MAP_JUNCTIONS <= m->njunctions) return;   /* (port: the original has no bound) */
    float *p = m->junction[m->njunctions++];
    p[0] = x, p[1] = 1.0f, p[2] = z, p[3] = j->centre[0], p[4] = 1.0f, p[5] = j->centre[2];
}

/* 0x432190 */
static void map_build(HTrackMap *m)
{
    const Road *r = g_road;
    int n = r ? r->nnodes : 0;
    float total = 0;
    for (int i = 0; i < n;) {
        float px = node_x(i), pz = node_z(i);
        float h = heading(node_z(i + 1 < n ? i + 1 : i) - pz, node_x(i + 1 < n ? i + 1 : i) - px);
        int s = seg_of(i);
        i++;
        if (s != seg_of(i)) continue;
        do {
            float prev = h;
            if (n <= i) goto measured;
            float qx = node_x(i), qz = node_z(i);
            h = heading(qz - pz, qx - px);
            total += turn(h, prev);
            i++;
            px = qx, pz = qz;
        } while (s == seg_of(i));
    }
measured:;
    float step = total * exe_f32(0x5b3284);
    m->npoints = m->njunctions = 0;
    for (int i = 0; i < n && m->npoints < MAP_POINTS;) {
        float px = node_x(i), pz = node_z(i);
        int s = seg_of(i);
        float h = heading(node_f(i, 0x5c) - pz, node_f(i, 0x54) - px);
        map_point(m, px, pz, s);
        const RoadJunction *j = end_junction(i);
        if (j) map_junction(m, px, pz, j);
        i++;
        float acc = 0;
        if (s == seg_of(i)) {
            do {
                float prev = h;
                if (n <= i) goto built;
                if (399 < m->npoints) break;
                float qx = node_x(i), qz = node_z(i);
                h = heading(qz - pz, qx - px);
                acc += turn(h, prev);
                if (step <= acc) {
                    map_point(m, px, pz, s);
                    acc -= step;
                }
                s = seg_of(i);
                j = end_junction(i);
                if (j) map_junction(m, qx, qz, j);
                i++;
                px = qx, pz = qz;
            } while (s == seg_of(i));
        }
        if (n <= i) break;
        if (m->npoints < MAP_POINTS) map_point(m, px, pz, s);
    }
built:;
    float maxx = 0, maxz = 0, minx = 0, minz = 0;   /* (from 0, as the original) */
    for (int i = 0; i < m->npoints; i++) {
        if (maxx < m->point[i][0]) maxx = m->point[i][0];
        if (maxz < m->point[i][2]) maxz = m->point[i][2];
        if (m->point[i][0] < minx) minx = m->point[i][0];
        if (m->point[i][2] < minz) minz = m->point[i][2];
    }
    float w = maxx - minx, d = maxz - minz;
    m->w = (int)w;
    m->d = (int)d;
    m->extent = (float)(m->w < m->d ? m->d : m->w);
    m->cx = (int)(w * 0.5f + minx);
    m->cz = (int)(d * 0.5f + minz);
}

static float map_zoom(void)   /* 0x41e9e0 */
{
    float z = 0;
    switch (HudConfig_choicei32(0x44)) {
    case 2: z = exe_f32(0x5b2a84); break;
    case 3: z = exe_f32(0x5b2a80); break;
    case 4: z = exe_f32(0x5b242c); break;
    default: break;
    }
    if (HudConfig_choicei32(0x40) != 3) return z * exe_f32(0x5b2a78) + exe_f32(0x5b2a74);
    return z * exe_f32(0x5b2a7c) + exe_f32(0x5b2400);
}

/* The view from above: flat, at the track's middle (zoomed out to it) or the player's car, turned with
   the car (mode 1) or the road (mode 3), scaled by the zoom. 0x4333a0 */
static void map_view(HTrackMap *m, EaglView *v, bool turned, bool road)
{
    Car *me = local_car();
    EaglMatrix *top = EAGL_StackTopForWrite(&v->view), t;
    EAGL_MatScale(top, 1.0f, 0, 1.0f, 1.0f);
    float z = map_zoom();
    if (exe_f32(0x5b2454) == z) {
        m->whole = 1;
        EAGL_MatTranslation(&t, -(float)m->cx, 100.0f, -(float)m->cz);
    } else {
        m->whole = 0;
        EAGL_MatTranslation(&t, me ? -CAR_F(me, 0x330)[0] : 0, 100.0f, me ? -CAR_F(me, 0x330)[2] : 0);
    }
    EAGL_MatPostMul(top, &t);
    EAGL_MatAxisAngle(&t, -90.0f, 1.0f, 0, 0);
    EAGL_MatPostMul(top, &t);
    if (road || turned) {
        float a = 0;
        if (road && me) {
            int16_t node;
            memcpy(&node, me->b + 8, 2);
            a = heading(node_f(node, 0x1c), node_f(node, 0x24)) * exe_f32(0x5b25cc);
        } else if (me) {
            a = heading(CAR_F(me, 0x364)[0], CAR_F(me, 0x364)[2]) * exe_f32(0x5b25cc) - exe_f32(0x5b3288);
        }
        EAGL_MatAxisAngle(&t, a, 0, 0, 1.0f);
        EAGL_MatPostMul(top, &t);
    }
    if (m->whole && HudConfig_choicei32(0x40) != 3) z = m->extent * exe_f32(0x5b23f0) + exe_f32(0x5b2484);
    float mx = g_r3d_mirror ? exe_f32(0x5b2624) : exe_f32(0x5b24a8);
    EAGL_MatScale(&t, mx * (exe_f32(0x5b24a8) / z), exe_f32(0x5b24a8) / z, 1.0f, 1.0f);
    EAGL_MatPostMul(top, &t);
    EAGL_ViewSetOrtho(v, 1.0f, 1.0f, 800.0f);
}

static uint32_t s_flash[CAR_MAX];     /* 0x5cd158: each car's blip colour (a cop's) */
static int32_t s_flash_tick[CAR_MAX]; /* 0x60fda8: its last turn */
static bool s_flash_read;

/* The cars: the racers placed (and the cops), a blip the size of their footprint times s. 0x433070 */
static void map_blips(HudArt *art, int s)
{
    if (!art) return;
    for (int i = g_ncars - 1; 0 <= i; i--) {
        Car *c = g_cars[i];
        uint32_t f = (uint32_t)*CAR_I(c, CAR_FLAGS);
        if (!((f & 1) || (f & 0x20)) || !c->b[0x7f]) continue;
        if ((f & 1) && g_race_mode != 9 && !(0 < *CAR_I(g_racers.c[i < g_racers.n ? i : 0], 0xb48))) continue;
        float r[3], fw[3], p[3];
        float hl = (float)s * *CAR_F(c, 0x3b0), hw = (float)s * *CAR_F(c, 0x3a8);
        for (int k = 0; k < 3; k++) r[k] = CAR_F(c, 0x364)[k] * hw, fw[k] = CAR_F(c, 0x37c)[k] * hl, p[k] = CAR_F(c, 0x330)[k];
        float q[12];
        for (int k = 0; k < 3; k++) {
            q[k] = p[k] + fw[k] + r[k];        /* front right */
            q[3 + k] = p[k] + fw[k] - r[k];    /* front left */
            q[6 + k] = p[k] - fw[k] - r[k];
            q[9 + k] = p[k] - fw[k] + r[k];
        }
        HudArt_setcorners(art, q);
        uint32_t a, b;
        if (!(f & 0x20)) {
            uint32_t col;
            memcpy(&col, CAR_DESCB(c) + 0x218, 4);
            if ((col & 0xff0000) < 0x14 && (col & 0xff00) < 0x14 && (col & 0xff) < 0x14) col = 0xff202020u;
            a = b = col;
        } else {
            /* a cop's blip flashes blue and red, turned each 0x20 ticks (0x60fda8, 0x5cd158 by car) */
            if (!s_flash_read) {
                /* (nine words, blue; the original's later cars read and write past them) */
                for (int k = 0; k < CAR_MAX; k++) s_flash[k] = exe_u32(0x5cd158 + 4u * (uint32_t)k);
                s_flash_read = true;
            }
            if (0x20 < g_sim_tick - s_flash_tick[i]) {
                s_flash_tick[i] = g_sim_tick;
                s_flash[i] = s_flash[i] != 0xc00000ffu ? 0xc00000ffu : 0xc0ff0000u;
            }
            b = s_flash[i];
            a = b != 0xc00000ffu ? 0xc00000ffu : 0xc0ff0000u;
        }
        HudArt_setcolours(art, b, a, a, b);
        HudArt_draw(art);
    }
}

/* The road's lines and the cars. 0x4335f0 */
static void map_world(HTrackMap *m)
{
    View_Begin(g_render, m->camera);
    RenderState_SetZFunc(g_render->device->rs, 7);
    bool road = HudConfig_choicei32(0x40) == 3;
    map_view(m, m->camera->view, HudConfig_choicei32(0x40) == 1 || road, road);
    static float v[0xfc][4];
    if (!road) {
        int k = 0;
        for (int i = 0; i < m->npoints - 1; i++) {
            if (memcmp(&m->point[i][3], &m->point[i + 1][3], 4) != 0) continue;
            float a[4] = {m->point[i][0], 0, m->point[i][2], 1}, b[4] = {m->point[i + 1][0], 0, m->point[i + 1][2], 1};
            memcpy(v[2 * k], a, 16);
            memcpy(v[2 * k + 1], b, 16);
            if (++k == 0x7e) {
                Hud2D_LinesNow(0xfc, &v[0][0], 0xffffffffu);
                k = 0;
            }
        }
        if (k) Hud2D_LinesNow(2 * k, &v[0][0], 0xffffffffu);
        k = 0;
        for (int i = 0; i < m->njunctions; i++) {
            float a[4] = {m->junction[i][0], 0, m->junction[i][2], 1}, b[4] = {m->junction[i][3], 0, m->junction[i][5], 1};
            memcpy(v[2 * k], a, 16);
            memcpy(v[2 * k + 1], b, 16);
            if (++k == 0x7e) {
                Hud2D_LinesNow(0xfc, &v[0][0], 0xffffffffu);
                k = 0;
            }
        }
        if (k) Hud2D_LinesNow(2 * k, &v[0][0], 0xffffffffu);
    }
    map_blips(m->blip, road ? 1 : (m->whole ? 0x3c : 0x14));
    RenderState_SetZFunc(g_render->device->rs, 3);
    View_End(g_render);
}

/* The grid over the map, its shade, the laps. 0x432ac0 */
static void map_grid(HTrackMap *m)
{
    int mw = HudConfig_i32(0xe4), mh = HudConfig_i32(0xe8), W = g_display.width, H = g_display.height;
    View_Begin(g_render, m->camera);
    RenderState_SetZFunc(g_render->device->rs, 3);
    Hud2D_View(g_render);
    if (!HudConfig_notrays()) {
        float q[16] = {0, 0, 2.0f, 1, (float)W, 0, 2.0f, 1, (float)W, (float)H, 2.0f, 1, 0, (float)H, 2.0f, 1};
        Hud2D_QuadNow(q, 0x3f000000u);   /* 0x465f60 */
    }
    int sp = HudConfig_i32(0x10c);
    if (0 < sp) {
        int n = mw / sp, step = sp * W / (mw ? mw : 1);
        for (int k = 1, x = step; k < n; k++, x += step) {
            float l[8] = {(float)x, 0, 0, 1, (float)x, (float)H, 0, 1};
            Hud2D_LineNow(l, m->grid);
        }
        n = mh / sp, step = sp * H / (mh ? mh : 1);
        for (int k = 1, y = step; k < n; k++, y += step) {
            float l[8] = {0, (float)y, 0, 1, (float)W, (float)y, 0, 1};
            Hud2D_LineNow(l, m->grid);
        }
    }
    int32_t px, py;
    HudConfig_pos(HUDPOS_MAP, &px, &py);
    if (px == lastpos[0] && py == lastpos[1] && !g_race_linear && m->label && m->lap && m->laps) {
        const char *s = LOC_string(HudConfig_loc(), 0x13);
        text_print_now(m->label, s ? s : "");
        float sx = (float)mw / (float)W, a, b, w, h;
        snprintf(m->lapsbuf, sizeof m->lapsbuf, "%d", g_race_laps);
        int ax = m->laps->x, ay = m->laps->y;
        text_extent(m->laps, m->lapsbuf, &a, &b, &w, &h);
        m->laps->x = (int)((float)ax - w / sx);
        text_print_now(m->laps, m->lapsbuf);
        m->laps->x = ax, m->laps->y = ay;
        Car *me = local_car();
        int now = me ? *CAR_I(me, 0xb20) + 1 : 1;
        if (g_race_laps <= now) now = g_race_laps;
        snprintf(m->lapbuf, sizeof m->lapbuf, "%d%s", now, exe_str(0x5ccde0));
        text_extent(m->lap, m->lapbuf, &a, &b, &w, &h);
        m->lap->x = (int)((float)(int)((float)ax - w / sx) - w / sx);
        m->lap->y = ay;
        text_print_now(m->lap, m->lapbuf);
    }
    lastpos[0] = px, lastpos[1] = py;
    View_End(g_render);
}

/* Shown by the second layout's +0x40 (1 turned with the car, 2 north up, 3 the road ahead). 0x4329c0 */
static void map_draw(HudVisual *v)
{
    HTrackMap *m = (HTrackMap *)v;
    if (g_race_view == 2 || g_race_view == 3) return;
    if ((HudConfig_choicei32(0x40) == 0 && !(v->state == 1 || v->state == 2)) || v->state == 0) return;   /* 0x41e7d0 */
    if (!m->camera || !g_render) return;
    PENDING_NOTE(0x4329c0, "the map's guard band off (EAGL context +0x117e4)");
    tray_draw(&m->tray);
    if (v->state == 3) {
        map_grid(m);
        map_world(m);
    }
}

/* Its place, its view (a camera over its rectangle), the laps' texts. 0x4327e0 */
static void map_setpos(HudVisual *v, int x, int y)
{
    HTrackMap *m = (HTrackMap *)v;
    int32_t px, py;
    HudConfig_setpos(HUDPOS_MAP, x, y);
    HudConfig_pos(HUDPOS_MAP, &px, &py);
    tray_place(&m->tray, px, py);
    m->rx = px, m->ry = py;
    if (m->camera) RCamera_Free(m->camera);
    m->camera = NULL;
    float W = (float)g_display.width, H = (float)g_display.height;
    float nx = (float)px / W, ny = (float)py / H, sw = (float)HudConfig_i32(0xe4) / W, sh = (float)HudConfig_i32(0xe8) / H;   /* 0x420c00, 0x420970 */
    if (g_render) m->camera = Render_CreateCamera(g_render, 4, nx, ny, nx + sw, ny + sh, 0, g_render->znear, g_render->zfar);
    if (!m->label || !m->label->font || !m->label->font->font) return;
    const char *s = LOC_string(HudConfig_loc(), 0x13);
    float a, b, c, h;
    text_extent(m->label, s ? s : "", &a, &b, &c, &h);
    int ty = (int)((H - (float)HudConfig_i32(0x38) / sh) - h / sh);
    m->label->x = HudConfig_i32(0x34), m->label->y = ty;
    if (m->laps) m->laps->x = (int)(W - (float)HudConfig_i32(0x34) / sw), m->laps->y = ty;
}

static void map_reset(HudVisual *v)   /* 0x432fe0 */
{
    HTrackMap *m = (HTrackMap *)v;
    tray_setsize(&m->tray, HudConfig_i32(0xe4), HudConfig_i32(0xe8), 0);
    int32_t x, y;
    HudConfig_pos(HUDPOS_MAP, &x, &y);
    v->vt->setpos(v, x, y);
    tray_colours(&m->tray);
    map_build(m);
    uint32_t c14 = (uint32_t)HudConfig_i32(0x14);
    if (m->label) m->label->colour = (uint32_t)HudConfig_i32(0x18), m->label->colour2 = c14;
    if (m->lap) m->lap->colour = HudConfig_scheme(), m->lap->colour2 = c14;
    if (m->laps) m->laps->colour = (uint32_t)HudConfig_i32(0x18), m->laps->colour2 = c14;
}

static void map_destroy(HudVisual *v)   /* 0x4320f0 */
{
    HTrackMap *m = (HTrackMap *)v;
    if (m->camera) RCamera_Free(m->camera);
    text_free(m->label);
    text_free(m->lap);
    text_free(m->laps);
    HudArt_free(m->dot);
    HudArt_free(m->blip);
    free(m);
}

/* Kept hidden but with its layout's +0x40 on and the HUD shown. 0x433360 */
static bool map_hidden(HudVisual *v) { (void)v; return !(HudConfig_choicei32(0x40) != 0 && always); }   /* 0x41e7d0, 0x41e7c0 */

static const HudVisualVtbl map_vt = {map_destroy, map_draw, map_reset, tray_setstate, map_setpos, tray_progress, NULL, NULL, map_hidden};

static HudVisual *map_new(void)
{
    HTrackMap *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    int32_t x, y;
    HudConfig_pos(HUDPOS_MAP, &x, &y);
    int w = HudConfig_i32(0xe4), h = HudConfig_i32(0xe8);
    tray_init(&m->tray, &map_vt, x, y, w, h, 0, true, 0, true, false);
    map_build(m);
    m->grid[0] = m->grid[1] = (uint32_t)HudConfig_i32(0x30);   /* 0x420470 */
    PENDING_NOTE(0x431e50, "the map's debug texts (0x60d4d4)");
    uint32_t c14 = (uint32_t)HudConfig_i32(0x14);
    m->label = text_new(HudConfig_font(0x4c), (uint32_t)HudConfig_i32(0x18), c14, 0, 0, w, 0, 0, false);
    m->lap = text_new(HudConfig_font(0x4c), HudConfig_scheme(), c14, 0, 0, w, 0, 0, false);
    m->laps = text_new(HudConfig_font(0x4c), (uint32_t)HudConfig_i32(0x18), c14, 0, 0, w, 0, 0, false);
    m->dot = HudArt_load(exe_str(0x5cd198), 0, 0, 0, 9, 9, true, false, false);      /* "radar" */
    m->blip = HudArt_load(exe_str(0x5cd198), 0, 0, 0, 0x1c, 0x3f, true, false, false);
    map_setpos(&m->tray.base, x, y);
    return &m->tray.base;
}

/* ---- the billboards (HBillBoard, vtable 0x5b28bc; 0x41a530): a panel opening downwards from its title bar
   to its full height, a title bar and a key bar with a dark band sweeping along them, a frame and the
   bars' shades ---- */

/* A rectangle's four corners (x y z w). 0x435880 */
static void box(int x, int y, int w, int h, float out[16])
{
    float q[16] = {(float)x, (float)y, 0, 1, (float)(x + w), (float)y, 0, 1, (float)(x + w), (float)(y + h), 0, 1, (float)x, (float)(y + h), 0, 1};
    memcpy(out, q, sizeof q);
}

static void quad_table(const float q[16], uint32_t table)
{
    uint32_t c[4];
    for (int k = 0; k < 4; k++) c[k] = exe_u32(table + 4u * (unsigned)k);
    Hud2D_Quad(q, c);
}

/* A frame of pad pixels round (x, y, w, h) in style s (0x5b3358 + 0x40 s). 0x435980 */
static void frame(int x, int y, int w, int h, int s, int p)
{
    if (g_hud_opaque || s < 0 || 2 < s) return;
    float fx = (float)x, fy = (float)y, x1 = (float)(x + w), y1 = (float)(y + h);
    float top[16] = {fx - p, fy - p, 0, 1, x1 + p, fy - p, 0, 1, x1, fy, 0, 1, fx, fy, 0, 1};
    float right[16] = {x1 + p, fy - p, 0, 1, x1 + p, y1 + p, 0, 1, x1, y1, 0, 1, x1, fy, 0, 1};
    float bottom[16] = {x1 + p, y1 + p, 0, 1, fx - p, y1 + p, 0, 1, fx, y1, 0, 1, x1, y1, 0, 1};
    float left[16] = {fx - p, y1 + p, 0, 1, fx - p, fy - p, 0, 1, fx, fy, 0, 1, fx, y1, 0, 1};
    uint32_t t = 0x5b3358u + 0x40u * (unsigned)s;
    quad_table(top, t);
    quad_table(right, t + 0x10);
    quad_table(bottom, t + 0x20);
    quad_table(left, t + 0x30);
}

/* Four quads b pixels inside (x, y, w, h): top, right, bottom, left, coloured from table (4 x 4 colours). */
static void inner(int x, int y, int w, int h, int b, uint32_t table)
{
    if (g_hud_opaque) return;
    float fx = (float)x, fy = (float)y, x1 = (float)(x + w), y1 = (float)(y + h);
    float top[16] = {fx, fy, 0, 1, x1, fy, 0, 1, x1, fy + b, 0, 1, fx, fy + b, 0, 1};
    float right[16] = {x1 - b, fy, 0, 1, x1, fy, 0, 1, x1, y1, 0, 1, x1 - b, y1, 0, 1};
    float bottom[16] = {fx, y1 - b, 0, 1, x1, y1 - b, 0, 1, x1, y1, 0, 1, fx, y1, 0, 1};
    float left[16] = {fx, fy, 0, 1, fx + b, fy, 0, 1, fx + b, y1, 0, 1, fx, y1, 0, 1};
    quad_table(top, table);
    quad_table(right, table + 0x10);
    quad_table(bottom, table + 0x20);
    quad_table(left, table + 0x30);
}

/* A bevel of b pixels inside (x, y, w, h). 0x436380 */
static void bevel(int x, int y, int w, int h, int b) { inner(x, y, w, h, b, 0x5b3478); }

static void flat(const float q[16], uint32_t col)   /* 0x419f70 */
{
    if (g_hud_opaque) col |= 0xff000000u;
    uint32_t c[4] = {col, col, col, col};
    Hud2D_Quad(q, c);
}

static void line2(const float *a, const float *b, uint32_t table)   /* 0x435900 + 0x419eb0 */
{
    float p[8] = {a[0], a[1], 0, 1, b[0], b[1], 0, 1};
    uint32_t c[2] = {exe_u32(table), exe_u32(table + 4)};
    Hud2D_Line(p, c);
}

typedef struct {
    HudVisual base;
    int32_t w, h;                 /* +0x10, +0x14 */
    float bar[16], keys[16], body[16];   /* +0x1c, +0x5c, +0x9c */
    int32_t frames;               /* +0xdc */
    int32_t cw, ch, step;         /* +0xe4, +0xe8, +0xec: the size now, its step */
    const char *title, *sub;      /* +0xf0, +0xf4 */
    HText *ttitle, *tsub, *tkey, *tkey2;   /* +0xf8 .. +0x104 */
    uint8_t keyhints;             /* +0x108 */
    int32_t dir, last;            /* +0x110, +0x114: the band's direction, its last place */
    uint8_t autoclose;            /* +0x118 */
    int32_t opened;               /* +0x11c */
} HBillBoard;

/* The bars and body for size (w, h) centred on the screen, the texts in the bars. 0x41ae40 */
static void bb_layout(HBillBoard *b, int w, int h, int pad)
{
    int x = (g_display.width - w) / 2, y = (g_display.height - h) / 2;
    box(x, y, w, pad, b->bar);
    box(x, h + pad * -2 + y, w, pad, b->keys);
    memcpy(b->body, b->bar + 12, 16);
    memcpy(b->body + 4, b->bar + 8, 16);
    if (b->keys[5] <= b->bar[13]) {
        memcpy(b->body + 8, b->bar + 8, 16);
        memcpy(b->body + 12, b->bar + 12, 16);
    } else {
        memcpy(b->body + 8, b->keys + 4, 16);
        memcpy(b->body + 12, b->keys, 16);
    }
    float a, c, d, th;
    float bh = exe_f32(0x5b265c), in = exe_f32(0x5b241c), half = exe_f32(0x5b23f0);
    if (b->ttitle && b->ttitle->font && b->ttitle->font->font) {
        text_extent(b->ttitle, b->title ? b->title : "", &a, &c, &d, &th);
        b->ttitle->x = (int)(b->bar[0] + in), b->ttitle->y = (int)((bh - th) * half + b->bar[1]);
    }
    if (b->tsub && b->tsub->font && b->tsub->font->font) {
        text_extent(b->tsub, b->sub ? b->sub : "", &a, &c, &d, &th);
        b->tsub->x = (int)(b->bar[4] - in), b->tsub->y = (int)((bh - th) * half + b->bar[1]);
    }
    const char *k1 = LOC_string(HudConfig_loc(), 0x46b4), *k2 = LOC_string(HudConfig_loc(), 0x46b5);
    if (b->tkey && b->tkey->font && b->tkey->font->font) {
        text_extent(b->tkey, k1 ? k1 : "", &a, &c, &d, &th);
        b->tkey->x = (int)(b->keys[0] + in), b->tkey->y = (int)((bh - th) * half + b->keys[1]);
    }
    if (b->tkey2 && b->tkey2->font && b->tkey2->font->font) {
        text_extent(b->tkey2, k2 ? k2 : "", &a, &c, &d, &th);
        b->tkey2->x = (int)(b->keys[4] - in), b->tkey2->y = (int)((bh - th) * half + b->keys[1]);
    }
}

static void bb_setpos(HudVisual *v, int x, int y) { bb_layout((HBillBoard *)v, x, y, 0x16); }   /* 0x41add0 */

static void bb_setstate(HudVisual *v, int32_t s)   /* 0x41adf0 */
{
    HBillBoard *b = (HBillBoard *)v;
    b->frames = 0;
    v->state = s;
    if (s == 1) b->opened = g_sim_tick;
    if (s == 1 || s == 2) Audio_UI(4);   /* 0x4aab80 */
}

/* The dark band along a bar at x: two quads to its sides. */
static void band(const float *r, float x)
{
    float l[16] = {r[0], r[1], r[2], r[3], x, r[5], 0, 1, x, r[9], 0, 1, r[12], r[13], r[14], r[15]};
    float rt[16] = {x, r[5], 0, 1, r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], x, r[9], 0, 1};
    quad_table(l, 0x5b2898);
    quad_table(rt, 0x5b28a8);
}

/* 0x41a7c0 */
static void bb_draw(HBillBoard *b)
{
    HudVisual *v = &b->base;
    if (v->state == 0) return;
    if (b->autoclose && b->opened && 0x280 < g_sim_tick - b->opened) {
        v->vt->setstate(v, 2);
        b->opened = 0;
    }
    if (v->state == 1) {
        b->ch += b->step;
        if (b->h <= b->ch) {
            b->cw = b->w, b->ch = b->h;
            v->state = 3;
        }
        b->frames++;
    } else if (v->state == 2) {
        b->ch -= b->step;
        if (b->ch < 0x29) {
            b->ch = 0x28;
            v->state = 0;
        }
        b->frames++;
    } else if (v->state == 3) {
        b->cw = b->w, b->ch = b->h;
    }
    v->vt->setpos(v, b->cw, b->ch);
    quad_table(b->body, 0x5b2858);
    int t = (int)fmod((double)g_sim_tick, (double)exe_f64(0x5b28f0));   /* (the race loop's tick 0x606ac8) */
    int s = (int)((unsigned)(t * (int)(b->bar[4] - b->bar[0])) >> 5);
    if (b->dir == 0 && s < b->last) b->dir = 1;
    else if (b->dir == 1 && s < b->last) b->dir = 0;
    b->last = s;
    band(b->bar, (float)(int)(b->dir ? b->bar[4] - (float)s : (float)s + b->bar[0]));
    band(b->keys, (float)(int)(b->dir ? b->keys[4] - (float)s : (float)s + b->keys[0]));
    if (v->state == 3) {
        text_print(b->ttitle, b->title ? b->title : "", 1.0f, 1.0f);
        text_print(b->tsub, b->sub ? b->sub : "", 1.0f, 1.0f);
        if (b->keyhints) {
            const char *k = LOC_string(HudConfig_loc(), 0x46b4);
            text_print(b->tkey, k ? k : "", 1.0f, 1.0f);
            if (v->flag) {
                k = LOC_string(HudConfig_loc(), 0x46b5);
                text_print(b->tkey2, k ? k : "", 1.0f, 1.0f);
            }
        }
    }
    int x = (int)b->bar[0], y = (int)b->bar[1];
    frame(x, y, (int)(b->keys[8] - b->bar[0]), (int)(b->keys[9] - b->bar[1]), exe_i32(0x5cc8e4), exe_i32(0x5cc8e8));
    tray_shade(x, y, (int)(b->bar[4] - b->bar[0]), (int)(b->bar[9] - b->bar[5]), 100);
    tray_shade((int)b->keys[0], (int)b->keys[1], (int)(b->keys[4] - b->keys[0]), (int)(b->keys[9] - b->keys[5]), 100);
}

static void bb_init(HBillBoard *b, const HudVisualVtbl *vt, int w, int h, const char *title, const char *sub, bool keyhints, bool flag,
                    bool autoclose)
{
    b->base.vt = vt;
    b->base.flag = flag;
    b->w = w, b->h = h;
    b->step = h / 5;
    b->title = title, b->sub = sub;
    b->keyhints = keyhints;
    b->autoclose = autoclose;
    const char *f = HudConfig_font(0x4c);
    b->ttitle = text_new(f, 0xffffffffu, (uint32_t)HudConfig_i32(0x14), 0, 0, 0, 0, 0, false);
    b->tsub = text_new(f, 0xffffffffu, (uint32_t)HudConfig_i32(0x14), 0, 0, 0, 0, 1, false);
    b->tkey = text_new(f, 0xffffffffu, (uint32_t)HudConfig_i32(0x14), 0, 0, 0, 0, 0, false);
    b->tkey2 = text_new(f, 0xffffffffu, (uint32_t)HudConfig_i32(0x14), 0, 0, 0, 0, 1, false);
    b->cw = w, b->ch = 0x28;
    bb_layout(b, w, b->ch, 0x16);
}

static void bb_free(HBillBoard *b)   /* 0x41a760 */
{
    text_free(b->ttitle);
    text_free(b->tsub);
    text_free(b->tkey);
    text_free(b->tkey2);
}

/* ---- the buttons (HButtonVisual, 0x104 bytes, vtable 0x5b2904; 0x41b160): a red bar with its text, amber
   under the mouse, a click when the button goes down on it ---- */

typedef struct {
    int32_t x, y, w, h;      /* (HVisual 0x436670) */
    float quad[16];          /* +0x14 */
    float lines[5][8];       /* +0x54: the frame's four sides and a second right one */
    const char *text;        /* +0xf4 */
    HText *ttext;            /* +0xf8 */
    uint32_t colour;         /* +0xfc: 0 the red / amber */
    uint8_t hover, clicked;  /* +0x100, +0x101 */
    uint8_t pressed;         /* +0x102 */
    uint8_t bevelled;        /* +0x103 */
} HButton;

/* The corners, the frame and the text centred in it. 0x41b540 */
static void button_layout(HButton *b, int x, int y, int w, int h)
{
    b->x = x, b->y = y, b->w = w, b->h = h;
    box(x, y, w, h, b->quad);
    float fx = (float)x, fy = (float)y, x1 = (float)(x + w), y1 = (float)(y + h);
    float l[5][8] = {{fx, fy, 0, 1, x1, fy, 0, 1}, {x1, fy, 0, 1, x1, y1, 0, 1}, {x1, y1, 0, 1, fx, y1, 0, 1},
                     {fx, y1, 0, 1, fx, fy, 0, 1}, {x1 + 1.0f, fy, 0, 1, x1 + 1.0f, y1, 0, 1}};
    memcpy(b->lines, l, sizeof l);
    if (b->ttext && b->ttext->font && b->ttext->font->font) {
        float a, c, tw, th;
        text_extent(b->ttext, b->text ? b->text : "", &a, &c, &tw, &th);
        b->ttext->x = (int)(((float)w - tw) * 0.5f + fx);
        b->ttext->y = (int)(((float)h - th) * 0.5f + fy);
    }
}

static HButton *button_new(int x, int y, int w, int h, const char *text, bool bevelled)
{
    HButton *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    b->text = text;
    b->bevelled = bevelled;
    b->ttext = text_new(HudConfig_font(0x4c), 0xffffffffu, (uint32_t)HudConfig_i32(0x14), x, y, w, h, 0, false);   /* 0x430e50 */
    button_layout(b, x, y, w, h);
    return b;
}

static void button_free(HButton *b)
{
    if (!b) return;
    text_free(b->ttext);
    free(b);
}

/* The click (0x41b320): the button goes down on it. */
static void button_input(HButton *b, int click)
{
    b->clicked = click == 1 && b->hover;
    if (b->clicked) Audio_UI(9);   /* 0x4aab80 */
}

/* A shadow d pixels to the right and below (x, y, w, h). 0x435ee0 (with d twice) */
static void shadow(int x, int y, int w, int h, int d)
{
    if (g_hud_opaque) return;
    float bot[16] = {(float)(x + d), (float)(y + h), 0, 1, (float)(x + w), (float)(y + h), 0, 1,
                     (float)(x + w + d), (float)(y + h + d), 0, 1, (float)(x + d), (float)(y + h + d), 0, 1};
    float rt[16] = {(float)(x + w), (float)(y + d), 0, 1, (float)(x + w + d), (float)(y + d), 0, 1,
                    (float)(x + w + d), (float)(y + h + d), 0, 1, (float)(x + w), (float)(y + h), 0, 1};
    quad_table(bot, 0x5b3418);
    quad_table(rt, 0x5b3428);
}

static int32_t s_mouse_x = -1, s_mouse_y = -1;   /* (MOUSE_getstate 6, sampled with the HUD's input) */
static uint32_t s_mouse_b;

/* 0x41b360 */
static void button_draw(HButton *b)
{
    float mx = (float)s_mouse_x, my = (float)s_mouse_y;
    b->hover = b->quad[0] < mx && mx < b->quad[4] && b->quad[1] < my && my < b->quad[13];
    uint32_t fill, ink = 0xff000000u;
    if (b->colour) fill = b->colour;
    else if (b->hover) fill = 0xfffab400u;
    else fill = 0xffbc0000u, ink = 0xffffffffu;
    flat(b->quad, fill);
    for (int i = 0; i < 5; i++) line2(b->lines[i], b->lines[i] + 4, 0x5b28f8);
    if (b->ttext) b->ttext->colour = ink;
    text_print(b->ttext, b->text ? b->text : "", 1.0f, 1.0f);
    if (b->bevelled) {
        if ((!(s_mouse_b & 1) || !b->hover) && !b->pressed) tray_shade(b->x, b->y, b->w, b->h, 100);   /* 0x435d00 */
        else if (!g_hud_opaque) inner(b->x, b->y, b->w, b->h, exe_i32(0x5cc910), 0x5b3438);   /* 0x436090 */
        shadow(b->x, b->y, b->w, b->h, 5);
    }
}

/* ---- the tables (HTable, 0x7b0 bytes, vtable 0x5b3078; 0x42e810): a header of column names, rows of
   eight cells of text, a cell's colour square, a row marked ---- */

enum { TBL_ROWS = 8, TBL_COLS = 6 };

typedef struct {
    char cell[TBL_ROWS][TBL_COLS][0x20];   /* +0x004 */
    struct { uint8_t on; uint32_t colour; } swatch[TBL_ROWS][TBL_COLS];   /* +0x604 */
    HText *text;                           /* +0x784 */
    int32_t x, y, w;                       /* +0x788 .. +0x790 */
    float xh, xw;                          /* +0x794, +0x798: "x"'s extent */
    int32_t cols, rows;                    /* +0x79c, +0x7a0 */
    const int32_t *colw;                   /* +0x7a4 (exe data) */
    const char *head[TBL_COLS];            /* +0x7a8 */
    int32_t mark;                          /* +0x7ac: the row marked (-1 none) */
} HTable;

static HTable *table_new(int x0, int y0, int x1, int y1, int cols, int rows, uint32_t colw, const char *const *head)
{
    HTable *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->cols = cols, t->rows = rows;
    t->colw = (const int32_t *)(const void *)exe_data(colw, 4u * (unsigned)cols);
    for (int c = 0; c < cols && c < TBL_COLS; c++) t->head[c] = head[c];
    t->mark = -1;
    t->text = text_new(HudConfig_font(0x64), 0xff000000u, (uint32_t)HudConfig_i32(0x14), 0, 0, 0, 0, 0, false);
    if (t->text && t->text->font && t->text->font->font) {
        float a, b;
        text_extent(t->text, exe_str(0x5ccfc0), &a, &b, &t->xw, &t->xh);
    }
    int sum = 0;
    for (int c = 0; c < cols; c++) sum += t->colw[c];
    t->w = sum;
    t->x = ((x1 - x0) - sum) / 2 + x0;
    t->y = (int)((unsigned)((y1 + (rows + 1) * -0x14) - y0) >> 1) + y0;
    for (int r = 0; r < TBL_ROWS; r++)
        for (int c = 0; c < TBL_COLS; c++) snprintf(t->cell[r][c], 0x20, "%s", exe_str(0x5ccdc0));
    return t;
}

static void table_free(HTable *t)
{
    if (!t) return;
    text_free(t->text);
    free(t);
}

static void table_swatch(int x, int y, uint32_t col)   /* 0x42f030 */
{
    float q[16];
    box(x, y, 0x12, 6, q);
    flat(q, (col & 0xff000000u) ? col : 0xff000000u);
    for (int k = 0; k < 4; k++) line2(q + 4 * k, q + 4 * ((k + 1) & 3), 0x5b306c);
}

/* 0x42e9b0 */
static void table_draw(HTable *t)
{
    float head[16], body[16], mark[16];
    box(t->x, t->y, t->w, 0x14, head);
    flat(head, 0xfffab400u);
    bevel(t->x, t->y, t->w, 0x14, 4);
    box(t->x, t->y + 0x14, t->w, t->rows * 0x14, body);
    flat(body, 0x40ffffffu);
    if (t->mark != -1) {
        box(t->x, t->y + t->mark * 0x14, t->w, 0x14, mark);
        flat(mark, 0x80fab400u);
    }
    line2(head, body + 12, 0x5b3064);
    line2(head + 4, body + 8, 0x5b3064);
    int acc = 0;
    for (int c = 0; c < t->cols - 1; c++) {
        acc += t->colw[c];
        float a[2] = {(float)acc + head[0], 20.0f + head[1]}, b[2] = {(float)acc + body[12], body[13]};
        line2(a, b, 0x5b3064);
    }
    line2(head, head + 4, 0x5b3064);
    line2(head + 12, head + 8, 0x5b3064);
    line2(body + 12, body + 8, 0x5b3064);
    if (!t->text || !t->text->font || !t->text->font->font) return;
    int x = (int)(head[0] + exe_f32(0x5b2404));
    float a, b, w, h;
    for (int c = 0; c < t->cols; c++) {
        const char *s = t->head[c] ? t->head[c] : "";
        text_extent(t->text, s, &a, &b, &w, &h);
        t->text->x = x, t->text->y = (int)(((head[13] - head[1]) - h) * exe_f32(0x5b23f0) + head[1]);
        text_print(t->text, s, 1.0f, 1.0f);
        x += t->colw[c];
    }
    for (int r = 0; r < t->rows; r++) {
        x = (int)(head[0] + exe_f32(0x5b2404));
        for (int c = 0; c < t->cols; c++) {
            const char *s = t->cell[r][c];
            text_extent(t->text, s, &a, &b, &w, &h);
            int ty = (int)(((head[13] - head[1]) - h) * exe_f32(0x5b23f0) + (float)(r * 0x14) + head[13]);
            int tx = x;
            if (t->swatch[r][c].on) {
                table_swatch(x, (int)((float)(r * 0x14) + head[13] + exe_f32(0x5b3090)), t->swatch[r][c].colour);
                tx = x + 0x16;
            }
            t->text->x = tx, t->text->y = ty;
            text_print(t->text, s, 1.0f, 1.0f);
            x += t->colw[c];
        }
    }
}

/* ---- the race's results (HRaceSummary, 0x1bc bytes, vtable 0x5b2e60; 0x42bc30) ---- */

typedef struct {
    HBillBoard bb;
    HTable *table;        /* +0x120 */
    uint8_t linear, trial, extra;   /* +0x124, +0x125, +0x148 */
    int32_t done[8];      /* +0x128 */
    const char *head[TBL_COLS];
} HRaceSummary;

/* Racer i's row at its place: the place's name ("1st" .. hud.loc 0x46c4..), the driver, the car with its
   colour, the best lap, the race's time ("-------" when none; a point-to-point race only the time), in
   the career the place's points. 0x42c170 */
static void summary_row(HRaceSummary *s, int i, int time, int best, int row)
{
    HTable *t = s->table;
    if (!t || row < 0 || TBL_ROWS <= row || g_racers.n <= i) return;
    Car *c = g_racers.c[i];
    s->done[i] = 1;
    const char *place = LOC_string(HudConfig_loc(), 0x46c4 + row);
    snprintf(t->cell[row][0], 0x20, "%s", place ? place : "");
    snprintf(t->cell[row][1], 0x20, "%.31s", (const char *)CAR_DESCB(c) + 0x17c);
    snprintf(t->cell[row][2], 0x20, "%.15s", (const char *)CAR_DESCB(c) + 0x534);
    t->swatch[row][2].on = 1;
    memcpy(&t->swatch[row][2].colour, CAR_DESCB(c) + 0x218, 4);
    int col = 3;
    if (!s->linear && g_race_mode != 3) {
        HTime_Format(best, t->cell[row][3], false);
        col = 4;
    }
    if (time == 0) snprintf(t->cell[row][col], 0x20, "%s", exe_str(0x5cce0c));
    else HTime_Format(time, t->cell[row][col], false);
    if (s->trial && !s->extra && col + 1 < TBL_COLS)
        snprintf(t->cell[row][col + 1], 0x20, "%d", g_race_tour.race_points[row]);   /* (the career's points by place, 0x6576d8) */
}

/* Opening: the rows from the results, the player's marked. 0x42c070 */
static void summary_setstate(HudVisual *v, int32_t st)
{
    HRaceSummary *s = (HRaceSummary *)v;
    memset(s->done, 0, sizeof s->done);
    if (v->state == 0) {
        for (int i = 0; i < g_racers.n; i++) {
            Car *c = g_racers.c[i];
            int row = *CAR_I(c, 0xcd4) - 1;
            if (0 <= row && row < g_racers.n) summary_row(s, i, *CAR_I(c, 0xcd8), *CAR_I(c, 0xcfc), row);
        }
        Car *me = local_car();
        if (s->table && me) s->table->mark = *CAR_I(me, 0xcd4);
    }
    bb_setstate(v, st);
}

static void summary_draw(HudVisual *v)   /* 0x42bfe0 */
{
    HRaceSummary *s = (HRaceSummary *)v;
    bb_draw(&s->bb);
    if (v->state != 0 && !(v->state == 1 || v->state == 2) && s->table) table_draw(s->table);
}

static bool bb_done(HudVisual *v) { return v->state == 0; }   /* 0x41b110 */

static void summary_destroy(HudVisual *v)   /* 0x42bfa0 */
{
    HRaceSummary *s = (HRaceSummary *)v;
    table_free(s->table);
    bb_free(&s->bb);
    free(s);
}

static const HudVisualVtbl summary_vt = {summary_destroy, summary_draw, NULL, summary_setstate, bb_setpos, NULL, bb_done, NULL, NULL};

static HudVisual *summary_new(const char *title)
{
    HRaceSummary *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    const char *sub = LOC_string(HudConfig_loc(), 0x46d2);
    bb_init(&s->bb, &summary_vt, exe_i32(0x5b2e00), exe_i32(0x5b2e04), title, sub, true, true, true);
    s->linear = g_race_linear != 0;
    s->trial = g_race_mode == 4;
    s->extra = 0;   /* (0x6577e4) */
    const uint8_t *loc = HudConfig_loc();
    /* The columns: 4 for a point-to-point or network race (0x5b2e0c), 5 with the best lap (0x5b2e1c); the
       skill test's 5 and 6 (0x5b2e30, 0x5b2e44) with its score. */
    static const int ids4[4] = {0, 0x46bf, 0x46c0, 0x46c2}, ids5[5] = {0, 0x46bf, 0x46c0, 0x46c1, 0x46c2};
    static const int ids5t[5] = {0, 0x46bf, 0x46c0, 0x46c2, 0x46c3}, ids6[6] = {0, 0x46bf, 0x46c0, 0x46c1, 0x46c2, 0x46c3};
    const int *ids;
    int n;
    uint32_t widths;
    if (!s->trial || s->extra) {
        if (!s->linear && g_race_mode != 3) ids = ids5, n = 5, widths = 0x5b2e1c;
        else ids = ids4, n = 4, widths = 0x5b2e0c;
    } else if (!s->linear) {
        ids = ids6, n = 6, widths = 0x5b2e44;
    } else {
        ids = ids5t, n = 5, widths = 0x5b2e30;
    }
    for (int c = 0; c < n; c++) s->head[c] = ids[c] ? LOC_string(loc, ids[c]) : exe_str(0x5b2e08);
    int x = (g_display.width - s->bb.w) / 2, y = (g_display.height - s->bb.h) / 2;   /* 0x41b120 */
    s->table = table_new(x, y, exe_i32(0x5b2e00) + x, exe_i32(0x5b2e04) + y, n, 8, widths, s->head);
    return &s->bb.base;
}

/* ---- the track's records (HTrackRecords, 0x158 bytes, vtable 0x5b32b0; 0x433790): the eight best race
   times of a point-to-point track or lap times of a circuit, with the drivers and their cars, the
   player's entered when good enough ---- */

typedef struct {
    HBillBoard bb;
    HTable *table;                 /* +0x120 */
    uint8_t spoken;                /* +0x124 */
    uint8_t race_record;           /* +0x125: the race's best time */
    uint8_t lap_record;            /* +0x126: the lap's */
    const char *head[4];           /* +0x128 */
} HTrackRecords;

static HudVisual *s_records;   /* 0x60ca5c */

/* The player's race time (a point-to-point track's) or best lap entered among the records, the ones
   after it moved down, its driver and car with it; the new best noted for the announcer. 0x433980 */
static void records_update(HudVisual *v)
{
    HTrackRecords *r = (HTrackRecords *)v;
    Car *me = local_car();
    if (!me) return;
    int32_t *rec = g_race_linear ? g_track_records : g_lap_records;
    int32_t t = g_race_linear ? *CAR_I(me, 0xcd8) : *CAR_I(me, 0xcfc);
    for (int i = 0; i < 8; i++) {
        if (rec[i] <= t || t == 0) continue;
        for (int k = 7; k > i; k--) {
            rec[k] = rec[k - 1];
            memcpy(g_record_names[k], g_record_names[k - 1], 32);
            memcpy(g_record_cars[k], g_record_cars[k - 1], 32);
        }
        rec[i] = t;
        snprintf(g_record_names[i], 32, "%s", (const char *)CAR_DESCB(me) + 0x17c);
        snprintf(g_record_cars[i], 32, "%s", (const char *)CAR_DESCB(me) + 0x534);
        break;
    }
    r->race_record = r->lap_record = 0;
    if (!g_race_linear) r->lap_record = t != 0 && t <= g_lap_records[0];
    else r->race_record = t <= g_track_records[0] && t != 0;
    r->spoken = 0;
}

/* Opening: the rows from the records (the place's name, the driver, the car, the time), the player's
   first marked. 0x433c50 */
static void records_setstate(HudVisual *v, int32_t st)
{
    HTrackRecords *r = (HTrackRecords *)v;
    HTable *t = r->table;
    Car *me = local_car();
    if (v->state == 0 && t) {
        int mark = -1;
        for (int i = 0; i < 8; i++) {
            const char *place = LOC_string(HudConfig_loc(), 0x46c4 + i);
            snprintf(t->cell[i][0], 0x20, "%s", place ? place : "");
            snprintf(t->cell[i][1], 0x20, "%.31s", g_record_names[i]);
            snprintf(t->cell[i][2], 0x20, "%s", g_record_cars[i]);
            HTime_Format(g_race_linear ? g_track_records[i] : g_lap_records[i], t->cell[i][3], false);
            if (me && (g_track_records[i] == *CAR_I(me, 0xcd8) || g_lap_records[i] == *CAR_I(me, 0xcfc)) && mark == -1) mark = i;
        }
        if (mark != -1) t->mark = mark + 1;
    }
    bb_setstate(v, st);
}

/* The billboard, its table once open; the announcer's line on the first frame: a new best lap's
   (0x4a63d0) or race time's (0x4a6310). 0x433920 */
static void records_draw(HudVisual *v)
{
    HTrackRecords *r = (HTrackRecords *)v;
    bb_draw(&r->bb);
    if (v->state == 0 || v->state == 1 || v->state == 2) return;
    if (r->table) table_draw(r->table);
    if (r->spoken) return;
    r->spoken = 1;
    if (r->lap_record) Audio_Say((int)(REAL_rand() % 5) + 0x7d);
    else if (r->race_record) Audio_Say((int)(REAL_rand() % 12) + 0x71);
}

static void records_destroy(HudVisual *v)   /* HTrackRecords_v0 0x4338e0 */
{
    HTrackRecords *r = (HTrackRecords *)v;
    table_free(r->table);
    bb_free(&r->bb);
    free(r);
}

static const HudVisualVtbl records_vt = {records_destroy, records_draw, records_update, records_setstate, bb_setpos, NULL, bb_done, NULL, NULL};

static HudVisual *records_new(void)
{
    HTrackRecords *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    const uint8_t *loc = HudConfig_loc();
    const char *title = LOC_string(loc, g_race_linear ? 0x46d5 : 0x46ee);
    int w = exe_i32(0x5b3290), h = exe_i32(0x5b3294);
    bb_init(&r->bb, &records_vt, w, h, "", title ? title : "", true, true, true);
    static const int ids[4] = {0, 0x46bf, 0x46c0, 0x46c2};
    for (int c = 0; c < 4; c++) r->head[c] = ids[c] ? LOC_string(loc, ids[c]) : exe_str(0x5b32a8);
    int x = (g_display.width - r->bb.w) / 2, y = (g_display.height - r->bb.h) / 2;   /* 0x41b120 */
    r->table = table_new(x, y, w + x, h + y, 4, 8, 0x5b3298, r->head);
    return &r->bb.base;
}

/* ---- the career's standings (HTournamentStandings, 0x1f0 bytes, vtable 0x5b3210; 0x4317c0): the
   tournament's racers by their points with this race's (a rally's by the total time), their earnings,
   the player's row marked ---- */

typedef struct { int32_t points, time, earnings, racer; } TourEntry;

/* The player's winnings by race (HWinnings, 0x6c bytes, vtable 0x5b34d8; 0x436790), shown while the
   mouse is over the player's earnings: each race's prize and safe-driving bonus, after the last race
   the bonus prize, then the total, on a dark box. */
typedef struct {
    HText *label, *amount;   /* +0x10, +0x14: left and right aligned */
    int32_t w, h;            /* +0x1c 200, +0x20: 30 a race run and 70 */
    float q[16];             /* +0x24: the box */
    int32_t line;            /* +0x64: "X"'s height and a half */
    int32_t row;             /* +0x68: the player's standing */
} HWinnings;

static HWinnings *winnings_new(void)
{
    HWinnings *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    w->label = text_new(HudConfig_font(100), 0xffffffffu, 0xffffffffu, 0, 0, 0, 0, 0, false);
    w->amount = text_new(HudConfig_font(100), 0xffffffffu, 0xffffffffu, 0, 0, 0, 0, 1, false);
    w->w = 200;
    w->h = g_race_tour.runs * 0x1e + 0x46;
    float a, b, tw, th = 0;
    if (w->label && w->label->font && w->label->font->font) text_extent(w->label, exe_str(0x5cd238), &a, &b, &tw, &th);
    w->line = (int)(th * exe_f32(0x5b23f0) + th);
    return w;
}

static void winnings_free(HWinnings *w)   /* HWinnings_v0 0x436880 */
{
    if (!w) return;
    text_free(w->label);
    text_free(w->amount);
    free(w);
}

/* The box about (x, y): up by the races run less one line, kept on the screen. 0x436d00 */
static void winnings_place(HWinnings *w, int x, int y)
{
    int h = (g_race_tour.runs * 2 + 3) * w->line + 0x14;
    y -= (g_race_tour.runs - 1) * w->line;
    if (g_display.height < y + h) y = g_display.height - h - 10;
    box(x, y, w->w, w->h, w->q);
}

static void winnings_line(HWinnings *w, int row, const char *label, const char *amount)
{
    int x = (int)(w->q[0] + exe_f32(0x5b241c)), y = (int)((float)(w->line * row) + w->q[1] + exe_f32(0x5b241c));
    if (w->label) w->label->x = x, w->label->y = y;
    text_print(w->label, label, 1.0f, 1.0f);
    if (w->amount) w->amount->x = x + 0xb4, w->amount->y = y;
    text_print(w->amount, amount, 1.0f, 1.0f);
}

/* (the lines' texts, kept till the 2D layer draws them: 0x610a8c's slots) */
static char s_winnings_text[44][0x40];

/* 0x4368c0 */
static void winnings_draw(HWinnings *w)
{
    const RaceTournament *t = &g_race_tour;
    const uint8_t *loc = HudConfig_loc();
    int row = 0, n = 0;
    char (*b)[0x40] = s_winnings_text;
    for (int r = 0; r < 10; r++) {
        if (t->won[r] <= 0 && t->safe_won[r] <= 0) continue;
        const char *p = LOC_string(loc, 0x46f5), *sb = LOC_string(loc, 0x46f6);
        snprintf(b[n], 0x40, "%s %d ", p ? p : "", r + 1);   /* ("%s %s ", 0x5cd240) */
        snprintf(b[n + 1], 0x40, "%d ", t->won[r]);
        winnings_line(w, row++, b[n], b[n + 1]);
        snprintf(b[n + 2], 0x40, "%s %d ", sb ? sb : "", r + 1);
        snprintf(b[n + 3], 0x40, "%d ", t->safe_won[r]);
        winnings_line(w, row++, b[n + 2], b[n + 3]);
        n += 4;
    }
    if (t->last_race) {
        const char *bl = LOC_string(loc, 0x46f7);
        snprintf(b[n], 0x40, "%s", bl ? bl : "");
        snprintf(b[n + 1], 0x40, "%d ", 0 <= w->row && w->row < 8 ? t->bonus[w->row] : 0);
        winnings_line(w, row++, b[n], b[n + 1]);
        n += 2;
    }
    const char *tot = LOC_string(loc, 0x46f8);
    snprintf(b[n], 0x40, "%s", tot ? tot : "");
    snprintf(b[n + 1], 0x40, "%d ", t->earnings[0]);   /* (racer 0's, whoever is the player) */
    winnings_line(w, row, b[n], b[n + 1]);
    float bottom = (float)(w->line + 10 + (int)((float)(w->line * row) + w->q[1] + exe_f32(0x5b241c)));
    w->q[9] = w->q[13] = bottom;
    quad_table(w->q, 0x5cd20c);   /* 0x419fb0 */
}

typedef struct {
    HBillBoard bb;
    TourEntry entry[8];            /* +0x120 */
    HTable *table;                 /* +0x1a0 */
    uint8_t spoken;                /* +0x1a4 */
    const char *head[2][4];        /* +0x1a8, +0x1b8 */
    HWinnings *winnings;           /* +0x1e8 */
    int32_t row;                   /* +0x1ec: the player's */
} HTourStandings;

static HudVisual *s_standings;   /* 0x60ca54 */
static HudVisual *s_standing_congrats;   /* 0x60ca68 (an HCongrats) */
static HudVisual *s_info;        /* 0x60c9fc */
static int32_t s_start_damage[8];   /* 0x60c9c8: each car's damage at the race's start */

/* A car's damage as the career's safe-driving bonus measures it: its ten zones (from z) summed, each
   step truncated. */
static int32_t zones_damage(const Car *c, int z)
{
    int32_t sum = 0;
    for (int k = 0; k < 10; k++) sum = (int32_t)((float)sum + *CAR_F((Car *)c, z + 4 * k));
    return sum;
}

/* The career race's results into the tournament's numbers: each racer's points and prize by place, the
   safe-driving bonus (whole to the winner, half the second's, a quarter the third's) to a car no more
   damaged than it started (else +0xcc0 marks it), the player's winnings by race, the total times.
   0x427620 */
static void tour_update(void)
{
    RaceTournament *t = &g_race_tour;
    t->runs++;
    for (int i = 0; i < g_racers.n && i < 8; i++) {
        Car *c = g_racers.c[i];
        int p = *CAR_I(c, 0xcd4) - 1;
        if (p < 0 || 7 < p) p = 7;   /* (the original indexes by it unchecked) */
        t->total_points[i] += t->race_points[p];
        t->earnings[i] += t->prize[p];
        bool clean = zones_damage(c, 0xc98) <= s_start_damage[i];
        if (!clean) c->b[0xcc0] = 1;
        int32_t bonus = 0;
        if (clean) {
            if (p == 0) bonus = t->safe_bonus;
            else if (p == 1) bonus = (int32_t)((float)t->safe_bonus * exe_f32(0x5b23f0));
            else if (p == 2) bonus = (int32_t)((float)t->safe_bonus * exe_f32(0x5b2468));
            t->earnings[i] += bonus;
        }
        if (i == g_race_local && 0 <= t->race && t->race < 10) {
            t->won[t->race] = t->prize[p];
            t->safe_won[t->race] = clean ? bonus : 0;
        }
        t->total_time[i] += *CAR_I(c, 0xcd8);
    }
}

/* The standings sorted (0x501e30, a bubble sort): by points, then earnings, then the lower time; a
   rally's by the lower time, then the lower earnings. */
static void tour_sort(TourEntry *e, int n, bool rally)
{
    for (int pass = 0; pass < n; pass++)
        for (int i = 0; i + 1 < n; i++) {
            TourEntry *a = &e[i], *b = &e[i + 1];
            bool swap = rally ? (b->time < a->time || (b->time == a->time && b->earnings < a->earnings))
                              : (a->points < b->points ||
                                 (a->points == b->points && (a->earnings < b->earnings ||
                                                             (a->earnings == b->earnings && b->time < a->time))));
            if (swap) {
                TourEntry x = *a;
                *a = *b, *b = x;
            }
        }
}

/* The standings from the tournament's numbers, sorted; after the last race the bonus prizes by final
   standing go into the earnings. 0x431ae0 */
static void standings_update(HudVisual *v)
{
    HTourStandings *s = (HTourStandings *)v;
    RaceTournament *t = &g_race_tour;
    int n = g_racers.n < 8 ? g_racers.n : 8;
    for (int i = 0; i < n; i++)
        s->entry[i] = (TourEntry){t->total_points[i], t->total_time[i], t->earnings[i], i};
    tour_sort(s->entry, n, t->rally != 0);
    if (t->last_race)
        for (int k = 0; k < n; k++) t->earnings[s->entry[k].racer] += t->bonus[k];
    s->spoken = 0;
}

/* The player's standing, 1 the first. 0x431de0 */
static int standings_rank(void)
{
    HTourStandings *s = (HTourStandings *)s_standings;
    if (!s) return 1;
    int n = g_racers.n < 8 ? g_racers.n : 8;
    for (int k = 0; k < n; k++)
        if (s->entry[k].racer == g_race_local) return k + 1;
    return n + 1;
}

/* Opening: the rows (the place's name, the driver, the points or a rally's time, the earnings), the
   player's marked. 0x431b90 */
static void standings_setstate(HudVisual *v, int32_t st)
{
    HTourStandings *s = (HTourStandings *)v;
    HTable *t = s->table;
    if (v->state == 0 && t) {
        s->row = 0;
        int n = g_racers.n < 8 ? g_racers.n : 8;
        for (int i = 0; i < n; i++) {
            const TourEntry *e = &s->entry[i];
            const char *place = LOC_string(HudConfig_loc(), 0x46c4 + i);
            snprintf(t->cell[i][0], 0x20, "%s", place ? place : "");
            Car *c = e->racer < g_racers.n ? g_racers.c[e->racer] : NULL;
            snprintf(t->cell[i][1], 0x20, "%.31s", c ? (const char *)CAR_DESCB(c) + 0x17c : "");
            if (!g_race_tour.rally) snprintf(t->cell[i][2], 0x20, "%d", e->points);
            else HTime_Format(e->time, t->cell[i][2], false);
            snprintf(t->cell[i][3], 0x20, "%d", 0 <= e->racer && e->racer < 8 ? g_race_tour.earnings[e->racer] : 0);
            if (e->racer == g_race_local) {
                s->row = i;
                if (s->winnings) s->winnings->row = i;   /* 0x436dc0 */
            }
        }
        t->mark = s->row + 1;
    }
    bb_setstate(v, st);
}

/* The billboard, its table once open; a win so far before the last race said (0x4a62a0); the player's
   winnings by race while the mouse is over the player's earnings (0x42f200, 0x42f280). 0x4319e0 */
static void standings_draw(HudVisual *v)
{
    HTourStandings *s = (HTourStandings *)v;
    bb_draw(&s->bb);
    if (v->state == 0 || v->state == 1 || v->state == 2) return;
    if (s->table) table_draw(s->table);
    if (!s->spoken) {
        s->spoken = 1;
        if (!g_race_tour.last_race && s->row == 0) Audio_Say(0x9e);
    }
    HTable *t = s->table;
    if (t && s->winnings) {
        int x0 = t->x + t->colw[0] + t->colw[1] + t->colw[2], x1 = x0 + t->colw[3];
        int y0 = t->y + (s->row * 5 + 5) * 4, y1 = t->y + (s->row * 5 + 10) * 4;
        if (x0 < s_mouse_x && s_mouse_x < x1 && y0 < s_mouse_y && s_mouse_y < y1) {
            winnings_place(s->winnings, (x1 - x0) / 2 + x0, (y1 - y0) / 2 + y0);   /* 0x436d00 */
            winnings_draw(s->winnings);
        }
    }
}

static void standings_destroy(HudVisual *v)   /* HTournamentStandings_v0 0x431990 */
{
    HTourStandings *s = (HTourStandings *)v;
    table_free(s->table);
    winnings_free(s->winnings);
    bb_free(&s->bb);
    free(s);
}

static const HudVisualVtbl standings_vt = {standings_destroy, standings_draw, standings_update, standings_setstate, bb_setpos, NULL, bb_done, NULL, NULL};

static HudVisual *standings_new(void)
{
    HTourStandings *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    const uint8_t *loc = HudConfig_loc();
    const char *title = LOC_string(loc, 0x46d4);
    int w = exe_i32(0x5b31e0), h = exe_i32(0x5b31e4);
    bb_init(&s->bb, &standings_vt, w, h, "", title ? title : "", true, true, true);
    static const int ids[2][4] = {{0, 0x46bf, 0x46cf, 0x46f1}, {0, 0x46bf, 0x46f2, 0x46f1}};
    for (int k = 0; k < 2; k++)
        for (int c = 0; c < 4; c++) s->head[k][c] = ids[k][c] ? LOC_string(loc, ids[k][c]) : exe_str(0x5b3208);
    int x = (g_display.width - s->bb.w) / 2, y = (g_display.height - s->bb.h) / 2;   /* 0x41b120 */
    bool rally = g_race_tour.rally != 0;   /* (0x6577e4) */
    s->table = table_new(x, y, w + x, h + y, 4, 8, rally ? 0x5b31f8 : 0x5b31e8, s->head[rally]);
    s->winnings = winnings_new();   /* 0x436790 */
    return &s->bb.base;
}

/* ---- a message on a billboard (HInfo, 0x128 bytes, vtable 0x5b2bfc; 0x4282b0): "Info" titled, its
   text centred ---- */

typedef struct {
    HBillBoard bb;
    const char *text;   /* +0x120 */
    HText *ttext;       /* +0x124 */
} HInfo;

static void info_draw(HudVisual *v)   /* HInfo_v1 0x428390 */
{
    HInfo *m = (HInfo *)v;
    bb_draw(&m->bb);
    if (v->state != 0 && !(v->state == 1 || v->state == 2) && m->text) text_print(m->ttext, m->text, 1.0f, 1.0f);
}

/* The text, centred on the billboard. 0x4283d0 */
static void info_settext(HudVisual *v, const char *text)
{
    HInfo *m = (HInfo *)v;
    if (!m) return;
    m->text = text;
    float a, b, w = 0, h = 0;
    if (m->ttext && m->ttext->font && m->ttext->font->font) text_extent(m->ttext, text ? text : "", &a, &b, &w, &h);
    int x = (g_display.width - m->bb.w) / 2, y = (g_display.height - m->bb.h) / 2;   /* 0x41b120 */
    if (m->ttext) {
        m->ttext->x = (int)((float)(m->bb.w / 2) - w * exe_f32(0x5b23f0) + (float)x);
        m->ttext->y = (int)((float)(m->bb.h / 2) - h * exe_f32(0x5b23f0) + (float)y);
    }
}

static void info_destroy(HudVisual *v)   /* HInfo_v0 0x428350 */
{
    HInfo *m = (HInfo *)v;
    text_free(m->ttext);
    bb_free(&m->bb);
    free(m);
}

static const HudVisualVtbl info_vt = {info_destroy, info_draw, NULL, bb_setstate, bb_setpos, NULL, bb_done, NULL, NULL};

static HudVisual *info_new(bool keyhints)
{
    HInfo *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    const char *title = LOC_string(HudConfig_loc(), 0x46ed);
    bb_init(&m->bb, &info_vt, exe_i32(0x5b2bf0), exe_i32(0x5b2bf4), "", title ? title : "", true, keyhints, true);
    m->ttext = text_new(HudConfig_font(0x4c), 0xff000000u, 0xff000000u, 0, 0, 0, 0, 0, false);
    return &m->bb.base;
}

/* ---- the mission's summary (HMissionSummary, 0x14c bytes, vtable 0x5b2c88; 0x428900): Factory Driver's
   verdict, the narrator's picture and name beside his words typed out a letter a frame, a button to try
   again after a failure ---- */

enum { MS_LINES = 16 };

typedef struct {
    HBillBoard bb;
    HudArt *face;                  /* +0x120: the narrator's (FactoryPeople.fsh) */
    HButton *retry;                /* +0x124 */
    char lines[MS_LINES][0x80];    /* +0x128: the words in lines */
    int nlines;
    HText *text;                   /* +0x12c */
    int32_t tx, ty;                /* +0x130, +0x134 */
    int32_t line, chars;           /* +0x138, +0x13c: the line being typed, its letters so far */
    HText *name;                   /* +0x144 (+0x140, the cones fallen: g_mission_knocked) */
    uint8_t small;                 /* +0x148: the name too wide, drawn smaller */
} HMissionSummary;

static char s_typed[0x80];   /* 0x60caa0 */

/* The words broken into lines of about 50 letters at the spaces (HMissionSummary_v5's second part). */
static void ms_wrap(HMissionSummary *m, const char *s)
{
    int len = (int)strlen(s), start = 0, last = len - 1;   /* (last: the original's is unset before a space) */
    for (int i = 0; i < len;) {
        int j = i;
        while (s[j] == ' ') last = j++;
        if (0x31 < j - start || len - 1 <= j) {
            if (j == len - 1) last = j;
            int n = last - start + 1;
            if (m->nlines < MS_LINES && 0 < n) {
                if (0x7f < n) n = 0x7f;
                memcpy(m->lines[m->nlines], s + start, (size_t)n);
                m->lines[m->nlines][n] = 0;
                m->nlines++;
            }
            start = last + 1;
        }
        i = j + 1;
    }
}

/* Opening: the verdict (once, unless shown already), its music, a message of the verdict's at random
   with the player's name, in lines. HMissionSummary_v5 0x4293b0 */
static void ms_setstate(HudVisual *v, int32_t st)
{
    HMissionSummary *m = (HMissionSummary *)v;
    m->nlines = 0;
    if (v->state == 0) {   /* 0x42a310 */
        Mission_Judge();
        REAL_rand();
        const int32_t *ids = g_mission_pass ? g_mission_win : g_mission_lose;
        int n = 0;
        for (int k = 0; k < 3; k++)
            if (0 < ids[k]) n++;
        uint32_t r = REAL_rand();
        Music_Play(g_mission_pass ? exe_str(0x5ccb28) : exe_str(0x5ccb20), 1);   /* "win_", "lose_" */
        if (n) {   /* (the original divides by none) */
            const char *f = LOC_string(HudConfig_loc(), ids[r % (unsigned)n]);
            Car *me = local_car();
            char words[0x280];
            snprintf(words, sizeof words, f ? f : "", me ? (const char *)CAR_DESCB(me) + 0x17c : "");
            ms_wrap(m, words);
        }
    }
    bb_setstate(v, st);
}

/* HMissionSummary_v1 0x428dc0 */
static void ms_draw(HudVisual *v)
{
    HMissionSummary *m = (HMissionSummary *)v;
    bb_draw(&m->bb);
    if (v->state == 0 || v->state == 1 || v->state == 2) {   /* 0x42a310, 0x42a370 */
        m->line = m->chars = 0;
        return;
    }
    if (m->face) {
        HudArt_draw(m->face);
        /* the name's plate under the picture, a black frame round both, their shadow */
        int x = m->face->x, y = m->face->y, w = m->face->w, h = m->face->h;
        float q[16];
        box(x, y + h, w, 0xf, q);
        flat(q, 0x4cffffffu);
        box(x, y, w, h + 0xf, q);
        uint32_t c[2] = {0xff000000u, 0xff000000u};
        for (int k = 0; k < 4; k++) {
            const float *a = q + 4 * k, *b = q + 4 * ((k + 1) & 3);
            float p[8] = {a[0], a[1], 0, 1, b[0], b[1], 0, 1};   /* 0x435900, 0x419eb0 */
            Hud2D_Line(p, c);
        }
        shadow(x, y, w, h + 0xf, 5);
    }
    const char *nm = LOC_string(HudConfig_loc(), 0x4700 + g_mission_narrator);
    text_print(m->name, nm ? nm : "", m->small ? 0.9f : 1.0f, 1.0f);
    if (g_mission_pass == 0 && m->retry) button_draw(m->retry);
    if (!m->text || !m->text->font || !m->text->font->font) return;
    float y = 0.0f, a, b, tw, th;
    double step = exe_f64(0x5b2cc0);
    for (int u = 0; u <= m->line; u++) {
        if (m->nlines <= u) continue;
        m->text->x = m->tx, m->text->y = (int)((float)m->ty + y);
        text_extent(m->text, m->lines[u], &a, &b, &tw, &th);
        y = (float)((double)th * step + (double)y);
        if (u < m->line) {
            text_print(m->text, m->lines[u], 1.0f, 1.0f);
            continue;
        }
        /* the line being typed: a letter more each frame, then the next */
        memset(s_typed, 0, sizeof s_typed);
        m->chars++;
        snprintf(s_typed, sizeof s_typed, "%.*s", m->chars, m->lines[u]);
        if ((int)strlen(m->lines[u]) < m->chars) {
            m->chars = 0;
            if (m->nlines <= ++m->line) m->line = m->nlines;
            memset(s_typed, 0, 0x33);
            text_print(m->text, m->lines[u], 1.0f, 1.0f);
            m->text->x = m->tx, m->text->y = (int)((float)m->ty + y);
        }
        text_print(m->text, s_typed, 1.0f, 1.0f);
    }
}

/* The button after a failure: try again (3). HMissionSummary_v9 0x429350 */
static int ms_input(HudVisual *v, int x, int y, int click)
{
    HMissionSummary *m = (HMissionSummary *)v;
    (void)x, (void)y;
    if (m->retry) button_input(m->retry, click);   /* 0x41b320 */
    if (v->state != 0 && g_mission_pass == 0 && m->retry && m->retry->clicked) return 3;
    PENDING_NOTE(0x42a330, "the billboards' key (0x19)");
    return 0;
}

static void ms_destroy(HudVisual *v)   /* 0x428c20 */
{
    HMissionSummary *m = (HMissionSummary *)v;
    HudArt_free(m->face);
    button_free(m->retry);
    text_free(m->text);
    text_free(m->name);
    bb_free(&m->bb);
    free(m);
}

static const HudVisualVtbl mission_vt = {ms_destroy, ms_draw, NULL, ms_setstate, bb_setpos, NULL, bb_done, ms_input, NULL};

static HudVisual *mission_new(void)
{
    HMissionSummary *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    const uint8_t *loc = HudConfig_loc();
    int w = exe_i32(0x5b2c70), h = exe_i32(0x5b2c74);
    bb_init(&m->bb, &mission_vt, w, h, LOC_string(loc, 0x46d6), LOC_string(loc, 0x46d7), true, true, false);
    int W = g_display.width, H = g_display.height;
    int fx = (W - w) / 2 + 0x14, fy = H / 2 - 0x40;   /* 0x41b120 */
    m->face = HudArt_load("factorypeople", g_mission_narrator, fx, fy, 0x36, 0x40, false, false, true);
    int bw = exe_i32(0x5b2c78), bh = exe_i32(0x5b2c7c);
    m->retry = button_new(W / 2 - bw / 2, H / 2 + bh * -4 + h / 2, bw, bh, LOC_string(loc, 0x46bd), true);
    m->text = text_new(HudConfig_font(0x64), 0xff000000u, (uint32_t)HudConfig_i32(0x14), 0, 0, 0, 0, 0, false);
    m->tx = W / 2 - w / 4;
    m->ty = H / 2 - h / 4;
    m->name = text_new(HudConfig_font(0x64), 0xcc000000u, 0xcc000000u, fx, fy, 0, 0, 0, false);
    if (m->name && m->name->font && m->name->font->font) {
        /* the name centred under the picture, or from its left edge and smaller when wider */
        const char *nm = LOC_string(loc, 0x4700 + g_mission_narrator);
        float a, b, tw, th, room = exe_f32(0x5b2c80);
        text_extent(m->name, nm ? nm : "", &a, &b, &tw, &th);
        if (tw < room) m->name->x = (int)((room - tw) * exe_f32(0x5b23f0) + (float)fx);
        else m->small = 1, m->name->x = fx + 2;
        m->name->y = fy + 0x42;
    }
    REAL_srand(app_ticks_ms() / 1000u);   /* srand(time(0)) (0x537e80): the port's clock in seconds */
    return &m->bb.base;
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

static const HudVisualVtbl speedometer_vt = {dial_destroy, speedometer_draw, dial_reset, tray_setstate, dial_setpos, tray_progress, NULL, NULL, NULL};
static const HudVisualVtbl tach_vt = {dial_destroy, tach_draw, dial_reset, tray_setstate, tach_setpos, tray_progress, NULL, NULL, NULL};

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
    if (g_race_derby) return;
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

static const HudVisualVtbl wrongway_vt = {wrongway_destroy, wrongway_draw, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

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

/* ---- the rear-view mirror (HRearview, 0xd4 bytes, vtable 0x5b2e98; 0x42c470): a tray at the layout's
   place 10 and size (+0xfc, +0x100), the mirror's camera over it (Render_SetRearView), its frame once
   the tray has stopped; the view itself is drawn after the HUD (0x468be0) ---- */

typedef struct {
    HTray tray;
    int32_t rx, ry, rw, rh;   /* +0xc4: the HRenderVisual (0x436670: its place and size) */
    int32_t lastx, lasty;     /* +0xcc */
} HRearview;

static HRearview *s_rearview;   /* 0x60ca3c */

/* Shown by the second layout's +0x5c. 0x42c6a0 */
static void rearview_draw(HudVisual *v)
{
    HRearview *r = (HRearview *)v;
    if (g_race_view == 2 || g_race_view == 3) return;
    if (HudConfig_choicei32(0x5c) == 0 && !(v->state == 1 || v->state == 2)) return;   /* 0x41e9c0, 0x42a370 */
    if (v->state == 0) return;   /* 0x42a310 */
    tray_draw(&r->tray);   /* 0x434070 */
    int32_t x, y;
    HudConfig_pos(HUDPOS_REARVIEW, &x, &y);   /* 0x420bf0 */
    if (r->lastx == x && r->lasty == y)
        frame(x, y, HudConfig_i32(0xfc), HudConfig_i32(0x100), 0, exe_i32(0x5cce30));   /* 0x435980 (style 0x60cb34: 0) */
    r->lastx = x, r->lasty = y;
}

/* Its place, the render visual's, the mirror's camera over the rectangle (z 1 .. 65000). 0x42c5e0 */
static void rearview_setpos(HudVisual *v, int x, int y)
{
    HRearview *r = (HRearview *)v;
    HudConfig_setpos(HUDPOS_REARVIEW, x, y);   /* 0x4207c0 */
    int32_t px, py;
    HudConfig_pos(HUDPOS_REARVIEW, &px, &py);
    tray_place(&r->tray, px, py);   /* 0x434240 */
    r->rx = px, r->ry = py;         /* (slot 3: 0x4366f0) */
    float W = (float)g_display.width, H = (float)g_display.height;
    float nx = (float)px / W, ny = (float)py / H;   /* 0x420c30 */
    float sw = (float)HudConfig_i32(0xfc) / W, sh = (float)HudConfig_i32(0x100) / H;   /* 0x4209d0 */
    Render_SetRearView(nx, ny, sw + nx, sh + ny, 1.0f, 65000.0f);   /* 0x469110 */
}

static void rearview_reset(HudVisual *v)   /* 0x42c730 */
{
    HRearview *r = (HRearview *)v;
    tray_setsize(&r->tray, HudConfig_i32(0xfc), HudConfig_i32(0x100), 0);   /* 0x420a50, 0x434000 */
    int32_t x, y;
    HudConfig_pos(HUDPOS_REARVIEW, &x, &y);
    v->vt->setpos(v, x, y);
    tray_colours(&r->tray);   /* 0x434550 */
}

/* Kept hidden but with its layout's +0x5c on and the HUD shown. 0x42c760 */
static bool rearview_hidden(HudVisual *v) { (void)v; return !(HudConfig_choicei32(0x5c) != 0 && always); }

static void rearview_destroy(HudVisual *v)   /* 0x42c5a0 */
{
    if ((HRearview *)v == s_rearview) s_rearview = NULL;
    free(v);
}

static const HudVisualVtbl rearview_vt = {rearview_destroy, rearview_draw, rearview_reset, tray_setstate, rearview_setpos, tray_progress, NULL, NULL, rearview_hidden};

static HudVisual *rearview_new(void)   /* 0x42c470 */
{
    HRearview *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    int32_t x, y;
    HudConfig_pos(HUDPOS_REARVIEW, &x, &y);
    int w = HudConfig_i32(0xfc), h = HudConfig_i32(0x100);
    tray_init(&r->tray, &rearview_vt, x, y, w, h, 0, true, 0, true, false);   /* 0x433ef0 */
    r->rw = w, r->rh = h;
    rearview_setpos(&r->tray.base, x, y);
    s_rearview = r;
    return &r->tray.base;
}

bool HUD_RearViewShown(void) { return always && s_rearview && s_rearview->tray.base.state == 3; }   /* 0x427560 */

/* ---- the lap's announcements (0x427790) ---- */

static int32_t s_laps_done, s_bestlap;   /* 0x60ca90, 0x60ca94 */

/* The most seconds any other racer is ahead of the local one (0x4353c0): each one's lead in road nodes
   (+0xb54) over the faster of the two speeds (the one behind's at least half the other's), as ticks
   then seconds; 0 while the local car is slower than 13.39 or level. *moving: faster than that. */
static int lead_ahead(bool *moving)
{
    Car *me = g_racers.c[g_race_local];
    int mine = *CAR_I(me, 0xb54), best = -10000;
    float sm = *CAR_F(me, 0x35c);
    for (int i = 0; i < g_racers.n; i++) {
        Car *o = g_racers.c[i];
        float so = *CAR_F(o, 0x35c);
        int d = *CAR_I(o, 0xb54) - mine;
        float v = d < 1 ? (sm * 0.5f < so ? so : sm * 0.5f) : (so * 0.5f < sm ? sm : so * 0.5f);   /* 0x5b23f0 */
        int t = 0;
        if (!(sm <= 13.39f) && d != 0) t = (int)lrintf((float)(d * 0x40) / v);   /* 0x5b2df8 */
        int sec = (t + ((t >> 31) & 0x3f)) >> 6;
        if (i != g_race_local && best < sec) best = sec;
    }
    *moving = 13.39f < sm;
    return best;
}

/* The local player's lap done (0x427790, from the progress 0x416330): its number said, then with
   others racing the gap (two: the lead; more: the place and the seconds behind), a best lap, the final
   lap next; not on the last lap, in a knockout's eliminating places, or watching a replay. */
void HUD_LapDone(void)
{
    if (g_race_linear) return;
    int was = s_laps_done++;
    Car *me = g_racers.c[g_race_local];
    int lap = *CAR_I(me, 0xb24 + 4 * s_laps_done);
    bool best = false;
    int b = lap;
    if (s_laps_done != 1) {
        b = s_bestlap;
        if (lap < s_bestlap) {
            best = true;
            b = lap;
        }
    }
    s_bestlap = b;
    bool last = s_laps_done == g_race_laps, next_last = s_laps_done == g_race_laps - 1;
    bool out = g_race_mode == 1 && g_racers.n - s_laps_done < *CAR_I(me, 0xb48);
    if (g_race_view == 2 || g_race_view == 3 || out || last) return;
    Audio_SayLap(was);
    if (1 < g_racers.n) {
        bool moving;
        int ahead = lead_ahead(&moving);
        if (g_racers.n == 2) {
            if (moving) Audio_SayLead(-ahead);
        } else {
            if (*CAR_I(me, 0xb48) <= g_racers.n) Audio_SayPlace(*CAR_I(me, 0xb48) - 1);
            if (moving) Audio_SayBehind(ahead);
        }
    }
    if (best) Audio_SayBestLap();
    if (next_last) Audio_SayFinalLap();
}

/* ---- the place congratulated (HCongrats, 0xbc bytes, vtable 0x5b2a40; 0x41cb10) ----

   At the race's end, for a podium place (only a win in game type 3): the screen's cross with the ring's
   gap at the centre, the gold, silver or bronze rings and the place's art fading in over 128 ticks, the
   announcer's line (and the winning or losing song), gone after 192 ticks. Its other use, the career's
   standing (0x60ca68), spins its arts, zooms the place's and slides it to a corner. */
typedef struct {
    int idx, last, period, count, elapsed;   /* (two cycles: +0x70 .., +0x88 ..) */
    int pad;
} CongratsCycle;

typedef struct {
    HudVisual base;
    HudArt *full, *ring, *outline;      /* +0x10, +0x14 (congrats1), +0x18 */
    HudArt *place[3];                   /* +0x1c, +0x20, +0x24: congrats3 1, congrats4 0, congrats4 1 */
    HudArt *spin[8];                    /* +0x28 .. +0x44 (the career's) */
    int32_t w, h, x0, y0;               /* +0x4c .. +0x58: the screen and its centre */
    int32_t x, y;                       /* +0x5c, +0x60 */
    int32_t sx, sy;                     /* +0x64, +0x68: where the slide starts */
    int32_t start;                      /* +0x6c: the first tick drawn */
    CongratsCycle cyc[2];               /* +0x70, +0x88 */
    float scale;                        /* +0xa0 */
    float dx, dy;                       /* +0xa4, +0xa8 */
    int32_t which;                      /* +0xac: the place (1 first; 0 none) */
    uint8_t raceover, spoken, skip;     /* +0xb0, +0xb1, +0xb2 */
    int32_t tx, ty;                     /* +0xb4, +0xb8: the slide's end */
} HCongrats;

static HudVisual *s_congrats;   /* 0x60ca64 */

/* The art's size, placed again at its rectangle's corner (HGraphicVisual_v5 0x423e70: a centred art drifts
   by half the change until placed again, as every caller here does). */
static void art_resize(HudArt *a, int w, int h)
{
    if (!a) return;
    a->w = w, a->h = h;
    HudArt_place(a, a->x, a->y);
}

static uint32_t congrats_colour(int place) { return exe_u32(0x5b2a0c + 4u * (uint32_t)place); }   /* (0: the table before) */

/* 0x41ddf0: the cycles (five patterns every 8 ticks; eight frames every 4), at the centre; the slide's
   end a fifth across and down less half the ring (by 0.5), over 32 ticks; the colours. */
static void congrats_reset(HCongrats *g)
{
    g->cyc[0] = (CongratsCycle){0, 0, 8, 5, 0, 0};
    g->cyc[1] = (CongratsCycle){0, 0, 4, 8, 0, 4};
    g->x = g->x0, g->y = g->y0;
    g->tx = (int)((float)g->w * exe_f32(0x5b23f4) - (float)exe_i32(0x5b2a28) * exe_f32(0x5b2a30) * exe_f32(0x5b23f0));
    g->ty = (int)((float)g->h * exe_f32(0x5b23f4));
    int n = (int)(exe_f32(0x5b2a34) * exe_f32(0x5b24e4));
    g->sx = g->x, g->sy = g->y;
    g->dx = (float)(g->x0 - g->tx) / (float)n;
    g->dy = (float)(g->y0 - g->ty) / (float)n;
    HudArt *all[14] = {g->full, g->ring, g->outline, g->place[0], g->place[1], g->place[2]};
    for (int i = 0; i < 8; i++) all[6 + i] = g->spin[i];
    for (int i = 0; i < 14; i++)
        if (all[i]) HudArt_place(all[i], g->x, g->y);
    uint32_t c = congrats_colour(g->which);
    if (g->full) HudArt_setcolours(g->full, 0x33ffffff, 0x33ffffff, 0x33ffffff, 0x33ffffff);
    if (g->ring) HudArt_setcolours(g->ring, c, c, c, c);
    if (g->outline) HudArt_setcolours(g->outline, 0x66ffffff, 0x66ffffff, 0x66ffffff, 0x66ffffff);
    for (int i = 0; i < 3; i++)
        if (g->place[i]) HudArt_setcolours(g->place[i], c, c, c, c);
    for (int i = 0; i < 8; i++)
        if (g->spin[i]) HudArt_setcolours(g->spin[i], c, c, c, c);
}

/* The cross over the screen (0x66ffffff), then its arms about the centre in the place's colour, by the
   first cycle's pattern: the four gaps' ends out to the edges and the ring's four radii. 0x41d3d0 */
static void congrats_lines(HCongrats *g)
{
    const float x = (float)g->x, y = (float)g->y, W = (float)g->w, H = (float)g->h;
    uint32_t cw[2] = {0x66ffffff, 0x66ffffff};
    float h0[8] = {0, y, 0, 1, W, y, 0, 1}, v0[8] = {x, 0, 0, 1, x, H, 0, 1};
    Hud2D_Line(h0, cw);
    Hud2D_Line(v0, cw);
    float r = (float)(int)((float)exe_i32(0x5b2a28) * g->scale * exe_f32(0x5b23f0));
    uint32_t c = congrats_colour(g->which), cc[2] = {c, c};
    const float La[8] = {x, 0, 0, 1, x, y - r, 0, 1}, Lb[8] = {x, y + r, 0, 1, x, H, 0, 1},
                Lc[8] = {0, y, 0, 1, x - r, y, 0, 1}, Ld[8] = {x + r, y, 0, 1, W, y, 0, 1};
    const float *A[5] = {Lc, Lc, Lc, La, Ld}, *B[5] = {Lb, Lb, La, Ld, Lb};
    int k = g->cyc[0].idx;
    Hud2D_Line(A[k], cc);
    Hud2D_Line(B[k], cc);
    const float M1[8] = {x, y, 0, 1, x, y - r, 0, 1}, M2[8] = {x, y, 0, 1, x, y + r, 0, 1},
                M3[8] = {x, y, 0, 1, x + r, y, 0, 1}, M4[8] = {x, y, 0, 1, x - r, y, 0, 1};
    const float *C[5] = {M1, M1, M3, M4, M1}, *D[5] = {M3, M3, M2, M2, M4};
    Hud2D_Line(C[k], cc);
    Hud2D_Line(D[k], cc);
}

/* The disc, its outline and the place's ring (congrats1, flipped by the first cycle's pattern), 128 by the
   scale. 0x41d8d0 */
static void congrats_rings(HCongrats *g)
{
    int w = (int)((float)exe_i32(0x5b2a28) * g->scale), h = (int)((float)exe_i32(0x5b2a2c) * g->scale);
    HudArt *a[2] = {g->full, g->outline};
    for (int i = 0; i < 2; i++) {
        if (!a[i]) continue;
        art_resize(a[i], w, h);
        HudArt_place(a[i], g->x, g->y);
        HudArt_draw(a[i]);
    }
    if (!g->ring) return;
    static const uint8_t flips[10] = {1, 1, 0, 1, 0, 0, 1, 0, 1, 1};   /* (the bytes of 0x1000101, 0x10000, 0x101) */
    int k = g->cyc[0].idx;
    art_resize(g->ring, w, h);
    art_mirror(g->ring, flips[k] != 0, 0);   /* 0x423c90 */
    g->ring->flipx = flips[k + 2];          /* 0x423d80 */
    HudArt_place(g->ring, g->x, g->y);
    HudArt_draw(g->ring);
}

/* The place's art at its size by the scale, in the place's colour (0x5b2a18) at alpha a. 0x41dbc0 */
static void congrats_place(HCongrats *g, int i, int a)
{
    if (0xff < a) a = 0xff;
    if (i < 0 || 2 < i || !g->place[i]) return;   /* (the original reads past its three; the skip flag keeps it there) */
    HudArt *art = g->place[i];
    uint32_t sz = 0x5b29b8 + 8u * (uint32_t)i;
    art_resize(art, (int)((float)exe_i32(sz) * g->scale), (int)((float)exe_i32(sz + 4) * g->scale));
    HudArt_place(art, g->x, g->y);
    uint32_t c = exe_u32(0x5b2a18 + 4u * (uint32_t)g->which) | (uint32_t)a << 24;
    HudArt_setcolours(art, c, c, c, c);
    HudArt_draw(art);
}

/* The spinning arts: the second cycle's frame at its size (0x5b29d0 ..) in the place's colour, the one
   before it over it at that size, half seen on the frame's first tick. 0x41da10 */
static void congrats_spin(HCongrats *g)
{
    int k = g->cyc[1].idx & 7, p = k == 0 ? 7 : k - 1;
    uint32_t sz = 0x5b29d0 + 8u * (uint32_t)k;
    int w = (int)((float)exe_i32(sz) * g->scale), h = (int)((float)exe_i32(sz + 4) * g->scale);
    if (g->spin[k]) {
        art_resize(g->spin[k], w, h);
        HudArt_place(g->spin[k], g->x, g->y);
        uint32_t c = congrats_colour(g->which);
        HudArt_setcolours(g->spin[k], c, c, c, c);
        HudArt_draw(g->spin[k]);
    }
    if (g->spin[p]) {
        art_resize(g->spin[p], w, h);
        HudArt_place(g->spin[p], g->x, g->y);
        int pad = g->cyc[1].pad ? g->cyc[1].pad : 1;
        uint32_t c = (uint32_t)(-((pad - g->cyc[1].elapsed) / pad)) << 31 | exe_u32(0x5b2a18 + 4u * (uint32_t)g->which);
        HudArt_setcolours(g->spin[p], c, c, c, c);
        HudArt_draw(g->spin[p]);
    }
}

/* The place's art zoomed: 2, 4 then 6 times its size over the zoom's thirds (u 0..1). 0x41dc90 */
static void congrats_zoom(HCongrats *g, int i, float u)
{
    if (i < 0 || 2 < i || !g->place[i]) return;
    static const int mult[3] = {2, 4, 6};
    int n = -(int)(u * exe_f32(0x5b23fc));
    if (n < 0) n = 0;
    if (2 < n) n = 2;
    uint32_t sz = 0x5b29b8 + 8u * (uint32_t)i;
    art_resize(g->place[i], exe_i32(sz) * mult[n], exe_i32(sz + 4) * mult[n]);
    HudArt_place(g->place[i], g->x, g->y);
    HudArt_draw(g->place[i]);
}

static void congrats_setstate(HudVisual *v, int32_t s)   /* HCongrats_v5 0x41dd60 */
{
    HCongrats *g = (HCongrats *)v;
    if (s != 3 && s != 1) {
        g->start = 0;
        g->which = 0;
        g->spoken = 1;
        v->state = s;
        return;
    }
    if (0 < g->which) {
        g->skip = g_race_type == 3 ? g->which != 1 : 3 < g->which;
        g->spoken = 0;
        congrats_reset(g);
    }
    v->state = s;
}

/* The announcer's race-over lines (0x4a6070 .. 0x4a6200): the place (0 first; 7 last, two ways), a win's,
   a loss's, the second of two's. */
static void say_finish_place(int p)
{
    int i = p == 0 ? (int)(REAL_rand() & 1) : p == 7 ? (int)(REAL_rand() & 1) + 8 : p + 1;
    Audio_Say(i + 7);
}

static void congrats_draw(HudVisual *v)   /* HCongrats_v1 0x41d070 */
{
    HCongrats *g = (HCongrats *)v;
    if (v->state == 0) return;
    if (g_race_mode == 0xb) {
        if (g->which == 0) return;
    } else if (g_race_mode == 5) {
        return;
    }
    if (g->which == 0) return;
    int32_t now = g_sim_tick;
    if (g->start == 0) g->start = now;
    int t = now - g->start;
    for (int i = 0; i < 2; i++) {
        CongratsCycle *c = &g->cyc[i];
        c->elapsed = now - c->last;
        if (c->period < c->elapsed) {
            c->last = now;
            c->idx = c->idx + 1 > c->count - 1 ? 0 : c->idx + 1;
            c->elapsed = 0;
        }
    }
    if (!g->spoken) {
        if (!g->raceover) {
            /* the tournament's standing after its last race (0x4a62d0), and unless the era ends the
               next line (0x4a6260) */
            if (g_race_tour.last_race && g->which < 4) {
                Audio_Say((int)(REAL_rand() & 1) * 3 + 0x20 + g->which - 1);
                if (!g_race_tour.end_of_era) Audio_Say((int)(REAL_rand() & 1) + 0x9c);
            }
        } else {
            int n = g_racers.n;
            switch (g_race_mode) {
            case 1:
                if (g->which == 1) Audio_Say((int)(REAL_rand() & 1) + 0x62);   /* 0x4a60b0 */
                else Audio_Say((int)(REAL_rand() % 7) + 100);                 /* 0x4a6120 */
                break;
            case 2:
                if (g->which == 1 && n == 2) Audio_Say((int)(REAL_rand() & 1) + 0x62);
                else if (n <= g->which) Audio_Say((int)(REAL_rand() % 7) + 100);
                else Audio_Say(0x61);                                          /* 0x4a60f0 */
                break;
            case 0: case 3: case 4: case 5: case 6: case 7: case 8: case 9:
                say_finish_place(g->which - 1);
                if (g->which == n && 1 < n) Audio_Say((int)(REAL_rand() & 1) + 0x11);   /* 0x4a6070 */
                break;
            default: break;
            }
        }
        if (g->raceover && 1 < g_racers.n) Music_Play(g->which == 1 ? exe_str(0x5ccb28) : exe_str(0x5ccb20), 1);   /* "win" / "lose " */
        g->spoken = 1;
    }
    if (g->skip) {
        v->vt->setstate(v, 0);
        g->which = 0;
        return;
    }
    congrats_lines(g);
    congrats_rings(g);
    if (g->raceover) {
        congrats_place(g, g->which - 1, t * 0xff / 0x80);
        if (0xc0 < t) v->vt->setstate(v, 0);
        return;
    }
    /* The standing: the arts spinning, the place's zooming, then it at full strength sliding to its
       corner as it shrinks to half. */
    if (t < 0xb0) congrats_spin(g);
    else if (t < 0xc0) congrats_zoom(g, g->which - 1, ((float)t - exe_f32(0x5b2a70)) * exe_f32(0x5b26a4));
    else congrats_place(g, g->which - 1, 0xff);
    if (0xbf < t && g->tx < g->x) {
        int u = t - 0xc0;
        g->x = (int)((float)g->sx - (float)u * g->dx);
        g->y = (int)((float)g->sy - (float)u * g->dy);
        if (g->x < g->tx) g->x = g->tx;
        if (g->y < g->ty) g->y = g->ty;
        g->scale = (float)(exe_f64(0x5b2750) - ((exe_f64(0x5b2750) - exe_f32(0x5b2a30)) * (double)(g->sx - g->x)) / (double)(g->sx - g->tx));
    }
}

/* Out of the modal list once 177 ticks have passed (it goes on drawing). HCongrats_v3 0x41e160 */
static bool congrats_done(HudVisual *v)
{
    HCongrats *g = (HCongrats *)v;
    return 0xb0 < g_sim_tick - g->start || v->state == 0;
}

static void congrats_destroy(HudVisual *v)   /* HCongrats_v0 0x41cf70 */
{
    HCongrats *g = (HCongrats *)v;
    HudArt *all[6] = {g->full, g->ring, g->outline, g->place[0], g->place[1], g->place[2]};
    for (int i = 0; i < 6; i++) HudArt_free(all[i]);
    for (int i = 0; i < 8; i++) HudArt_free(g->spin[i]);
    free(g);
}

static const HudVisualVtbl congrats_vt = {congrats_destroy, congrats_draw, NULL, congrats_setstate, NULL, NULL, congrats_done, NULL, NULL};

/* 0x41cb10: the arts centred (at the place, still 0, the constructor gives them), sized by the tables at
   0x5b29b8; the text (0x430e50, 0x80fab400) it makes is never drawn. */
static HudVisual *congrats_new(void)
{
    HCongrats *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    g->base.vt = &congrats_vt;
    g->scale = 1.0f;
    g->raceover = 1;
    g->w = g_display.width, g->h = g_display.height;
    g->x0 = g->w / 2, g->y0 = g->h / 2;
    static const struct { const char *file; int index; uint32_t size; } arts[14] = {
        {"congrats3", 1, 0x5b29b8}, {"congrats4", 0, 0x5b29c0}, {"congrats4", 1, 0x5b29c8},
        {"321go2", 1, 0x5b29d0}, {"321go", 3, 0x5b29d8}, {"321go", 2, 0x5b29e0}, {"congrats2", 0, 0x5b29e8},
        {"congrats2", 1, 0x5b29f0}, {"congrats2", 2, 0x5b29f8}, {"congrats2", 3, 0x5b2a00},
        {"congrats3", 0, 0x5b2a08}, {"circlefull", 0, 0x5b2a28}, {"congrats1", 0, 0x5b2a28},
        {"circleoutline", 0, 0x5b2a28}};
    HudArt *made[14];
    for (int i = 0; i < 14; i++)
        made[i] = HudArt_load(arts[i].file, arts[i].index, g->x, g->y, exe_i32(arts[i].size), exe_i32(arts[i].size + 4),
                              false, true, false);
    for (int i = 0; i < 3; i++) g->place[i] = made[i];
    for (int i = 0; i < 8; i++) g->spin[i] = made[3 + i];
    g->full = made[11], g->ring = made[12], g->outline = made[13];
    PENDING_NOTE(0x430e50, "HCongrats' text (made, never drawn)");
    congrats_reset(g);
    return &g->base;
}

/* ---- the HUD ---- */

/* ---- the pause menu (HPause, 300 bytes, vtable 0x5b2d54; 0x42a3a0) and the exit's (HExit, 0x130 bytes,
   vtable 0x5b2b0c; 0x4218c0): billboards with a column of buttons in the middle of the screen ---- */

enum { MENU_MAX = 4 };

typedef struct {
    HBillBoard bb;
    HButton *button[MENU_MAX];   /* +0x120 .. */
    int nbutton;
    int action[MENU_MAX];        /* each button's action (slot 9) */
} HMenu;

/* 0x42a5b0 / 0x421b40: the billboard, its buttons once it's open */
static void menu_draw(HudVisual *v)
{
    HMenu *m = (HMenu *)v;
    bb_draw(&m->bb);
    if (v->state != 0 && !(v->state == 1 || v->state == 2))
        for (int i = 0; i < m->nbutton; i++)
            if (m->button[i]) button_draw(m->button[i]);
}

/* 0x42a600 / 0x421b90 */
static int menu_input(HudVisual *v, int x, int y, int click)
{
    (void)x, (void)y;
    HMenu *m = (HMenu *)v;
    for (int i = 0; i < m->nbutton; i++)
        if (m->button[i]) button_input(m->button[i], click);
    if (v->state == 0) return 0;   /* 0x42a310 */
    for (int i = 0; i < m->nbutton; i++)
        if (m->button[i] && m->button[i]->clicked) return m->action[i];
    return 0;
}

static void menu_destroy(HudVisual *v)   /* 0x42a550 */
{
    HMenu *m = (HMenu *)v;
    for (int i = 0; i < m->nbutton; i++) button_free(m->button[i]);
    bb_free(&m->bb);
    free(m);
}

static const HudVisualVtbl menu_vt = {menu_destroy, menu_draw, NULL, bb_setstate, bb_setpos, NULL, bb_done, menu_input, NULL};

/* The billboard (w, h) titled by hud.loc's title id, n buttons of (bw, bh) by their ids, 1.6 of their
   height apart, centred on the screen. */
static HudVisual *menu_new(uint32_t wsize, uint32_t bsize, int title, int n, const int *ids, const int *actions)
{
    HMenu *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    const uint8_t *loc = HudConfig_loc();
    const char *t = LOC_string(loc, title);
    /* (the left of the title bar is the global 0x5e8e50's text: empty in the port) */
    bb_init(&m->bb, &menu_vt, exe_i32(wsize), exe_i32(wsize + 4), "", t ? t : "", false, false, false);
    int bw = exe_i32(bsize), bh = exe_i32(bsize + 4);
    float step = (float)((double)bh * exe_f64(0x5b2b00));
    int x = g_display.width / 2 - bw / 2;
    int y = (int)((float)(g_display.height / 2) - step * (float)n * 0.5f);   /* 0x5b2600 / 0x5b2404 = n */
    for (int i = 0; i < n && i < MENU_MAX; i++) {
        const char *s = LOC_string(loc, ids[i]);
        m->button[i] = button_new(x, y, bw, bh, s ? s : "", true);
        m->action[i] = actions[i];
        m->nbutton++;
        y = (int)((float)y + step);
    }
    return &m->bb.base;
}

static HudVisual *s_pause, *s_exit;   /* 0x60ca44, 0x60ca48 */

/* ---- the question (HYesNoOverlay, 0x138 bytes, vtable 0x5b3554; 0x437370): "are you sure?" on a
   billboard, up to two lines of hud.loc and the yes and no buttons, 1.4 of their height apart ---- */

typedef struct {
    HBillBoard bb;
    HText *line[2];       /* +0x120, +0x124 */
    HButton *yes, *no;    /* +0x128, +0x12c */
    int32_t text[2];      /* +0x130, +0x134: hud.loc ids, -1 none */
} HYesNo;

static HudVisual *s_yesno;   /* 0x60c9ac */
static bool s_quit_asked;    /* 0x60ca9c */

static void yesno_settext(HudVisual *v, int a, int b)   /* 0x4375e0 */
{
    HYesNo *q = (HYesNo *)v;
    if (q) q->text[0] = a, q->text[1] = b;
}

static void yesno_draw(HudVisual *v)   /* HYesNoOverlay_v1 0x437600 */
{
    HYesNo *q = (HYesNo *)v;
    bb_draw(&q->bb);
    if (v->state == 0 || v->state == 1 || v->state == 2) return;
    for (int k = 0; k < 2; k++)
        if (q->text[k] != -1) {
            const char *s = LOC_string(HudConfig_loc(), q->text[k]);
            text_print(q->line[k], s ? s : "", 1.0f, 1.0f);
        }
    button_draw(q->yes);
    button_draw(q->no);
}

/* 0x1c yes, 0x1d no. HYesNoOverlay_v9 0x4376a0 */
static int yesno_input(HudVisual *v, int x, int y, int click)
{
    (void)x, (void)y;
    HYesNo *q = (HYesNo *)v;
    if (q->yes) button_input(q->yes, click);
    if (q->no) button_input(q->no, click);
    if (v->state == 0) return 0;
    if (q->yes && q->yes->clicked) return 0x1c;
    if (q->no && q->no->clicked) return 0x1d;
    return 0;
}

static void yesno_destroy(HudVisual *v)   /* HYesNoOverlay_v0 0x437570 */
{
    HYesNo *q = (HYesNo *)v;
    text_free(q->line[0]);
    text_free(q->line[1]);
    button_free(q->yes);
    button_free(q->no);
    bb_free(&q->bb);
    free(q);
}

static const HudVisualVtbl yesno_vt = {yesno_destroy, yesno_draw, NULL, bb_setstate, bb_setpos, NULL, bb_done, yesno_input, NULL};

static HudVisual *yesno_new(void)
{
    HYesNo *q = calloc(1, sizeof *q);
    if (!q) return NULL;
    const uint8_t *loc = HudConfig_loc();
    const char *t = LOC_string(loc, 0x46fe);
    bb_init(&q->bb, &yesno_vt, exe_i32(0x5b3540), exe_i32(0x5b3544), "", t ? t : "", false, false, false);
    int bw = exe_i32(0x5b3548), bh = exe_i32(0x5b354c);
    float step = (float)((double)bh * exe_f64(0x5b2938));
    int x = g_display.width / 2 - bw / 2;
    float y0 = (float)(g_display.height / 2) - step * exe_f32(0x5b2404) * exe_f32(0x5b23f0);
    int y = (int)y0;
    for (int k = 0; k < 2; k++) {
        q->line[k] = text_new(HudConfig_font(0x4c), 0xc0000000u, (uint32_t)HudConfig_i32(0x14), x, y, 0, 0, 0, false);
        y = (int)((float)y + step);
    }
    const char *ys = LOC_string(loc, 0x46dd), *ns = LOC_string(loc, 0x46de);
    q->yes = button_new(x, y, bw, bh, ys ? ys : "", true);
    y = (int)((float)y + step);
    q->no = button_new(x, y, bw, bh, ns ? ns : "", true);
    q->text[0] = q->text[1] = -1;
    return &q->bb.base;
}

/* ---- the replay's save (HReplaySave, 0x130 bytes, vtable 0x5b2f8c; 0x42dfc0): a billboard (500 x 325)
   titled 0x46d8 asking for a name (0x46fa) above a text entry (HTextEntryVisual, 0x158 bytes, vtable
   0x5b3160; 0x430bb0: an amber pressed button whose text is its own buffer, eleven characters, a grey
   "I" blinking after it every sixteen ticks), then the save (0x46d9) and cancel (0x46da) buttons, all
   142 x 19, 1.4 of their height apart (the question's), the entry half of three steps over the
   screen's middle, the name a step and a half above the cancel ---- */

typedef struct {
    HButton *b;             /* (HButtonVisual 0x41b160) */
    HText *cursor;          /* +0x104: 0xff808080 */
    char text[0x40];        /* +0x108 */
    int32_t len, cap;       /* +0x148, +0x14c */
    int32_t blink_at;       /* +0x150 */
    uint8_t blink;          /* +0x154 */
} HTextEntry;

typedef struct {
    HBillBoard bb;
    HTextEntry entry;       /* +0x120 */
    HButton *save, *cancel; /* +0x124, +0x128 */
    HText *label;           /* +0x12c */
} HReplaySave;

static HudVisual *s_replaysave;   /* 0x60c9b0 */
static bool s_save_asked;         /* 0x60ca9d: the question is the overwrite's */

/* The typed characters (the window's WM_CHAR queue, 0x610a80): taken from its end, one a call. */
static int s_chars[0x40], s_nchars;

void HUD_TypeKey(uint8_t code, bool shift)
{
    int ch = Input_KeyChar(code, shift);
    if (ch && s_nchars < (int)(sizeof s_chars / sizeof *s_chars)) s_chars[s_nchars++] = ch;
}

static void chars_clear(void) { s_nchars = 0; }   /* 0x434fc0 */

/* The last character typed: ok but for the control ones but backspace and those a file name can't
   have, back the backspace. 0x435590 */
static bool char_take(int *ch, bool *back)
{
    *back = false;
    if (s_nchars == 0) return false;
    int c = s_chars[--s_nchars];
    *ch = c;
    bool ok = !((c < 0x20 && c != 8) || strchr("\\/:*?\"<>|%", c));
    if (c == 8) ok = *back = true;
    return ok;
}

/* The race loop's tick for the cursor (0x606ac8, which runs on while the replay is paused): the
   port's clock at 64 a second. */
static int32_t loop_tick(void) { return (int32_t)((uint64_t)app_ticks_ms() * 64u / 1000u); }

static void entry_draw(HTextEntry *e)   /* HTextEntryVisual_v6 0x430d00 */
{
    button_draw(e->b);
    if (0x10 < loop_tick() - e->blink_at) {
        e->blink_at = loop_tick();
        e->blink = !e->blink;
    }
    if (e->blink && e->b->ttext && e->b->ttext->font && e->b->ttext->font->font) {
        float a, c, w, h;   /* (0x41b290: the text's end) */
        text_extent(e->b->ttext, e->text, &a, &c, &w, &h);
        e->cursor->x = (int)((float)e->b->ttext->x + w) + 1;
        e->cursor->y = e->b->ttext->y;
        text_print(e->cursor, exe_str(0x5cd088), 1.0f, 1.0f);   /* "I" */
    }
}

static void replaysave_draw(HudVisual *v)   /* HReplaySave_v1 0x42e220 */
{
    HReplaySave *r = (HReplaySave *)v;
    bb_draw(&r->bb);
    if (v->state == 0 || v->state == 1 || v->state == 2) return;
    entry_draw(&r->entry);
    button_draw(r->save);
    button_draw(r->cancel);
    const char *s = LOC_string(HudConfig_loc(), 0x46fa);
    text_print(r->label, s ? s : "", 1.0f, 1.0f);
}

/* Open: the buttons' clicks, the last character typed into the name (backspace: out of it); the save
   with a name: written ("%s.rpl" 0x5ccebc) when the save directory doesn't have it (0x10), else the
   overwrite asked (0x11); the cancel 0x12. HReplaySave_v9 0x42e290 */
static int replaysave_input(HudVisual *v, int x, int y, int click)
{
    (void)x, (void)y;
    HReplaySave *r = (HReplaySave *)v;
    if (v->state != 3) return 0;
    button_input(r->entry.b, click);
    button_input(r->save, click);
    button_input(r->cancel, click);
    int ch;
    bool back;
    if (char_take(&ch, &back)) {
        HTextEntry *e = &r->entry;
        if (!back) {
            if (e->len < e->cap) e->text[e->len++] = (char)ch;   /* 0x430cb0 */
        } else if (0 < e->len) {
            e->text[--e->len] = 0;   /* 0x430ce0 */
        }
    }
    if (r->save->clicked && r->entry.len != 0) {
        char f[0x50];
        snprintf(f, sizeof f, "%s.rpl", r->entry.text);   /* ("%s%s.rpl" 0x5ccec4 in 0x65b360 for the test) */
        if (Replay_Exists(f)) return 0x11;
        Replay_Write(f);
        return 0x10;
    }
    if (r->cancel->clicked) return 0x12;
    return 0;
}

/* The overwrite's yes: the name's file written whatever it holds. 0x42e400(1) */
static void replaysave_overwrite(void)
{
    HReplaySave *r = (HReplaySave *)s_replaysave;
    if (!r || r->entry.len == 0) return;
    char f[0x50];
    snprintf(f, sizeof f, "%s.rpl", r->entry.text);
    Replay_Write(f);
}

static void replaysave_destroy(HudVisual *v)   /* HReplaySave_v0 0x42e1b0 */
{
    HReplaySave *r = (HReplaySave *)v;
    text_free(r->entry.cursor);
    button_free(r->entry.b);
    button_free(r->save);
    button_free(r->cancel);
    text_free(r->label);
    bb_free(&r->bb);
    free(r);
}

static const HudVisualVtbl replaysave_vt = {replaysave_destroy, replaysave_draw, NULL, bb_setstate, bb_setpos, NULL, bb_done, replaysave_input, NULL};

static HudVisual *replaysave_new(void)   /* 0x42dfc0 */
{
    HReplaySave *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    const uint8_t *loc = HudConfig_loc();
    const char *t = LOC_string(loc, 0x46d8);
    bb_init(&r->bb, &replaysave_vt, exe_i32(0x5b2f78), exe_i32(0x5b2f7c), "", t ? t : "", false, false, false);
    int bw = exe_i32(0x5b2f80), bh = exe_i32(0x5b2f84);
    float step = (float)((double)bh * exe_f64(0x5b2938));
    float three = step * exe_f32(0x5b2600);
    int x = g_display.width / 2 - bw / 2;
    int y = (int)((float)(g_display.height / 2) - three * exe_f32(0x5b23f0));
    HTextEntry *e = &r->entry;
    e->cap = 0xb;
    e->blink = 1;
    e->b = button_new(x, y, bw, bh, e->text, true);
    if (e->b) {
        e->b->colour = 0xfffab400u;   /* 0x41b300 */
        e->b->pressed = 1;            /* 0x41b310 */
    }
    e->cursor = text_new(HudConfig_font(0x4c), 0xff808080u, (uint32_t)HudConfig_i32(0x14), 0, 0, 0, 0, 0, false);
    y = (int)((float)y + step);
    const char *ss = LOC_string(loc, 0x46d9), *cs = LOC_string(loc, 0x46da);
    r->save = button_new(x, y, bw, bh, ss ? ss : "", true);
    y = (int)((float)y + step);
    r->cancel = button_new(x, y, bw, bh, cs ? cs : "", true);
    y = (int)((float)y - three);
    r->label = text_new(HudConfig_font(0x4c), 0xc0000000u, (uint32_t)HudConfig_i32(0x14), x, y, 0, 0, 0, false);
    return &r->bb.base;
}

/* ---- the replay's panel (HReplay, 0x19c bytes, vtable 0x5b2f3c; 0x42c890): a fading tray at the bottom
   centre with seven buttons of replay.fsh's art (camera, restart, pause / play, quit, speed, summary, save
   replay; a network game's only camera, the next and previous car to observe, summary), shown while the
   mouse is over it or was within forty frames, their names after forty frames over them ---- */

typedef struct {
    HTray tray;
    HudArt *camera, *pause, *play, *quit, *restart;   /* +0xc4 .. +0xd4: art 0, 1 (10 networked), 11, 2, 3 */
    HudArt *speed_art[4];      /* +0xd8 .. +0xe4: art 5 .. 8 (their copies +0xf0 .. +0xfc) */
    HudArt *summary, *save;    /* +0xe8, +0xec: art 4, 9 */
    int32_t hover[8];          /* +0x108 .. +0x124: frames over each button (to 40), then off the tray */
    HText *text;               /* +0x128 */
    int32_t bx[7], by[7];      /* +0x12c .. +0x160: the buttons' corners */
    int32_t tx, ty;            /* +0x164, +0x168: the tray's place */
    int32_t lx, ly;            /* +0x16c, +0x170: the names' row */
    int32_t speed;             /* +0x174: the speed's art, 5 .. 8 */
    uint8_t keep;              /* +0x178: shown whatever the mouse (0x42d410, 0x42d420) */
    uint8_t net;               /* +0x198 */
    uint8_t paused;            /* +0x199: the play button in the pause's place */
} HReplay;

static HudVisual *s_replay;   /* 0x60ca58 */
static uint8_t s_replay_down[7];   /* 0x60cb50 .. 0x60cb56: each button pressed and not yet released */
static int32_t bw(void) { return exe_i32(0x5b2ef0); }
static int32_t bh(void) { return exe_i32(0x5b2ef4); }

/* The places (HReplay_v13 0x42de70): the seven buttons 30 apart along the bottom, centred. */
static void replay_layout(HReplay *r, int w, int h, int lineoff)
{
    int x0 = g_display.width / 2 - (bw() * 7 + 0xb4) / 2, y = g_display.height - 0x1e;
    r->lx = x0, r->ly = y;
    for (int i = 0; i < 7; i++) r->bx[i] = x0 + (bw() + 0x1e) * i, r->by[i] = y;
    r->ty = g_display.height - 0x21;
    r->tx = x0 - 3;
    tray_setsize(&r->tray, w, h, lineoff);   /* 0x434000 */
    tray_place(&r->tray, r->tx, r->ty);      /* 0x434240 */
}

/* The alpha the fade gives (state 1 in, 2 out). */
static uint32_t replay_alpha(HReplay *r, int of)
{
    HudVisual *v = &r->tray.base;
    int p = v->state == 1 ? v->vt->progress(v) : v->state == 2 ? 100 - v->vt->progress(v) : 100;
    return (uint32_t)(p * of / 100) << 24 | 0xfab400u;
}

/* A button's frame (0x42d440): its four edges, bright with the mouse over it. */
static void replay_frame(HReplay *r, int x, int y)
{
    float q[16];
    box(x, y, bw(), bh(), q);
    uint32_t hi = replay_alpha(r, 0xff), lo = replay_alpha(r, 0x4c);
    bool over = !(s_mouse_x < x || x + bw() < s_mouse_x || s_mouse_y < y || bh() + y < s_mouse_y);
    uint32_t c[2] = {over ? hi : lo, over ? hi : lo};
    float top[8] = {q[0], q[1], 0, 1, q[4], q[5], 0, 1};
    Hud2D_Line(top, c);
    float bottom[8] = {q[0], q[1] + (float)bh(), 0, 1, q[4], q[5] + (float)bh(), 0, 1};
    Hud2D_Line(bottom, c);
    float right[8] = {q[4], q[5], 0, 1, q[8], q[9] + 1.0f, 0, 1};
    Hud2D_Line(right, c);
    float left[8] = {q[12], q[13], 0, 1, q[0], q[1], 0, 1};
    Hud2D_Line(left, c);
}

static void replay_art(HudArt *a, uint32_t c)
{
    if (!a) return;
    HudArt_setcolours(a, c, c, c, c);
    HudArt_draw(a);
}

/* The buttons' names (0x42dc90): over a button forty frames, its name on a light box above it. */
static void replay_names(HReplay *r)
{
    const uint8_t *loc = HudConfig_loc();
    const char *name[7] = {
        LOC_string(loc, 0x46e3), LOC_string(loc, r->net ? 0x46f3 : 0x46e4),
        LOC_string(loc, r->net ? 0x46f4 : r->paused ? 0x46fd : 0x46e5), LOC_string(loc, 0x4712),
        LOC_string(loc, 0x46e6), LOC_string(loc, 0x46e7), LOC_string(loc, 0x46e8),
    };
    for (int i = 0; i < 7; i++) {
        int x = r->lx + (bw() + 0x1e) * i, y = r->ty - 0x10;
        if (r->hover[i] < 0x28 || !name[i] || !r->text || !r->text->font || !r->text->font->font) continue;
        float a, b, w, h;
        text_extent(r->text, name[i], &a, &b, &w, &h);
        float q[16];
        box(x, y, (int)(w + exe_f32(0x5b2608)), 0xe, q);
        flat(q, 0x40ffffffu);
        r->text->x = x + 4;
        r->text->y = (int)((exe_f32(0x5b2450) - h) * exe_f32(0x5b23f0) + (float)y);
        text_print(r->text, name[i], 1.0f, 1.0f);
    }
}

/* HReplay_v1 0x42d160: in the race-over's replay, shown (fading in) while the mouse is about, faded out
   after forty frames away; drawn while not hidden. */
static void replay_draw(HudVisual *v)
{
    HReplay *r = (HReplay *)v;
    if (g_race_view != 2) return;   /* (or a network game's observer: not ported) */
    if (r->hover[7] < 0x28 || r->keep) {
        if (v->state == 0 || v->state == 2) v->vt->setstate(v, 1);
    } else if (v->state == 3 || v->state == 1) {
        v->vt->setstate(v, 2);
    }
    if (v->state == 0) return;
    tray_draw(&r->tray);   /* 0x434070 */
    uint32_t c = replay_alpha(r, 0xff);
    replay_art(r->camera, c);
    replay_frame(r, r->bx[0], r->by[0]);
    replay_art(r->paused ? r->play : r->pause, c);
    replay_frame(r, r->bx[2], r->by[2]);
    replay_art(r->restart, c);
    replay_frame(r, r->bx[1], r->by[1]);
    replay_art(r->summary, c);
    replay_frame(r, r->bx[5], r->by[5]);
    if (!r->net) {
        replay_art(r->quit, c);
        replay_frame(r, r->bx[3], r->by[3]);
        replay_art(r->speed_art[r->speed - 5], c);
        replay_frame(r, r->bx[4], r->by[4]);
        replay_art(r->save, c);
        replay_frame(r, r->bx[6], r->by[6]);
    }
    replay_names(r);
}

/* Over button i: its frames counted, and its action on the press (once until released). */
static bool replay_over(HReplay *r, int i, int x, int y, int press, int *hit)
{
    if (x < r->bx[i] || bw() + r->bx[i] < x || y < r->by[i] || bh() + r->by[i] < y) {
        r->hover[i] = 0;
        return false;
    }
    bool now = false;
    if (press == 2) {
        if (!s_replay_down[i]) now = true, s_replay_down[i] = 1;
    } else {
        s_replay_down[i] = 0;
    }
    if (0x28 < ++r->hover[i]) r->hover[i] = 0x28;
    (void)hit;
    return now;
}

/* HReplay_v9 0x42d800: the mouse over the tray keeps it up; a press on a button gives its action: 6
   camera, 7 restart (the network's 0x17 the previous car), 8 pause / play (0x16 the next car), 9 quit,
   10 .. 13 the speeds in turn (2, 3, 1, 0: their art 5 .. 8), 0xe the summary, 0xf save the replay. */
static int replay_input(HudVisual *v, int x, int y, int press)
{
    HReplay *r = (HReplay *)v;
    HTray *t = &r->tray;
    int a = 0;
    bool in = !(x < t->x || t->x + t->w < x || y < t->y || t->y + t->h < y);   /* 0x434410 */
    if (!in) {
        if (0x28 < ++r->hover[7]) r->hover[7] = 0x28;
    } else {
        r->hover[7] = 0;
    }
    if (replay_over(r, 0, x, y, press, NULL)) a = 6;
    if (replay_over(r, 1, x, y, press, NULL)) {
        if (!r->net) r->speed = 5, a = 7;
        else a = 0x17;
    }
    if (replay_over(r, 2, x, y, press, NULL)) {
        if (!r->net) a = 8, r->paused = !r->paused;
        else a = 0x16;
    }
    if (replay_over(r, 5, x, y, press, NULL)) a = 0xe;
    if (!r->net) {
        if (replay_over(r, 3, x, y, press, NULL)) a = 9;
        if (replay_over(r, 4, x, y, press, NULL)) {
            if (8 < ++r->speed) r->speed = 5;
            a = r->speed == 5 ? 10 : r->speed == 6 ? 0xb : r->speed == 7 ? 0xc : 0xd;
        }
        if (replay_over(r, 6, x, y, press, NULL)) a = 0xf;
    }
    return a;
}

static void replay_destroy(HudVisual *v)   /* 0x42d080 */
{
    HReplay *r = (HReplay *)v;
    HudArt *arts[] = {r->camera, r->pause, r->play, r->quit, r->restart, r->summary,
                      r->speed_art[0], r->speed_art[1], r->speed_art[2], r->speed_art[3], r->save};
    for (size_t i = 0; i < sizeof arts / sizeof arts[0]; i++) HudArt_free(arts[i]);
    text_free(r->text);
    free(r);
}

static void replay_setpos(HudVisual *v, int x, int y) { tray_corners(&((HReplay *)v)->tray, x, y); }

static const HudVisualVtbl replay_vt = {
    .destroy = replay_destroy, .draw = replay_draw, .setstate = tray_setstate, .setpos = replay_setpos,
    .progress = tray_progress, .input = replay_input,
};

static HudArt *replay_load(HReplay *r, int index, int b, uint32_t size)
{
    int w = exe_i32(size), h = exe_i32(size + 4);
    return HudArt_load(exe_str(0x5cce90), index, (bw() - w) / 2 + 1 + r->bx[b], (bh() - h) / 2 + 1 + r->by[b], w, h, false, false, false);   /* "replay" */
}

static HudVisual *replay_new(bool net)   /* 0x42c890 */
{
    HReplay *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    int w = bw() * 7 + 0xba, h = bh() + 6;   /* 0x60cb48, 0x60cb4c (0x42c840 .. 0x42c870) */
    tray_init(&r->tray, &replay_vt, 0, 0, w, h, 0, false, 1, false, true);   /* 0x433ef0 */
    r->net = net;
    replay_layout(r, w, h, 0);
    r->camera = replay_load(r, 0, 0, 0x5b2ef8);
    r->pause = replay_load(r, net ? 10 : 1, 2, 0x5b2f00);
    r->play = replay_load(r, 0xb, 2, 0x5b2f30);
    r->quit = replay_load(r, 2, 3, 0x5b2f08);
    r->restart = replay_load(r, 3, 1, 0x5b2f10);
    for (int i = 0; i < 4; i++) r->speed_art[i] = replay_load(r, 5 + i, 4, 0x5b2f18);
    r->summary = replay_load(r, 4, 5, 0x5b2f20);
    r->save = replay_load(r, 9, 6, 0x5b2f28);
    r->text = text_new(HudConfig_font(0x64), 0xff000000u, (uint32_t)HudConfig_i32(0x14), 0, 0, 0, 0, 0, false);
    r->speed = 5;
    return &r->tray.base;
}

void HUD_ReplayPlaying(void) { if (s_replay) ((HReplay *)s_replay)->paused = 0; }   /* 0x42df90 */

static bool s_file_replay;   /* 0x60c9b4: a replay file's race */

void HUD_Init(void)
{
    HUD_Free();
    s_laps_done = s_bestlap = 0;   /* (0x426c50's reset) */
    g_mission_forced = -1;         /* 0x60c9ec */
    s_noscreens = false;           /* 0x60ca10 */
    hidden = false;                /* 0x60ca00 */
    /* A replay file's race (the view 2 from the start): no race screens, the summary's button idle,
       and the next song (0x4ae2a0). */
    s_file_replay = g_race_view == 2;   /* 0x60c9b4 */
    if (s_file_replay) Music_Play(NULL, 0);
    /* The player's name (the race description's +0x558 +0x17c), the layout chosen (0x657db8), the
       screen. */
    HudConfig_Load(g_race_player, 0, g_display.width, g_display.height);
    always = true;  /* (HUD_LoadStrings 0x41e190 sets 0x60c970) */
    add(gear_new(), true, false);
    add(speed_new(), true, false);
    s_time = time_new(&time_vt, HUDPOS_TIME, HudConfig_i32(0xa4), HudConfig_i32(0xa8));   /* 0x4315d0 */
    add(s_time, true, false);
    s_cones_down = 0;
    s_multitime = time_new(&multitime_vt, HUDPOS_MULTITIME, HudConfig_choicei32(0), HudConfig_choicei32(4));   /* 0x42a0f0 */
    add(s_multitime, true, false);
    add(tdamage_new(), true, false);   /* 0x42f880 */
    add(position_new(), true, false);
    add(gdamage_new(), true, false);   /* 0x421e20 */
    add(speedometer_new(), true, false);
    add(tach_new(), true, false);
    add(tachdigital_new(), true, false);
    add(map_new(), true, true);   /* 0x431e50 (drawn last) */
    PENDING_NOTE(0x428490, "HLeader (0x60ca38): the network race leader's picture (draws only in a network session)");
    add(rearview_new(), true, true);   /* 0x42c470 (0x60ca3c) */
    add(countdown_new(), false, false);
    s_congrats = congrats_new();   /* 0x41cb10 (0x60ca64) */
    add(s_congrats, false, false);
    {
        int id = g_race_mode == 1 ? 0x46ff : g_race_mode == 2 ? 0x46f9 : 0x46d1;
        const char *title = LOC_string(HudConfig_loc(), id);
        summary = summary_new(title ? title : "");   /* 0x42bc30 */
        add(summary, false, false);
    }
    mission = mission_new();   /* 0x428900 */
    add(mission, false, false);
    s_records = records_new();   /* 0x433790 (0x60ca5c) */
    add(s_records, false, false);
    s_standing_congrats = congrats_new();   /* 0x41cb10 (0x60ca68) */
    add(s_standing_congrats, false, false);
    s_standings = standings_new();   /* 0x4317c0 (0x60ca54) */
    add(s_standings, false, false);
    s_info = info_new(true);   /* 0x4282b0(1) (0x60c9fc) */
    add(s_info, false, false);
    static const int pause_ids[3] = {0x46bc, 0x46bd, 0x46be}, pause_actions[3] = {2, 3, 0x1a};
    s_pause = menu_new(0x5b2d40, 0x5b2d48, 0x46d0, 3, pause_ids, pause_actions);   /* 0x42a3a0 */
    add(s_pause, false, false);
    static const int exit_ids[4] = {0x46b6, 0x46b7, 0x46b8, 0x46b9}, exit_actions[4] = {0x15, 4, 5, 0x18};
    s_exit = menu_new(0x5b2af0, 0x5b2af8, 0x46ba, 4, exit_ids, exit_actions);      /* 0x4218c0 */
    s_replaysave = replaysave_new();   /* 0x42dfc0 (0x60c9b0) */
    add(s_replaysave, false, false);
    s_save_asked = false;
    s_yesno = yesno_new();   /* 0x437370 (0x60c9ac) */
    add(s_yesno, false, false);
    s_replay = replay_new(g_race_mode == 3);   /* 0x42c890 (0x60ca58) */
    add(s_replay, false, false);
    s_quit_asked = false;
    g_race_quit = 0;   /* (0x657e34) */
    add(s_exit, false, false);
    nmodal = 0;
    wrongway = wrongway_new();   /* 0x436dd0 (drawn on its own) */
    s_pointer = HudArt_load(exe_str(0x5cccd8), 0, 0, 0, exe_i32(0x5b2be8), exe_i32(0x5b2bec), false, false, false);   /* "Pointer" */
    Hud2D_Init();   /* 0x419d70 */
    {
        uint32_t b;
        if (!MOUSE_state(&s_mousex, &s_mousey, &b)) s_mousex = s_mousey = 0;   /* (MOUSE_getstate 6) */
    }
    s_pointer_ticks = 0;
    HudConfig_trays(true);   /* (0x657de4, the trays option: on) */
}

bool HUD_ModalShown(void) { return nmodal > 0; }   /* 0x426a20 */

/* The HUD hidden or shown again while a modal visual is up (the pause menu's own key). 0x425710 */
void HUD_TogglePauseMenu(void)
{
    if (nmodal > 0) hidden = !hidden;
}

static void push_modal(HudVisual *v)
{
    if (v && nmodal < 8) modal[nmodal++] = v;
}

/* The race over: the player's place congratulated, the results, the track's records. 0x426c50 */
static void race_screens(bool again)
{
    if (g_race_mode == 6 || g_race_mode == 7 || g_race_mode == 8) return;
    if (s_file_replay) return;
    if (g_race_mode <= 5 && s_congrats && !again) {
        /* The place: a knockout's 1 (8 when the car was knocked out, +0xb50 3), the network race's by its
           standings (pending: the car's), else the camera's car's (+0xcd4). (0x60ca98 overrides it when
           set; nothing sets it here.) */
        Car *c = g_camera_car < g_racers.n ? g_racers.c[g_camera_car] : NULL;
        int place = 0;
        if (g_race_derby == 1) place = c && *CAR_I(c, 0xb50) == 3 ? 8 : 1;
        else if (c) place = *CAR_I(c, 0xcd4);
        if (g_race_mode == 3) PENDING_NOTE(0x48d720, "the network race's place");
        ((HCongrats *)s_congrats)->which = place;   /* 0x41e150 */
        push_modal(s_congrats);
        ((HCongrats *)s_congrats)->raceover = 1;     /* 0x41dd40(0) */
    }
    if (g_race_derby) return;
    if (g_race_mode == 0 || g_race_mode == 3 || g_race_mode == 1 || g_race_mode == 2 || g_race_mode == 5) {
        push_modal(summary);
        if (summary) summary->flag = 1;   /* (slot 11: the second key's hint) */
        if (g_race_mode != 3) push_modal(s_records);
    } else if (g_race_mode == 4) {
        /* the career: the results first, then its boards */
        push_modal(summary);
        if (summary) summary->flag = 1;
        int rank = standings_rank();   /* 0x431de0 */
        if (!again && g_race_tour.last_race && rank < 4 && s_standing_congrats) {
            /* the tournament won or placed: its standing congratulated (0x41e150, 0x41dd40(1)) */
            push_modal(s_standing_congrats);
            ((HCongrats *)s_standing_congrats)->which = rank;
            ((HCongrats *)s_standing_congrats)->raceover = 0;
        }
        push_modal(s_standings);
        if (g_race_tour.last_race && 3 < rank && s_info) {
            push_modal(s_info);
            info_settext(s_info, LOC_string(HudConfig_loc(), 0x46f0));   /* 0x4283d0 */
        }
        push_modal(s_records);
    } else if (g_race_mode == 9) {
        /* Factory Driver: the mission's summary (0x60ca50) up, its opening (HMissionSummary_v5 0x4293b0)
           judging the mission */
        push_modal(mission);
    } else if (g_race_mode == 11) {
        push_modal(summary);
    } else if (g_race_mode == 10) {
        /* a win's message by the era (hud.loc 0x46e9 ..) with the announcer's (0x4a6450, not in a
           replay's), else the loss's (0x46ec) */
        push_modal(s_info);
        Car *me = local_car();
        if (me && *CAR_I(me, 0xcd4) == 1) {
            int era = 0 <= g_race_tour.era && g_race_tour.era < 3 ? g_race_tour.era : 0;   /* (the original indexes unchecked) */
            info_settext(s_info, LOC_string(HudConfig_loc(), 0x46e9 + era));
            if (g_race_view != 2 && g_race_view != 3) Audio_Say(0xa0);
        } else {
            info_settext(s_info, LOC_string(HudConfig_loc(), 0x46ec));
        }
    }
}

void HUD_Pause(void)
{
    if (!s_pause || nmodal == 8) return;
    modal[nmodal++] = s_pause;
    s_pause->vt->setstate(s_pause, 1);
}

/* Every modal visual set to s and the list emptied. */
static void close_modals(int32_t s)
{
    if (nmodal && modal[0]->vt->setstate) modal[0]->vt->setstate(modal[0], s);
    nmodal = 0;
}

int HUD_HandleInput(int x, int y, int event)
{
    MOUSE_state(&s_mouse_x, &s_mouse_y, &s_mouse_b);
    (void)x, (void)y;
    x = s_mouse_x, y = s_mouse_y;
    int c = event;   /* (0x435370: 2 the left button went down, 1 it came up; the buttons act on 1) */
    int a = 0;
    if (nmodal && modal[0]->vt->input) a = modal[0]->vt->input(modal[0], x, y, c);
    switch (a) {
    case 2:   /* resume */
        close_modals(2);
        break;
    case 3:   /* restart */
        if (g_race_mode == 4 || g_race_mode == 10 || g_race_mode == 2) return 0;
        close_modals(0);
        HUD_RaceStart();   /* 0x426a40 with 0 */
        s_restarted = true;
        s_pointer_ticks = 0;
        {
            uint32_t b;
            if (!MOUSE_state(&s_mousex, &s_mousey, &b)) s_mousex = s_mousey = 0;   /* (MOUSE_getstate 6) */
        }
        break;
    case 4:   /* exit to Windows: asked first (0x46fb) */
        modal[0]->vt->setstate(modal[0], 2);
        if (s_yesno && nmodal < 8) modal[nmodal++] = s_yesno;
        yesno_settext(s_yesno, 0x46fb, -1);
        s_quit_asked = true;
        a = 0;
        break;
    case 5:
        close_modals(0);
        g_race_quit = 1;   /* 0x657e34 */
        break;
    case 0x15:
        close_modals(0);
        s_noscreens = true;
        g_race_quit = 1;
        break;
    case 0x1c:   /* yes */
        if (s_quit_asked) {
            s_quit_asked = false;
            g_race_quit = 1;
            close_modals(0);
            a = 4;
        } else if (s_save_asked) {   /* the overwrite's: written, the replay on, the question closed */
            s_save_asked = false;
            a = 8;
            replaysave_overwrite();   /* 0x42e400(1) */
            HUD_ReplayPlaying();      /* 0x42df90 */
            modal[0]->vt->setstate(modal[0], 2);
        }
        break;
    case 0x10:   /* the replay saved: the replay on again */
    case 0x12:   /* the save cancelled */
        modal[0]->vt->setstate(modal[0], 2);
        a = 8;
        HUD_ReplayPlaying();   /* 0x42df90 */
        break;
    case 0x11:   /* the name's file there: the overwrite asked (0x46db, 0x46dc) */
        modal[0]->vt->setstate(modal[0], 2);
        if (s_yesno && nmodal < 8) modal[nmodal++] = s_yesno;
        yesno_settext(s_yesno, 0x46db, 0x46dc);
        s_save_asked = true;
        break;
    case 0x1d:   /* no: the exit's menu again */
        if (s_quit_asked) {
            s_quit_asked = false;
            modal[0]->vt->setstate(modal[0], 2);
            if (nmodal < 8) modal[nmodal++] = s_exit;
        } else if (s_save_asked) {   /* the save again (0x60ca9d left set) */
            modal[0]->vt->setstate(modal[0], 2);
            if (s_replaysave && nmodal < 8) modal[nmodal++] = s_replaysave;
        }
        break;
    case 0x18:   /* the exit's back: the pause again */
        s_exit->vt->setstate(s_exit, 2);
        if (nmodal < 8) modal[nmodal++] = s_pause;
        break;
    case 0x1a:   /* the pause's quit: the exit's menu */
        s_pause->vt->setstate(s_pause, 2);
        if (nmodal < 8) modal[nmodal++] = s_exit;
        break;
    default:
        break;
    }
    /* The race over (its replay): with no modal's action, the replay panel's. 7 restart (the results'
       screens not shown again, the next song), 0xe the race's screens again, 0xf the save's
       question (HReplaySave, pending). */
    if (g_race_view != 2 || a != 0 || !s_replay) return a;
    a = s_replay->vt->input(s_replay, x, y, c);
    if (a == 7) {
        HUD_ReplayRestart();
        return 7;
    }
    if (a == 0xe) {
        close_modals(0);
        if (nmodal == 0 && !g_race_quit && !s_file_replay && !g_race_derby) race_screens(true);   /* 0x426c50(1) */
        if (nmodal && modal[0]->vt->setstate) modal[0]->vt->setstate(modal[0], 1);
        return 0xe;
    }
    if (a == 0xf) {
        /* the save's billboard opened over the replay, the typed keys dropped (0x434fc0); the replay
           paused (8, 0x42dfb0) unless it is */
        close_modals(0);
        if (s_replaysave && nmodal < 8) modal[nmodal++] = s_replaysave;
        if (s_replaysave) s_replaysave->vt->setstate(s_replaysave, 1);
        chars_clear();
        HReplay *r = (HReplay *)s_replay;
        if (r->paused) return 0xf;   /* 0x42dfa0 */
        r->paused = 1;
        return 8;
    }
    return a;
}

void HUD_ReplayRestart(void)
{
    s_noscreens = true;
    Music_Play(NULL, 0);
}

void HUD_HideAll(void)
{
    for (int i = 0; i < nlist; i++)
        if (list[i].v->state != 0 && list[i].v->vt->setstate) list[i].v->vt->setstate(list[i].v, 0);
    nmodal = 0;
}


/* Both of 0x426a40's ends: the replay panel's play button back (0x42df90), the wrong way's blink over
   (0x436e90); the lap announcer's count and best (0x60ca90, 0x60ca94). */
static void raceover_common(void)
{
    if (s_replay) ((HReplay *)s_replay)->paused = 0;
    if (wrongway) {
        HWrongWay *w = (HWrongWay *)wrongway;
        w->shown = 1;
        w->acc = w->last = 0;
    }
}

void HUD_RaceStart(void)
{
    /* 0x426a40 with 0: the race-over's state cleared, each car's damage kept for the safe-driving
       bonus, the cones and the time's penalties none. (0x60ca98, the place the debug keys 0x427900
       force, is never set in the port.) */
    raceover_common();
    s_place = 0;
    s_raceover_done = s_restarted = false;
    for (int i = 0; i < g_racers.n && i < 8; i++) {
        s_start_damage[i] = g_cars[i] ? zones_damage(g_cars[i], 0x4e4) : 0;
        if (g_racers.c[i]) g_racers.c[i]->b[0xcc0] = 0;
    }
    s_cones_down = 0;   /* 0x60ca04 */
    if (s_time) ((HTime *)s_time)->penalties = 0;   /* 0x4317b0 */
    if (mission) g_mission_knocked = 0;             /* 0x4297f0 */
    s_laps_done = s_bestlap = 0;
}

void HUD_RaceOver(void)
{
    /* 0x426a40 with 1: the player's place (a network race's from the session, 0x48d720); the race-over's
       screens once, not after "race again" nor when skipped (the replay's restart, the pause's view
       replay: 0x60ca10, cleared instead). */
    raceover_common();
    Car *me = local_car();
    s_place = me ? *CAR_I(me, 0xcd4) : 0;
    if (!s_raceover_done && !s_restarted && !s_noscreens) {
        /* The records take the player's time first, when the player has a place (or in a mission). */
        if (s_place != 0 || g_race_mode == 9) {
            if (g_race_mode == 4) {
                tour_update();   /* 0x427620 */
                if (s_standings) s_standings->vt->reset(s_standings);   /* 0x431ae0 */
            }
            if (s_records) s_records->vt->reset(s_records);   /* 0x433980 */
            race_screens(false);   /* 0x426c50 */
        }
        s_raceover_done = true;
    } else s_noscreens = false;
    s_laps_done = s_bestlap = 0;
    s_restarted = false;
    /* (port: the list's first is shown at once; the original's starts hidden and is passed) */
    if (nmodal && modal[0]->vt->setstate) modal[0]->vt->setstate(modal[0], 1);
}

void HUD_Free(void)
{
    HudArt_free(s_pointer);
    s_pointer = NULL;
    nmodal = 0;
    s_pause = s_exit = NULL;
    s_replay = NULL;
    s_replaysave = NULL;
    summary = mission = s_time = s_multitime = NULL;
    s_congrats = NULL;
    s_yesno = NULL;
    s_records = NULL;
    s_standings = s_standing_congrats = s_info = NULL;
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
    RenderState_SetZFunc(g_render->device->rs, 7);
    Hud2D_View(r);         /* 0x4670c0 */
}

static void end_pass(RenderSystem *r)
{
    Hud2D_Flush();   /* 0x41a350 */
    View_End(r);
}

/* The widescreen option's bars (0x427ac0): black, the height's 0.15 (0x5b2638) plus 2 (0x5b2580) at the
   top and the bottom, at depth 65000. */
static void letterbox(void)
{
    float w = (float)g_display.width, h = (float)g_display.height;
    float b = (float)(int)((double)h * exe_f32(0x5b2638) + exe_f32(0x5b2580));   /* (x87: one rounding) */
    float top[16] = {0, 0, 65000, 1, w, 0, 65000, 1, w, b, 65000, 1, 0, b, 65000, 1};
    float bot[16] = {0, h - b, 65000, 1, w, h - b, 65000, 1, w, h, 65000, 1, 0, h, 65000, 1};
    flat(top, 0xff000000u);
    flat(bot, 0xff000000u);
}

/* The overlay (0x4257d0): the mouse's pointer, at the mouse for 128 ticks after it moves, all the time
   with a modal visual up or in the replay's view. (Then the cheat menu's two map flags, HCheat's +0x170
   and +0x171 onto the map's +0x108 and +0x109: the cheat menu isn't ported.) The depth function is
   left at 7, as the original leaves it. */
void HUD_DrawOverlay(RenderSystem *r, RCamera *c)
{
    if (!r || !c) return;
    pass(r, c);
    if (s_pointer_ticks != 0) {
        s_pointer_ticks += s_pointer_last - g_sim_tick;   /* (0x606ac4) */
        if (s_pointer_ticks < 0) s_pointer_ticks = 0;
    }
    int32_t x = s_mousex, y = s_mousey;
    uint32_t b;
    if (!MOUSE_state(&x, &y, &b)) x = s_mousex, y = s_mousey;   /* (MOUSE_getstate 6) */
    bool moved = false;
    if (s_mousex != x || s_mousey != y) {
        moved = true;
        s_pointer_ticks = 0x80;
        s_mousex = x, s_mousey = y;
    }
    if ((g_race_view == 2 || nmodal != 0 || moved || s_pointer_ticks != 0) && s_pointer) {
        HudArt_place(s_pointer, x, y);   /* 0x423b10 */
        HudArt_draw(s_pointer);
    }
    Hud2D_Flush();   /* 0x41a350 */
    View_End(r);
    s_pointer_last = g_sim_tick;
}

void HUD_Draw(RenderSystem *r, RCamera *c)
{
    if (hidden || !r || !c) return;
    /* The render state's depth function and fog, saved, then fog off; both back at the end. */
    RenderState *rs = r->device->rs;
    int zfunc = rs->state28, fog = rs->state15;
    RenderState_SetFogMode(rs, 0);
    pass(r, c);
    if (g_renderopts.widescreen) letterbox();   /* 0x427ac0 */
    end_pass(r);
    /* The front visuals, once the race has started. */
    pass(r, c);
    for (int i = 0; i < nlist; i++) {
        HudEntry *e = &list[i];
        if (!e->last && e->front && 0x1c0 < g_sim_tick && (always || e->v->state == 2) && (g_race_view == 0 || g_race_view == 1))
            e->v->vt->draw(e->v);
    }
    /* A mission's replay: its times shown (sliding in) and drawn whatever their trays. */
    if (g_race_view == 2 && g_race_mode == 9 && s_time && s_multitime) {
        if (s_time->state == 0 && s_time->vt->setstate) s_time->vt->setstate(s_time, 1);
        if (s_multitime->state == 0 && s_multitime->vt->setstate) s_multitime->vt->setstate(s_multitime, 1);
        s_time->vt->draw(s_time);
        s_multitime->vt->draw(s_multitime);
    }
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
    if (nmodal == 0) {
        /* No modal visual: once the race has started the hidden front visuals show, sliding in. 0x425de0 */
        if (0x1c0 < g_sim_tick && (g_race_view == 0 || g_race_view == 1))
            for (int i = 0; i < nlist; i++) {
                HudEntry *e = &list[i];
                if (e->front && (always || e->v->state == 2) && !(e->v->vt->hidden && e->v->vt->hidden(e->v)) && e->v->state == 0 && e->v->vt->setstate)
                    e->v->vt->setstate(e->v, 1);
            }
    } else {
        /* The first modal visual finished (hidden, or done): the next shows. The front visuals settled
           slide away meanwhile. */
        HudVisual *m = modal[0];
        if (m->state == 0 || (m->vt->done && m->vt->done(m))) {
            memmove(modal, modal + 1, sizeof modal[0] * (size_t)(nmodal - 1));
            nmodal--;
            if (nmodal && modal[0]->vt->setstate) modal[0]->vt->setstate(modal[0], 1);
        }
        for (int i = 0; i < nlist; i++) {
            HudEntry *e = &list[i];
            if (e->front && e->v->state == 3 && !(g_race_view == 2 && g_race_mode == 9) && e->v->vt->setstate) e->v->vt->setstate(e->v, 2);
        }
    }
    /* The replay's panel: with a modal visual up it stays (0x42d410); once none is, it lets go and its
       timer starts over (0x42d420). */
    if (g_race_view == 2 && s_replay) {
        HReplay *rp = (HReplay *)s_replay;
        if (nmodal == 0) {
            if (rp->keep) rp->hover[7] = 0, rp->keep = 0;   /* 0x42d430 */
        } else rp->keep = 1;
    }
    /* The standings shown: the standing's congratulation goes. */
    if (s_standings && s_standing_congrats && s_standings->state == 2 && s_standing_congrats->state != 0 && s_standing_congrats->vt->setstate)
        s_standing_congrats->vt->setstate(s_standing_congrats, 0);
    /* (and in a network race 0x427c30: its messages) */
    RenderState_SetZFunc(rs, zfunc);
    RenderState_SetFogMode(rs, fog);
    /* A mission with cones: when more traffic objects are down than last counted, one more cone (however
       many fell) for the summary and a penalty on the time, flashed. */
    if ((g_race_view == 0 || g_race_view == 1) && g_race_mode == 9 && (g_mission_cones != -1 || g_mission_penalty != -1)) {
        int n = 0;
        for (int i = 0; i < g_ntrafficcars; i++)   /* (0x65807c's count over 0x5e8c24) */
            if (g_traffic[i] && *CAR_I(g_traffic[i], 0x42c) != 0) n++;
        if (n != s_cones_down) {
            Audio_Cone();   /* 0x4aac20 */
            s_cones_down = n;
            g_mission_knocked++;   /* 0x4297e0 */
            if (s_time) {
                HTime *t = (HTime *)s_time;
                t->flash = 0x10;   /* 0x431790 */
                value_red(&t->combo);
                t->penalties++;    /* 0x4317a0 */
            }
        }
    }
}
