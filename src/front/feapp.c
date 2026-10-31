/* The front end's application (feapp.h). */
#include "front/feapp.h"
#include "front/fe2d.h"
#include "front/fe_parts.h"
#include "pending.h"
#include "platform.h"
#include "render/render.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { STACK = 16 };

static FertControl *s_stack[STACK];   /* +0xd8: the screens */
static FertControl *s_bar;            /* +0xe0: the bottom bar (FEBottomBarMenu, bottombar.lay) */
static FEResult s_result;
static char s_track[64] = "coastal";
static int s_laps = 1, s_opponents;
static int s_depth;
static RCamera *s_camera;
static int32_t s_mouse[2] = {-1, -1}; /* the pump's last poll (+0x14 ..) */
static uint32_t s_buttons;
static bool s_ready;

/* A key of fe.txt, the options file the original reads with Options_Read 0x4b6450 (not ported yet). */
static bool fe_key(const char *key, char *out, size_t cap)
{
    size_t n = 0, kl = strlen(key);
    char *t = (char *)vfs_read_all("fe.txt", &n);
    bool found = false;
    if (!t) return false;
    char *b = realloc(t, n + 1);
    if (!b) {
        free(t);
        return false;
    }
    b[n] = 0;
    for (char *l = b; l && *l;) {
        char *e = strpbrk(l, "\r\n");
        if (e) *e++ = 0;
        while (*l == ' ') l++;
        if (!strncmp(l, key, kl) && (l[kl] == ' ' || l[kl] == '=')) {
            char *q = strchr(l, '=');
            if (q) {
                q++;
                while (*q == ' ') q++;
                snprintf(out, cap, "%s", q);
                found = true;
            }
        }
        l = e;
    }
    free(b);
    return found;
}

static int fe_int(const char *key, int def)
{
    char v[64];
    return fe_key(key, v, sizeof v) ? atoi(v) : def;
}

static int language(void) { return fe_int("language", 0); }

static FertControl *load_screen(const char *name)
{
    char path[300];
    snprintf(path, sizeof path, "%s%s.lay", FE_DIR_LAYOUTS, name);
    FertControl *w = FertControl_new(true);
    if (!w) return NULL;
    if (!FertWindow_LoadLayout(w, path)) {
        FertControl_free(w);
        return NULL;
    }
    /* (a second update: the text parts' sizes measured by the first) */
    if (w->screen) FertControl_Update(w->screen);
    for (int i = 0; i < w->nchildren; i++) FertControl_Update(w->children[i]);
    for (int i = 0; i < w->nchildren; i++) {
        FertControl *c = w->children[i];
        if (FertControl_findState(c, "hover") >= 0) c->fecaps |= 4;   /* (+0x110 as the template's states) */
    }
    return w;
}

bool FEApp_Init(const char *start)
{
    if (!g_render) return false;
    if (!s_ready) {
        Fert_ListControls(FE_DIR_CONTROLS);          /* 0x529eb0 */
        FEStrings_Load(language());                  /* 0x4e0090 */
        FEParts_Init();
        FE2D_Init();                                 /* 0x4d88c0 */
        s_camera = Render_CreateCamera(g_render, 5, 0, 0, 1, 1, 0xff000000u, 1.0f, 100.0f);
        PENDING_NOTE(0x4d2e70, "FEApplication_Run: the data areas, saves, 3D rooms, music, movies");
        s_bar = load_screen("bottombar");
        s_ready = true;
    }
    s_result = FE_RUNNING;
    while (s_depth) FertControl_free(s_stack[--s_depth]);   /* (FEApplication_Run starts its stack anew) */
    return FEApp_GotoScreen(start, true);
}

bool FEApp_GotoScreen(const char *name, bool push)
{
    FertControl *w = load_screen(name);
    if (!w) {
        char m[200];
        snprintf(m, sizeof m, "front end: no screen %s", name);
        plat_log(m);
        return false;
    }
    if (!push && s_depth) FertControl_free(s_stack[--s_depth]);
    if (s_depth == STACK) {
        FertControl_free(w);
        return false;
    }
    s_stack[s_depth++] = w;
    return true;
}

void FEApp_Back(void)
{
    if (s_depth > 1) FertControl_free(s_stack[--s_depth]);
}

FertControl *FEApp_Top(void) { return s_depth ? s_stack[s_depth - 1] : NULL; }

/* ---------------------------------------------------------------- the controls' behaviour */

static const char *data_str(const FertControl *c, const char *name)
{
    for (int k = 0; k < c->nparts; k++) {
        FertAttr *a = FertAttrList_find(&c->parts[k]->attrs, name);
        if (a) return FertAttr_str(a);
    }
    return NULL;
}

/* The state of the flags (0x4c7e70). */
static void refresh_state(FertControl *c)
{
    uint32_t f = c->feflags;
    if (f & 2) return;
    const char *n = "normal";
    if (f & 0x20) n = "hidden";
    else if (f & 0x10) n = "disabled";
    else if ((c->fecaps & 0x80) && (f & 0x40) && (f & 4)) n = "focusedhover";
    else if ((c->fecaps & 0x80) && (f & 0x40)) n = "focused";
    else if ((c->fecaps & 0x100) && (f & 8) && (f & 4)) n = "selectedhover";
    else if ((c->fecaps & 0x100) && (f & 8)) n = "selected";
    else if (f & 4) n = "hover";
    FertControl_setState(c, n);
}

static void on_enter(FertControl *c)   /* 0x4c7760 */
{
    if ((c->fecaps & 4) && !(c->feflags & 4)) {
        c->feflags |= 4;
        refresh_state(c);
    }
}

static void on_leave(FertControl *c)   /* 0x4c7820 */
{
    if (c->feflags & 4) {
        c->feflags &= ~4u;
        refresh_state(c);
    }
}

FEResult FEApp_Result(void) { return s_result; }
const char *FEApp_Track(void) { return s_track; }
int FEApp_Laps(void) { return s_laps; }
int FEApp_Opponents(void) { return s_opponents; }

static bool can_play(void)
{
    FertControl *w = FEApp_Top();
    if (!w || !w->screen) return false;
    for (int k = 0; k < w->screen->nparts; k++) {
        FertAttr *a = FertAttrList_find(&w->screen->parts[k]->attrs, "canplaygame");
        if (a) return FertAttr_int(a) != 0;   /* (0x4d5df0: data.canplaygame) */
    }
    return false;
}

/* The callbacks the bottom bar's buttons and the screens' controls name (0x50cd90 through the screen's
   callback list; FECallBackList_Dispatch 0x50b7f0): the common ones here, the screens' own pending. */
static void callback(const char *cb)
{
    if (!strcmp(cb, "GOBACK")) {
        FEApp_Back();
    } else if (!strcmp(cb, "OPTIONS")) {
        FEApp_GotoScreen("options", true);
    } else if (!strcmp(cb, "QUIT")) {
        s_result = FE_QUIT;
    } else if (!strcmp(cb, "PLAYGAME")) {
        if (!can_play()) return;
        /* The race description the screens leave in the options (0x4d2e70's end; the data areas'
           accessors pending: fe.txt's as the original's defaults). */
        char v[64];
        if (fe_key("track_name", v, sizeof v)) snprintf(s_track, sizeof s_track, "%s", v);
        s_laps = fe_int("num_laps", 1);
        s_opponents = fe_int("numopponentracecars", 0);
        s_result = FE_PLAY;
    } else {
        char m[160];
        snprintf(m, sizeof m, "front end: callback %s (0x50cd90, pending)", cb);
        plat_log(m);
    }
}

static void on_up(FertControl *c)
{
    if (c->feflags & 0x10) return;   /* 0x4c7a10: disabled */
    if (!strcmp(c->id, "NFS5.GOTOBUTTON") || !strcmp(c->id, "NFS5.BOTTOMBARBUTTON")) {   /* 0x4c8f50 */
        PENDING_NOTE(0x4aab80, "the front end's click sound (5)");
        const char *cb = data_str(c, "callback");
        if (cb && *cb) callback(cb);
        if (s_result != FE_RUNNING) return;
        const char *dest = data_str(c, "destination");
        if (dest && *dest) FEApp_GotoScreen(dest, true);
    }
}

/* A window's routing of an event (0x5285e0). */
static void window_event(FertControl *w, int type, int32_t x, int32_t y)
{
    FertControl *target = w->focus;
    switch (type) {
    case 7: {   /* a move: enter and leave */
        w->mouse[0] = x;
        w->mouse[1] = y;
        FertControl *h = FertWindow_hit(w, x, y);
        if (h != w->hover) {
            if (w->hover) on_leave(w->hover);
            w->hover = h;
            if (h) on_enter(h);
        }
        break;
    }
    case 4:
    case 8:
        if (!target) target = FertWindow_hit(w, w->mouse[0], w->mouse[1]);
        if (target && type == 8) on_up(target);
        break;
    }
}

/* ---------------------------------------------------------------- the pump's pass and the frame */

static void poll(void)   /* 0x529530 */
{
    FertControl *w = FEApp_Top();
    float fx, fy, wheel;
    uint32_t b;
    if (!w || !plat_mouse(&fx, &fy, &b, &wheel)) return;
    int dw, dh;
    plat_display_size(&dw, &dh);
    int mw = g_display.width, mh = g_display.height;
    if (dw <= 0 || dh <= 0 || mw <= 0 || mh <= 0) return;
    /* the present's letterbox undone (thrash_dev.c's dev_present) */
    float sx = 1, sy = 1, a = (float)mw / (float)mh, da = (float)dw / (float)dh;
    if (da > a) sx = a / da;
    else sy = da / a;
    float nx = (fx / (float)dw * 2.0f - 1.0f) / sx, ny = (fy / (float)dh * 2.0f - 1.0f) / sy;
    int32_t mx = (int32_t)((nx + 1.0f) * 0.5f * (float)mw), my = (int32_t)((ny + 1.0f) * 0.5f * (float)mh);
    if (mx != s_mouse[0] || my != s_mouse[1]) {
        s_mouse[0] = mx;
        s_mouse[1] = my;
        if (s_bar) window_event(s_bar, 7, mx, my);
        window_event(w, 7, mx, my);
    }
    uint32_t changed = b ^ s_buttons;
    s_buttons = b;
    if (changed & 1) {
        /* (FEApplication_v3 0x4d22a0: the bottom bar first, then the top screen) */
        if (s_bar && FertWindow_hit(s_bar, mx, my)) window_event(s_bar, (b & 1) ? 4 : 8, mx, my);
        else window_event(FEApp_Top(), (b & 1) ? 4 : 8, mx, my);
    }
}

void FEApp_Frame(void)
{
    poll();
    FertControl *w = FEApp_Top();
    TexManager *tm = g_texgraphics ? g_texgraphics->textures : NULL;
    Render_BeginFrame(g_render);
    if (tm) TexManager_begin(tm);
    View_Begin(g_render, s_camera);
    RCamera_Setup2D(s_camera);
    g_fe_depth = 0.99998474f;   /* 0x4d2980 */
    if (s_bar) {
        /* the play button enabled with the screen's canplaygame (0x4d7c70 -> 0x4c79c0 / 0x4c79e0) */
        for (int i = 0; i < s_bar->nchildren; i++) {
            FertControl *c = s_bar->children[i];
            if (strcmp(c->name, "ID_PLAYGAMEBUTTON")) continue;
            uint32_t f = c->feflags;
            if (can_play()) c->feflags &= ~0x10u;
            else if (FertControl_findState(c, "disabled") >= 0) c->feflags |= 0x10;
            if (f != c->feflags) refresh_state(c);
        }
    }
    if (w) {
        for (int i = 0; i < w->nchildren; i++) FertControl_Update(w->children[i]);   /* (the controls' ticks) */
        FEWindow_Draw(w);
    }
    if (s_bar) {
        for (int i = 0; i < s_bar->nchildren; i++) FertControl_Update(s_bar->children[i]);
        FEWindow_Draw(s_bar);   /* (0x4d2980: the bottom bar over the screens) */
    }
    FE2D_Flush();
    View_End(g_render);
    if (tm) TexManager_end(tm);
    Render_EndFrame(g_render);
}
