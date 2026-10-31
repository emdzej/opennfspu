/* The race HUD (see hud.h). */
#include "game/hud.h"
#include "exe.h"
#include "game/car.h"
#include "game/car_drive.h"
#include "eagl/eagl.h"
#include "pending.h"
#include "render/hud2d.h"
#include "render/hudart.h"
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

static const HudVisualVtbl go_vt = {go_destroy, go_draw};   /* 0x5b27ec */

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

/* ---- the HUD ---- */

void HUD_Init(void)
{
    HUD_Free();
    Hud2D_Init();   /* 0x419d70 */
    always = true;  /* (HUD_LoadStrings 0x41e190 sets 0x60c970) */
    PENDING_NOTE(0x424420, "HUD_Init's other visuals (gauges, gear, lap, position, time, map, messages)");
    add(countdown_new(), false, false);
}

void HUD_Free(void)
{
    for (int i = 0; i < nlist; i++)
        if (list[i].v) list[i].v->vt->destroy(list[i].v);
    free(list);
    list = NULL;
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
    PENDING_NOTE(0x425910, "HUD_Draw's modal visuals, replay and network parts");
}
