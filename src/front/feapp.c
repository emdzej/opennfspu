/* The front end's application (feapp.h). */
#include "front/feapp.h"
#include "front/fe2d.h"
#include "front/fe_parts.h"
#include "front/feaccess.h"
#include "front/fe3d.h"
#include "game/audio.h"
#include "game/music.h"
#include "game/options.h"
#include "real/mouse.h"
#include "exe.h"
#include "app.h"
#include "pending.h"
#include "platform.h"
#include "render/r3dcar.h"
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
static char s_car[64] = "964";
static int s_depth;
static bool s_sound;
static int32_t s_music_at;            /* FEMainScreen +0x290: the tick the last song started */
static FertControl *s_parent;         /* the window of the control an event is for */
static FertControl *s_overlay;        /* +0xe4: the screen's overlay (its "screen" id, 0x4e1010) */
static char s_overlay_id[32];
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

static FertControl *load_screen(const char *name);
static bool indicator_draw(FertControl *c, int32_t ox, int32_t oy);
static void select_panel(FertControl *w, const char *name);

/* A screen's panels (FEPanel_v20 0x4ce480: closed and hidden until chosen) and radio groups (FERadioGroup
   0x4cf8a0: open at once, their screen's selected item choosing its panel, 0x4cf8e0). */
static void open_panels(FertControl *w)
{
    for (int i = 0; i < w->nchildren; i++) {
        FertControl *c = w->children[i];
        if (!strcmp(c->id, "NFS5.RADIOGROUP") || !strcmp(c->id, "NFS5.PANEL")) c->feflags |= 0x20;   /* 0x4c7a50 */
    }
    for (int i = 0; i < w->nchildren; i++) {
        FertControl *c = w->children[i];
        if (strcmp(c->id, "NFS5.RADIOGROUP")) continue;
        const char *f = NULL;
        for (int k = 0; k < c->nparts && !f; k++) {
            FertAttr *a = FertAttrList_find(&c->parts[k]->attrs, "filename");
            if (a) f = FertAttr_str(a);
        }
        if (!f || !*f || !(c->sub = load_screen(f))) continue;
        c->feflags &= ~0x20u;   /* 0x4ce4d0: open, shown (0x4c7a80) */
        for (int j = 0; j < c->sub->nchildren; j++) {
            FertControl *t = c->sub->children[j];
            int sel = FertControl_findState(t, "selected");
            if (sel >= 0 && t->state == sel) {
                t->feflags |= 8;
                for (int k = 0; k < t->nparts; k++) {
                    FertAttr *a = FertAttrList_find(&t->parts[k]->attrs, "panel");
                    if (a) select_panel(w, FertAttr_str(a));
                }
            }
        }
    }
}

/* The group's panel by name opened, the one open before closed (FERadioGroup_v29 0x4cf960, FEPanel
   0x4ce4d0 / 0x4ce540). */
static void select_panel(FertControl *w, const char *name)
{
    for (int i = 0; w && i < w->nchildren; i++) {
        FertControl *c = w->children[i];
        if (strcmp(c->id, "NFS5.PANEL")) continue;
        if (!strcmp(c->name, name)) {
            if (!c->sub) {
                const char *f = NULL;
                for (int k = 0; k < c->nparts && !f; k++) {
                    FertAttr *a = FertAttrList_find(&c->parts[k]->attrs, "filename");
                    if (a) f = FertAttr_str(a);
                }
                if (f && *f) c->sub = load_screen(f);
            }
            if (c->sub) c->feflags &= ~0x20u;
            /* the audio area's tick (0x4f6xxx: its main panel opened with the audio panel) */
            if (c->sub && !strcmp(c->name, "ID_audio_panel")) select_panel(c->sub, "ID_MAINSCREENPANEL");
        } else if (c->sub) {
            FertControl_free(c->sub);
            c->sub = NULL;
            c->feflags |= 0x20;
        }
    }
}

/* The panel of that name in the screen or in its open panels' screens (the group's own panels). */
static bool select_panel_deep(FertControl *w, const char *name)
{
    for (int i = 0; w && i < w->nchildren; i++)
        if (!strcmp(w->children[i]->id, "NFS5.PANEL") && !strcmp(w->children[i]->name, name)) {
            select_panel(w, name);
            return true;
        }
    for (int i = 0; w && i < w->nchildren; i++)
        if (w->children[i]->sub && select_panel_deep(w->children[i]->sub, name)) return true;
    return false;
}

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
        if (FertControl_findState(c, "selected") >= 0) c->fecaps |= 0x100;
    }
    open_panels(w);
    return w;
}

bool FEApp_Init(const char *start)
{
    if (!g_render) return false;
    if (!s_ready) {
        Fert_ListControls(FE_DIR_CONTROLS);          /* 0x529eb0 */
        FEStrings_Load(language());                  /* 0x4e0090 */
        FEParts_Init();
        FEData_Init();                               /* (the data areas' accessors, 0x4e4bc0 ..) */
        FE2D_Init();                                 /* 0x4d88c0 */
        FE_ControlDrawHook = indicator_draw;
        s_camera = Render_CreateCamera(g_render, 5, 0, 0, 1, 1, 0xff000000u, 1.0f, 100.0f);
        PENDING_NOTE(0x4d2e70, "FEApplication_Run: the data areas, saves, the 3D rooms but the garage, movies");
        s_bar = load_screen("bottombar");
        s_ready = true;
    }
    s_result = FE_RUNNING;
    g_r3d_frontend = 1;
    /* The sound (GameMain's Audio_Init 0x4a6610 before the front end; the port's race closes it at its
       end, so it opens again here) and the menus' music. */
    if (!s_sound) s_sound = Audio_Init();
    s_music_at = -1;   /* 0x65b928: the application (the car renderer's showroom mode) */
    while (s_depth) FertControl_free(s_stack[--s_depth]);   /* (FEApplication_Run starts its stack anew) */
    return FEApp_GotoScreen(start, true);
}

void FEApp_End(void)
{
    if (s_sound) {
        Music_FadeOut(0x5dc);   /* (FEApplication_Run's end) */
        Audio_Shutdown();       /* (the port's race opens it again) */
        s_sound = false;
    }
    FE3D_Free();
    g_r3d_frontend = 0;
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

static int data_int_of(const FertControl *c, const char *name);
static bool indicator_click(FertControl *c, int32_t mx);
static const char *data_str(const FertControl *c, const char *name)
{
    for (int k = 0; k < c->nparts; k++) {
        FertAttr *a = FertAttrList_find(&c->parts[k]->attrs, name);
        if (a) return FertAttr_str(a);
    }
    return NULL;
}

static int data_int_of(const FertControl *c, const char *name)
{
    for (int k = 0; k < c->nparts; k++) {
        FertAttr *a = FertAttrList_find(&c->parts[k]->attrs, name);
        if (a) return a->type == FATTR_INT ? FertAttr_int(a) : atoi(FertAttr_str(a));
    }
    return 0;
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
const char *FEApp_Car(void) { return s_car; }
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
    } else if (!strcmp(cb, "SHOWPLAYLISTPANEL") || !strcmp(cb, "HIDEPLAYLISTPANEL")) {
        /* the audio area's (0x4f6ca0, 0x4f6cb0): the CD player's play list or the audio settings */
        FertControl *w = FEApp_Top();
        for (int i = 0; w && i < w->nchildren; i++)
            if (!strcmp(w->children[i]->name, "ID_audio_panel") && w->children[i]->sub)
                select_panel(w->children[i]->sub, cb[0] == 'S' ? "ID_PLAYLISTPANEL" : "ID_MAINSCREENPANEL");
    } else if (!strcmp(cb, "QUIT")) {
        s_result = FE_QUIT;
    } else if (!strcmp(cb, "PLAYGAME")) {
        if (!can_play()) return;
        /* The race description the screens leave in the options (0x4d2e70's end; the data areas'
           accessors pending: fe.txt's as the original's defaults). */
        char v[64];
        (void)v;
        snprintf(s_track, sizeof s_track, "%s", FEData_Track());
        snprintf(s_car, sizeof s_car, "%s", FEData_Car());
        s_laps = fe_int("num_laps", 1);
        s_opponents = FEData_Opponents();
        s_result = FE_PLAY;
    } else {
        char m[160];
        snprintf(m, sizeof m, "front end: callback %s (0x50cd90, pending)", cb);
        plat_log(m);
    }
}

static bool is_combo(const FertControl *c) { return !strcmp(c->id, "NFS5.ComboBoxSlider"); }

static void on_up(FertControl *c)
{
    if (c->feflags & 0x10) return;   /* 0x4c7a10: disabled */
    if (is_combo(c)) {
        /* (FEComboBox opens its selection box, the list to pick from; the port steps to the next item
           for now) */
        FEAccessor *a = FEAccess_Find(data_str(c, "accessor"));
        if (a) FEAccess_SetPos(a, FEAccess_Pos(a) + 1);
        return;
    }
    if (!strcmp(c->id, "NFS5.GRAPHICTABBUTTON") || !strcmp(c->id, "NFS5.TABBUTTON")) {
        /* FERadioItem: it selected, its siblings not (the window w holds them), its panel opened in the
           screen (the group's slot 29) */
        for (int i = 0; s_parent && i < s_parent->nchildren; i++) {
            FertControl *t = s_parent->children[i];
            uint32_t f = t->feflags;
            if (t == c) t->feflags |= 8;
            else t->feflags &= ~8u;
            if (f != t->feflags) refresh_state(t);
        }
        const char *panel = data_str(c, "panel");
        if (panel) select_panel_deep(FEApp_Top(), panel);
        return;
    }
    if (!strcmp(c->id, "NFS5.GOTOBUTTON") || !strcmp(c->id, "NFS5.BOTTOMBARBUTTON")) {   /* 0x4c8f50 */
        PENDING_NOTE(0x4aab80, "the front end's click sound (5)");
        const char *cb = data_str(c, "callback");
        if (cb && *cb) callback(cb);
        if (s_result != FE_RUNNING) return;
        const char *dest = data_str(c, "destination");
        if (dest && *dest) FEApp_GotoScreen(dest, true);
    }
}

/* The control under (x, y): a panel only where its screen has one (FEPanel 0x4ce570 passes the events
   on to its screen); hidden ones not. */
static FertControl *hit(FertControl *w, int32_t x, int32_t y)
{
    int32_t pos[2] = {0, 0};
    FertAttr *pa = FertAttrList_find(&w->attrs, "pos");
    if (pa) FertAttr_point(pa, pos);
    for (int i = 0; i < w->nchildren; i++) {
        FertControl *c = w->children[i];
        if (c->feflags & 0x20) continue;
        if (c->sub) {
            if (hit(c->sub, x - pos[0], y - pos[1])) return c;
            continue;
        }
        if (FertControl_contains(c, x - pos[0], y - pos[1])) return c;
    }
    return NULL;
}

/* A window's routing of an event (0x5285e0). */
static void window_event(FertControl *w, int type, int32_t x, int32_t y)
{
    FertControl *target = w->focus;
    for (int i = 0; type == 7 && i < w->nchildren; i++)
        if (w->children[i]->sub && !(w->children[i]->feflags & 0x20)) window_event(w->children[i]->sub, 7, x, y);
    switch (type) {
    case 7: {   /* a move: enter and leave */
        w->mouse[0] = x;
        w->mouse[1] = y;
        FertControl *h = hit(w, x, y);
        if (h != w->hover) {
            if (w->hover) on_leave(w->hover);
            w->hover = h;
            if (h) on_enter(h);
        }
        break;
    }
    case 4:
    case 8:
        if (!target) target = hit(w, w->mouse[0], w->mouse[1]);
        if (target && target->sub) {
            window_event(target->sub, type, x, y);
            break;
        }
        s_parent = w;
        if (target && type == 4 && indicator_click(target, w->mouse[0])) break;
        if (target && type == 8) on_up(target);
        break;
    }
}

/* ---------------------------------------------------------------- the pump's pass and the frame */

static void poll(void)   /* 0x529530 */
{
    FertControl *w = FEApp_Top();
    int32_t mx, my;
    uint32_t b;
    if (!w || !MOUSE_state(&mx, &my, &b)) return;
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

/* The screen's room (the application's +0xe8 from the screen's "room", 0x4d2137 -> 0x4e6f10) and the car
   its 3D car control puts in it (FECar3DControl_v21 0x4bf9a0: position type 0, the accessor's car). */
/* The overlay screen of an id (0x4e1010, with the 3D rooms' detail on, 0x5d9d38): the ovals and their
   frames over the room, under the screen. */
static const char *overlay_layout(const char *id)
{
    if (!strcmp(id, "ID_SCREEN")) return "screenempty";
    if (!strcmp(id, "ID_DARKOVAL")) return "screendarkoval";
    if (!strcmp(id, "ID_FULLOVAL")) return "screenfulloval";
    if (!strcmp(id, "ID_MOVIEOVAL")) return "screenmovieoval";
    if (!strcmp(id, "ID_OPTIONOVAL")) {
        PENDING_NOTE(0x4e1010, "the in-race options' lighter oval (screenoptionovallite)");
        return "screenoptionoval";
    }
    return NULL;
}

static void sync_overlay(FertControl *w)
{
    const char *id = "";
    if (w && w->screen) {
        FertAttr *a = FertAttrList_find(&w->screen->attrs, "screen");
        for (int k = 0; !a && k < w->screen->nparts; k++) a = FertAttrList_find(&w->screen->parts[k]->attrs, "screen");
        if (a) id = FertAttr_str(a);
    }
    if (!strcmp(id, s_overlay_id)) return;
    snprintf(s_overlay_id, sizeof s_overlay_id, "%s", id);
    FertControl_free(s_overlay);
    s_overlay = NULL;
    const char *lay = overlay_layout(id);
    if (lay) s_overlay = load_screen(lay);
}

static void sync_room(FertControl *w)
{
    sync_overlay(w);
    const char *room = NULL;
    if (w && w->screen) {
        FertAttr *a = FertAttrList_find(&w->screen->attrs, "room");
        for (int k = 0; !a && k < w->screen->nparts; k++) a = FertAttrList_find(&w->screen->parts[k]->attrs, "room");
        if (a) room = FertAttr_str(a);
    }
    FE3D_SetRoom(room);
    const char *car = NULL;
    for (int i = 0; w && i < w->nchildren; i++) {
        FertControl *c = w->children[i];
        if (strcmp(c->id, "NFS5.3DCAR")) continue;
        const char *pt = data_str(c, "positiontype");
        if (pt && atoi(pt) != 0) continue;
        car = FEAccess_CarFile(FEAccess_Find(data_str(c, "accessor")));
    }
    FE3D_SetCar(car);
}

static uint32_t data_colour(const FertControl *c, const char *name)
{
    for (int k = 0; k < c->nparts; k++) {
        FertAttr *a = FertAttrList_find(&c->parts[k]->attrs, name);
        if (a) return a->type == FATTR_COLOR || a->type == FATTR_INT ? FertAttr_color(a) : (uint32_t)strtoul(FertAttr_str(a), NULL, 0);
    }
    return 0;
}

static uint32_t mix(uint32_t a, uint32_t b, float t)   /* (0x4d8300: a to b by t of 0..255) */
{
    uint32_t o = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        float x = (float)((a >> sh) & 0xff), y = (float)((b >> sh) & 0xff);
        o |= (uint32_t)(int)(x + (y - x) * t) << sh;
    }
    return o;
}

static void control_rect(FertControl *c, int32_t r[4])
{
    int32_t pos[2] = {0, 0}, size[2] = {0, 0};
    FertAttr *a = FertAttrList_find(&c->attrs, "pos");
    if (a) FertAttr_point(a, pos);
    a = FertAttrList_find(&c->attrs, "size");
    if (a) FertAttr_point(a, size);
    r[0] = pos[0], r[1] = pos[1], r[2] = pos[0] + size[0], r[3] = pos[1] + size[1];
}

/* An indicator bar (FEGouraudIndicatorBar_v15 0x4ca170 -> 0x521530): its accessor's share in per cent
   (0x4ca270) as a bar of two gouraud halves, c0 to c50 and c50 to c100 (no c50: their mean), cut where
   the share ends, with a drop shadow. (Port: the colours blended linearly; the bar's place from its
   control, the .pos override FE_LoadPos 0x4e21c0 pends; growing to the left pends.) */
static bool indicator_draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (strcmp(c->id, "NFS5.GouraudIndicatorBar")) return false;
    if (c->feflags & 0x20) return true;
    FEAccessor *a = FEAccess_Find(data_str(c, "accessor"));
    if (!a) return true;
    int n = FEAccess_Count(a);
    float pct = n ? (float)(FEAccess_Pos(a) * 100 / n) : 0;
    int32_t r[4];
    control_rect(c, r);
    float x = (float)(r[0] + ox), y = (float)(r[1] + oy), w = (float)(r[2] - r[0]), h = (float)(r[3] - r[1]);
    uint32_t c0 = data_colour(c, "c0"), c50 = data_colour(c, "c50"), c100 = data_colour(c, "c100");
    if (c50 == 0) c50 = c100 ? mix(c0, c100, 0.5f) : c0;
    else if (!c100) c100 = c50;
    float half = w * 0.5f, l1 = pct < 50 ? pct * w / 100.0f : half, l2 = pct > 50 ? (pct - 50) * w / 100.0f : 0;
    uint32_t e1 = mix(c0, c50, l1 / half), e2 = mix(c50, c100, l2 / half);
    FE2D_DepthStep();
    if (l1 > 0) FE2D_Gouraud(x, y, l1, h, c0, e1, c0, e1);
    if (l2 > 0) FE2D_Gouraud(x + l1, y, l2, h, c50, e2, c50, e2);
    if (l1 + l2 > 4.0f && data_int_of(c, "hasshadow")) FEStyle_DropShadow(x, y, l1 + l2, h, 0xff, 4, 4);
    return true;
}

/* A click on a changeable bar sets its accessor there (FEGouraudIndicatorBar_v5 0x4c9f50). */
static bool indicator_click(FertControl *c, int32_t mx)
{
    if (strcmp(c->id, "NFS5.GouraudIndicatorBar")) return false;
    FEAccessor *a = FEAccess_Find(data_str(c, "accessor"));
    if (!a || !data_int_of(c, "changeable")) return true;
    int32_t r[4];
    control_rect(c, r);
    int n = FEAccess_Count(a), w = r[2] - r[0];
    if (w <= 0) return true;
    int p = (mx - r[0]) * n / w;
    if (n <= p) p = n - 1;
    if (p < 0) p = 0;
    FEAccess_SetPos(a, p);
    return true;
}

/* The accessor watches' texts (FEAccessorWatch's tick: the accessor's text for the path's argument into
   its label), in the screen and its panels. */
static void update_watches(FertControl *w)
{
    for (int i = 0; w && i < w->nchildren; i++) {
        FertControl *c = w->children[i];
        if (c->sub) update_watches(c->sub);
        if (strcmp(c->id, "NFS5.AccessorWatch")) continue;
        const char *path = data_str(c, "accessor");
        FEAccessor *a = FEAccess_Find(path);
        int k = FertControl_findPart(c, "label");
        if (!a || k < 0) continue;
        char arg[32] = "";
        const char *p = path ? strchr(path, '(') : NULL;
        if (p) {
            snprintf(arg, sizeof arg, "%s", p + 1);
            char *e = strchr(arg, ')');
            if (e) *e = 0;
        }
        FertAttr *t = FertAttrList_find(&c->parts[k]->attrs, "text");
        const char *v = FEAccess_TextArg(a, arg);
        if (t && strcmp(FertAttr_str(t), v)) {
            FertAttr_set(t, v);
            FertControl_Update(c);
        }
    }
}

static void update_all(FertControl *w)
{
    for (int i = 0; w && i < w->nchildren; i++) {
        FertControl_Update(w->children[i]);
        if (w->children[i]->sub) update_all(w->children[i]->sub);
    }
}

/* The combo boxes' values (FEComboBox's own draw 0x4c53e0, its styling pending): right-aligned in the
   value part's place; in the screen and its panels. */
static void draw_combos(FertControl *w)
{
    for (int i = 0; i < w->nchildren; i++)
        if (w->children[i]->sub && !(w->children[i]->feflags & 0x20)) draw_combos(w->children[i]->sub);
    for (int i = 0; i < w->nchildren; i++) {
        FertControl *c = w->children[i];
        if (!is_combo(c) || c->state == FertControl_findState(c, "hidden")) continue;
        FEAccessor *a = FEAccess_Find(data_str(c, "accessor"));
        int k = FertControl_findPart(c, "label");
        if (!a || k < 0) continue;
        FertAttr *f = FertAttrList_find(&c->parts[k]->attrs, "fontPathName");
        char path[300];
        FE_ResolvePath(f ? FertAttr_str(f) : "", path, sizeof path);
        HudFont *font = FEFont_Get(path);
        const char *t = FEAccess_Text(a);
        int n = (int)strlen(t) + 1;
        char *copy = FE2D_Alloc(n);
        if (!font || !copy) continue;
        memcpy(copy, t, (size_t)n);
        int32_t pos[2] = {0, 0}, width = 0;
        FertAttr *pa = FertAttrList_find(&c->attrs, "pos");
        if (pa) FertAttr_point(pa, pos);
        const char *ws = data_str(c, "width");
        if (ws) width = atoi(ws);
        FE2D_DepthStep();
        FE2D_Text(font, (float)(pos[0] + width - 13), (float)(pos[1] + 2), 2, 0xffffffffu, copy, 1.0f, 1.0f);
    }
}

/* The menus' music (FEMainScreen_v1 0x50faf0): a song of "Menu*" (0x5dca14) every 0x1e00 ticks (a minute),
   the one before faded out, at the options' level (MUSICLEVEL). */
static void menu_music(void)
{
    if (!s_sound) return;
    int32_t now = (int32_t)((uint64_t)app_ticks_ms() * 128u / 1000u);   /* TIMER_ticks */
    if (s_music_at < 0 || s_music_at < now - 0x1e00) {
        Music_FadeOut(0);   /* 0x4ae190 */
        Music_SetVolume((int32_t)((exe_u32(0x5d14b0) * (uint32_t)Options_Int(0x5d618c, (int)exe_u32(0x5d1740))) >> 7));
        Music_Play(exe_str(0x5dca14), 0);
        s_music_at = now;
    }
}

void FEApp_Frame(void)
{
    poll();
    menu_music();
    FertControl *w = FEApp_Top();
    TexManager *tm = g_texgraphics ? g_texgraphics->textures : NULL;
    sync_room(w);
    Render_BeginFrame(g_render);
    if (tm) TexManager_begin(tm);
    /* the room first, in the 3D camera (0x4d2980); then the 2D over it */
    s_camera->clear = FE3D_Draw() ? 0 : 0xff000000u;
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
    if (s_overlay) {   /* (0x4d2980: the overlay before the screens) */
        for (int i = 0; i < s_overlay->nchildren; i++) FertControl_Update(s_overlay->children[i]);
        FEWindow_Draw(s_overlay);
    }
    if (w) {
        update_watches(w);
        update_all(w);   /* (the controls' ticks) */
        FEWindow_Draw(w);
        draw_combos(w);
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
