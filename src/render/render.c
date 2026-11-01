/* The render system (render.h): Render_Init and the render system object, the graphics device, the
   render-state object, display mode and quality, cameras and views, the race frame. */
#include "render/render.h"
#include "render/sky.h"
#include "game/camera.h"
#include "game/hud.h"
#include "exe.h"
#include "platform.h"
#include "pending.h"
#include "real/real.h"
#include "render/render_pending.h"
#include "render/r3dcar.h"
#include "render/rlayers.h"
#include "render/rmaterial.h"
#include "render/rtex.h"
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
    PENDING_NOTE(0x5365e0, "FONT_init (the device's font)");
    EAGL_ContextCreate();
    EAGL_ContextInit(g_eagl, d->mode);
    EAGL_RSReset();
    EAGL_ContextResetStages(g_eagl);
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
   exist here; DDRAW_init, the 2D window (0x5341f0), MOUSE_init, KEY_init and the timer task (0x55fc00)
   belong to other modules. */
bool GLIDE_env(bool windowed, const char *driver, int display, int mode)
{
    if (!THRASH_opendll(driver)) return false;           /* THRASH_drivername 0x5753f0 */
    if (!THRASH.fn.init()) return false;                 /* THRASH_openwindow */
    if (display) THRASH.fn.selectdisplay(display);
    THRASH.fn.setvideomode(mode, 2, 1);
    PENDING_NOTE(0x5341f0, "WINDOW_create (the 2D window of the mode)");
    return true;
}

/* ---- display mode and quality ---- */

static int card_type;               /* 0x6197b0: Render_DetectCard's card (pending: 0, never 0xe) */
uint8_t g_render_modalpha;          /* 0x6197aa */

int Render_CardType(void) { return card_type; }

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
    PENDING_NOTE(0x44e760, "Render_DetectCard");
    /* (of it: whether the driver modulates alpha, setstate 0x1c3, except on the card types 2, 3 and 0xe;
       the port's card is type 0) */
    g_render_modalpha = THRASH.fn.setstate(THRASH_STATE_MODULATEALPHA, 0) != 0 && card_type != 2 && card_type != 3 &&
                        card_type != 0xe;
    Render_SetupQuality();
    EAGL_SetDisplaySize((float)w, (float)h);             /* 0x537370: the 2D window's size */
}

/* 0x44ebe0. The globals other than the method key stay in this module until their readers are ported
   (shadows 0x6197e5, particles 0x6197e0, specials 0x6197ec/0x6197f0/0x6197f4, car detail 0x6197c4). */
static int specials_level;          /* 0x6197f4 */
uint8_t g_render_skiplod0;          /* 0x6197e5: every quality level clears it in this build */
uint8_t g_render_extras;            /* 0x6197e6: the 0x40000 'pr' lists and the effects (card type isn't 0xe) */
static int car_detail;              /* 0x6197c4 */
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
    case 0: specials_level = 0; g_render_extras = 0; break;
    case 1: specials_level = 1; g_render_extras = 0; break;
    case 2: specials_level = 2; break;
    default:
        specials_level = 3;
        PENDING_NOTE(0x6197a6, "the card's trilinear flag (Render_DetectCard)");
        break;
    }
    if (g_renderopts.carchrome == 0) g_rmethod_key |= 1;
    else g_rmethod_key |= 3;
    /* the chrome option: 0 none, 1 (0x6197c8/c9 only), 2 the dynamic lights and the reflections too */
    g_render_carlights = g_render_carenv = g_renderopts.carchrome >= 2;
    car_detail = g_renderopts.cardetail == 0 ? 0 : g_renderopts.cardetail == 1 ? 1 : 2;
    if (RTex_flagsallowed && (g_rmethod_key & 2)) g_rmethod_key = (g_rmethod_key & ~3u) | 4;
    (void)specials_level;
    (void)car_detail;
}

/* ---- the render system ---- */

RenderSystem *Render_Construct(RenderSystem *r, const char *driver, int device, int windowed)
{
    memset(r, 0, sizeof *r);
    r->znear = 0.15f;                   /* 0x3e19999a */
    r->zfar = 700.0f;                   /* 0x442f0000 */
    PENDING_NOTE(0x466240, "the render system's lists (+0x14)");
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
    r->textures = g->textures;
    PENDING_NOTE(0x53c140, "MEM_info");
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
       card samples at, 0x44e740) is the driver's business here: the GL driver samples at D3D's centres. */
    PENDING_NOTE(0x44e740, "the UV test's texel offset");
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
    PENDING_NOTE(0x4b6ec0, "FE_SetRenderer (the front end's graphics)");
    PENDING_NOTE(0x465cb0, "the render system's heap (0x62810c)");
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
    PENDING_NOTE(0x628140, "the race views' global");
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
    c->viewdist = (float)g_renderopts.viewdistance;      /* 0x6197cc */
    c->viewdist2 = c->viewdist * c->viewdist;
    Camera_ToView(c->update[1], c, c->update[0]);        /* Camera_Track 0x461060 -> 0x464e40 */
    RenderState_SetCull(g_render->device->rs, g_r3d_mirror ? 1 : 2);
    c->clear = Sky_ClearColour(false);                   /* 0x459940, 0x466da0 */
    if (c->view) c->view->background = c->clear;
    View_Begin(g_render, c);
    Render_PendingWorld(c);                              /* 0x468a20 */
    View_End(g_render);
    Render_PendingAfterViews();                          /* 0x44fd70 */
}

void Render_Frame(RaceViews *rv)
{
    Render_BeginFrame(g_render);
    Render_PendingParticles();                           /* Particles_Tick by the timer (0x628148) */
    Render_PendingFrameSetup();                          /* 0x44fb80, 0x44f070, 0x44eb50 */
    Render_MainViews(rv);
    View_Begin(g_render, rv->overlay);
    View_End(g_render);
    HUD_Draw(g_render, rv->overlay);                     /* 0x425910 */
    Render_PendingHud(rv->overlay);                      /* 0x468be0, HUD_DrawOverlay */
    View_Begin(g_render, rv->overlay);
    View_End(g_render);
    Render_EndFrame(g_render);
}
