/* The render system (render.h): Render_Init and the render system object, the graphics device, the
   render-state object, display mode and quality, cameras and views, the race frame. */
#include "render/render.h"
#include "render/rdraw.h"
#include "render/drive_selftest.h"
#include "render/particles.h"
#include "game/race.h"
#include "game/race_car.h"
#include "game/track.h"
#include "vfs.h"
#include "real/ddraw.h"
#include "real/window.h"
#include "render/sky.h"
#include "game/camera.h"
#include "game/hud.h"
#include "exe.h"
#include "real/font.h"
#include "platform.h"
#include "pending.h"
#include "real/real.h"
#include "render/rworld.h"
#include "render/r3dcar.h"
#include "render/rlayers.h"
#include "render/rmaterial.h"
#include "render/rtex.h"
#include "render/rlists.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RenderSystem *g_render;          /* 0x628130 */
RenderState *g_racerstate;       /* 0x62814c */
RenderDisplay g_display;         /* 0x619780.. */

/* ---- the registry (0x4b7030, 0x4b7130; values table 0x5d6f28: 12 bytes per value) ---- */

enum { RREG_TABLE = 0x5d6f28, RREG_COUNT = 5 };

static void reg_param_name(const char *name, char *dst, int cap)
{
    int n = 0;
    for (; *name && n + 1 < cap; name++) dst[n++] = *name == ' ' ? '_' : (char)tolower((unsigned char)*name);
    dst[n] = 0;
}

/* The value's launch parameter, else its default: what RegQueryValueEx gives after the original wrote
   the default of a missing value. */
void RRegistry_GetString(int key, char *dst, int cap)
{
    dst[0] = 0;
    if (key < 0 || key >= RREG_COUNT || cap <= 0) return;
    uint32_t rec = RREG_TABLE + 12u * (uint32_t)key;
    char pname[64];
    reg_param_name(exe_str(exe_u32(rec)), pname, sizeof pname);
    if (plat_param(pname, dst, (size_t)cap)) return;
    if (exe_u32(rec + 4) == 1) snprintf(dst, (size_t)cap, "%s", exe_str(exe_u32(rec + 8)));
}

int RRegistry_GetInt(int key)
{
    if (key < 0 || key >= RREG_COUNT) return 0;
    uint32_t rec = RREG_TABLE + 12u * (uint32_t)key;
    char pname[64], v[32];
    reg_param_name(exe_str(exe_u32(rec)), pname, sizeof pname);
    if (plat_param(pname, v, sizeof v)) return atoi(v);
    return (int)exe_u32(rec + 8);
}

/* ---- the render-state object ---- */

RenderState *RenderState_ctor(RenderState *rs)
{
    rs->v00 = 0;
    rs->state28 = 3;
    rs->state04 = 1;
    rs->state39 = 1;
    rs->cull = 1;
    rs->state09 = rs->state15 = rs->state0e = rs->state16 = 0;
    rs->state17 = 0x3f7fff00u;          /* 0.99999 */
    rs->state0f = -1;
    rs->state3c = 0;
    rs->biasbase = 0;
    rs->state43 = 0x3f800000u;          /* 1.0 */
    RenderState_Send(rs);
    TEXTURE_sendstates();               /* 0x535870 */
    return rs;
}

void RenderState_Send(RenderState *rs)
{
    EAGL_RSSet(0x28, (uintptr_t)rs->state28);
    EAGL_RSSet(4, (uintptr_t)rs->state04);
    EAGL_RSSet(9, (uintptr_t)rs->state09);
    EAGL_RSSet(0x15, (uintptr_t)rs->state15);
    EAGL_RSSetAlias(0xe, (uintptr_t)rs->state0e);
    EAGL_RSSetAlias(0x16, (uintptr_t)rs->state16);
    EAGL_RSSetAlias(0x17, rs->state17);
    EAGL_RSSet(0xf, (uintptr_t)rs->state0f);
    EAGL_RSSet(0x3c, (uintptr_t)rs->state3c);
    EAGL_RSSet(0x39, (uintptr_t)rs->state39);
    EAGL_RSSetAlias(0x43, rs->state43);
    EAGL_RSSetDevice(rs->cull, rs->biasbase);
}

void RenderState_SetZFunc(RenderState *rs, int zfunc)
{
    rs->state28 = zfunc;
    EAGL_RSSet(0x28, (uintptr_t)zfunc);
}

void RenderState_SetZWrite(RenderState *rs, int on)
{
    rs->state39 = on;
    EAGL_RSSet(0x39, (uintptr_t)on);
}

/* The new cull is sent at once only while the material states' last cull value (0x5ded4c, the cull pair
   of EAGL's table) is 1, i.e. culling is on; otherwise the next layer with culling on reads it (EAGL_RSSetCull,
   through EAGL_RSSetDevice, the port's copy of the object's cull). */
void RenderState_SetCull(RenderState *rs, int cull)
{
    if (rs->cull != cull) {
        rs->cull = cull;
        EAGL_RSSetDevice(rs->cull, rs->biasbase);
        if (EAGL_RSValue(3) == 1) EAGL_RSSet(2, (uintptr_t)cull);
    }
}

/* The fog's states (THRASH 0x15 the mode, 0x0e the density, 0x0f the colour). 0x537670, 0x537690, 0x5376f0 */
void RenderState_SetFogMode(RenderState *rs, int mode)
{
    rs->state15 = mode;
    EAGL_RSSet(0x15, (uintptr_t)mode);
}

void RenderState_SetFogDensity(RenderState *rs, float density)
{
    uint32_t u;
    memcpy(&u, &density, 4);
    rs->state0e = (int32_t)u;
    EAGL_RSSetAlias(0xe, u);
}

void RenderState_SetFogColour(RenderState *rs, uint32_t argb)
{
    rs->state0f = (int32_t)argb;
    EAGL_RSSet(0xf, argb);
}

/* ---- the graphics device ---- */

static const char *driver_path;     /* 0x69ecf8 */
static int driver_display;          /* 0x69ed08 */

static void dev_destroy(RDevice *d, int free_it)
{
    if (d->rs) free(d->rs);
    d->rs = NULL;
    if (free_it) free(d);
}

static bool dev_open(RDevice *d)
{
    d->mode = 1;
    if (!GLIDE_env(false, driver_path, driver_display, 1)) return false;
    TexGraphics_setdevice(d->graphics, d);       /* 0x5401a0 */
    d->about = THRASH.fn.about();
    d->caps = d->about ? ((const uint32_t *)(const void *)d->about)[0x40 / 4] : 0;
    RenderState *rs = malloc(sizeof *rs);
    d->rs = rs ? RenderState_ctor(rs) : NULL;
    FONT_init();   /* 0x5365e0 */
    EAGL_ContextCreate();
    EAGL_ContextInit(g_eagl, d->mode);
    EAGL_RSReset();
    EAGL_ContextResetStages(g_eagl);
    return true;
}

/* Slot 7 (0x5388a0): the driver's video mode set (two buffers, 32 bits asked: the mode's depth then), the
   EAGL context for it, the texture cache's states and the render states sent again, the display window
   locked once more for its size (0x5341f0). */
static bool dev_setmode(RDevice *d, int mode)
{
    if (!THRASH.fn.setvideomode(mode, 2, 2)) return false;
    d->mode = mode;
    EAGL_ContextInit(g_eagl, mode);
    TEXTURE_sendstates();   /* 0x535870 */
    EAGL_RSReset();         /* (the 0x5ded60 functions on the 0x5ded30 pairs) */
    if (d->rs) RenderState_Send(d->rs);   /* (the record's states, 0x5375a0 .. 0x537710) */
    d->about = THRASH.fn.about();
    WINDOW_displayinit();
    return true;
}

static void dev_begin(RDevice *d) { THRASH.fn.window(2); }
static void dev_present(RDevice *d) { THRASH.fn.pageflip(); }

/* The clear: the render state's background (+0, setstate 3) over the whole display. */
static void dev_clear(RDevice *d)
{
    THRASH.fn.setstate(3, (uintptr_t)(d->rs ? (uint32_t)d->rs->v00 : 0));
    THRASH.fn.clip(0, 0, g_display.width, g_display.height);
    THRASH.fn.clearwindow();
}

static const RDeviceVtbl device_vt = { dev_destroy, dev_open, dev_begin, dev_present, dev_clear };   /* 0x5bc41c */

/* GLIDE_env 0x556910: the Glide environment variables (SST_VIDEO_24BPP, FX_GLIDE_NO_SPLASH, ...) don't
   exist here; MOUSE_init, KEY_init and the timer task (0x55fc00) belong to other modules.
   The display's window is locked once for its size and depth (0x5341f0 on 0x5de650). */
bool GLIDE_env(bool windowed, const char *driver, int display, int mode)
{
    if (!THRASH_opendll(driver)) return false;           /* THRASH_drivername 0x5753f0 */
    if (!THRASH.fn.init()) return false;                 /* THRASH_openwindow */
    if (display) THRASH.fn.selectdisplay(display);
    THRASH.fn.setvideomode(mode, 2, 1);
    DDRAW_init();
    WINDOW_displayinit();
    return true;
}

/* ---- display mode and quality ---- */

static int card_type;               /* 0x6197b0: Render_DetectCard's card */
uint8_t g_render_modalpha;          /* 0x6197aa */
RenderCaps g_rcaps;

int Render_CardType(void) { return card_type; }

/* Whether the driver takes a state's test value (setstate's answer), the state put back. */
static bool takes(uint32_t state, uintptr_t value)
{
    intptr_t was = THRASH.fn.getstate(state);
    bool ok = THRASH.fn.setstate(state, value) != 0;
    THRASH.fn.setstate(state, (uintptr_t)was);
    return ok;
}

void Render_DetectCard(void)
{
    const ThrashAbout *a = THRASH.fn.about();
    g_rcaps.b799 = 0;
    g_rcaps.colourkey = takes(0x24, 0x80);
    g_rcaps.b79d = 1;
    g_rcaps.state2f = takes(0x2f, 1);
    g_rcaps.state6 = takes(6, 2);
    g_rcaps.noglobalclut = ~(a->flags >> 6) & 1;
    g_rcaps.state15 = takes(0x15, 1);
    g_rcaps.state7 = takes(7, 1);
    g_rcaps.b7a3 = g_rcaps.b7a4 = 0;
    g_rcaps.mipmap = takes(0xb, 1);
    g_rcaps.trilinear = takes(0xb, 3);
    g_rcaps.state38 = takes(0x38, 1);
    g_rcaps.texmax = a->texheightmax <= a->texwidthmax ? a->texheightmax : a->texwidthmax;
    g_rcaps.stages = a->numstages;
    RTex_mipsallowed = g_renderopts.nomip == 0;   /* 0x6197ae */
    g_rcaps.trident = 0;
    g_rcaps.fogtable = takes(4, 2);
    /* the blend states 6 and 8 tried and put back (their answers unused) */
    takes(0x29, 6);
    takes(0x29, 8);
    RTex_flagsallowed = false;                    /* 0x6197a8 */
    g_rcaps.vramtotal = (uint32_t)a->textureramsize;
    if (strstr(a->device, exe_str(0x5ce924))) {   /* "Trident Blade" */
        g_rcaps.trident = 1;
        THRASH.fn.setstate(0x3c, 1);
    }
    if (a->signature == 0x33444632u || a->signature == 0x33444658u) {   /* '2FD3', 'XFD3': Glide */
        g_rcaps.vram = (uint32_t)a->textureramsize;
        g_rcaps.glide = 1;
        g_render_modalpha = 1;
        g_rcaps.b7a9 = 1;
        card_type = 0;
        return;
    }
    if (a->subtype == 1) RTex_mipsallowed = false;
    if (a->subtype == 9 || a->subtype == 10) {
        g_rcaps.vram = (uint32_t)a->textureramsize;
        g_rcaps.glide = 1;
    } else {
        /* ("Voodoo" and subtype 0xf clear the flags allowed again) */
        g_rcaps.glide = 0;
        g_rcaps.vram = (uint32_t)(a->textureramsize + (g_display.bpp >> 3) * g_display.height * g_display.width * -3);
    }
    g_rcaps.b7a9 = 0;
    card_type = a->subtype;
    g_render_modalpha = THRASH.fn.setstate(THRASH_STATE_MODULATEALPHA, 0) != 0;
    if (card_type == 3 || card_type == 0xe || card_type == 2) g_render_modalpha = 0;
}

void Render_DisplayInit(void)
{
    int w = 640, h = 480, bpp = 16;
    if (!g_render->windowed) {
        /* The device's mode list (+0x24) and its first entry (about block +0x28: width, height, depth). */
        /* The device's current mode (slot 9): the driver's entry of the mode set (1 = 640x480x16). */
        const ThrashAbout *a = THRASH.fn.about();
        if (a && a->modes && a->nmodes > 1) { w = a->modes[1].width; h = a->modes[1].height; bpp = a->modes[1].bpp; }
    }
    g_display.stamp = 1;
    g_display.bpp = bpp == 15 ? 16 : bpp;
    g_display.width = w;
    g_display.height = h;
    Render_DetectCard();   /* 0x44e760 */
    Render_SetupQuality();
    EAGL_SetDisplaySize((float)w, (float)h);             /* 0x537370: the 2D window's size */
}

/* 0x572740: how far a driver mode is from the one asked for, 10000 when it isn't one: the size exactly,
   the mode usable (+0x10) with the buffers asked (+0x18 for a flipped chain, else +0x14); then the
   depths' difference, doubled only when the mode's is below the one asked (the original's), a
   palettised mode never for a true-colour ask, a true-colour mode four times as far for a palettised one. */
static int mode_distance(const ThrashMode *m, int w, int h, int bpp, int buffers, int flip)
{
    int d = 10000;
    int have = flip ? m->unknown6 : m->unknown5;
    if (m->width == w && m->height == h && m->unknown4 != 0 && buffers <= have) {
        d = m->bpp - bpp;
        if (d < 0) d *= -2;
        if (m->bpp < 9) return 8 < bpp ? 10000 : d;
        if (bpp < 9) d <<= 2;
    }
    return d;
}

/* The nearest of the driver's modes (0x537430 -> 0x5727b0), the first exact one; 0 for none. */
static int find_mode(int w, int h, int bpp, int buffers, int flip)
{
    const ThrashAbout *a = THRASH.fn.about();
    int best = 10000, at = 0;
    for (int i = 1; a && i <= a->nmodes; i++) {
        int d = mode_distance(&a->modes[i], w, h, bpp, buffers, flip);
        if (d < best) {
            best = d;
            at = i;
            if (d == 0) return i;
        }
    }
    return at;
}

/* The video mode (0x467f70) when it isn't the one set: the device's (slot 7), the display's globals
   from the mode's entry with the card's detection again (0x44eac0), the quality (0x44ebe0) and the 2D
   window's size (0x537370); the mouse's cursor hidden (ShowCursor) has no counterpart here. */
void Render_SetMode(RenderSystem *r, int mode)
{
    r->mode = mode;
    if (mode == g_display.stamp) return;
    int w = 640, h = 480, bpp = 16;
    if (!r->windowed) {
        const ThrashAbout *a = THRASH.fn.about();
        /* (no mode found: the original sets the list's empty entry 0; the port keeps the one open) */
        if (!a || mode < 1 || mode > a->nmodes) return;
        w = a->modes[mode].width, h = a->modes[mode].height, bpp = a->modes[mode].bpp;   /* 0x44e5f0 */
        if (!dev_setmode(r->device, mode)) return;
    }
    g_display.stamp = mode;
    g_display.bpp = bpp == 15 ? 16 : bpp;
    g_display.width = w;
    g_display.height = h;
    Render_DetectCard();
    Render_SetupQuality();
    EAGL_SetDisplaySize((float)w, (float)h);
}

static const ThrashAbout *about(void) { return THRASH.fn.about ? THRASH.fn.about() : NULL; }   /* (no driver: none) */

/* The driver's display modes (0x44e5d0: the device's slot 9, the about block's nmodes; 1 windowed). */
int Render_ModeCount(void)
{
    if (g_render && g_render->windowed) return 1;
    const ThrashAbout *a = about();
    return a ? a->nmodes : 0;
}

/* A mode's size and depth (0x44e5f0; 640 x 480 x 16 windowed). */
void Render_ModeInfo(int i, int *w, int *h, int *bpp)
{
    const ThrashAbout *a = about();
    if ((g_render && g_render->windowed) || !a || i < 0 || i > a->nmodes) {
        *w = 640, *h = 480, *bpp = 16;
        return;
    }
    *w = a->modes[i].width, *h = a->modes[i].height, *bpp = a->modes[i].bpp;
}

/* Whether the options may offer a mode (0x44e650): a flipped chain of more than one buffer, 640 x 480 at
   least; on the cards of type 9 and 10 (Voodoo2 and 3) under 2 or 4 million pixel bytes (w x h x 6);
   off Glide, the texture memory left after three pages of it above 1.5 MB (2 MB above 640 wide, 4 MB
   deeper than 16 bits). Windowed only mode 0. */
bool Render_ModeUsable(int i)
{
    if (g_render && g_render->windowed) return i == 0;
    const ThrashAbout *a = about();
    if (!a || i < 0 || i > a->nmodes) return false;
    const ThrashMode *m = &a->modes[i];
    bool ok = 1 < m->unknown6 && 0x27f < m->width && 0x1df < m->height;
    if (card_type == 9) ok = ok && m->width * m->height * 6 < 2000000;
    else if (card_type == 10) ok = ok && m->width * m->height * 6 < 4000000;
    if (ok && !g_rcaps.glide) {
        int bytes = (m->bpp == 15 ? 16 : m->bpp) / 8;
        int left = (int)g_rcaps.vramtotal - bytes * m->width * m->height * 3;
        int need = 0x10 < m->bpp ? 4000000 : 0x280 < m->width ? 2000000 : 1500000;
        ok = need < left;
    }
    return ok;
}

/* A display size and depth (Render_SetVideoMode 0x467f00) when it isn't the one open (width 0: the
   open size at that depth): the nearest mode with the buffers in a flipped chain, mode 0 windowed. The
   front end and the intro movies ask for 640 x 480 x 16, the race for fe.txt's. */
void Render_SetVideoMode(RenderSystem *r, int w, int h, int bpp, int buffers)
{
    if (!r || (w == g_display.width && h == g_display.height && bpp == g_display.bpp)) return;
    if (w == 0) w = g_display.width, h = g_display.height;
    if (r->windowed) {
        Render_SetMode(r, 0);
        return;
    }
    Render_SetMode(r, find_mode(w, h, bpp, buffers, 1));
}

/* 0x44ebe0. The globals other than the method key stay in this module until their readers are ported
   (shadows 0x6197e5, specials 0x6197ec); the car detail 0x6197c4 is render/r3dcar.h's g_r3d_quality. */
int32_t g_render_specials;          /* 0x6197f4 */
int32_t g_render_precip;            /* 0x6197f0: the weather's most drops */
uint8_t g_render_skiplod0;          /* 0x6197e5: every quality level clears it in this build */
uint8_t g_render_extras;            /* 0x6197e6: the 0x40000 'pr' lists and the effects (card type isn't 0xe) */
uint8_t g_render_skidmarks;         /* 0x6197e7: the skid marks (game/skid.h) */
uint32_t g_render_skidmask = 0xf;   /* 0x6197ec: a pair replaces the last on the steps it masks off */
uint8_t g_render_carlights, g_render_carenv;   /* 0x6197ca, 0x6197cb: the cars' dynamic lights and reflections */

void Render_SetupQuality(void)
{
    /* Gamma: 1% steps (0x5b3ca0), at least 0.25 (0x5b2468), through the setstate alias (0x2e). */
    float g = (float)g_renderopts.gammacorrection * 0.01f;
    if (g <= 0.25f) g = 0.25f;
    THRASH.setstate_alias(0x2e, THRASH_f2u(g));
    g_rmethod_key = 0;
    if (g_renderopts.headlights == 1) g_rmethod_key = 8;
    TEXTURE_settrilinear(false);
    g_render_extras = card_type != 0xe;
    g_render_skiplod0 = 0;
    switch (g_renderopts.specialfx) {
    case 0: g_render_specials = 0; g_render_precip = 60; g_render_extras = 0; g_render_particles = 0; g_render_skidmarks = 0; g_render_skidmask = 0xf; break;
    case 1: g_render_specials = 1; g_render_precip = 140; g_render_extras = 0; g_render_particles = 100; g_render_skidmarks = 0; g_render_skidmask = 0xf; break;
    case 2: g_render_specials = 2; g_render_precip = 220; g_render_particles = 200; g_render_skidmarks = 1; g_render_skidmask = 7; break;
    default:
        g_render_skidmarks = 1;
        g_render_skidmask = 7;
        g_render_specials = 3;
        g_render_precip = 300;
        g_render_particles = 0x100;
        if (g_rcaps.trilinear) TEXTURE_settrilinear(true);   /* 0x5358b0 */
        break;
    }
    if (g_renderopts.carchrome == 0) g_rmethod_key |= 1;
    else g_rmethod_key |= 3;
    /* the chrome option: 0 none, 1 (0x6197c8/c9 only), 2 the dynamic lights and the reflections too */
    g_render_carlights = g_render_carenv = g_renderopts.carchrome >= 2;
    g_r3d_quality = g_renderopts.cardetail == 0 ? 0 : g_renderopts.cardetail == 1 ? 1 : 2;   /* 0x6197c4 */
    if (RTex_flagsallowed && (g_rmethod_key & 2)) g_rmethod_key = (g_rmethod_key & ~3u) | 4;
}

/* ---- the render system ---- */

RenderSystem *Render_Construct(RenderSystem *r, const char *driver, int device, int windowed)
{
    memset(r, 0, sizeof *r);
    r->znear = 0.15f;                   /* 0x3e19999a */
    r->zfar = 700.0f;                   /* 0x442f0000 */
    RLists_Init();                       /* 0x466240: the draw lists (+0x14) */
    r->cpu = 0;                          /* CPU_detect: the port measures nothing */
    r->windowed = (uint8_t)windowed;
    r->mode = 1;
    g_render = r;
    /* The display depth check (fewer than 8 bits: "Invalid screen depth") has no meaning here. */
    driver_path = driver;
    driver_display = device;
    TexGraphics *g = calloc(1, sizeof *g);
    TexGraphics_register(g);             /* 0x5400f0 */
    r->graphics = g;
    RDevice *d = calloc(1, sizeof *d);
    d->vt = &device_vt;
    d->graphics = g;
    r->device = d;
    if (!d->vt->open(d)) {
        REAL_abort("render", 0, "3D graphics hardware could not be initialized");
        return r;
    }
    Render_DisplayInit();
    /* fe.txt's display (WIDTH 0x657a48, HEIGHT, DEPTH) when it isn't the one open: the nearest mode with
       two buffers in a flipped chain (0x537430; mode 0 when windowed) */
    Render_SetVideoMode(r, g_renderopts.width, g_renderopts.height, g_renderopts.depth, 2);
    r->textures = g->textures;
    /* MEM_info 0x53c140 (GlobalMemoryStatus's physical memory) goes to the quality level's choice: the
       port's machine is RTex_machine (render/rtex.h). */
    /* The 2D view: orthographic over the display, y down (scale 2/w, -2/h after a translation by -1). */
    EaglView *v = calloc(1, sizeof *v);
    EAGL_ViewInit(v, d);
    r->view2d = v;
    EAGL_ViewSetViewport(v, (float)g_display.width, (float)g_display.height, 0, 0, 0, 1);
    EAGL_ViewSetOrtho(v, 1.0f, -1.0f, 1.0f);
    EaglMatrix m;
    EAGL_MatTranslation(&m, -1.0f, 1.0f, 0.0f);
    EaglMatrix s;
    EAGL_MatScale(&s, 2.0f / (float)g_display.width, -2.0f / (float)g_display.height, 1.0f, 1.0f);
    EAGL_MatPreMul(&m, &s);
    *EAGL_StackTopForWrite(&v->view) = m;
    /* The UV test (0x467ba0..0x467e60: an 8x8 texture drawn and read back to find the texel centre the
       card samples at, its two results kept by 0x44e740 at 0x6197ab / 0x6197ac) is the driver's business
       here: the GL driver samples at D3D's centres, the test's no-offset outcome. */
    THRASH.fn.setstate(7, 1);
    return r;
}

void Render_Init(void)
{
    char driver[112], res[80], path[112];
    snprintf(driver, sizeof driver, "%s", g_renderopts.driver);
    int device = g_renderopts.renderdevice;
    if (!strcmp(driver, "registry")) {           /* 0x5cf7bc */
        RRegistry_GetString(RREG_DRIVER, driver, sizeof driver);
        if (!driver[0]) {
            REAL_abort("render", 0, "Run 3D Setup before running the game.");
            return;
        }
        if (!strcmp(driver, "dx")) {             /* "dx" + the D3D device's version "7" */
            device = RRegistry_GetInt(RREG_DEVICE);
            strncat(driver, "7", sizeof driver - strlen(driver) - 1);
        } else
            device = 0;
        strncat(driver, "z", sizeof driver - strlen(driver) - 1);
        RRegistry_GetString(RREG_RESOLUTION, res, sizeof res);
    } else
        snprintf(res, sizeof res, "%dx%d", g_renderopts.width, g_renderopts.height);
    /* The drivers' folder (*0x65b304) is a path prefix on Windows; the port's drivers are registered
       by name (THRASH_registerdll). */
    snprintf(path, sizeof path, "%s", driver);
    (void)res;
    if (!g_render) {
        RenderSystem *r = malloc(sizeof *r);
        g_render = r ? Render_Construct(r, strdup(path), device, 0) : NULL;
    }
    /* FE_SetRenderer 0x4b6ec0: an empty call (0x516210), the debug frame-rate text's scale (0x5e517c 0.5,
       0x5e5180 1), its counters cleared (0x569b00) and its place at the display's bottom less 128
       (0x6afbbc, 0x6afbc0), the flag 0x65b398 set: none of them is read in this build. Then 0x465cb0: the
       line object at 0x62810c (the 0x59ee20 material: a coordinates layer and the next layer), which
       render/hud2d.c keeps as its material. */
    RLayers_Register();                          /* 0x449a60 */
}

void Render_Shutdown(void)
{
    if (!g_render) return;
    THRASH.fn.restore();
    if (g_render->device) g_render->device->vt->destroy(g_render->device, 1);
    if (g_render->graphics) TexGraphics_unregister(g_render->graphics);
    free(g_render->graphics);
    free(g_render->view2d);
    free(g_render);
    g_render = NULL;
}

/* The port checks for the render system: the headless tests draw cars without one (the original always
   has it). */
void Render_SetCull(RenderSystem *r)
{
    if (!r || !r->device || !r->device->rs) return;
    RenderState *rs = r->device->rs;
    RenderState_SetCull(g_render->device->rs, 3 - rs->cull);
}

void Render_BeginFrame(RenderSystem *r)
{
    r->device->vt->begin(r->device);
    if (g_renderopts.drawsync) THRASH.fn.sync(0);
}

void Render_EndFrame(RenderSystem *r) { r->device->vt->present(r->device); }

/* ---- cameras ---- */

void RFrame_Init(RFrame *f)
{
    static const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};   /* 0x5bb2d0 */
    memcpy(f->orient, identity, sizeof identity);
    f->eye[0] = f->eye[1] = f->eye[2] = 0;
    memcpy(f->rot, identity, sizeof identity);
    f->trans[0] = f->trans[1] = f->trans[2] = 0;
}

/* The back axis turned to the front, the orientation transposed into the rotation, and the translation
   the eye turned by it, negated (the real library's vector transform 0x5327b0: row vectors, single
   precision). The original's transformer adds its current translation vector too (0x6a3b34); it is not
   set by this path and taken as zero here. */
void RFrame_ToView(RFrame *f)
{
    f->orient[6] = -f->orient[6];
    f->orient[7] = -f->orient[7];
    f->orient[8] = -f->orient[8];
    const float *o = f->orient;
    float *m = f->rot;
    m[0] = o[0]; m[4] = o[4]; m[8] = o[8];
    m[3] = o[1]; m[1] = o[3];
    m[6] = o[2]; m[2] = o[6];
    m[7] = o[5]; m[5] = o[7];
    float e[3] = {-f->eye[0], -f->eye[1], -f->eye[2]};
    for (int j = 0; j < 3; j++) f->trans[j] = e[0] * m[j] + e[1] * m[3 + j] + e[2] * m[6 + j];
}

RCamera *Render_CreateCamera(RenderSystem *r, int type, float x0, float y0, float x1, float y1,
                             uint32_t clear, float znear, float zfar)
{
    EaglView *v = calloc(1, sizeof *v);
    if (v) EAGL_ViewInit(v, r->device);
    if (v) v->background = clear;
    RCamera *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->view = v;
    c->stamp = g_display.stamp;
    c->x0 = x0;
    c->v008 = 0;
    c->y0 = y0;
    c->type = type;
    c->znear = znear;
    c->x1 = x1;
    c->y1 = y1;
    c->clear = clear;
    c->zfar = zfar;
    RFrame_Init(&c->frame);
    c->halffov = 45.0f;
    c->active = 0;
    c->update[0] = type;
    c->update[1] = c->v008;
    EAGL_ViewSetViewport(v, (c->x1 - c->x0) * (float)g_display.width, (c->y1 - c->y0) * (float)g_display.height,
                         (float)g_display.width * c->x0, (float)g_display.height * c->y0, 1.0f, 65536.0f);
    return c;
}

void RCamera_Free(RCamera *c)
{
    if (!c) return;
    free(c->view);
    free(c);
}

void RCamera_Setup(RCamera *c)
{
    EaglView *v = c->view;
    if (c->stamp != g_display.stamp) {
        c->stamp = g_display.stamp;
        EAGL_ViewSetViewport(v, (c->x1 - c->x0) * (float)g_display.width, (c->y1 - c->y0) * (float)g_display.height,
                             (float)g_display.width * c->x0, (float)g_display.height * c->y0, 1.0f, 65536.0f);
    }
    RFrame f = c->frame;
    RFrame_ToView(&f);
    EAGL_MatFromRotTrans(EAGL_StackTopForWrite(&v->view), f.rot, f.trans);
    /* Aspect from the viewport's share of a 640x480 screen (0x5b2a8c, 0x5b2a88). */
    EAGL_ViewSetPerspective(v, c->halffov + c->halffov,
                            ((c->x1 - c->x0) * 640.0f) / ((c->y1 - c->y0) * 480.0f), c->znear, c->zfar);
    if (c->active) EAGL_ContextViewport(g_eagl);
}

void RCamera_Setup2D(RCamera *c)
{
    EaglView *v = c->view;
    float w = (float)g_display.width, h = (float)g_display.height;
    EAGL_ViewSetOrtho(v, 1.0f, 0, 10.0f);
    EAGL_MatTranslation(EAGL_StackTopForWrite(&v->view), -1.0f, 1.0f, -0.1f);
    EaglMatrix sc;
    EAGL_MatScale(&sc, 2.0f / w, -2.0f / h, -1.0f, 1.0f);   /* 0x5b2580, 0x5b2628 */
    EAGL_MatPreMul(EAGL_StackTopForWrite(&v->view), &sc);
    if (c->active) EAGL_ContextViewport(g_eagl);
}

void View_Begin(RenderSystem *r, RCamera *c)
{
    RLayers_ResetCache();               /* 0x44a090 */
    r->camera = c;
    RCamera_Setup(c);
    c->active = 1;
    g_render->depthfactor = (c->zfar / (c->zfar - c->znear)) * c->znear;
    /* The coordinate layers read the eye through 0x628130 -> +0x10 -> +0x10c (rlayers.h). */
    memcpy(g_rlayer_eye, c->frame.eye, sizeof g_rlayer_eye);
    EAGL_ViewBegin(c->view);
    if (c->clear) EAGL_ViewClear(c->view);
    EAGL_ContextViewport(g_eagl);
}

void View_End(RenderSystem *r)
{
    RCamera *c = r->camera;
    EAGL_ViewEnd(c ? c->view : NULL);
    if (c) c->active = 0;
    r->camera = NULL;
}

/* ---- the race frame ---- */

RaceViews *RaceViews_ctor(RaceViews *rv)
{
    rv->v00 = 0;
    rv->main = Render_CreateCamera(g_render, 0, 0, 0, 1, 1, 0xff77bbffu, g_render->znear, g_render->zfar);
    if (g_renderopts.widescreen) { rv->main->y0 = 0.15f; rv->main->y1 = 0.85f; }   /* 0x466df0 */
    rv->overlay = Render_CreateCamera(g_render, 3, 0, 0, 1, 1, 0, g_render->znear, g_render->zfar);
    rv->mirror = Render_CreateCamera(g_render, 4, 0.5f, 0.5f, 1, 1, 0xff000000u, g_render->znear, g_render->zfar);
    /* (0x628140 keeps rv for 0x464e40 and the listener 0x465360, which read its main camera; the port
       hands the views to them) */
    return rv;
}

void Render_MainViews(RaceViews *rv)
{
    RCamera *c = rv->main;
    g_render->v64 = 0;
    c->flags[0] = 1;
    c->flags[1] = 4;
    c->flags[2] = c->flags[3] = c->flags[4] = 1;
    c->v12c = 2;
    c->range = 400.0f;
    c->viewdist = g_render_viewdist;                     /* 0x6197cc */
    c->viewdist2 = c->viewdist * c->viewdist;
    Camera_ToView(c->update[1], c, c->update[0]);        /* Camera_Track 0x461060 -> 0x464e40 */
    RenderState_SetCull(g_render->device->rs, g_r3d_mirror ? 1 : 2);
    c->clear = Sky_ClearColour(false);                   /* 0x459940, 0x466da0 */
    if (c->view) c->view->background = c->clear;
    View_Begin(g_render, c);
    Render_World(c);                                     /* 0x468a20 */
    View_End(g_render);
    RDraw_ClearVisited();                                /* 0x44fd70 */
}

/* ---- the rear-view mirror (0x628144) ---- */

static RCamera *s_rear;   /* 0x628144 */

static float f32(uint32_t va) { union { uint32_t u; float f; } c = {exe_u32(va)}; return c.f; }

void Render_SetRearView(float x0, float y0, float x1, float y1, float znear, float zfar)   /* 0x469110 */
{
    if (s_rear) RCamera_Free(s_rear);
    s_rear = g_render ? Render_CreateCamera(g_render, 1, x0, y0, x1, y1, 0xff77bbffu, znear, zfar) : NULL;   /* 0x467230 */
}

/* 0x468be0: with the HUD's mirror shown (and not in a replay's TV views), the world behind the car into
   its rectangle: a third of the view distance (50 at least), the race camera's mirror flag set meanwhile
   (the sky's mirrored colours; the viewed car left out) and the cull the other way round. */
void Render_RearView(void)
{
    if (g_race_view == 2 || g_race_view == 3 || !HUD_RearViewShown() || !s_rear) return;   /* 0x427560 */
    RCamera *c = s_rear;
    g_render->v64 = 1;
    c->flags[0] = 0;
    c->flags[1] = 3;
    c->flags[2] = c->flags[3] = c->flags[4] = 1;
    c->v12c = 1;
    c->range = 200.0f;
    float d = g_render_viewdist * f32(0x5b2680);
    c->viewdist = f32(0x5b251c) <= d ? d : 50.0f;
    c->viewdist2 = c->viewdist * c->viewdist;
    Camera_ToView(c->update[1], c, c->update[0]);        /* 0x461060 */
    g_racecam[0].flags2 |= 0x10;
    RenderState_SetCull(g_render->device->rs, g_r3d_mirror ? 2 : 1);   /* 0x537600 */
    c->clear = Sky_ClearColour(true);                    /* 0x459940, 0x466da0 */
    if (c->view) c->view->background = c->clear;
    View_Begin(g_render, c);
    Render_World(c);                                     /* 0x468a20 */
    View_End(g_render);
    RDraw_ClearVisited();                                /* 0x44fd70 */
    g_racecam[0].flags2 &= (uint8_t)~0x10;
}

/* ---- the view distance along the track (0x61bc38: Track_LoadKeyframes 0x44fa10, 0x44fb80) ----

   <track>_forward.key (_backwards.key for the reversed track): a version (1), a count, then pairs of a
   road node and the view distance from it on. Without the file (or an old version, "Old version of
   keyframes detected") 400 pairs of (-1, 400) of which the first two count: (0, 600), (100000, 600). */
static int32_t *s_keys;   /* the pairs from [2] */
static int s_keyat;       /* the pair last found */
float g_render_viewdist = 400.0f;   /* 0x6197cc */

static void keys_seek(int node)   /* 0x44fb00 */
{
    const int32_t *k = s_keys;
    int n = k[1];
    if (!g_race_reverse) {
        s_keyat = 0;
        if (n != 2 && -1 < n - 2) {
            int i;
            do {
                i = s_keyat;
                if (k[2 + 2 * i] <= node && node < k[2 + 2 * (i + 1)]) return;
                s_keyat = i + 1;
            } while (i + 1 < n - 2);
        }
    } else {
        s_keyat = n - 1;
        if (1 < s_keyat)
            while (node <= k[2 + 2 * (s_keyat - 1)] || k[2 + 2 * s_keyat] < node)
                if (--s_keyat < 2) return;
    }
}

/* The distance for a road node: the pair found last if the node is still in its span (forward: after
   its node up to the next's; backwards: from the one before up to its own), else sought again. 0x44fb80 */
static float keys_find(int node)
{
    const int32_t *k = s_keys;
    int i = s_keyat;
    bool in = !g_race_reverse ? k[2 + 2 * i] < node && node <= k[2 + 2 * (i + 1)]
                              : k[2 + 2 * (i - 1)] <= node && node < k[2 + 2 * i];
    if (!in) keys_seek(node);
    float d;
    memcpy(&d, &k[2 + 2 * s_keyat + 1], 4);
    return d;
}

void Render_LoadViewKeys(const char *track)   /* 0x44f9b0, Track_LoadKeyframes 0x44fa10 */
{
    Render_FreeViewKeys();
    char path[256];
    snprintf(path, sizeof path, exe_str(g_race_reverse ? 0x5ceacc : 0x5ceab8), g_track_dir, track);
    size_t size = 0;
    uint8_t *d = vfs_read_all(path, &size);
    if (d && (size < 8 || d[0] != 1 || d[1] || d[2] || d[3] || size < 8 + 8 * (size_t)(d[4] | d[5] << 8 | d[6] << 16 | (uint32_t)d[7] << 24))) {
        if (size >= 4) plat_log("Old version of keyframes detected");   /* 0x5cea80 */
        free(d);
        d = NULL;
    }
    if (!d) {
        d = calloc(1, 0xc88);
        if (!d) return;
        int32_t *k = (int32_t *)d;
        for (int i = 0; i < 400; i++) {
            k[2 + 2 * i] = -1;
            float f = 400.0f;
            memcpy(&k[3 + 2 * i], &f, 4);
        }
        k[1] = 2;
        k[2] = 0;
        float f = 600.0f;
        memcpy(&k[3], &f, 4);
        k[4] = 100000;
        memcpy(&k[5], &f, 4);
    }
    s_keys = (int32_t *)d;
    s_keys[0] = 1;
    s_keyat = 0;
    keys_seek(0);
}

void Render_FreeViewKeys(void)   /* 0x44f9e0 */
{
    free(s_keys);
    s_keys = NULL;
}

/* The view distance (0x44f070): the track's by the camera's road node times the option's share (0x5ce90c:
   1/2, 2/3, 5/6, 1), the whole of it in a replay's camera narrower than 37.5 degrees; at least 50. */
static void set_viewdist(float d)
{
    float k = 1.0f;
    if (g_race_view < 2 || 37.5f <= g_racecam[0].fov) {   /* 0x5b3c9c */
        int a = g_renderopts.adjustableviewdistance;
        uint32_t u = exe_u32(0x5ce90c + 4u * (uint32_t)(a < 0 ? 0 : a > 3 ? 3 : a));
        memcpy(&k, &u, 4);
    }
    g_render_viewdist = k * d;
    if (g_render_viewdist < 50.0f) g_render_viewdist = 50.0f;   /* 0x5b251c */
}

/* 0x44eb50: the false sort layers' level: none at the low special effects, 2 at the highest; at 2 the
   race's by the first car's +0x35c (0 from 15 up, 0x5b243c), the replay's by the camera's field of view
   (2 under 37.5, 0x5b3c9c), else 1. */
int32_t g_render_falsesort;   /* 0x6197e8 */

static void falsesort_level(void)
{
    switch (g_renderopts.specialfx) {
    case 0:
    case 1: g_render_falsesort = 0; return;
    case 2: break;
    case 3: g_render_falsesort = 2; return;
    default: return;
    }
    if (g_race_view < 2) {
        if (g_cars[0] && 15.0f <= *CAR_F(g_cars[0], 0x35c)) {
            g_render_falsesort = 0;
            return;
        }
    } else if (g_racecam[0].fov < 37.5f) {
        g_render_falsesort = 2;
        return;
    }
    g_render_falsesort = 1;
}

void Render_Frame(RaceViews *rv)
{
    Render_BeginFrame(g_render);
    Particles_Frame(g_drive_paused);                     /* Particles_Tick by the timer (0x628148; 0x606a98, 0x606a8c never set here) */
    if (s_keys) set_viewdist(keys_find(g_racecam[0].surf.node));   /* 0x44fb80, 0x44f070 (the camera's +0xdc) */
    falsesort_level();                                   /* 0x44eb50 */
    Render_MainViews(rv);
    View_Begin(g_render, rv->overlay);
    View_End(g_render);
    HUD_Draw(g_render, rv->overlay);                     /* 0x425910 */
    Render_RearView();                                   /* 0x468be0 */
    HUD_DrawOverlay(g_render, rv->overlay);              /* 0x4257d0 */
    View_Begin(g_render, rv->overlay);
    View_End(g_render);
    Render_EndFrame(g_render);
}
