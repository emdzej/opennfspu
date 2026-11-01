/* The front end's application (feapp.h). */
#include "front/feapp.h"
#include "front/fe2d.h"
#include "front/fe_parts.h"
#include "front/feaccess.h"
#include "front/fe3d.h"
#include "front/fesave.h"
#include "front/femarket.h"
#include "game/audio.h"
#include "game/music.h"
#include "game/options.h"
#include "real/mouse.h"
#include "real/font.h"
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
/* The dialog up (FEDialog 0x511c60 and its kinds): one button (0x512d00), two (FEDialogTwoItem 0x5133a0),
   an input and two (FEDialogInputCancel 0x513b50). done: closed by its first button (1) or second (2). */
enum { DLG_ONE, DLG_TWO, DLG_INPUT };
enum { DLG_FOR_NONE, DLG_FOR_SELL, DLG_FOR_OFFER, DLG_FOR_DECAL };
static struct {
    bool up;
    int kind, purpose, done;
    char text[300];
    int b1, b2;              /* the buttons' strings */
    char input[16];          /* FEDialogInput's text (+0xe4), its length cap (+0xe8), digits only (+0xf1) */
    int cap;
    bool digits;
    float x, y, w, h, tw;
} s_dialog;
static int dialog_button_hit(int32_t mx, int32_t my);
static void dialog_key(uint8_t code);
static void dialog_open(int kind, const char *text, int b1, int b2);
static bool s_sound;
static uint32_t s_colours[4];         /* the race car's colours (the description's +0x218 ..) */
static bool s_havecol;
static int s_tour_race = -1;          /* the career race being run (its index in the tournament) */
static int s_owned_race = -1;         /* the owned car raced (its index in the save) */
static int s_gametype;                /* Game.GameType (0x690ad4): 4 the career (the other modes' values pend) */
static FertControl *s_tourny_win;     /* the tournament screen's (FETournamentCallBackList 0x5051a0) */
static int s_tourny_offer = -1;       /* its car with an offer (+0x2f0), an owned index */
static bool s_used_parts;             /* the used car screen's parts shown, not its damage (+0x290) */
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
static bool listbox_click(FertControl *c, int32_t mx, int32_t my);
static FertControl *control_named(FertControl *w, const char *name);
static void select_panel(FertControl *w, const char *name);
static FertControl *load_screen(const char *name);

/* A panel's screen loaded (FEPanel 0x4ce4d0 -> 0x4e0da0), placed at the panel's position (its screen
   "moveable", 0x526e20: the layout's places are the panel's). */
static FertControl *load_panel(FertControl *c, const char *file)
{
    FertControl *s = load_screen(file);
    FertAttr *pp = FertAttrList_find(&c->attrs, "pos"), *sp = s ? FertAttrList_find(&s->attrs, "pos") : NULL;
    if (pp && sp) {
        int32_t pos[2];
        FertAttr_point(pp, pos);
        FertAttr_setPoint(sp, pos[0], pos[1]);
    }
    return s;
}

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
        if (!f || !*f || !(c->sub = load_panel(c, f))) continue;
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
                if (f && *f) c->sub = load_panel(c, f);
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

/* An attribute of the screen's own part ("screen", "callbacklist", ...); "" without it. */
static const char *screen_attr(FertControl *w, const char *name)
{
    if (!w || !w->screen) return "";
    for (int k = 0; k < w->screen->nparts; k++) {
        FertAttr *a = FertAttrList_find(&w->screen->parts[k]->attrs, name);
        if (a) return FertAttr_str(a);
    }
    return "";
}

static FertControl *load_screen(const char *name)
{
    char path[300];
    /* (0x4e0da0: ".lay" 0x5d9bd0 added when the name hasn't it) */
    snprintf(path, sizeof path, "%s%s%s", FE_DIR_LAYOUTS, name, strstr(name, exe_str(0x5d9bd0)) ? "" : exe_str(0x5d9bd0));
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
        if (FertControl_findState(c, "disabled") >= 0) c->fecaps |= 0x10;
        if (FertControl_findState(c, "hidden") >= 0) c->fecaps |= 0x20;
    }
    if (!strcmp(screen_attr(w, "callbacklist"), "ID_USEDCARCBL")) s_used_parts = false;   /* (0x4f6790) */
    if (!strcmp(screen_attr(w, "callbacklist"), "ID_BUYPARTSCBL")) FEData_PartsOpen();   /* (0x518ba0) */
    if (!strcmp(screen_attr(w, "callbacklist"), "ID_TOURNYSCREEN")) {   /* (0x5051a0: its first offer) */
        s_tourny_win = w;
        s_tourny_offer = Market_FirstOffer();
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

void FEApp_RaceWear(const int32_t *ids, const float *wear, int n)
{
    if (s_owned_race >= 0) FEData_AddWear(s_owned_race, ids, wear, n);
    s_owned_race = -1;
    FESave_Write();
}

void FEApp_RaceOver(const int *places, const int *times, int n)
{
    if (s_tour_race >= 0) FEData_TourResult(s_tour_race, places, times, n);
    s_tour_race = -1;
    if (s_gametype == 4) Market_Update(FEData_CareerYear());   /* (FEApplication_Run's 0x4ed390) */
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
const uint32_t *FEApp_Colours(void) { return s_havecol ? s_colours : NULL; }
int FEApp_Parts(int32_t ids[20]) { return s_owned_race >= 0 ? FEData_OwnedParts(s_owned_race, ids) : -1; }
bool FEApp_Setup(float set[17]) { return s_owned_race >= 0 && FEData_OwnedSetup(s_owned_race, set); }
const char *FEApp_Track(void) { return s_track; }
const char *FEApp_Car(void) { return s_car; }
int FEApp_Laps(void) { return s_laps; }
int FEApp_Opponents(void) { return s_opponents; }

static int play_mode(void)   /* (0x4d5df0: data.canplaygame: 1 a race of the choices, 2 the career's) */
{
    FertControl *w = FEApp_Top();
    if (!w || !w->screen) return 0;
    for (int k = 0; k < w->screen->nparts; k++) {
        FertAttr *a = FertAttrList_find(&w->screen->parts[k]->attrs, "canplaygame");
        if (a) return FertAttr_int(a);
    }
    return 0;
}

static bool can_play(void) { return play_mode() != 0; }

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
    } else if (!strcmp(cb, "DEALERCARINC") || !strcmp(cb, "DEALERCARDEC")) {
        /* FEDealershipCallbackList_v57 / v58 (0x50e600, 0x50e630): the dealer's next or last car */
        FEAccessor *a = FEAccess_Find("DealerCar.Car");
        if (a) FEAccess_SetPos(a, FEAccess_Pos(a) + (cb[9] == 'I' ? 1 : -1));
    } else if (!strcmp(cb, "BUYCAR")) {
        /* (the screen's callback list's: FEDealershipCallbackList_v55 0x50e5f0, FEUsedCarCallbackList_v55 0x4f68f0) */
        int m = !strcmp(screen_attr(FEApp_Top(), "callbacklist"), "ID_USEDCARCBL") ? FEData_BuyUsed() : FEData_BuyCar();
        if (m > 0) {
            char b[200];
            (void)b;
            FEApp_Message(m);   /* 0x512d00 */
        }
    } else if (!strcmp(cb, "SHOWPARTS") || !strcmp(cb, "SHOWDAMAGE")) {
        s_used_parts = cb[4] == 'P';   /* FEUsedCarCallbackList_v57 0x4f6bc0 / _v58 0x4f6c30 (+0x290) */
    } else if (!strcmp(cb, "BUYPARTCALLBACK") || !strcmp(cb, "SELLPARTCALLBACK") || !strcmp(cb, "CLEARLISTCALLBACK") ||
               !strcmp(cb, "REMOVEITEMCALLBACK") || !strcmp(cb, "PURCHASECALLBACK")) {
        int m = FEData_PartsAction(cb);   /* (FEPurchasePartsCBL_v55 .. _v59) */
        if (m > 0) FEApp_Message(m);   /* (0x32a: 0x512e70) */
    } else if (!strcmp(cb, "CHANGEDECAL")) {
        /* FEPaintShopCBL_v55 0x510a80: the race number's dialog (0x513b10: string 0xfb, digits, 2 at most) */
        dialog_open(DLG_INPUT, FEStrings_Get(0xfb), 0x2be, 0x2bf);
        s_dialog.digits = true;
        s_dialog.cap = 2;
        s_dialog.purpose = DLG_FOR_DECAL;
    } else if (!strcmp(cb, "DEFAULTSETTINGS")) {
        FEData_DefaultSetup();
    } else if (!strcmp(cb, "SAVESETTINGS") || !strcmp(cb, "LOADSETTINGS")) {
        FEData_KeepSetup(cb[0] == 'L');
    } else if (!strcmp(cb, "QUICKFIX")) {
        int m = FEData_QuickFix();   /* FEQuickFixCallBackList 0x50e9f0 */
        if (m > 0) FEApp_Message(m);
    } else if (!strcmp(cb, "SELLCAR")) {
        /* FEGarageCallBackList_v57 0x50f590: not the only car (the message 0x3c); else its confirmation
           panel (+0x2d8 slot 0x50) and the sale at the car's value: pending (0x4f83f0) */
        Save *sv = FESave_Current();
        if (sv && 1 < sv->ncars - Market_Listed()) {
            /* (+0x2d8: FEDialogInputCancel 0x513980(800, digits, 8): the asking price) */
            dialog_open(DLG_INPUT, FEStrings_Get(800), 0x2be, 0x2bf);
            s_dialog.digits = true;
            s_dialog.cap = 8;
            s_dialog.purpose = DLG_FOR_SELL;
        } else {
            FEApp_Message(0x3c);   /* (0x512d00(0x3c, 0x2be)) */
        }
    } else if (!strcmp(cb, "DONTSELLCAR")) {
        /* FEGarageCallBackList_v58 0x50f610: the garage's car taken off the market (0x4f88e0) */
        Market_Unlist(FEData_OwnedIndex("OwnedCars.Car"));
        FESave_Write();
    } else if (!strcmp(cb, "TOURNREMAININC") || !strcmp(cb, "TOURNREMAINDEC")) {
        /* the pre-race screen's arrows (FEPreRaceCallBackList): the tournament's next or last race left */
        FEAccessor *a = FEAccess_Find("Tournament.RemainingTracks");
        if (a) FEAccess_SetPos(a, FEAccess_Pos(a) + (cb[11] == 'I' ? 1 : -1));
    } else if (!strncmp(cb, "SETTYPE", 7) && strcmp(cb, "SETTYPECAREERMODE")) {
        s_gametype = 0;   /* (FEMainScreen's 0x5107a0 .. 0x510800: not the career) */
    } else if (!strcmp(cb, "SETTYPECAREERMODE")) {
        s_gametype = 4;
        /* FEMainScreen_v63 0x510430: the career (Game.GameType 4, the car lists again), its tournaments when
           it was started (PlayerInfo's 0x691dd5), else its first screen, which starts it */
        Save *sv = FESave_Current();
        if (sv && sv->flags[0]) {
            FEApp_GotoScreen("tournament", true);
        } else {
            FEApp_GotoScreen("careerfirst", true);
            if (sv) sv->flags[0] = 1;
        }
    } else if (!strcmp(cb, "QUIT")) {
        s_result = FE_QUIT;
    } else if (!strcmp(cb, "PLAYGAME")) {
        if (!can_play()) return;
        /* The race description the screens leave in the options (0x4d2e70's end; the data areas'
           accessors pending: fe.txt's as the original's defaults). */
        char v[64];
        (void)v;
        s_tour_race = -1;
        if (play_mode() == 2) {
            /* the career's race: the tournament's next track, the player's chosen car (the race's laps and
               opponents from the tournament: pending, the options') */
            const char *t = FEData_TourTrack();
            const char *c = FEAccess_CarFile(FEAccess_Find("OwnedCars.Car"));
            if (!t || !c) return;
            s_tour_race = FEData_TourRace();   /* (before the laps: they're its) */
            s_owned_race = FEData_OwnedIndex("OwnedCars.Car");
            snprintf(s_track, sizeof s_track, "%s", t);
            snprintf(s_car, sizeof s_car, "%s", c);
            s_havecol = FEAccess_CarColours(FEAccess_Find("OwnedCars.Car"), s_colours);
            PENDING_NOTE(0x4d2e70, "the career race's laps and opponents from its tournament (the port: seven)");
        } else {
            snprintf(s_track, sizeof s_track, "%s", FEData_Track());
            snprintf(s_car, sizeof s_car, "%s", FEData_Car());
            s_havecol = FEAccess_CarColours(FEAccess_Find("zMergedList.Car"), s_colours);
            s_owned_race = FEData_OwnedIndex("zMergedList.Car");
        }
        s_laps = s_tour_race >= 0 ? FEData_TourLaps() : fe_int("num_laps", 1);
        s_opponents = s_tour_race >= 0 ? 7 : FEData_Opponents();   /* (the standings' eight racers) */
        s_result = FE_PLAY;
    } else {
        char m[160];
        snprintf(m, sizeof m, "front end: callback %s (0x50cd90, pending)", cb);
        plat_log(m);
    }
}

static bool is_combo(const FertControl *c) { return !strcmp(c->id, "NFS5.ComboBoxSlider"); }
/* A colour box (FEColorBox: its swatch and the selection box pend; a click steps its colour). */
static bool is_colourbox(const FertControl *c) { return !strcmp(c->id, "NFS5.ColorBox"); }

static void on_up(FertControl *c)
{
    if (c->feflags & 0x10) return;   /* 0x4c7a10: disabled */
    if (!strcmp(c->id, "NFS5.CARSETTINGBUTTON")) {   /* FECarSettingButton: its accessor a step on */
        FEAccessor *a = FEAccess_Find(data_str(c, "accessor"));
        if (a) FEAccess_SetPos(a, FEAccess_Pos(a) + data_int_of(c, "step"));
        return;
    }
    if (is_combo(c) || is_colourbox(c)) {
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
        /* (0x4c82c0: no rect, so no hit, with data.nosize 1, the passive controls': pictures, labels,
           rectangles, watches) */
        if (data_int_of(c, "nosize") == 1) continue;
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
        if (target && type == 4 && listbox_click(target, w->mouse[0], w->mouse[1])) break;
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
    if (s_dialog.up) {   /* (the dialog's the only one taking the mouse and the keys) */
        uint8_t k[16];
        int nk = plat_key_presses(k, 16);
        for (int i = 0; i < nk && s_dialog.up; i++) dialog_key(k[i]);
        int hit = (changed & 1) && !(b & 1) ? dialog_button_hit(mx, my) : 0;
        if (hit && s_dialog.up) {
            s_dialog.up = false;
            s_dialog.done = hit;
        }
        return;
    }
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
    uint32_t col[4];
    bool havecol = false;
    for (int i = 0; w && i < w->nchildren; i++) {
        FertControl *c = w->children[i];
        if (strcmp(c->id, "NFS5.3DCAR")) continue;
        const char *pt = data_str(c, "positiontype");
        if (pt && atoi(pt) != 0) continue;
        FEAccessor *acc = FEAccess_Find(data_str(c, "accessor"));
        car = FEAccess_CarFile(acc);
        havecol = FEAccess_CarColours(acc, col);
        if (acc) FE3D_SetIndex(FEAccess_Pos(acc));
    }
    FE3D_SetCar(car, havecol ? col : NULL);
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
/* The account (FEAccountDisplayControl_v15 0x4bd180): its parts, then the player's money (Tournament_Data's
   "CASH", 0x5058a0) in Univers7b, one character at a time from the rect's right edge leftwards (9 a
   digit, 3 a comma). */
static bool account_draw(FertControl *c, int32_t ox, int32_t oy)
{
    FEControl_Draw(c, ox, oy);   /* 0x4c7e30 */
    Save *sv = FESave_Current();
    if (!sv) return true;
    char path[300];
    FE_ResolvePath("Univers7b.ffn", path, sizeof path);   /* (0x4db960 "Univers7b") */
    HudFont *font = FEFont_Get(path);
    if (!font) return true;
    int32_t r[4];
    FertControl_rect(c, r);
    int32_t pos[2] = {0, 0};
    FertAttr *pa = FertAttrList_find(&c->attrs, "pos");
    if (pa) FertAttr_point(pa, pos);
    const char *t = FE_MoneyText(sv->money);
    float x = (float)(r[2] + ox - 1);
    for (int i = (int)strlen(t) - 1; 0 <= i; i--) {
        char *ch = FE2D_Alloc(2);
        if (!ch) break;
        ch[0] = t[i], ch[1] = 0;
        FE2D_Text(font, x, (float)(pos[1] + oy + 3), 2, 0xffffffffu, ch, 1.0f, 1.0f);   /* (0x4d8280: white at the fade) */
        x += t[i] == ',' ? -3.0f : -9.0f;
    }
    return true;
}

/* Text broken into lines of width (0x522900 -> 0x522e80; the port breaks at spaces, greedily), drawn
   from (x0, y) one font height apart, aligned 0 left, 1 centred, 2 right (0x522b10); its height. */
static float draw_wrapped(HudFont *font, const char *text, float x0, float y, float width, int align, uint32_t colour, bool draw)
{
    float y0 = y, fx, fy, fw, fh;
    FONT_extent(font->font, "X", &fx, &fy, &fw, &fh);
    float lh = fh > 0 ? fh : 10.0f;
    char line[256];
    const char *p = text;
    while (*p) {
        int n = 0, last = -1;
        while (p[n] && p[n] != '\n') {
            char t[256];
            int m = n + 1 < 255 ? n + 1 : 255;
            memcpy(t, p, (size_t)m);
            t[m] = 0;
            FONT_extent(font->font, t, &fx, &fy, &fw, &fh);
            if (fw > width && last >= 0) break;
            if (p[n] == ' ') last = n;
            n++;
            if (n >= 255) break;
        }
        int len = n;
        if (p[n] && p[n] != '\n' && last >= 0) len = last;
        memcpy(line, p, (size_t)len);
        line[len] = 0;
        FONT_extent(font->font, line, &fx, &fy, &fw, &fh);
        float x = align == 1 ? x0 + (width - fw) * 0.5f : align == 2 ? x0 + width - fw : x0;
        char *copy = draw ? FE2D_Alloc(len + 1) : NULL;
        if (copy) {
            memcpy(copy, line, (size_t)len + 1);
            FE2D_Text(font, x, y, 0, colour, copy, 1.0f, 1.0f);
        }
        y += lh;
        p += len;
        while (*p == ' ' || *p == '\n') p++;
    }
    return y - y0;
}

/* The one-button message (FEDialogOneItemSelfDeleting 0x512d00 on FEDialog 0x511c60): the string's text
   wrapped at 300 in a box of 320 at x 160, centred on 240 by its height + 0x15 (0x512ea0), an OK button
   (string 0x2be, 80 x 18 at 120 across, under the text) that closes it. Its draw (0x512310): a gouraud body
   5 wider each way, four bevel edges, the text centred in black, a drop shadow; over every screen, the
   only one taking the mouse while it's up. (The button's own look: the port's red bar.) */
static void dialog_open(int kind, const char *text, int b1, int b2)
{
    memset(&s_dialog, 0, sizeof s_dialog);
    s_dialog.up = true;
    s_dialog.kind = kind;
    snprintf(s_dialog.text, sizeof s_dialog.text, "%s", text ? text : "");
    s_dialog.b1 = b1;
    s_dialog.b2 = b2;
    s_dialog.x = 160.0f, s_dialog.w = 320.0f;
    s_dialog.tw = 300.0f;   /* 0x43960000 */
    s_dialog.h = 100.0f;
    s_dialog.y = 240.0f - 50.0f;
}

void FEApp_Message(int string_id)
{
    dialog_open(DLG_ONE, FEStrings_Get(string_id), 0x2be, 0);
}

static HudFont *dialog_font(void)
{
    char fp[300];
    FE_ResolvePath("Univers11b.ffn", fp, sizeof fp);   /* (0x69a060: the dialogs' font) */
    return FEFont_Get(fp);
}

/* The places (relative to the box) of the buttons and the input: the one-button's under the text
   (0x512ea0); the others' (0x5133a0, 0x513b50) stacked from the text's height + 20: the input, then
   0x12 lower the first button, 0x15 lower the second; the box that and 0x15, centred on 240. */
static float dialog_layout(float *b1y, float *b2y, float *iny)
{
    HudFont *f = dialog_font();
    float th = f && f->font ? draw_wrapped(f, s_dialog.text, 0, 0, s_dialog.tw, 1, 0, false) : 20.0f;
    if (s_dialog.kind == DLG_ONE) {
        s_dialog.h = th + 18.0f + 0x15;   /* (the text, the button, 0x15) */
        s_dialog.y = (float)(0xf0 - (int)(th + 0x15) / 2);
        *b1y = s_dialog.h - 18.0f - 4.0f;
        *b2y = *iny = 0;
        return th;
    }
    int y = (int)(th + 20.0f);   /* (__ftol of the height + 20, 0x5b2470) */
    *iny = (float)y;
    if (s_dialog.kind == DLG_INPUT) y += 0x12;
    *b1y = (float)y;
    y += 0x15;
    *b2y = (float)y;
    y += 0x15;
    s_dialog.h = (float)y;
    s_dialog.y = (float)(0xf0 - y / 2);
    return th;
}

static int dialog_button_hit(int32_t mx, int32_t my)
{
    float b1y, b2y, iny;
    dialog_layout(&b1y, &b2y, &iny);
    float bx = s_dialog.x + 120.0f, y = s_dialog.y;
    if (bx <= mx && mx < bx + 80.0f && y + b1y <= my && my < y + b1y + 18.0f) return 1;
    if (s_dialog.kind != DLG_ONE && bx <= mx && mx < bx + 80.0f && y + b2y <= my && my < y + b2y + 18.0f) return 2;
    return 0;
}

/* A key to the dialog (FEDialogInput_v4 0x514530 as the input's; FEDialogInputCancel_v1 0x5140e0 takes
   its Enter as OK): the keys as their characters (the port maps the digits, the keypad's and the letters;
   the original reads WM_CHAR), backspace the last one off, Enter done; a digits-only input takes 0..9 up
   to its cap. Each key the front end's sound 9. */
static void dialog_key(uint8_t code)
{
    if (s_dialog.kind != DLG_INPUT) return;
    static const char row1[] = "1234567890", rowq[] = "qwertyuiop", rowa[] = "asdfghjkl", rowz[] = "zxcvbnm";
    static const uint8_t pad[] = {0x52, 0x4f, 0x50, 0x51, 0x4b, 0x4c, 0x4d, 0x47, 0x48, 0x49};   /* keypad 0..9 */
    int ch = 0;
    if (code >= 0x02 && code <= 0x0b) ch = row1[code - 2];
    else if (code >= 0x10 && code <= 0x19) ch = rowq[code - 0x10];
    else if (code >= 0x1e && code <= 0x26) ch = rowa[code - 0x1e];
    else if (code >= 0x2c && code <= 0x32) ch = rowz[code - 0x2c];
    else if (code == 0x39) ch = ' ';
    else if (code == 0x0e) ch = 8;
    else if (code == 0x1c || code == 0x9c) ch = 13;
    for (int i = 0; i < 10; i++)
        if (code == pad[i]) ch = '0' + i;
    if (!ch) return;
    PENDING_NOTE(0x4aab80, "the front end's key sound (9)");
    size_t n = strlen(s_dialog.input);
    if (ch == 8) {
        if (n) s_dialog.input[n - 1] = 0;
    } else if (ch == 13) {
        s_dialog.up = false;
        s_dialog.done = 1;
    } else if ((!s_dialog.digits || (ch >= '0' && ch <= '9')) && (int)n < s_dialog.cap) {
        s_dialog.input[n] = (char)ch;
        s_dialog.input[n + 1] = 0;
    }
}

static void dialog_button(float bx, float by, int str, HudFont *f)
{
    FE2D_DepthStep();
    FE2D_Gouraud(bx, by, 80.0f, 18.0f, 0xffe00c0cu, 0xff820000u, 0xffe00c0cu, 0xff820000u);
    const char *t = FEStrings_Get(str);
    if (f && f->font && t) {
        FE2D_DepthStep();
        draw_wrapped(f, t, bx, by + 2.0f, 80.0f, 1, 0xffffffffu, true);
    }
}

static void dialog_draw(void)
{
    if (!s_dialog.up) return;
    float b1y, b2y, iny;
    dialog_layout(&b1y, &b2y, &iny);
    float x = s_dialog.x, y = s_dialog.y, w = s_dialog.w, h = s_dialog.h, c = 5.0f, c2 = 10.0f;
    FE2D_DepthStep();
    FE2D_Gouraud(x - c, y - c, w + c2, h + c2, 0xff82a2b0u, 0xff82a2b0u, 0xe5fed66bu, 0xe5fed66bu);
    const float top[8] = {x - c, y - c, x + w + c, y - c, x, y, x + w, y};
    const uint32_t ct[4] = {0xcccfdce0u, 0xb2dce6e9u, 0xcccfdce0u, 0xb29dbed0u};
    const float left[8] = {x - c, y - c, x, y, x - c, y + h + c, x, y + h};
    const uint32_t cl[4] = {0xb29cd1ffu, 0x809cd1ffu, 0xb2ffffffu, 0x80ffffffu};
    const float right[8] = {x + w, y, x + w + c, y - c, x + w, y + h, x + w + c, y + h + c};
    const uint32_t cr[4] = {0xb2193b6au, 0xe5193b6au, 0xb22a1711u, 0xe52a1711u};
    const float bot[8] = {x, y + h, x + w, y + h, x - c, y + h + c, x + w + c, y + h + c};
    const uint32_t cb[4] = {0xb261492au, 0xb261492au, 0xe561492au, 0xe561492au};
    FE2D_DepthStep();
    FE2D_Quad(top, ct);
    FE2D_Quad(left, cl);
    FE2D_Quad(right, cr);
    FE2D_Quad(bot, cb);
    FEStyle_DropShadow(x - c, y - c, w + c2, h + c2, 0xff, 4, 4);
    HudFont *f = dialog_font();
    FE2D_DepthStep();
    if (f && f->font) draw_wrapped(f, s_dialog.text, x + c2, y + c2, s_dialog.tw, 1, 0xff000000u, true);
    if (s_dialog.kind == DLG_INPUT && f && f->font) {
        /* FEDialogInput_v11 0x5143c0: its text in black centred on its place (160 across), (width + 8) / 2
           to its left; the cursor a 1 x 14 bar at its end, on for 32 of every 64 ticks. */
        float fx, fy, fw = 0, fh;
        FONT_extent(f->font, s_dialog.input, &fx, &fy, &fw, &fh);
        float cx = x + 160.0f, ty = y + iny;
        char *copy = FE2D_Alloc((int)strlen(s_dialog.input) + 1);
        if (copy) {
            strcpy(copy, s_dialog.input);
            FE2D_Text(f, cx - (fw + 8.0f) * 0.5f, ty, 0, 0xff000000u, copy, 1.0f, 1.0f);
        }
        uint32_t ticks = (uint32_t)((uint64_t)app_ticks_ms() * 128u / 1000u);   /* TIMER_ticks */
        if ((ticks & 0x3f) < 0x20) FE2D_Gouraud((fw + 8.0f) * 0.5f + cx - 8.0f, ty - 1.0f, 1.0f, 14.0f, 0xff000000u, 0xff000000u, 0xff000000u, 0xff000000u);
    }
    dialog_button(x + 120.0f, y + b1y, s_dialog.b1, f);
    if (s_dialog.kind != DLG_ONE) dialog_button(x + 120.0f, y + b2y, s_dialog.b2, f);
}

/* A word-wrapped watch (FEWordWrapAccessorWatch_v15 0x5225e0 -> 0x522b10): its accessor's text broken into
   lines of the control's width (0x522900 -> 0x522e80; the port breaks at spaces, greedily), each line one
   font height lower, aligned by data.align (0 left, 1 centred, 2 right). (Its typing animation pends.) */
static bool wordwrap_draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (c->feflags & 0x20) return true;
    const char *path = data_str(c, "accessor");
    FEAccessor *a = FEAccess_Find(path);
    int k = FertControl_findPart(c, "label");
    if (!a || k < 0) return true;
    char arg[32] = "";
    const char *pp = path ? strchr(path, '(') : NULL;
    if (pp) {
        snprintf(arg, sizeof arg, "%s", pp + 1);
        char *e = strchr(arg, ')');
        if (e) *e = 0;
    }
    const char *text = FEAccess_TextArg(a, arg);
    FertAttr *f = FertAttrList_find(&c->parts[k]->attrs, "fontPathName");
    char fp[300];
    FE_ResolvePath(f ? FertAttr_str(f) : "", fp, sizeof fp);
    HudFont *font = FEFont_Get(fp);
    if (!font || !font->font || !text) return true;
    FertAttr *fc = FertAttrList_find(&c->parts[k]->attrs, "foreColor");
    uint32_t colour = fc ? FertAttr_color(fc) : 0xffffffffu;
    int32_t r[4];
    control_rect(c, r);
    float width = (float)(r[2] - r[0]);
    int align = data_int_of(c, "align");
    draw_wrapped(font, text, (float)(r[0] + ox), (float)(r[1] + oy), width, align, colour, true);
    return true;
}

/* A car setting's slider (FECarSettingSlider): a knob along its width at its accessor's place (the port's
   bar; its shapes, weeicons.fsh, pend). */
static bool setting_draw(FertControl *c, int32_t ox, int32_t oy)
{
    FEAccessor *a = FEAccess_Find(data_str(c, "accessor"));
    if (!a) return true;
    int32_t pos[2] = {0, 0};
    FertAttr *pa = FertAttrList_find(&c->attrs, "pos");
    if (pa) FertAttr_point(pa, pos);
    int w = data_int_of(c, "width"), h = data_int_of(c, "height"), n = FEAccess_Count(a);
    float f = n > 1 ? (float)FEAccess_Pos(a) / (float)(n - 1) : 0;
    FE2D_DepthStep();
    FE2D_Rect((float)(pos[0] + ox) + f * (float)(w - 6), (float)(pos[1] + oy), 6.0f, (float)h, 0xff202020u);
    return true;
}

/* A list box (NFS5.ListBox: FEComboBox always open, +0x1dd; its list's draw, 0x4c53e0 from LAB_004c60ce):
   its accessor's rows from the first shown down its box (data.width x data.selectionboxmaxheight), each
   drawn by its accessor (the parts lists': FEPartsAllPartsAccessor_v15 0x514ef0, FEPartsShoppingBasket_v15
   0x515800): a category's label (its string centred between two lines) where the category changes, the
   chosen row on the selected colour, a group's header with its icon (WeeIcons.fsh 3 shut / 0xb open), the
   part's name and its price right of ID_PRICEPOS. The row chosen is kept in view (the scroll bar the
   original keeps, +0x1bc, pends). */
static struct { FertControl *c; int first; } s_lists[8];

static int *list_first(FertControl *c)
{
    for (int i = 0; i < 8; i++)
        if (s_lists[i].c == c) return &s_lists[i].first;
    for (int i = 0; i < 8; i++)
        if (!s_lists[i].c || i == 7) {
            s_lists[i].c = c, s_lists[i].first = 0;
            return &s_lists[i].first;
        }
    return &s_lists[0].first;
}

static HudFont *list_font(void)
{
    char fp[300];
    FE_ResolvePath("Univers8b.ffn", fp, sizeof fp);   /* (data.fontpath) */
    return FEFont_Get(fp);
}

static void list_text(HudFont *f, float x, float y, const char *t, uint32_t colour, int align)
{
    if (!f || !f->font || !t) return;
    float fx, fy, fw, fh;
    FONT_extent(f->font, t, &fx, &fy, &fw, &fh);
    char *copy = FE2D_Alloc((int)strlen(t) + 1);
    if (!copy) return;
    strcpy(copy, t);
    FE2D_Text(f, align == 2 ? x - fw : align == 1 ? x - fw * 0.5f : x, y, 0, colour, copy, 1.0f, 1.0f);
}

static bool listbox_draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (c->feflags & 0x20) return true;
    FEAccessor *a = FEAccess_Find(data_str(c, "accessor"));
    if (!a) return true;
    int32_t r[4];
    control_rect(c, r);
    float x = (float)(r[0] + ox), y = (float)(r[1] + oy), w = (float)data_int_of(c, "width"), h = (float)data_int_of(c, "selectionboxmaxheight");
    int n = FEAccess_Count(a), pos = FEAccess_Pos(a), *first = list_first(c);
    /* the chosen row in view */
    if (pos < *first) *first = pos;
    for (;;) {
        int used = 0;
        for (int i = *first; i <= pos && i < n; i++) {
            FEPartsRow row;
            if (FEData_PartsRow(a, i, &row)) used += row.height;
        }
        if (used <= (int)h || *first >= pos) break;
        (*first)++;
    }
    FertControl *pp = control_named(s_parent ? s_parent : FEApp_Top(), "ID_PRICEPOS");
    if (!pp) pp = control_named(FEApp_Top(), "ID_PRICEPOS");
    int32_t pr[4] = {0, 0, 0, 0};
    if (pp) control_rect(pp, pr);
    float px = pp ? (float)(pr[0] + ox - 4) : x + w - 4.0f;
    HudFont *f = list_font();
    char icons[300];
    FE_ResolvePath("WeeIcons.fsh", icons, sizeof icons);
    FEShapes_Load(icons);
    float yy = y;
    FE2D_DepthStep();
    for (int i = *first; i < n; i++) {
        FEPartsRow row;
        if (!FEData_PartsRow(a, i, &row)) continue;
        if (yy + row.height > y + h + 1) break;
        bool chosen = i == pos;
        if (row.label) {   /* 0x5186c0: two lines and the category's name centred */
            FE2D_Gouraud(x, yy, w, 1.0f, 0xff000000u, 0xff000000u, 0xff000000u, 0xff000000u);
            list_text(f, x + w * 0.5f, yy + 1.0f, FEStrings_Get(row.category), 0xff000000u, 1);
            FE2D_Gouraud(x, yy + 12.0f, w, 1.0f, 0xff000000u, 0xff000000u, 0xff000000u, 0xff000000u);
            yy += 13.0f;
        }
        uint32_t bg = chosen ? 0xffbc0000u : row.header ? 0x60000000u : 0;   /* (highlightcolorselected) */
        if (bg) FE2D_Gouraud(x, yy, w, 13.0f, bg, bg, bg, bg);
        uint32_t fg = chosen ? 0xffffffffu : row.dim ? 0x40000000u : 0xff000000u;
        float tx = x + 3.0f;
        if (row.header) {
            FeImage *ic = FEShapes_Image(icons, row.open ? 0xb : 3);
            if (ic) {
                FE2D_Image(ic, x, yy, (float)ic->w, (float)ic->h, 0xffffffffu, 0, false);
                tx = x + (float)ic->w + 2.0f;
            }
            if (!chosen) fg = 0xffffffffu;
        }
        list_text(f, tx, yy + 1.0f, row.text, fg, 0);
        if (row.has_price) {
            char pb[24];
            snprintf(pb, sizeof pb, "%d", (int)row.price);   /* ("%d" 0x5cd1b0) */
            list_text(f, px, yy + 1.0f, pb, fg, 2);
        }
        yy += 14.0f;
    }
    return true;
}

/* A press on a list box chooses its row there (FEComboBox's 0x4c66c0 on its list). */
static bool listbox_click(FertControl *c, int32_t mx, int32_t my)
{
    if (strcmp(c->id, "NFS5.ListBox")) return false;
    FEAccessor *a = FEAccess_Find(data_str(c, "accessor"));
    if (!a) return true;
    int32_t r[4];
    control_rect(c, r);
    (void)mx;
    int n = FEAccess_Count(a), *first = list_first(c);
    int yy = r[1];
    for (int i = *first; i < n; i++) {
        FEPartsRow row;
        if (!FEData_PartsRow(a, i, &row)) continue;
        int top = yy + (row.label ? 13 : 0);
        if (my >= top && my < top + 14) {
            FEAccess_SetPos(a, i);
            PENDING_NOTE(0x4aab80, "the front end's click sound (5)");
            return true;
        }
        yy += row.height;
    }
    return true;
}

static bool indicator_draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (!strcmp(c->id, "NFS5.CarSettingSlider")) return setting_draw(c, ox, oy);
    if (!strcmp(c->id, "NFS5.ACCOUNTTHING")) return account_draw(c, ox, oy);
    if (!strcmp(c->id, "NFS5.WORDWRAPACCESSORWATCH")) return wordwrap_draw(c, ox, oy);
    if (!strcmp(c->id, "NFS5.ListBox")) return listbox_draw(c, ox, oy);
    if (is_colourbox(c)) {
        /* FEComboBox's draw (0x4c53e0) for a colour box (+0x1de): the bar, then its colour (its slot 0x74,
           the accessor's) as a 20 x 10 swatch 40 from its right and 4 down, outlined in black. (The custom
           colour picker, FEColorSelectBox_v17 0x4c3800, and the drop-down pend.) */
        if (c->feflags & 0x20) return true;
        FEControl_Draw(c, ox, oy);
        FEAccessor *a = FEAccess_Find(data_str(c, "accessor"));
        if (!a) return true;
        int32_t r[4];
        control_rect(c, r);
        float x = (float)(r[2] + ox - 0x28), y = (float)(r[1] + oy + 4);
        uint32_t col = FEAccess_Colour(a) | 0xff000000u, k = 0xff000000u;
        FE2D_DepthStep();
        FE2D_Gouraud(x, y, 20.0f, 10.0f, col, col, col, col);
        const float t[8] = {x, y, x + 20.0f, y, x, y + 1.0f, x + 20.0f, y + 1.0f}, bm[8] = {x, y + 9.0f, x + 20.0f, y + 9.0f, x, y + 10.0f, x + 20.0f, y + 10.0f};
        const float l[8] = {x, y, x + 1.0f, y, x, y + 10.0f, x + 1.0f, y + 10.0f}, rt[8] = {x + 19.0f, y, x + 20.0f, y, x + 19.0f, y + 10.0f, x + 20.0f, y + 10.0f};
        const uint32_t kk[4] = {k, k, k, k};
        FE2D_DepthStep();
        FE2D_Quad(t, kk);
        FE2D_Quad(bm, kk);
        FE2D_Quad(l, kk);
        FE2D_Quad(rt, kk);
        return true;
    }
    if (strcmp(c->id, "NFS5.GouraudIndicatorBar")) return false;
    if (c->feflags & 0x20) return true;
    const char *path = data_str(c, "accessor");
    FEAccessor *a = FEAccess_Find(path);
    if (!a) return true;
    char arg[32] = "";
    const char *pp = path ? strchr(path, '(') : NULL;
    if (pp) {
        snprintf(arg, sizeof arg, "%s", pp + 1);
        char *e = strchr(arg, ')');
        if (e) *e = 0;
    }
    float pct = (float)FEAccess_Percent(a, arg);
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

/* A control of the screen by name (FEApplication's 0x5287b0), in its panels too. */
static FertControl *control_named(FertControl *w, const char *name)
{
    for (int i = 0; w && i < w->nchildren; i++) {
        FertControl *c = w->children[i];
        if (!strcmp(c->name, name)) return c;
        FertControl *s = c->sub ? control_named(c->sub, name) : NULL;
        if (s) return s;
    }
    return NULL;
}

static void set_flag(FertControl *w, const char *name, uint32_t flag, bool on)   /* 0x4c79c0 .. 0x4c7a80 */
{
    FertControl *c = control_named(w, name);
    if (!c) return;
    uint32_t f = c->feflags;
    if (on && (c->fecaps & flag)) c->feflags |= flag;
    else if (!on) c->feflags &= ~flag;
    if (f != c->feflags) refresh_state(c);
}
/* 0x50cf40: enabled or not (0x10); 0x50cfd0: hidden or not (0x20). */
static void enable(FertControl *w, const char *name, bool on) { set_flag(w, name, 0x10, !on); }
static void hide(FertControl *w, const char *name, bool on) { set_flag(w, name, 0x20, on); }

/* The garage's callback list's update (FEGarageCallBackList_v1 0x50f250): its buttons by the car shown
   (OwnedCars.Car) and the mode (the used and new cars, the parts and the repairs the career's; a car for
   sale shows "remove from market" for "sell"); the asking price's dialog closed with OK and a text: the
   car put up at it (0x4f8760), the market's turn (0x4f7350) in the career's year. */
static void garage_update(FertControl *w)
{
    Save *sv = FESave_Current();
    int car = FEData_OwnedIndex("OwnedCars.Car");
    bool has = sv && car >= 0 && car < sv->ncars, listed = has && sv->cars[car].rec[0x691];
    bool career = s_gametype == 4;
    enable(w, "ID_CHOOSECARBOX", has);
    enable(w, "ID_CARSETUP", has);
    enable(w, "ID_PAINTBUTTON", has);
    enable(w, "ID_SETTINGSBUTTON", has);
    enable(w, "ID_USEDCARSBUTTON", career);
    enable(w, "ID_NEWCARSBUTTON", career);
    enable(w, "ID_PARTSBUTTON", has && career);
    enable(w, "ID_REPAIRBUTTONGARAGE", has && career);
    if (career) {
        hide(w, "ID_REMOVEFMBUTTON", !has || !listed);
        enable(w, "ID_SELLCARBUTTON", has);
        hide(w, "ID_SELLCARBUTTON", listed && has);
    } else {
        hide(w, "ID_REMOVEFMBUTTON", true);
        enable(w, "ID_SELLCARBUTTON", false);
        hide(w, "ID_SELLCARBUTTON", false);
    }
    if (s_dialog.purpose == DLG_FOR_SELL && !s_dialog.up) {
        if (s_dialog.done == 1 && s_dialog.input[0]) {
            const char *t = s_dialog.input;   /* (0x523aa0: "0x" hex, else atol) */
            unsigned hx;
            long price = strlen(t) >= 3 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X') && sscanf(t + 2, "%x", &hx) == 1 ? (long)hx : atol(t);
            Market_List(car, (int32_t)price);
            Market_Update(FEData_CareerYear());
            FESave_Write();
        }
        s_dialog.purpose = DLG_FOR_NONE;
    }
}

/* The tournament screen's update's offers (FETournamentCallBackList_v1 0x5053a0): the car with an offer
   asked about (+0x2e4: FEDialogTwoItem 0x513370, yes 700 / no 0x2bd, the text 0x327 of the offer, the
   car's name, the asking price); yes takes it (0x501740, the garage at its first car), no turns it down
   (0x4f8cc0); then the next one. (The screen's buttons it disables meanwhile: the dialog has the mouse.) */
static void tourny_update(FertControl *w)
{
    if (w != s_tourny_win) return;
    if (s_dialog.purpose == DLG_FOR_OFFER) {
        if (s_dialog.up) return;
        Save *sv = FESave_Current();
        if (s_dialog.done == 1) {
            Market_Accept(s_tourny_offer);
            FEAccessor *a = FEAccess_Find("OwnedCars.Car");
            if (a) FEAccess_SetPos(a, 0);
        } else {
            Market_Decline(s_tourny_offer);
        }
        (void)sv;
        FESave_Write();
        s_dialog.purpose = DLG_FOR_NONE;
        s_tourny_offer = Market_FirstOffer();
        return;
    }
    if (s_tourny_offer < 0 || s_dialog.up) return;
    Save *sv = FESave_Current();
    if (!sv || s_tourny_offer >= sv->ncars) {
        s_tourny_offer = -1;
        return;
    }
    const uint8_t *r = sv->cars[s_tourny_offer].rec;
    int32_t offer, price;
    memcpy(&offer, r + 0x694, 4);
    memcpy(&price, r + 0x68c, 4);
    char text[300];
    snprintf(text, sizeof text, FEStrings_Get(0x327), offer, (const char *)r + 4 + 2, price);
    dialog_open(DLG_TWO, text, 700, 0x2bd);
    s_dialog.purpose = DLG_FOR_OFFER;
}

/* The used car screen's update (FEUsedCarCallbackList_v1 0x4f6930): buying, the test drive and the car
   box with a car to show; the damage panel or the parts panel and its list (SHOWPARTS / SHOWDAMAGE), the
   button for the other. */
static void usedcar_update(FertControl *w)
{
    FEAccessor *u = FEAccess_Find("UsedCar.Car");
    bool has = u && FEAccess_Count(u) > 0;
    enable(w, "ID_BUYCARBUTTON", has);
    enable(w, "ID_TESTDRIVEUSEDBUTTON", has);
    enable(w, "ID_CHOOSEUSEDBOX", has);
    bool parts = has && s_used_parts;
    hide(w, "ID_SHOWPARTSBUTTON", !has || parts);
    hide(w, "ID_SHOWDAMAGEBUTTON", has ? !parts : true);
    hide(w, "ID_PARTSBOX", !parts);
    if (has) enable(w, parts ? "ID_SHOWDAMAGEBUTTON" : "ID_SHOWPARTSBUTTON", true);
    else enable(w, "ID_SHOWPARTSBUTTON", false);
    const char *want = parts ? "ID_PARTSPANEL" : "ID_USEDCARDAMAGEPANEL";
    FertControl *pc = control_named(w, want);
    if (pc && (!pc->sub || (pc->feflags & 0x20))) select_panel(w, want);
}

static void screen_update(FertControl *w)   /* the screen's callback list's update (its v1) */
{
    const char *cbl = screen_attr(w, "callbacklist");
    if (!strcmp(cbl, "ID_GARAGECBL")) garage_update(w);
    else if (!strcmp(cbl, "ID_USEDCARCBL")) usedcar_update(w);
    else if (!strcmp(cbl, "ID_BUYPARTSCBL")) {
        bool buy, sell, basket, sel;   /* FEPurchasePartsCBL_v1 0x518cf0 */
        bool part = FEData_PartsState(&buy, &sell, &basket, &sel);
        enable(w, "ID_PARTSDESC", part);
        enable(w, "ID_BUYPARTBUTTON", buy);
        enable(w, "ID_SELLPARTBUTTON", sell);
        enable(w, "ID_CLEARLIST", basket);
        enable(w, "ID_REMOVEFROMLIST", sel);
        enable(w, "ID_PURCHASEPART", basket);
        enable(w, "ID_PARTDESCRIPBUTTON", buy);
    }
    else if (!strcmp(cbl, "ID_PAINTSHOPCBL")) {
        FEData_PaintShop();
        if (s_dialog.purpose == DLG_FOR_DECAL && !s_dialog.up) {
            /* (the race number's dialog closed: OK with a text its number, else none, -1) */
            if (s_dialog.done == 1) {
                FEAccessor *dn = FEAccess_Find("OwnedCar.DecalNum");
                if (dn) FEAccess_SetPos(dn, (s_dialog.input[0] ? atoi(s_dialog.input) : -1) + 1);
            }
            s_dialog.purpose = DLG_FOR_NONE;
        }
    }
    else if (!strcmp(cbl, "ID_TOURNYSCREEN")) tourny_update(w);
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
        screen_update(w);
        update_watches(w);
        update_all(w);   /* (the controls' ticks) */
        FEWindow_Draw(w);
        draw_combos(w);
    }
    if (s_bar) {
        for (int i = 0; i < s_bar->nchildren; i++) FertControl_Update(s_bar->children[i]);
        FEWindow_Draw(s_bar);   /* (0x4d2980: the bottom bar over the screens) */
    }
    dialog_draw();
    FE2D_Flush();
    View_End(g_render);
    if (tm) TexManager_end(tm);
    Render_EndFrame(g_render);
}
