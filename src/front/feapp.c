/* The front end's application (feapp.h). */
#include "front/feapp.h"
#include "render/rtex.h"
#include "front/femovie.h"
#include "front/fecarlogo.h"
#include "front/fetrackmap.h"
#include "front/fehudpreview.h"
#include "front/fecd.h"
#include "front/fecolour.h"
#include "front/fectrl.h"
#include "front/feshowcase.h"
#include "front/fechronicle.h"
#include "front/feendofera.h"
#include "front/fe2d.h"
#include "front/fe_parts.h"
#include "front/feaccess.h"
#include "front/fe3d.h"
#include "front/fesave.h"
#include "front/fecar3d.h"
#include "front/fepointer.h"
#include "front/femarket.h"
#include "front/fefactory.h"
#include "support/crt.h"
#include "game/audio.h"
#include "game/input.h"
#include "game/music.h"
#include "game/options.h"
#include "game/mission.h"
#include "game/race_car.h"
#include "real/mouse.h"
#include "real/font.h"
#include "exe.h"
#include "app.h"
#include "pending.h"
#include "render/carpages.h"
#include "platform.h"
#include "render/r3dcar.h"
#include "render/render.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { STACK = 16 };

static FertControl *s_stack[STACK];   /* +0xd8: the screens */
static char s_stack_name[STACK][64];  /* (their layouts' names) */
static char s_saved[STACK][64];       /* the stack when the race began, to come back to (0x4d2e70's) */
static int s_nsaved;
static FertControl *s_bar;            /* +0xe0: the bottom bar (FEBottomBarMenu, bottombar.lay) */
static FEResult s_result;
static char s_track[64] = "coastal";
static int s_laps = 1, s_opponents, s_traffic, s_cops;
static bool s_reverse, s_mirror;      /* 0x4fb630's third and fourth: the race's REVERSE and MIRROR */
static bool s_replay_chosen;          /* 0x699fc9: the load replay screen's start (the race a replay) */
static char s_replay_file[112];       /* its REPLAYFILENAME (option 0x110: "%s.rpl" 0x5ccebc) */
static int16_t s_traffic_slots[7];   /* the career race's own traffic cars (0x4ec590's +0x382) */
static char s_car[64] = "964";
static int s_depth;
/* The dialog up (FEDialog 0x511c60 and its kinds): one button (0x512d00), two (FEDialogTwoItem 0x5133a0),
   an input and two (FEDialogInputCancel 0x513b50). done: closed by its first button (1) or second (2). */
enum { DLG_ONE, DLG_TWO, DLG_INPUT };
enum { DLG_FOR_NONE, DLG_FOR_SELL, DLG_FOR_OFFER, DLG_FOR_DECAL, DLG_FOR_TOUR, DLG_FOR_REMOVE, DLG_FOR_KILLREPLAY, DLG_FOR_FORFEIT };
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
static FertControl *focused_input(FertControl *w);
static int s_driver_pic = 8;          /* 0x5d8f2c: the create player screen's driver */
static void inputbox_keys(FertControl *c, const uint8_t *k, int nk);
static bool inputbox_draw(FertControl *c, int32_t ox, int32_t oy);
static bool picture_draw(FertControl *c, int32_t ox, int32_t oy);
static bool picture_click(FertControl *c);
static void dialog_open(int kind, const char *text, int b1, int b2);
static bool s_sound;

bool FEApp_Sound(void)
{
    if (!s_sound) s_sound = Audio_Init();
    return s_sound;
}

static uint32_t s_colours[4];         /* the race car's colours (the description's +0x218 ..) */
static bool s_havecol;
static int s_tour_race = -1;          /* the career race being run (its index in the tournament) */
static int s_owned_race = -1;         /* the owned car raced (its index in the save) */
static int s_gametype;                /* Game.GameType (0x690ad4): 0 quick race, 1, 2 knockouts, 3 multiplayer, 4 career, 9 Factory Driver */
static FertControl *s_tourny_win;     /* the tournament screen's (FETournamentCallBackList 0x5051a0) */
static int s_tourny_offer = -1;       /* its car with an offer (+0x2f0), an owned index */
static bool s_used_parts;             /* the used car screen's parts shown, not its damage (+0x290) */
static int s_grid_pers[8], s_grid_start[9], s_ngrid = -1;   /* the knockout's and the career's grid for the race */
static float s_skill = 1.0f;
static bool s_era_race;   /* 0x6577dc of the race just run: the last race of its era's last tournament */   /* the opponents' skill factor (the career race's difficulty, +0x3cc) */
static bool s_back_locked;            /* FEApplication +0x17c (0x4d5670 / 0x4d5690): the back button off */
static bool s_testdrive;              /* 0x699fc8: the race a test drive (0x5dc1b8 its type: 6 new, 7 used, 8 setup) */
static int s_testdrive_type;          /* 0x5dc1b8 */
static bool s_factory_race;           /* the race Factory Driver's mission */
static RaceOpponent s_opp[8];         /* its opponents (0x4eb830) */
static int s_nopp = -1;
static int s_fd_cheat;                /* FEFactoryDriverCBL +0x150: Factory.MissionCheat when last seen */
static int32_t s_music_at;            /* FEMainScreen +0x290: the tick the last song started */
static bool s_cd_started;             /* FEScreenOptions +0x2a8: the shown song chosen on its first tick */
static FertControl *s_parent;         /* the window of the control an event is for */
static FertControl *s_overlay;        /* +0xe4: the screen's overlay (its "screen" id, 0x4e1010) */
static char s_overlay_id[32];
static RCamera *s_camera;
static int32_t s_mouse[2] = {-1, -1}; /* the pump's last poll (+0x14 ..) */
static uint32_t s_buttons;
/* The bottom bar's animating buttons (FEAnimatingGoToButton, NFS5.BOTTOMBARBUTTON, 0x284 bytes, vtable
   0x5b5d80): three frame runs, plain (+0x18c), hovered (+0x1a4) and pressed (+0x1bc, with
   data.seperatehovermousedown), each its first and last frame, its ticks, a wait (+0x198 ..) of its
   min .. max delay; the run going (+0x184) since +0x188. */
typedef struct { int first, last, ticks, wait, min, max; } AnimRun;
typedef struct {
    FertControl *c;
    AnimRun run[3];
    bool going;
    uint32_t since;
} AnimButton;
static AnimButton s_anims[8];
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
int FEApp_Language(void) { return language(); }

static FertControl *load_screen(const char *name);
static bool indicator_draw(FertControl *c, int32_t ox, int32_t oy);
static bool listbox_click(FertControl *c, int32_t mx, int32_t my);
static bool ko_list_click(FertControl *c, int32_t my);
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

/* A screen let go (its callback list's destructor: the victory screens' 0x506300 / 0x519f10 unlock the
   back button, 0x4d5690). */
static void screen_free(FertControl *w)
{
    const char *cbl = screen_attr(w, "callbacklist");
    if (!strcmp(cbl, "ID_VICTORYSCREEN") || !strcmp(cbl, "ID_FDVICTORYSCREEN")) s_back_locked = false;
    FertControl_free(w);
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
        if (FertControl_findState(c, "focused") >= 0) c->fecaps |= 0x80;
        if (FertControl_findState(c, "disabled") >= 0) c->fecaps |= 0x10;
        if (FertControl_findState(c, "hidden") >= 0) c->fecaps |= 0x20;
    }
    if (!strcmp(screen_attr(w, "callbacklist"), "ID_USEDCARCBL")) s_used_parts = false;   /* (0x4f6790) */
    if (!strcmp(screen_attr(w, "callbacklist"), "ID_BUYPARTSCBL")) FEData_PartsOpen();   /* (0x518ba0) */
    if (!strcmp(screen_attr(w, "callbacklist"), "IDS_ADVREPAIRCBL")) FEData_RepairOpen();   /* (0x518850) */
    if (!strcmp(screen_attr(w, "callbacklist"), "ID_OPTIONSSCREEN")) {
        /* (FEScreenOptions' constructor 0x510ed0: the menu song faded out over 1.5 s, the CD player's
           song unchosen, so its first tick plays the one shown) */
        Music_FadeOut(0x5dc);
        FECD_Select(-1);
        s_cd_started = false;
    }
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
        PENDING_NOTE(0x4d2e70, "FEApplication_Run: the data areas, the 3D rooms but the garage, movies");
        s_bar = load_screen("bottombar");
        memset(s_anims, 0, sizeof s_anims);
        s_ready = true;
    }
    Render_SetVideoMode(g_render, 640, 480, 16, 2);   /* (FEApplication_Run 0x4d2e70's) */
    s_result = FE_RUNNING;
    s_replay_chosen = false;   /* (FECallBackList's constructor 0x50c120) */
    FESave_ListReplays();      /* (the data areas anew: FEdaUserList_v0 0x4e5400's Replay.List) */
    g_r3d_frontend = 1;
    Render_LoadCommonFsh(g_render->textures);   /* 0x448a40 (FEApplication_Run's, then R3DCar_LoadCommon) */
    /* The sound (GameMain's Audio_Init 0x4a6610 before the front end; the port's race closes it at its
       end, so it opens again here) and the menus' music. */
    FEApp_Sound();
    FECD_Init();       /* 0x4bdfb0 (then the player's save's CD player section over it) */
    FESave_CDApply();
    s_music_at = -1;   /* 0x65b928: the application (the car renderer's showroom mode) */
    while (s_depth) screen_free(s_stack[--s_depth]);   /* (FEApplication_Run starts its stack anew) */
    if (s_nsaved) {
        /* back from a race (0x4d2e70 after it): the screens it began from, the knockout between its races
           on its race information (knockoutraceinfo 0x5d9354) */
        int n = s_nsaved;
        s_nsaved = 0;
        bool ok = true;
        if (s_gametype == 9 && n > 0) {
            /* (0x4d2e70's Factory Driver stack: the first screen, factorydriver unless all missions are
               done, a level's end on the victory screen with its message popped up) */
            ok = FEApp_GotoScreen(s_saved[0], true);
            if (ok && !FEFactory_AllDone()) ok = FEApp_GotoScreen("factorydriver", true);
            if (ok && FEFactory_LevelDone()) {
                ok = FEApp_GotoScreen("FactoryVictory", true);
                if (ok) ok = FEApp_GotoScreen("levelvictory", true);
            }
            if (ok) return true;
            while (s_depth) screen_free(s_stack[--s_depth]);
            return FEApp_GotoScreen(start, true);
        }
        if (s_gametype == 4 && s_era_race && FEData_EraEnded() && n > 0) {
            /* (0x4d2e70's career stack after the last race of an era's last tournament, TOURNAMENTENDOFERA
               0x6577dc and 0x504db0: the first screen, the victory room, the era's end over it) */
            s_era_race = false;
            ok = FEApp_GotoScreen(s_saved[0], true);
            if (ok) ok = FEApp_GotoScreen(exe_str(0x5d93ac), true);   /* "VictoryRoom" */
            if (ok) ok = FEApp_GotoScreen(exe_str(0x5d93a0), true);   /* "endofera" */
            if (ok) return true;
            while (s_depth) screen_free(s_stack[--s_depth]);
            return FEApp_GotoScreen(start, true);
        }
        s_era_race = false;
        for (int i = 0; i < n && ok; i++) ok = FEApp_GotoScreen(s_saved[i], true);
        if (ok && FEData_KnockoutOn()) FEApp_GotoScreen("knockoutraceinfo", true);
        if (ok) return true;
        while (s_depth) screen_free(s_stack[--s_depth]);
    }
    if (!FEApp_GotoScreen(start, true)) return false;
    /* (0x4d2e70: the players listed and player.lst's loaded, 0x4dee70; without one the player is
       chosen or made first, GotoScreen("selectname") over the game setup) */
    if (!strcasecmp(start, "gamesetup") && !FESave_HavePlayer()) FEApp_GotoScreen("selectname", true);
    return true;
}

void FEApp_RaceWear(const int32_t *ids, const float *wear, int n)
{
    if (s_owned_race >= 0 && !s_testdrive) FEData_AddWear(s_owned_race, ids, wear, n);
    s_owned_race = -1;
    FESave_Write();
}

void FEApp_RaceRecord(const char *name, const char *car, int32_t best, int32_t time)
{
    /* 0x4ed590: the player's best lap (a point-to-point race's time) among the track's records, but for
       Factory Driver's and the network's races (and a race quit, which doesn't come here) */
    if (s_factory_race || s_gametype == 3) return;
    FEData_TrackRecordEnter(s_track, name, car, g_race_linear ? time : best);
}

static int32_t s_mission_stats[3];
void FEApp_MissionStats(const int32_t stats[3]) { memcpy(s_mission_stats, stats, sizeof s_mission_stats); }

void FEApp_RaceOver(const int *places, const int *times, const bool *clean, int n, bool quit)
{
    /* a quit race (0x657e34): 0x4ed590 records nothing, a career race only its tournament's end */
    if (s_tour_race >= 0) {
        s_era_race = !quit && g_race_tour.end_of_era;   /* (TOURNAMENTENDOFERA, 0x6577dc) */
        if (quit) FEData_TourEnd();
        else FEData_TourResult(s_tour_race, places, times, clean, n);
    }
    s_tour_race = -1;
    if (s_gametype == 5) {   /* (the career's test drive: 0x504b20 at the front end's exit, back to 4) */
        s_gametype = 4;
        FEData_SetGameType(4);
    }
    if (s_gametype == 2 && !quit) FEData_KnockoutResult(places, n);   /* (0x4ddb10) */
    FEData_RefreshCars();   /* (FEApplication_Run's 0x4ed390: 0x4f34c0, then the career's market) */
    if (s_gametype == 4) Market_Update(FEData_CareerYear());
    if (s_factory_race && !quit) {
        /* 0x4ed590's type 9: the verdict (0x657834) and the results into the mission (0x51a770: the
           local racer's time, damage and cones from the results' stats, 0x5e75a0), or the cheat's skip
           (0x657e2c, 0x51a8e0) */
        if (g_mission_skip) FEFactory_Skip(g_mission_pass);
        else FEFactory_Result(s_mission_stats[0], s_mission_stats[1], s_mission_stats[2], g_mission_pass);
        FESave_Write();
    }
    s_factory_race = false;
    s_nopp = -1;
    s_testdrive = false;
}

void FEApp_End(void)
{
    /* the stack to come back to (the knockout's race information off it: it's put back while the
       knockout runs) */
    s_nsaved = 0;
    for (int i = 0; i < s_depth; i++)
        if (strcasecmp(s_stack_name[i], "knockoutraceinfo")) snprintf(s_saved[s_nsaved++], sizeof s_saved[0], "%s", s_stack_name[i]);
    if (s_sound) {
        Music_FadeOut(0x5dc);   /* (FEApplication_Run's end) */
        Audio_Shutdown();       /* (the port's race opens it again) */
        s_sound = false;
    }
    FE3D_Free();
    FEChronicle_Free();    /* 0x506920 (0x4d5d40) */
    FEEndOfEra_Free();
    FEPointer_Free();      /* 0x4e1450 */
    FEShowcase_Load(NULL); /* 0x5081e0 (the port's: the race's memory) */
    R3DCar_FreeCommon();   /* 0x443a80 (FEApplication_Run's end: made again for the race's mode) */
    Render_FreeCommonFsh();   /* 0x448af0 */
    g_r3d_frontend = 0;
}

/* A popup (the screens with a popup circle: the original keeps them in their screen's list, +0x8 ..,
   drawn over it; the port stacks them). */
static void screen_entered(void);
static bool is_popup(const FertControl *w)
{
    for (int i = 0; w && i < w->nchildren; i++)
        if (!strcmp(w->children[i]->id, "NFS5.POPUPCIRCLE")) return true;
    return false;
}

/* Back to the screen under the popups (0x4d1e40(-1): the top screen's popups closed). */
static void close_popups(void)
{
    while (s_depth > 1 && is_popup(s_stack[s_depth - 1])) screen_free(s_stack[--s_depth]);
    screen_entered();
}

/* FEFactoryDriverCBL's constructor (0x519180): its callbacks CHEATFACTORY, PROFILEDEC, PROFILEINC (the
   port's callback()); the welcome the first time, the first mission's words before it (its brief 0xbc7),
   else the mission's brief, popped up; Factory.MissionCheat remembered. */
static void factory_open(void)
{
    Save *sv = FESave_Current();
    const uint8_t *m = FEFactory_Mission();
    int32_t desc = 0;
    if (m) memcpy(&desc, m, 4);
    if (sv && !sv->flags[1]) {
        sv->flags[1] = 1;
        FEApp_GotoScreen("factwelcome", true);
    } else if (sv && desc == 0xbc7 && !sv->flags[2]) {
        sv->flags[2] = 1;
        FEApp_GotoScreen("factfirst", true);
    } else {
        FEApp_GotoScreen("missionbrief", true);
    }
    FEAccessor *a = FEAccess_Find("Factory.MissionCheat");
    s_fd_cheat = a ? FEAccess_Pos(a) : 0;
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
    if (!push && s_depth) screen_free(s_stack[--s_depth]);
    if (s_depth == STACK) {
        FertControl_free(w);
        return false;
    }
    snprintf(s_stack_name[s_depth], sizeof s_stack_name[0], "%s", name);
    s_stack[s_depth++] = w;
    screen_entered();
    if (!strcasecmp(name, "factorydriver")) factory_open();
    if (!strcmp(screen_attr(w, "callbacklist"), "ID_VICTORYSCREEN")) {
        /* FEVictoryScreen's constructor (0x506210): Game.GameType 10, the back button locked (0x4d5670)
           and its message over it (VictoryMessage 0x5dba20, popped) */
        s_gametype = 10;
        FEData_SetGameType(10);
        s_back_locked = true;
        FEApp_GotoScreen(exe_str(0x5dba20), true);
    }
    return true;
}

void FEApp_Back(void)
{
    if (s_depth > 1) screen_free(s_stack[--s_depth]);
    screen_entered();
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

void FEApp_RefreshState(FertControl *c) { refresh_state(c); }

static void on_enter(FertControl *c)   /* 0x4c7760 */
{
    if ((c->fecaps & 4) && !(c->feflags & 4)) {
        c->feflags |= 4;
        refresh_state(c);
    }
}

static void on_leave(FertControl *c)   /* 0x4c7820 */
{
    FEChronicle_Leave(c);
    if (c->feflags & 4) {
        c->feflags &= ~4u;
        refresh_state(c);
    }
}

FEResult FEApp_Result(void) { return s_result; }
const uint32_t *FEApp_Colours(void) { return s_havecol ? s_colours : NULL; }

int FEApp_Parts(int32_t ids[20])
{
    if (s_factory_race) {   /* the mission's car object (0x69c8c8) and its parts */
        const SaveCar *c = FEFactory_Car();
        if (!c) return -1;
        int n = 0;
        for (int i = c->ninstalled; i < c->ninstalled + c->nother && n < 20; i++) ids[n++] = c->parts[i][0];
        for (int i = n; i < 20; i++) ids[i] = 0;
        return n;
    }
    return s_owned_race >= 0 ? FEData_OwnedParts(s_owned_race, ids) : -1;
}

bool FEApp_Setup(float set[17])
{
    if (s_factory_race) {
        const SaveCar *c = FEFactory_Car();
        if (c) memcpy(set, c->rec + 0x6d0, 17 * sizeof(float));
        return c != NULL;
    }
    return s_owned_race >= 0 && FEData_OwnedSetup(s_owned_race, set);
}

int FEApp_GameType(void) { return s_testdrive ? s_testdrive_type : s_gametype; }

float FEApp_Skill(void) { return s_skill; }

int FEApp_OpponentCars(RaceOpponent out[8])
{
    if (s_nopp < 0) return -1;
    memcpy(out, s_opp, sizeof s_opp);
    return s_nopp;
}
const char *FEApp_Track(void) { return s_track; }
const char *FEApp_Car(void) { return s_car; }
int FEApp_Laps(void) { return s_laps; }
bool FEApp_Reverse(void) { return s_reverse; }
bool FEApp_Mirror(void) { return s_mirror; }
const char *FEApp_ReplayFile(void) { return s_replay_chosen ? s_replay_file : NULL; }

/* A direction's string id (Track<n>.Direction: 0x19a forward, 0x19b reversed, 0x19c mirrored, 0x19d
   both) into the race's flags (0x4fb630). */
static void set_direction(int id)
{
    s_reverse = id == 0x19b || id == 0x19d;
    s_mirror = id == 0x19c || id == 0x19d;
}
int FEApp_Opponents(void) { return s_opponents; }
int FEApp_Cops(void) { return s_cops; }

int FEApp_Traffic(int16_t slots[7])
{
    memcpy(slots, s_traffic_slots, sizeof s_traffic_slots);
    return s_traffic;
}

/* The race's kind for the game type (0x4ec... option 0x10 RACE_TYPE): a quick race 0 (1 with the
   flag 5 of 0x65b940: pending), a quick knockout 3 (the laps knock the last car out), the knockout and
   the career 2, multiplayer and the test drives 0, Factory Driver 4 with a mission's conditions (pending:
   0). */
int FEApp_Grid(int pers[8], int start[9])
{
    if (s_ngrid < 0) return -1;
    memcpy(pers, s_grid_pers, sizeof s_grid_pers);
    memcpy(start, s_grid_start, sizeof s_grid_start);
    start[8] = 0;
    return s_ngrid;
}

int FEApp_RaceType(void)
{
    if (s_testdrive) return 0;
    switch (s_gametype) {
    case 9: return s_opponents ? 0 : 4;   /* (0x4eab10: a skill test without opponents) */
    case 1: return 3;
    case 2:
    case 4: return 2;
    case 0: return FE_Cheat(5) ? 1 : 0;   /* (Fuzzyfuz: the pursuit) */
    default: return 0;
    }
}

int FEApp_Damage(void)
{
    /* 0x4eab10's DAMAGE (0x20) by the race type: Game.Damage's for 0 and 1, and for 2 in game type 2;
       else on (1, or for some types the stream's address: not 0 either) */
    int t = FEApp_RaceType();
    FEAccessor *d = FEAccess_Find("Game.Damage");
    if (t == 0 || t == 1 || (t == 2 && s_gametype == 2)) return d ? FEAccess_Pos(d) : 1;
    return 1;
}

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
/* 0x4d1e40(-1): the screens of the stack's history popped, back to its first (the player screens over
   the game setup's, 0x4d2e70's). */
static void back_to_first(void)
{
    while (s_depth > 1) FEApp_Back();
}

/* The player screens' callbacks (FEMainScreen, vtable 0x5b9d64). Each ends in 0x504b20, the tournament
   accessor's refresh. */
static bool player_callback(const char *cb)
{
    FEAccessor *users = FEAccess_Find("UserList.UserList"), *in = FEAccess_Find("UserList.NameInput");
    if (!strcmp(cb, "SETNAMETHENBACK")) {   /* _v55 0x50ffa0: the chosen player's save loaded */
        if (users && FEAccess_Count(users)) {
            FEData_RefreshCars();   /* (0x4f34c0, before the load: the year it had) */
            if (FESave_LoadStem(FESave_PlayerStem(FEAccess_Pos(users)), false)) {
                FEData_KnockoutStop();   /* (0x4ddaf0) */
                FEApp_Back();
            } else {
                FEApp_Message(0x326);
            }
            FESave_ListPlayers();
        }
    } else if (!strcmp(cb, "REMOVEPLAYER")) {   /* _v56 0x510050: "Are you sure?" (0x323) first */
        if (users && FEAccess_Count(users)) {
            dialog_open(DLG_TWO, FEStrings_Get(0x323), 0x2be, 0x2bf);
            s_dialog.purpose = DLG_FOR_REMOVE;
        }
    } else if (!strcmp(cb, "CREATEPLAYERENTER")) {   /* _v57 0x510060: the name cleared, the default driver */
        FEData_NameInput()[0] = 0;
        s_driver_pic = 8;   /* (0x4cf1c0) */
    } else if (!strcmp(cb, "CREATEPLAYERDONE")) {   /* _v58 0x510080 */
        char name[100];
        snprintf(name, sizeof name, "%s", FEData_NameInput());
        if (!name[0]) return true;
        if (FEData_CheatCode(name)) {   /* (a cheat's code: the name cleared) */
            FEData_NameInput()[0] = 0;
        } else if (FESave_NameTaken(name)) {
            FEApp_Message(0x321);
        } else if (!FESave_Create(name, s_driver_pic)) {
            FEApp_Message(0x325);
        } else {
            /* (the name added to the list and chosen, the list made anew from the saves, the one chosen
               then, player.lst's, loaded) */
            FESave_ListPlayers();
            if (FESave_LoadStem(FESave_PlayerStem(FEAccess_Pos(users)), true)) {
                FEData_KnockoutStop();
                back_to_first();
            } else {
                FEApp_Message(0x326);
            }
        }
    } else if (!strcmp(cb, "CANCELCREATEPLAYER")) {   /* _v66 0x5103c0 */
        FEApp_Back();
    } else if (!strcmp(cb, "LOADGAMEBUTTON")) {   /* 0x5103d0: the player's save written */
        FESave_Write();
    } else {
        return false;
    }
    (void)in;
    FEData_TourRefresh();   /* (0x504b20, and its Game.GameType 5, 10 or 11 back to 4) */
    if (s_gametype == 5 || s_gametype == 10 || s_gametype == 11) {
        s_gametype = 4;
        FEData_SetGameType(4);
    }
    return true;
}

/* The options screen's CD player buttons (FEScreenOptions_Command 0x510ed0's, its slots 57 .. 64). */
static bool cd_callback(const char *cb)
{
    FEAccessor *av = FEAccess_Find("CDPlayer.AvailableList"), *sl = FEAccess_Find("CDPlayer.SelectedList"),
               *ti = FEAccess_Find("CDPlayer.Title");
    if (!strcmp(cb, "PLAYSONG")) {   /* _v61 0x5119a0: the chosen song from its start */
        Music_FadeOut(0);
        const char *n = FECD_CurrentName();
        Music_Play(n, 0);
    } else if (!strcmp(cb, "STOPSONG")) {   /* _v62 0x5119c0 */
        Music_FadeOut(0);
    } else if (!strcmp(cb, "SKIPFWD") || !strcmp(cb, "SKIPBK")) {   /* _v63 0x5119d0, _v64 0x5119e0 */
        if (ti) FEAccess_SetPos(ti, FEAccess_Pos(ti) + (cb[4] == 'F' ? 1 : -1));
    } else if (!strcmp(cb, "ADDSONG")) {
        /* _v57 0x5118c0: the available row chosen added, unless 40 are (message 0x32f) */
        if (av && sl && FEAccess_Pos(av) >= 0) {
            if (FEAccess_Count(sl) > 0x27) FEApp_Message(0x32f);   /* (FEDialogOneItemSelfDeleting 0x512d00) */
            else FECD_Add(FECD_Id(FEAccess_Pos(av)));
        }
    } else if (!strcmp(cb, "REMOVESONG")) {   /* _v58 0x511940: the chosen row out, the choice dropped */
        if (sl && FEAccess_Pos(sl) >= 0) FECD_Remove(FEAccess_Pos(sl));
        FEAccess_Unselect(sl);
    } else if (!strcmp(cb, "CLEARALL")) {   /* _v59 0x511980 */
        FECD_Clear();
    } else if (!strcmp(cb, "RESETLIST")) {   /* _v60 0x511990 */
        FECD_Reset();
    } else {
        return false;
    }
    return true;
}

static void callback(const char *cb)
{
    if (player_callback(cb)) return;
    if (!strcmp(screen_attr(FEApp_Top(), "callbacklist"), "ID_CHRONICLE") && FEChronicle_Callback(cb)) return;
    if (FEData_CompareCallback(cb)) return;   /* (and SHOWCASE.MERGEDCAR's compared car) */
    if (FEShowcase_Callback(cb)) return;   /* (the base list's SHOWCASE.<car> functors) */
    if (!strcmp(screen_attr(FEApp_Top(), "callbacklist"), "IDS_ADVREPAIRCBL")) {
        int m = FEData_RepairCallback(cb);
        if (m > 0) FEApp_Message(m);
        if (m >= 0) return;
    }
    if (!strcmp(cb, "GOBACK") || !strcmp(cb, "TESTDRIVECHECK")) {
        /* the car setup's (FECarSetupCallbackList_v45 0x4f1360, 0x4f1100 for TESTDRIVECHECK): only with the
           car's required parts (0x5019d0 -> 0x4ef890, else its message, 0x512e70) */
        if (!strcmp(screen_attr(FEApp_Top(), "callbacklist"), "ID_CARSETUP")) {
            char m[600];
            if (!FEData_CarComplete(m, sizeof m)) {
                dialog_open(DLG_ONE, m, 0x2be, 0);
                return;
            }
        }
        if (cb[0] == 'T') FEApp_GotoScreen("testdrivecarsetup", true);
        else FEApp_Back();
    } else if (!strcmp(cb, "OPTIONS")) {
        FEApp_GotoScreen("options", true);
    } else if (!strcmp(cb, "DEFAULTCONTROLS")) {   /* 0x511820: the chosen controller's defaults */
        FEAccessor *a = FEAccess_Find("Controller.AllControllers");
        FECtrl_Map(FECtrl_ListDevice(a ? FEAccess_Pos(a) : 0));
    } else if (cd_callback(cb)) {
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
    } else if (!strcmp(cb, "INSTALLPART") || !strcmp(cb, "UNINSTALLPART")) {
        FEData_InstallAction(cb);
    } else if (!strcmp(cb, "KNOCKOUTTRACKINC") || !strcmp(cb, "KNOCKOUTTRACKDEC")) {
        /* 0x50d4c0 / 0x50d500: the slot's track on or back (0x690a84 the room's way) */
        FEAccessor *a = FEAccess_Find("Knockout.Track");
        if (a) FEAccess_SetPos(a, FEAccess_Pos(a) + (cb[13] == 'I' ? 1 : -1));
    } else if (!strcmp(cb, "MAKERANDOMKNOCKOUT")) {
        /* 0x50cc30: every slot's traffic, direction, laps and track at random (the C runtime's rand) */
        for (int k = 1; k <= 7; k++) {
            static const char *f[4] = {"Traffic", "Direction", "Laps", "Track"};
            for (int j = 0; j < 4; j++) {
                char nm[32];
                snprintf(nm, sizeof nm, "Track%d.%s", k, f[j]);
                FEAccessor *a = FEAccess_Find(nm);
                int n = a ? FEAccess_Count(a) : 0;
                if (n) FEAccess_SetPos(a, CRT_rand() % n);
            }
        }
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
    } else if (!strcmp(cb, "GOTOQUICKFIX")) {   /* the garage's (FEQuickFixCallBackList slot 55 0x50e960) */
        FEData_QuickFixClear();
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
        /* FEMainScreen_v59 .. _v64 (0x510510 ..): Game.GameType 0 quick race, 1 quick knockout, 2 knockout,
           3 multiplayer, 9 Factory Driver (its screen, factorydriver or factdone once done: pending) */
        s_gametype = !strcmp(cb, "SETTYPEQUICKKNOCKOUT") ? 1 : !strcmp(cb, "SETTYPEKNOCKOUT") ? 2
                   : !strcmp(cb, "SETTYPEMULTIPLAYER") ? 3 : !strcmp(cb, "SETTYPEFACTORYDRIVER") ? 9 : 0;
        FEData_SetGameType(s_gametype);
        FEData_RefreshCars();   /* (0x4f34c0) */
        if (s_gametype == 9) {
            /* FEMainScreen_v64 0x510600: its missions (0x4f34c0), then their screen, factdone once all
               are done */
            FEFactory_Load();
            FEApp_GotoScreen(FEFactory_AllDone() ? "factdone" : "factorydriver", true);
        }
    } else if (!strcmp(cb, "GOBACKMINUSONE") || !strcmp(cb, "FDMSGGOBACK")) {
        /* the application's GOBACKMINUSONE (0x50de70, its slot 0xb4) and FEFactoryDriverCBL's message's
           (0x4d1e40(-1)): the popups closed */
        close_popups();
    } else if (!strcmp(cb, "FACTORYDRIVERDONECONFIRM")) {
        FEApp_Back();   /* (0x4d1e40(0)) */
    } else if (!strcmp(cb, "PROFILEINC") || !strcmp(cb, "PROFILEDEC")) {
        /* FEFactoryDriverCBL's (0x519360 / 0x519330): Factory.ProfileNum's next or last (slots 0x54, 0x50) */
        FEAccessor *a = FEAccess_Find("Factory.ProfileNum");
        if (a) FEAccess_SetPos(a, FEAccess_Pos(a) + (cb[7] == 'I' ? 1 : -1));
    } else if (!strcmp(cb, "ACCEPTPRIZECAR")) {
        /* FEFDVictoryScreenCBL 0x519f50: the last mission's prize car owned (0x501820: white, the
           interior black, no stripes, no number; its +0x6a4 price 0), its level's words shown, back */
        const uint8_t *m = FEFactory_Last();
        if (m) {
            int16_t id;
            memcpy(&id, m + 0x10, 2);
            const int32_t col[4] = {-1, (int32_t)0xff000000u, 0, 0};
            FEData_AddOwned(id, col, 0, -1);
        }
        FEFactory_ClearEndOfLevel();
        FESave_Write();
        FEApp_Back();
    } else if (!strcmp(cb, "CHEATFACTORY")) {
        /* FEFactoryCallBackList_v55 0x5192f0 (by 0x4f6ca0, slot 0xdc): the mission marked with the last
           verdict (0x657834), no time, damage or cones (0x51a770) */
        FEFactory_Result(0, 0, 0, g_mission_pass);
    } else if (!strcmp(cb, "SETTYPECAREERMODE")) {
        s_gametype = 4;
        FEData_SetGameType(4);
        FEData_RefreshCars();
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
    } else if (!strcmp(cb, "TESTDRIVEUSED") || !strcmp(cb, "TESTDRIVENEW") || !strcmp(cb, "TESTDRIVECARSETUP")) {
        /* FECallBackList_v48 .. _v50 (0x50c9f0 ..): a test drive (0x699fc8 1, the type 0x5dc1b8 7 used, 6 new,
           8 the car setup's), then PLAYGAME (slot 0xd4): Track1.Track's track, the car of the screen's
           accessor, alone. (The race's own type for a test drive, from 0x690aac through 0x4ee430, pends: the
           port runs it as a race of the options' laps; no wear and no result are kept.) */
        const char *acc = cb[9] == 'U' ? "UsedCar.Car" : cb[9] == 'N' ? "DealerCar.Car" : "OwnedCars.Car";
        FEAccessor *ca = FEAccess_Find(acc);
        const char *c = FEAccess_CarFile(ca);
        if (!c) return;
        s_tour_race = -1;
        s_owned_race = cb[9] == 'C' ? FEData_OwnedIndex("OwnedCars.Car") : -1;   /* (its parts and setup) */
        snprintf(s_track, sizeof s_track, "%s", FEData_Track());
        snprintf(s_car, sizeof s_car, "%s", c);
        s_havecol = FEAccess_CarColours(ca, s_colours);
        s_laps = fe_int("num_laps", 1);
        s_opponents = 0;
        s_ngrid = -1;
        s_skill = 1.0f;
        s_testdrive = true;
        s_traffic = 5;   /* (0x4fb630 for the test drives, types 6 .. 8) */
        memset(s_traffic_slots, 0, sizeof s_traffic_slots);
        s_testdrive_type = cb[9] == 'U' ? 7 : cb[9] == 'N' ? 6 : 8;
        set_direction(0);   /* (0x4fb630's types 6 .. 8: forward) */
        s_result = FE_PLAY;
    } else if (!strcmp(cb, "TESTDRIVE")) {
        /* The pre-race screen's (FETournamentCallBackList 0x505720): with a car owned (0x691dfc),
           Game.GameType 5 and the race started (0x4d5200(0), its checks), else back to 4: the tournament's
           chosen track with its laps, reverse and mirror (0x4fb630's types 4, 5), alone, no traffic
           (0x4ec590), in zMergedList.Car's car (0x4eadb0: the Quick Race list's, not the career's chosen
           one). The used cars' (0x516210) does nothing. */
        if (!strcmp(screen_attr(FEApp_Top(), "callbacklist"), "ID_VICTORYSCREEN")) {
            /* FEVictoryScreen's (0x506340): Game.GameType 11 and the race started (0x4d5200(0)): the
               prize race's track (0x4fb630's types 10, 11), alone (0x4ec590), in the prize car
               (0x4eadb0: its first colour, +0x3ec, all four; none: 0xffff0000) */
            int laps;
            bool rev, mir;
            int traffic;
            const char *t = FEData_PrizeRace(&laps, &rev, &mir, &traffic);
            const uint8_t *pc = FEAccess_CarRecord(FEAccess_Find("PrizeCars.Car"));
            if (!t || !pc) return;
            s_gametype = 11;
            FEData_SetGameType(11);
            s_reverse = rev;
            s_mirror = mir;
            s_laps = laps;
            snprintf(s_track, sizeof s_track, "%s", t);
            snprintf(s_car, sizeof s_car, "%s", (const char *)pc + 0x7f);
            int16_t nc;
            memcpy(&nc, pc + 0x3e6, 2);
            uint32_t c = 0xffff0000u;
            if (nc > 0) {
                memcpy(&c, pc + 0x3ec, 4);
                c = c >> 24 | (c >> 8 & 0xff00u) | (c << 8 & 0xff0000u) | c << 24;
            }
            for (int i = 0; i < 4; i++) s_colours[i] = c;
            s_havecol = true;
            s_owned_race = -1;
            s_testdrive = false;
            s_factory_race = false;
            s_tour_race = -1;
            s_opponents = 0;
            s_nopp = 0;
            s_ngrid = -1;
            s_skill = 1.0f;
            s_traffic = traffic;
            memset(s_traffic_slots, 0, sizeof s_traffic_slots);
            s_result = FE_PLAY;
            return;
        }
        if (strcmp(screen_attr(FEApp_Top(), "callbacklist"), "ID_TOURNYSCREEN")) return;
        Save *sv = FESave_Current();
        if (!sv || !sv->ncars || !can_play()) return;
        const char *t = FEData_TourTrack(), *c = FEData_Car();
        if (!t || !c) return;
        s_gametype = 5;
        FEData_SetGameType(5);
        int race = FEData_TourRace();
        const uint8_t *tr = FEData_Tournament();
        s_reverse = tr && race >= 0 && tr[0x3a4 + 0xa4 * race] != 0;
        s_mirror = tr && race >= 0 && tr[0x3a5 + 0xa4 * race] != 0;
        s_laps = FEData_TourLaps();
        snprintf(s_track, sizeof s_track, "%s", t);
        snprintf(s_car, sizeof s_car, "%s", c);
        s_havecol = FEAccess_CarColours(FEAccess_Find("zMergedList.Car"), s_colours);
        s_owned_race = FEData_OwnedIndex("zMergedList.Car");
        s_testdrive = false;
        s_factory_race = false;
        s_tour_race = -1;
        s_opponents = 0;
        s_nopp = 0;
        s_ngrid = -1;
        s_skill = 1.0f;
        s_traffic = 0;
        memset(s_traffic_slots, 0, sizeof s_traffic_slots);
        s_result = FE_PLAY;
    } else if (!strcmp(cb, "TRACK1INC") || !strcmp(cb, "TRACK1DEC") || !strcmp(cb, "LOOPEDINC") ||
               !strcmp(cb, "LOOPEDDEC")) {
        /* 0x50d440 / 0x50d480, 0x50d5c0 / 0x50d600: the location screens' track on or back (the
           accessor's slots 0x54 / 0x50, round), 0x690a84 the room's turn 0 or 1 */
        FEAccessor *a = FEAccess_Find(cb[0] == 'T' ? "Track1.Track" : "LoopedTrack.Track");
        if (a) FEAccess_SetPos(a, FEAccess_Pos(a) + (cb[strlen(cb) - 3] == 'I' ? 1 : -1));
    } else if (!strcmp(cb, "FORFEITTOURNY")) {
        /* the pre-race screen's (0x505780): its question (+0x2e8, FEDialogTwoItem 0x5131f0(0x338), the
           buttons 700 and 0x2bd) */
        dialog_open(DLG_TWO, FEStrings_Get(0x338), 700, 0x2bd);
        s_dialog.purpose = DLG_FOR_FORFEIT;
    } else if (!strcmp(cb, "RESETRECORDS")) {
        FEData_ResetRecords();
    } else if (!strcmp(cb, "GOBACKNOVALIDATION") || !strcmp(cb, "DECLINE")) {
        /* the car setup's and the victory room's (0x50c2b0: 0x4d1e40(0), one screen back, no check) */
        FEApp_Back();
    } else if (!strcmp(cb, "STARTREPLAY")) {
        /* FECallBackList_v51 0x50ca50: the replay chosen (0x699fc9) and the race started (v53 0x50c550 ->
           0x4d5200(1): no checks), its file the race's (0x4ecc60's caller: options 0x97 2, 0x110) */
        FEAccessor *rl = FEAccess_Find("Replay.List");
        if (!rl || !FEAccess_Count(rl)) return;
        s_replay_chosen = true;
        snprintf(s_replay_file, sizeof s_replay_file, "%s.rpl", FEAccess_Text(rl));
        s_result = FE_PLAY;
    } else if (!strcmp(cb, "KILLREPLAY")) {
        /* _v52 0x50ca60: its question (+0x274, FEDialogTwoItem 0x5131f0(0x328)) first */
        dialog_open(DLG_TWO, FEStrings_Get(0x328), 0x2be, 0x2bf);
        s_dialog.purpose = DLG_FOR_KILLREPLAY;
    } else if (!strcmp(cb, "PLAYGAME")) {
        s_testdrive = false;
        s_traffic = 0;
        memset(s_traffic_slots, 0, sizeof s_traffic_slots);
        s_ngrid = -1;
        s_skill = 1.0f;
        s_nopp = -1;
        s_factory_race = false;
        set_direction(0);
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
            int m = FEData_TourCheck();
            if (m == 0x35a) {
                /* (0x4d5200: FEDialogTwoItem 0x5131f0, its buttons ok 0x2be and cancel 0x2bf) */
                dialog_open(DLG_TWO, FEStrings_Get(0x35a), 0x2be, 0x2bf);
                s_dialog.purpose = DLG_FOR_TOUR;
                return;
            }
            if (m) {
                FEApp_Message(m);
                return;
            }
            s_tour_race = FEData_TourRace();   /* (before the laps: they're its) */
            {
                /* (0x4fb630's types 4, 5: the race's reverse +0x3a4 and mirror +0x3a5) */
                const uint8_t *tr = FEData_Tournament();
                s_reverse = tr && s_tour_race >= 0 && tr[0x3a4 + 0xa4 * s_tour_race] != 0;
                s_mirror = tr && s_tour_race >= 0 && tr[0x3a5 + 0xa4 * s_tour_race] != 0;
            }
            s_owned_race = FEData_OwnedIndex("OwnedCars.Car");
            snprintf(s_track, sizeof s_track, "%s", t);
            snprintf(s_car, sizeof s_car, "%s", c);
            s_havecol = FEAccess_CarColours(FEAccess_Find("OwnedCars.Car"), s_colours);
            s_traffic = FEData_TourTraffic(s_traffic_slots);
            /* its racers, on the grid by the standings (0x4ec590's type 4: 0x4eb430, 0x502c10) */
            s_nopp = FEData_TourOpponents(s_opp, s_grid_start, &s_skill);
            s_ngrid = s_nopp;
        } else if (s_gametype == 2) {
            /* the knockout (0x4d5200 -> 0x4ecc60, 0x4ddc00): started with its first race, its slot's
               track and laps, the racers still in on their places' grid */
            if (!FEData_KnockoutOn()) FEData_KnockoutStart();
            int laps = 1;
            s_ngrid = FEData_KnockoutRace(s_track, sizeof s_track, &laps, s_grid_pers, s_grid_start);
            set_direction(FEData_KnockoutDirection());
            /* (0x4ddc00: each racer still in in its own car, of its personality) */
            for (int i = 0; i < s_ngrid; i++) FEData_KnockoutOpponent(s_grid_pers[i], &s_opp[i]);
            s_nopp = s_ngrid;
            snprintf(s_car, sizeof s_car, "%s", FEData_Car());
            s_havecol = FEAccess_CarColours(FEAccess_Find("zMergedList.Car"), s_colours);
            s_owned_race = FEData_OwnedIndex("zMergedList.Car");
            s_laps = laps;
            s_opponents = s_ngrid;
            s_traffic = FEData_KnockoutTraffic();
            s_testdrive = false;
            s_result = FE_PLAY;
            return;
        } else if (s_gametype == 9) {
            /* Factory Driver (0x4eab10's type 9): the mission's track and laps (0x4fb630), its car
               (FactoryDriver.Car, 0x51aab0) with its colours and parts, its opponents one by one in their
               own cars (0x4eb830; their personalities by id, 0x4e2900) */
            int laps = 1;
            const char *t = FEFactory_Track(&laps);
            const SaveCar *fc = FEFactory_Car();
            const uint8_t *m = FEFactory_Mission();
            if (!t || !fc || !m) return;
            {
                int16_t dir;   /* (reversed when its +0x28 is 2; never mirrored) */
                memcpy(&dir, m + 0x28, 2);
                s_reverse = dir == 2;
                s_mirror = false;
            }
            {
                int16_t lvl;   /* (its traffic level +0x54: 0x5db440's count) */
                memcpy(&lvl, m + 0x54, 2);
                s_traffic = 0 <= lvl && lvl < 4 ? (int)exe_u32(0x5db440 + 4u * (uint32_t)lvl) : 0;
            }
            s_cops = m[0x26] ? 2 : 0;   /* (0x4ec590: two police cars for a mission with police) */
            snprintf(s_track, sizeof s_track, "%s", t);
            snprintf(s_car, sizeof s_car, "%s", (const char *)fc->rec + 4 + 0x7f);
            s_havecol = FEAccess_CarColours(FEAccess_Find("FactoryDriver.Car"), s_colours);
            s_owned_race = -1;
            s_factory_race = true;
            int pers[5], cars[5];
            int n = FEFactory_Opponents(pers, cars);
            memset(s_opp, 0, sizeof s_opp);
            for (int i = 0; i < n; i++) {
                s_opp[i].pers = FEData_PersonalityIndex(pers[i]);
                if (!FEData_OpponentCar(cars[i], s_opp[i].spec, s_opp[i].colours, &s_opp[i].stripe, &s_opp[i].racenum))
                    s_opp[i].spec[0] = 0;
            }
            s_nopp = n;
            s_laps = laps;
            s_opponents = n;
            s_result = FE_PLAY;
            return;
        } else if (s_gametype == 1) {
            /* the quick knockout: LoopedTrack.Track's track, Game.NumKOOpponents opponents and as many
               laps (0x4ec480's option 0x11) */
            snprintf(s_track, sizeof s_track, "%s", FEData_LoopedTrack());
            snprintf(s_car, sizeof s_car, "%s", FEData_Car());
            s_havecol = FEAccess_CarColours(FEAccess_Find("zMergedList.Car"), s_colours);
            s_owned_race = FEData_OwnedIndex("zMergedList.Car");
        } else {
            snprintf(s_track, sizeof s_track, "%s", FEData_Track());
            snprintf(s_car, sizeof s_car, "%s", FEData_Car());
            s_havecol = FEAccess_CarColours(FEAccess_Find("zMergedList.Car"), s_colours);
            s_owned_race = FEData_OwnedIndex("zMergedList.Car");
        }
        s_laps = s_tour_race >= 0 ? FEData_TourLaps() : s_gametype == 1 ? FEData_KOOpponents() : fe_int("num_laps", 1);
        /* The traffic (0x4fb630's fifth: Track1.Traffic's place, none, light, medium, heavy, for the quick
           race and the quick knockout) */
        if (s_tour_race < 0 && (s_gametype == 0 || s_gametype == 1)) {
            FEAccessor *ta = FEAccess_Find("Track1.Traffic");
            s_traffic = ta ? FEAccess_Pos(ta) : 0;
            set_direction(FEAccess_ItemId(FEAccess_Find("Track1.Direction")));   /* (types 0, 1, 3) */
        }
        s_opponents = s_tour_race >= 0 ? 7 : s_gametype == 1 ? FEData_KOOpponents() : FEData_Opponents();   /* (the standings' eight racers) */
        if (s_tour_race < 0) s_nopp = FEData_RaceOpponents(s_opponents, s_opp);   /* (0x4eb230) */
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

static void combo_open(FertControl *c);

static void on_up(FertControl *c)
{
    if (c->feflags & 0x10) return;   /* 0x4c7a10: disabled */
    if (!strcmp(c->id, "NFS5.CARSETTINGBUTTON")) {   /* FECarSettingButton: its accessor a step on */
        FEAccessor *a = FEAccess_Find(data_str(c, "accessor"));
        if (a) FEAccess_SetPos(a, FEAccess_Pos(a) + data_int_of(c, "step"));
        return;
    }
    if (!strcmp(c->id, "NFS5.CheckBox")) {   /* FECheckBox_v9 0x4c3340: a click in it turns its accessor over */
        FEAccessor *a = FEAccess_Find(data_str(c, "boolAccessor"));
        if (a) FEAccess_SetPos(a, !FEAccess_Pos(a));   /* (FEBoolAccessor's slot 29) */
        return;
    }
    if (is_combo(c)) {
        combo_open(c);   /* (0x4c66c0, closed) */
        return;
    }
    if (is_colourbox(c)) {   /* (FEColorSelectBox is a combo box: its colours' list) */
        FEAccess_AllowCustom(FEAccess_Find(data_str(c, "accessor")), data_int_of(c, "allowcustom") != 0);   /* _v21 0x4c37c0 */
        combo_open(c);
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
        Audio_UI(5);   /* 0x4aab80 */
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
        FEChronicle_Move(h, x, y);
        if (h) FECar3D_Move(h, x, y);
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
        if (target && type == 4 && FEChronicle_Down(target, w->mouse[0], w->mouse[1])) break;
        if (target && type == 8 && FEChronicle_Up(target)) break;
        if (target && type == 4 && FECar3D_Click(target, w->mouse[0], w->mouse[1])) break;
        if (target && type == 4 && indicator_click(target, w->mouse[0])) break;
        if (target && type == 4 && listbox_click(target, w->mouse[0], w->mouse[1])) break;
        if (target && type == 4 && picture_click(target)) break;
        if (target && type == 4 && FECtrl_Click(target)) break;
        if (target && type == 4 && FECtrl_DeadZonePress(target, w->mouse[0])) break;
        if (target && type == 4 && ko_list_click(target, w->mouse[1])) break;
        if (target && type == 8) on_up(target);
        break;
    }
}

/* ---------------------------------------------------------------- the pump's pass and the frame */

static void *s_slider_held;   /* (the list bar pressed: its moves and its release while held) */
static void slider_held_move(int mx, int my);
static void slider_held_up(void);
static bool combo_mouse(bool down, int32_t mx, int32_t my);

/* The control under the mouse, down through the panels (their windows' hovers). */
static FertControl *deepest_hover(FertControl *w)
{
    FertControl *h = w ? w->hover : NULL;
    while (h && h->sub && h->sub->hover) h = h->sub->hover;
    return h;
}

static void poll(void)   /* 0x529530 */
{
    FertControl *w = FEApp_Top();
    int32_t mx, my;
    uint32_t b;
    if (!w || !MOUSE_state(&mx, &my, &b)) return;
    if (FEColour_Box()) {   /* (the colour picker open has the mouse: its box keeps the focus) */
        bool moved = mx != s_mouse[0] || my != s_mouse[1];
        uint32_t ch = b ^ s_buttons;
        s_mouse[0] = mx, s_mouse[1] = my, s_buttons = b;
        if ((ch & 1) && (b & 1)) FEColour_Mouse(0, mx, my, true);
        else if (ch & 1) FEColour_Mouse(2, mx, my, false);
        else if (moved) FEColour_Mouse(1, mx, my, (b & 1) != 0);
        return;
    }
    if (mx != s_mouse[0] || my != s_mouse[1]) {
        s_mouse[0] = mx;
        s_mouse[1] = my;
        if (s_bar) window_event(s_bar, 7, mx, my);
        window_event(w, 7, mx, my);
        if (s_slider_held) slider_held_move(mx, my);
        FECtrl_DeadZoneMouse(deepest_hover(w), mx, false);
    }
    uint32_t changed = b ^ s_buttons;
    s_buttons = b;
    if ((changed & 1) && !(b & 1)) FECtrl_DeadZoneMouse(NULL, mx, true);
    if ((changed & 1) && !(b & 1) && s_slider_held) {
        slider_held_up();
    }
    if ((changed & 1) && !(b & 1)) FEChronicle_Up(NULL);   /* (the chronicle's drags end anywhere) */
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
    {
        FertControl *cs = FECtrl_Waiting(w);
        if (cs) {
            uint8_t k[16];
            int nk = plat_key_presses(k, 16);
            FECtrl_Keys(cs, k, nk);
        }
    }
    {
        FertControl *in = focused_input(w);
        if (in) {
            uint8_t k[16];
            int nk = plat_key_presses(k, 16);
            inputbox_keys(in, k, nk);
        }
    }
    if ((changed & 1) && combo_mouse((b & 1) != 0, mx, my)) return;   /* (an open combo box has the mouse first) */
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
    /* (the lighter screenoptionovallite is for FEApplication_Run's mode 1, the race's options, RaceLoop's
       HUD action 1: no HUD visual returns 1, 0x425eb0's visuals' slot 9, so the race never opens them) */
    if (!strcmp(id, "ID_OPTIONOVAL")) return "screenoptionoval";
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
    FertControl *ctl = NULL;
    FEAccessor *ctl_acc = NULL;
    FE3D_BeginControlCars();
    for (int i = 0; w && i < w->nchildren; i++) {
        FertControl *c = w->children[i];
        if (strcmp(c->id, "NFS5.3DCAR")) continue;
        const char *pt = data_str(c, "positiontype");
        if (pt && atoi(pt) != 0) {
            FEAccessor *acc = FEAccess_Find(data_str(c, "accessor"));
            uint32_t cc[4];
            bool hc = FEAccess_CarColours(acc, cc);
            FE3D_SetControlCar(atoi(pt), FEAccess_CarFile(acc), hc ? cc : NULL);
            continue;
        }
        FEAccessor *acc = FEAccess_Find(data_str(c, "accessor"));
        car = FEAccess_CarFile(acc);
        havecol = FEAccess_CarColours(acc, col);
        if (acc) FE3D_SetIndex(FEAccess_Pos(acc), FEAccess_Count(acc));
        ctl = c, ctl_acc = acc;
    }
    FE3D_EndControlCars();
    FE3D_SetCar(car, havecol ? col : NULL);
    /* its doors, lids and roof, the camera into the car, the damage shown (FECar3DControl_v1 0x4c0100) */
    float dmg[10];
    const char *ds = ctl ? data_str(ctl, "damage") : NULL;
    bool show = ds && atoi(ds) != 0 && FEAccess_CarDamage(ctl_acc, dmg);
    FECar3D_SetWindow(w);
    FECar3D_Tick(ctl, show ? dmg : NULL);
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

/* Text broken into lines of width (0x522e80; the port breaks at spaces, greedily). The step (+0x10) is the
   last line's extent height + 2 (0x5b2580), truncated; the height (+4) each line's extent + 2, summed. */
enum { WRAP_LINES = 32 };
static char s_wrap[WRAP_LINES][256];
static int wrap_lines(HudFont *font, const char *text, float width, int *step, float *total)
{
    int nlines = 0;
    float fx, fy, fw, fh = 0;
    *step = 0, *total = 0;
    const char *p = text;
    while (*p && nlines < WRAP_LINES) {
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
        memcpy(s_wrap[nlines], p, (size_t)len);
        s_wrap[nlines][len] = 0;
        FONT_extent(font->font, s_wrap[nlines], &fx, &fy, &fw, &fh);
        *total = (float)(int)(fh + *total + 2.0f);
        *step = (int)(fh + 2.0f);
        nlines++;
        p += len;
        while (*p == ' ' || *p == '\n') p++;
    }
    return nlines;
}

static void wrap_text(HudFont *font, const char *t, float x, float y, uint32_t colour)
{
    char *copy = FE2D_Alloc((int)strlen(t) + 1);
    if (!copy) return;
    strcpy(copy, t);
    FE2D_Text(font, x, y, 0, colour, copy, 1.0f, 1.0f);
}

static uint32_t fade_a(uint32_t c, int a);
/* The lines drawn from (x0, y) one step apart, aligned 0 left, 1 centred, 2 right (0x522b10). Typed
   (chars >= 0): the first chars characters, the lines' in turn; in the line it ends in, the last of them
   at rem thirds of the colour's alpha (0x4d82a0) and, when the cursor's shape is there, the cursor
   (WeeIcons GLNT) 0x12 to its left, white at the colour's alpha (0x4d8280); the lines past it none. */
static void wrap_draw(HudFont *font, int nlines, int step, float x0, float y, float width, int align, uint32_t colour,
                      int chars, int rem, FeImage *cursor)
{
    float fx, fy, fw, fh;
    for (int i = 0; i < nlines; i++, y += (float)step) {
        char *l = s_wrap[i];
        int len = (int)strlen(l);
        FONT_extent(font->font, l, &fx, &fy, &fw, &fh);
        float x = align == 1 ? x0 + (width - fw) * 0.5f : align == 2 ? x0 + width - fw : x0;
        if (chars < 0 || chars >= len) {
            if (chars >= 0) chars -= len;
            wrap_text(font, l, x, y, colour);
            continue;
        }
        if (chars == 0) continue;
        char last = l[chars - 1];
        l[chars - 1] = 0;
        FONT_extent(font->font, l, &fx, &fy, &fw, &fh);
        float cx = (float)(int)(x + fw);
        wrap_text(font, l, x, y, colour);
        l[chars - 1] = last;
        char one[2] = {last, 0};
        wrap_text(font, one, cx, y, fade_a(colour, rem * 0xff / 3));
        if (cursor) FE2D_Image(cursor, cx - 18.0f, y, (float)cursor->w, (float)cursor->h, (colour >> 24) << 24 | 0xffffffu, 0, false);
        chars = 0;
    }
}

/* Text wrapped and drawn whole (see wrap_lines, wrap_draw); its height. */
static float draw_wrapped(HudFont *font, const char *text, float x0, float y, float width, int align, uint32_t colour, bool draw)
{
    int step;
    float total;
    int n = wrap_lines(font, text, width, &step, &total);
    if (draw) wrap_draw(font, n, step, x0, y, width, align, colour, -1, 0, NULL);
    return total;
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

void FEApp_Callback(const char *cb) { callback(cb); }

void FEApp_Click(int32_t x, int32_t y)
{
    FertControl *w = FEApp_Top();
    if (!w) return;
    window_event(w, 7, x, y);
    window_event(w, 4, x, y);
    FEChronicle_Up(NULL);
    window_event(w, 8, x, y);
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
    Audio_UI(9);   /* 0x4aab80 */
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

/* The word-wraps' typing (data.animating, +0x180 / +0x144): started at the screen's entry (v21 0x522440 /
   0x522220: the start and now ticks, a wait of 0x97 ticks); after the wait, nothing drawn, the clock starts
   again and the text types at a character every 3 ticks (TIMER_ticks, 128 a second). The now tick follows
   the timer while the screen isn't fading (0x4c83f0 == 0; the port has no fades). */
typedef struct { FertControl *c; uint32_t start, now; int wait; } TypeState;
static TypeState s_types[8];

static uint32_t timer_ticks(void) { return (uint32_t)((uint64_t)app_ticks_ms() * 128u / 1000u); }

static void type_start(TypeState *t, bool animating)
{
    t->start = t->now = timer_ticks();
    t->wait = animating ? 0x97 : 0;
}

static TypeState *type_state(FertControl *c, bool animating)
{
    for (int i = 0; i < 8; i++)
        if (s_types[i].c == c) return &s_types[i];
    for (int i = 0; i < 8; i++)
        if (!s_types[i].c || i == 7) {
            s_types[i].c = c;
            type_start(&s_types[i], animating);
            return &s_types[i];
        }
    return &s_types[0];
}

static void lists_drop_stale(void);
static bool control_in(const FertControl *w, const FertControl *c)
{
    if (w == c) return true;
    for (int i = 0; w && i < w->nchildren; i++)
        if (control_in(w->children[i], c)) return true;
    return false;
}

static bool on_stack(const FertControl *c)
{
    for (int k = 0; k < s_depth; k++)
        if (control_in(s_stack[k], c)) return true;
    return false;
}

static int anim_wait(const AnimRun *r)   /* min + rand() % (max - min), min without a span */
{
    return r->min < r->max ? r->min + CRT_rand() % (r->max - r->min) : r->min;
}

/* On screen (FEAnimatingGoToButton_v20 0x4c9180): the runs from its data (ticks at least 1), their waits. */
static AnimButton *anim_of(FertControl *c)
{
    AnimButton *free_slot = NULL;
    for (int i = 0; i < 8; i++) {
        if (s_anims[i].c == c) return &s_anims[i];
        if (!s_anims[i].c && !free_slot) free_slot = &s_anims[i];
    }
    if (!free_slot) return NULL;
    AnimButton *b = free_slot;
    static const char *const pre[3] = {"", "hover", "hovermousedown"};
    b->c = c;
    for (int k = 0; k < 3; k++) {
        char n[48];
        AnimRun *r = &b->run[k];
        snprintf(n, sizeof n, "%sstartframe", pre[k]);
        r->first = data_int_of(c, n);
        snprintf(n, sizeof n, "%sendframe", pre[k]);
        r->last = data_int_of(c, n);
        snprintf(n, sizeof n, "%stotalticks", pre[k]);
        r->ticks = data_int_of(c, n);
        snprintf(n, sizeof n, "%smindelay", pre[k]);
        r->min = data_int_of(c, n);
        snprintf(n, sizeof n, "%smaxdelay", pre[k]);
        r->max = data_int_of(c, n);
        if (!r->ticks) r->ticks = 1;
    }
    b->going = false;
    b->since = timer_ticks();
    for (int k = 0; k < 3; k++) b->run[k].wait = anim_wait(&b->run[k]);
    return b;
}

/* A part's frame (0x526e20 on <part>.shapeIndex, then the part's update). */
static void part_frame(FertControl *c, const char *part, int frame)
{
    int i = FertControl_findPart(c, part);
    if (i < 0) return;
    FertPart *p = c->parts[i];
    FertAttr *a = FertAttrList_find(&p->attrs, "shapeIndex");
    if (!a) return;
    char v[16];
    snprintf(v, sizeof v, "%d", frame);
    FertAttr_set(a, v);
    FertAttr_eval(a);
    if (Fert_PartEvalHook) Fert_PartEvalHook(p);
}

static void anim_frame(FertControl *c, int frame) { part_frame(c, "picture", frame); }

/* Its update (FEAnimatingGoToButton_v1 0x4c9460): disabled the plain run's first frame; else the run
   of its state (pressed with the separate run, hovered, plain): after its wait it plays its frames over
   its ticks, then waits again (a max delay under 1: it plays on, from the start). */
static void anim_update(FertControl *c)
{
    AnimButton *b = anim_of(c);
    if (!b) return;
    if (c->feflags & 0x10) {   /* 0x4c7a10 */
        anim_frame(c, b->run[0].first);
        return;
    }
    AnimRun *r = &b->run[0];
    bool hover = c->feflags & 4;   /* 0x4c7860 */
    if (data_int_of(c, "seperatehovermousedown") && hover && (s_buttons & 1) && !(c->feflags & 0x30)) r = &b->run[2];   /* 0x4c7880 */
    else if (hover) r = &b->run[1];
    uint32_t now = timer_ticks();
    if (!b->going) {
        if ((int)(b->since + (uint32_t)r->wait) < (int)now) {
            b->going = true;
            b->since = now;
        }
        anim_frame(c, r->first);
    }
    if (b->going) {
        if ((int)(b->since + (uint32_t)r->ticks) < (int)now) {
            if (r->max < 1) b->since = now;
            else {
                b->going = false;
                r->wait = anim_wait(r);
                b->since = now;
                anim_frame(c, r->first);
            }
        }
        if (b->going) {
            int f = (int)(now - b->since) * (r->last - r->first) / r->ticks + r->first;
            if (f < r->first) f = r->first;
            if (f > r->last) f = r->last;
            anim_frame(c, f);
        }
    }
}

/* The screens changed: the typing states of controls no longer on the stack dropped (a screen's controls
   are made with it, so a new screen's start afresh: their v21). */
static void screen_entered(void)
{
    for (int i = 0; i < 8; i++) {
        bool on = false;
        for (int k = 0; k < s_depth && s_types[i].c && !on; k++) on = control_in(s_stack[k], s_types[i].c);
        if (!on) memset(&s_types[i], 0, sizeof s_types[i]);
    }
    lists_drop_stale();
    FEMovieBox_Drop(on_stack);
    FECarLogo_Drop(on_stack);
    FETrackMap_Drop(on_stack);
    FEHudPreview_Drop(on_stack);
    /* (FEChronicleCallBackList 0x506450, made with each of its screens) */
    if (s_depth && !strcmp(screen_attr(FEApp_Top(), "callbacklist"), "ID_CHRONICLE")) FEChronicle_Open();
    /* (FEEndOfEraCallBackList 0x4fcaa0, made with its screen; freed when it's gone, 0x4fe390) */
    bool eoe = s_depth && !strcmp(screen_attr(FEApp_Top(), "callbacklist"), "ID_ENDOFERASCREEN");
    if (eoe && !FEEndOfEra_On()) FEEndOfEra_Open();
    else if (!eoe && FEEndOfEra_On()) FEEndOfEra_Free();
}

/* A word-wrapped watch (FEWordWrapAccessorWatch_v15 0x5225e0 -> 0x522b10): its accessor's text broken into
   lines of the control's width (0x522900 -> 0x522e80), aligned by data.align (0 left, 1 centred, 2
   right), typed when data.animating. */
static bool wordwrap_draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (c->feflags & 0x20) return true;
    int k = FertControl_findPart(c, "label");
    if (k < 0) return true;
    const char *text;
    if (!strcmp(c->id, "NFS5.WORDWRAPCONTROL")) {
        /* FEWordWrapControl (v15 0x522270): its label's own string, wrapped the same way */
        FertAttr *sid = FertAttrList_find(&c->parts[k]->attrs, "stringID");
        FertAttr *tx = FertAttrList_find(&c->parts[k]->attrs, "text");
        text = sid && c->parts[k]->kind == FPART_LOCALIZEDTEXT ? FEStrings_Get(atoi(FertAttr_str(sid))) : tx ? FertAttr_str(tx) : NULL;
    } else {
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
        text = FEAccess_TextArg(a, arg);
    }
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
    bool animating = data_int_of(c, "animating") != 0;
    TypeState *t = type_state(c, animating);
    t->now = timer_ticks();
    if (t->wait) {
        if (t->wait < (int)(timer_ticks() - t->start)) type_start(t, false);
        return true;
    }
    int step;
    float total;
    int n = wrap_lines(font, text, width, &step, &total);
    FeImage *cursor = NULL;
    if (animating) {   /* (0x522900: the cursor with the 3D rooms' detail on, 0x5d9d38) */
        char icons[300];
        FE_ResolvePath("WeeIcons.fsh", icons, sizeof icons);
        FEShapes_Load(icons);
        cursor = FEShapes_Named(icons, "GLNT");   /* (0x5d7bf0) */
    }
    int typed = (int)(t->now - t->start);
    wrap_draw(font, n, step, (float)(r[0] + ox), (float)(r[1] + oy), width, align, colour,
              animating ? typed / 3 : -1, typed % 3, cursor);
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
/* A list's scroll bar (FEEmbossedVerticalSlider_MainFunctionality, 0x154 bytes, vtable 0x5b6a64; 0x4da860),
   over its value (the list's "<name> Sliderbar", FEPositionableAccessor 0x4bbdd0: a place of a count): the
   up and down arrows (WeeIcons.fsh ARup/ARov/ARin, A2up/A2ov/A2in: normal, hovered, pressed), the track
   between them and the knob along it; pressing an arrow steps the value and repeats every 150 ms, the
   knob drags, the track moves the knob to the mouse and drags it. In screen coordinates. */
typedef struct {
    int count, pos;            /* the accessor's +0x14, +0x18 */
    int step;                  /* +0xdc: the arrow's step (-1, 1, 0 none) */
    uint32_t repeat;           /* +0xd8: the repeat's next time (the port's clock; 0 none) */
    int drag_y, drag_pos;      /* +0xe4, +0xe8 */
    int pressed, hover;        /* +0xec, +0xf0: 1 up, 2 down, 3 the knob, 4 the track */
    int base, last;            /* +0xf4: the knob's top at 0, +0xf8: the count it was sized for */
    int knob[4], track[4], down[4], up[4];   /* +0xfc, +0x10c, +0x11c, +0x12c: x y w h */
} ListSlider;

static void slider_setpos(ListSlider *s, int v)   /* 0x4bbe20 */
{
    if (s->count - 1 < v) v = s->count - 1;
    s->pos = v < 0 ? 0 : v;
}

static void slider_setcount(ListSlider *s, int n)   /* 0x4bbef0 (a place past the end back to 0) */
{
    s->count = n;
    if (n <= s->pos) s->pos = 0;
}

/* The knob's length and place from the value (_v11 0x4dacd0): the track less the arrows, less one pixel
   a place, at least 16. */
static void slider_refresh(ListSlider *s)
{
    int room = s->track[3] - 2 * s->down[3];
    if (s->last != s->count) {
        s->last = s->count;
        int h = room - s->count;
        s->knob[2] = 0xe;
        s->knob[3] = 0xf < h ? h : 0x10;
    }
    s->knob[1] = s->base + (s->count ? s->pos * (room - s->knob[3]) / s->count : 0);
}

static void slider_layout(ListSlider *s, int x, int y, int h)   /* 0x4da9c0 */
{
    int knob[4] = {x + 1, y + 0xd, 0xe, 0x1e}, track[4] = {x, y, 0x10, h}, down[4] = {x, y - 0xd + h, 0x10, 0xd},
        up[4] = {x, y, 0x10, 0xd};
    memcpy(s->knob, knob, sizeof knob);
    memcpy(s->track, track, sizeof track);
    memcpy(s->down, down, sizeof down);
    memcpy(s->up, up, sizeof up);
    s->last = 0;
    if (s->last != s->count) {   /* (the knob sized as the refresh does) */
        s->last = s->count;
        int kh = s->track[3] - 2 * s->down[3] - s->count;
        s->knob[2] = 0xe;
        s->knob[3] = 0xf < kh ? kh : 0x10;
    }
    s->base = s->knob[1];
    slider_refresh(s);
}

static bool in_box(const int b[4], int x, int y) { return b[0] <= x && x <= b[0] + b[2] && b[1] <= y && y <= b[1] + b[3]; }

static void slider_hit(ListSlider *s, int x, int y)   /* 0x4dada0 */
{
    s->hover = in_box(s->up, x, y) ? 1 : in_box(s->down, x, y) ? 2 : in_box(s->knob, x, y) ? 3 : in_box(s->track, x, y) ? 4 : 0;
}

/* A press (_v5 0x4daaf0): true when on the bar. */
static bool slider_down(ListSlider *s, int x, int y)
{
    slider_hit(s, x, y);
    bool on = true;
    switch (s->hover) {
    case 0: on = false; break;
    case 1: s->step = -1; Audio_UI(8);   /* 0x4aab80 */ break;
    case 2: s->step = 1; Audio_UI(8);   /* 0x4aab80 */ break;
    case 3: s->drag_y = y, s->drag_pos = s->pos; break;
    case 4:   /* the knob's top to the mouse, then dragged from there */
        s->drag_y = s->knob[1], s->drag_pos = s->pos;
        s->knob[1] = y;
        slider_hit(s, x, y);
        break;
    }
    s->pressed = s->hover;
    if (s->step != 0) {
        s->repeat = app_ticks_ms() + 150;   /* 0x529050 (150 ms, repeating) */
        slider_setpos(s, s->pos + s->step);   /* 0x4bc220 */
        slider_refresh(s);
    }
    return on;
}

/* A move (_v8 0x4db460): the knob dragged while it or the track is under the mouse. */
static void slider_move(ListSlider *s, int x, int y)
{
    slider_hit(s, x, y);
    if (s->pressed == 3 && (s->hover == 3 || s->hover == 4)) {
        int room = s->track[3] - 2 * s->down[3] - s->knob[3];
        if (room) slider_setpos(s, (y - s->drag_y) * s->count / room + s->drag_pos);
        slider_refresh(s);
    }
}

static void slider_up(ListSlider *s)   /* _v9 0x4dac30 */
{
    s->repeat = 0;
    s->step = 0;
    s->pressed = 0;
}

static void slider_held_move(int mx, int my) { slider_move(s_slider_held, mx, my); }
static void slider_held_up(void)
{
    slider_up(s_slider_held);
    s_slider_held = NULL;
}


static void slider_tick(ListSlider *s)   /* _v10 0x4dac80: the timer's repeat while still on the arrow */
{
    uint32_t now = app_ticks_ms();
    if (!s->repeat || now < s->repeat) return;
    s->repeat += 150;
    if (s->pressed == s->hover) {
        slider_setpos(s, s->pos + s->step);
        slider_refresh(s);
    }
}

static uint32_t fade_a(uint32_t c, int a) { return a == 0xff ? c : (uint32_t)(((c >> 24) * (uint32_t)a) / 0xff) << 24 | (c & 0xffffffu); }   /* 0x4d82a0 */

static void slider_draw(ListSlider *s, int ox, int oy, int a)   /* 0x4dafe0 */
{
    const int *t = s->track, *k = s->knob;
    float tx = (float)(t[0] + ox), ty = (float)(t[1] + oy);
    FE2D_Rect(tx, ty, (float)t[2], (float)t[3], fade_a(0x80fedd81u, a));
    FEStyle_Inset(tx, ty, (float)t[2], (float)t[3], a, 3);
    FEStyle_Emboss((float)(k[0] + ox), (float)(k[1] + oy), (float)k[2], (float)k[3], a, 7);
    int cx0 = t[0] + ox, cy0 = s->up[3] + s->up[1] + oy, cx1 = t[0] + ox + t[2], cy1 = oy + s->down[1];
    uint32_t w0 = fade_a(0x19ffffffu, a), w1 = fade_a(0xffffffffu, a), d0 = fade_a(0xcc000000u, a), d1 = fade_a(0x19000000u, a);
    FE2D_GouraudClipped((float)(t[0] + 1 + ox), (float)(k[1] - 3 + oy), 14.0f, 3.0f, w0, w0, w1, w1, cx0, cy0, cx1, cy1);
    FE2D_GouraudClipped((float)(t[0] + 1 + ox), (float)(k[3] + k[1] + oy), 14.0f, 3.0f, d0, d0, d1, d1, cx0, cy0, cx1, cy1);
    char icons[300];
    FE_ResolvePath("WeeIcons.fsh", icons, sizeof icons);   /* ("%sWeeIcons.fsh" 0x5d976c) */
    static const char *const names[6] = {"ARup", "ARov", "ARin", "A2up", "A2ov", "A2in"};   /* 0x5d9764 .. 0x5d973c */
    for (int arrow = 0; arrow < 2; arrow++) {
        int part = arrow + 1, *b = arrow ? s->down : s->up;
        int i = s->pressed == part ? 2 : s->hover == part ? 1 : 0;
        FeImage *im = FEShapes_Named(icons, names[3 * arrow + i]);
        if (im) FE2D_Image(im, (float)(b[0] + ox), (float)(b[1] + oy), (float)im->w, (float)im->h, (uint32_t)a << 24 | 0xffffffu, 0, false);   /* 0x4d95a0 */
    }
    FE2D_Frame(tx, ty, (float)t[2], (float)t[3], (uint32_t)a << 24);   /* 0x4d8cb0 */
}

/* The list boxes' own state: their scroll bar, whether it's needed (+0x1dc), the rows' height shown
   (+0x1c4). */
typedef struct {
    FertControl *c;
    ListSlider slider;
    bool made, scroll;
    int shown;
} ListState;
static ListState s_lists[8];

static bool control_in(const FertControl *w, const FertControl *c);
static void lists_drop_stale(void)   /* (as screen_entered's) */
{
    for (int i = 0; i < 8; i++) {
        bool on = false;
        for (int k = 0; k < s_depth && s_lists[i].c && !on; k++) on = control_in(s_stack[k], s_lists[i].c);
        if (on) continue;
        if (s_slider_held == &s_lists[i].slider) s_slider_held = NULL;
        memset(&s_lists[i], 0, sizeof s_lists[i]);
    }
}

static ListState *list_state(FertControl *c)
{
    for (int i = 0; i < 8; i++)
        if (s_lists[i].c == c) return &s_lists[i];
    for (int i = 0; i < 8; i++)
        if (!s_lists[i].c || i == 7) {
            if (s_slider_held == &s_lists[i].slider) s_slider_held = NULL;
            memset(&s_lists[i], 0, sizeof s_lists[i]);
            s_lists[i].c = c;
            return &s_lists[i];
        }
    return &s_lists[0];
}

/* The rows' height against the box (0x4c4f70: the box's at most, the bar needed then), and the bar's
   count: the first rows the list can start at, from the rows that fit from the bottom (0x4c5030). */
static void list_update(ListState *l, FEAccessor *a, int x, int y, int w, int maxh)
{
    int n = FEAccess_Count(a);
    if (!l->made) {
        l->made = true;
        slider_layout(&l->slider, x + w - 0x10, y, maxh);   /* (0x4da860: at the box's right, 0x4daae0 wide) */
    }
    int total = 0;
    for (int i = 0; i < n; i++) {
        FEPartsRow row;
        if (FEData_PartsRow(a, i, &row)) total += row.height;
    }
    int count = 0;
    if (n) {
        int left = n, sum = 0;
        while (0 < l->shown && 0 < left && sum < l->shown) {
            FEPartsRow row;
            if (FEData_PartsRow(a, left - 1, &row)) sum += row.height;
            left--;
        }
        count = left + 2;
    }
    slider_setcount(&l->slider, count);
    l->shown = total;
    l->scroll = maxh <= total;
    if (l->scroll) l->shown = maxh;
    else slider_setcount(&l->slider, 0);
    if (l->scroll) slider_refresh(&l->slider);
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
    int n = FEAccess_Count(a), pos = FEAccess_Pos(a);
    ListState *l = list_state(c);
    list_update(l, a, r[0], r[1], (int)w, (int)h);
    if (l->scroll) w -= 16.0f;   /* (the bar's room, 0x4e23d0) */
    slider_tick(&l->slider);
    if (s_slider_held != &l->slider) slider_hit(&l->slider, s_mouse[0], s_mouse[1]);   /* (_v8's, _v7's on leaving) */
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
    for (int i = l->slider.pos; i < n; i++) {
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
        if (row.status[0]) {   /* (at its control's x + 3: ID_INSTALLYPOS, ID_DAMAGEPOS) */
            FertControl *ip = row.status_pos ? control_named(FEApp_Top(), row.status_pos) : NULL;
            int32_t ir[4] = {0, 0, 0, 0};
            if (ip) control_rect(ip, ir);
            list_text(f, ip ? (float)(ir[0] + ox + 3) : x + w * 0.7f, yy + 1.0f, row.status, chosen ? fg : row.status_dim ? 0x80000000u : fg, row.status_align);
        }
        if (row.has_price) {
            char pb[24];
            snprintf(pb, sizeof pb, "%d", (int)row.price);   /* ("%d" 0x5cd1b0) */
            list_text(f, px, yy + 1.0f, pb, fg, 2);
        }
        yy += 14.0f;
    }
    if (l->scroll) {
        FE2D_DepthStep();
        slider_draw(&l->slider, ox, oy, 0xff);
    }
    return true;
}

/* The knockout's slots (FEKnockoutTrackListControl_v15 0x4f9730): the slots raced (Game.NumKOOpponents of
   the seven) shaded (0x80fedd81), the chosen slot (Knockout.TrackNumber, back to 0 when past them) in
   the highlight, lines between the rows and down at 0xb, 0x71, 0xa4 and 0xcc, an outline; a press
   (_v9 0x4f9a10) chooses the slot there. */
static bool ko_list_draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (c->feflags & 0x20) return true;
    FEAccessor *t = FEAccess_Find("Knockout.TrackNumber");
    int n = FEData_KOOpponents();
    if (!t) return true;
    if (FEAccess_Pos(t) >= n) FEAccess_SetPos(t, 0);
    int32_t r[4];
    control_rect(c, r);
    float x = (float)(r[0] + ox), y = (float)(r[1] + oy), w = (float)(r[2] - r[0]);
    float rh = (float)(r[3] - r[1]) * 0.14285715f;   /* (0x5b7e78: a seventh) */
    const uint32_t bg = 0x80fedd81u, hl = 0xffffbb00u, ln = 0x80000000u, k = 0xff000000u;
    FE2D_DepthStep();
    FE2D_Gouraud(x, y, w, rh * (float)n, bg, bg, bg, bg);
    FE2D_DepthStep();
    FE2D_Gouraud(x, y + (float)FEAccess_Pos(t) * rh, w, rh, hl, hl, hl, hl);
    FE2D_DepthStep();
    for (int i = 1; i < n; i++) FE2D_Gouraud(x, y + (float)i * rh, w, 1.0f, ln, ln, ln, ln);
    static const int cols[4] = {0xb, 0x71, 0xa4, 0xcc};
    for (int i = 0; i < 4; i++) FE2D_Gouraud(x + (float)cols[i], y, 1.0f, rh * (float)n, ln, ln, ln, ln);
    const float h = rh * (float)n;
    FE2D_Gouraud(x, y, w, 1.0f, k, k, k, k);
    FE2D_Gouraud(x, y + h - 1.0f, w, 1.0f, k, k, k, k);
    FE2D_Gouraud(x, y, 1.0f, h, k, k, k, k);
    FE2D_Gouraud(x + w - 1.0f, y, 1.0f, h, k, k, k, k);
    return true;
}

static bool ko_list_click(FertControl *c, int32_t my)
{
    if (strcmp(c->id, "NFS5.KNOCKOUTTRACKLIST")) return false;
    FEAccessor *t = FEAccess_Find("Knockout.TrackNumber");
    int32_t r[4];
    control_rect(c, r);
    if (!t || r[3] <= r[1]) return true;
    int slot = (my - r[1]) * 7 / (r[3] - r[1]);
    if (slot >= 0 && slot < FEData_KOOpponents()) FEAccess_SetPos(t, slot);
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
    ListState *l = list_state(c);
    if (l->scroll && slider_down(&l->slider, mx, my)) {   /* (0x4c6b30's to the bar first) */
        s_slider_held = &l->slider;
        return true;
    }
    int n = FEAccess_Count(a);
    int yy = r[1];
    for (int i = l->slider.pos; i < n && yy - r[1] <= l->shown; i++) {
        FEPartsRow row;
        if (!FEData_PartsRow(a, i, &row)) continue;
        int top = yy + (row.label ? 13 : 0);
        if (my >= top && my < top + 14) {
            FEAccess_SetPos(a, i);
            Audio_UI(5);   /* 0x4aab80 */
            return true;
        }
        yy += row.height;
    }
    return true;
}

/* Factory Driver's pictures: FEFactoryDriverPictureControl (v21 0x5199a0, v15 0x519b10), the narrator's
   face of FactoryPeople.fsh (data_source "profile": the team profile's person), and
   FEFactoryDriverMapControl (v21 0x519c10), the mission's map (FEData\Factory\<+0x3f>.fsh, its first
   shape), each at its control's place in white (0x4d8280 at the fade). */
static bool factory_draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (c->feflags & 0x20) return true;
    const uint8_t *m = FEFactory_Mission();
    char path[300];
    int k = 0;
    if (!strcmp(c->id, "NFS5.FACTORYPIC")) {
        FE_ResolvePath("FactoryPeople.fsh", path, sizeof path);   /* ("%sFactoryPeople.fsh" 0x5dd29c) */
        const char *src = data_str(c, "data_source");
        if (src && !strcmp(src, "profile")) {
            FEAccessor *a = FEAccess_Find("Factory.ProfileNum");
            static const int8_t face[6] = {2, 0, 3, 1, 4, 5};
            int p = a ? FEAccess_Pos(a) : 0;
            k = p >= 0 && p < 6 ? face[p] : 0;
        } else {
            int16_t n = 0;
            if (m) memcpy(&n, m + 0x1e, 2);
            k = n;
        }
    } else {
        if (!m) return true;
        snprintf(path, sizeof path, "FEData\\Factory\\%s.fsh", (const char *)m + 0x3f);   /* ("%s%s.fsh" 0x5cccc8) */
    }
    FEShapes_Load(path);
    FeImage *img = FEShapes_Image(path, k);
    if (!img) return true;
    int32_t pos[2] = {0, 0};
    FertAttr *pa = FertAttrList_find(&c->attrs, "pos");
    if (pa) FertAttr_point(pa, pos);
    FE2D_DepthStep();
    FE2D_Image(img, (float)(pos[0] + ox), (float)(pos[1] + oy), (float)img->w, (float)img->h, 0xffffffffu, 0, false);
    return true;
}

/* The gouraud bar (0x521530) from..to per cent of w: up to 50 c0 to c50, past it c50 to c100 (without
   c100: c0, their half-way mix, c50), each half's end colour by how far it reaches; going left the first
   half's start colour by how far it starts in. Its drop shadow (0x520780) at 3, 4, going left from its
   right end back, at -4, 4. */
static void gouraud_bar(int from, int to, float x, float y, float w, float h, uint32_t c0, uint32_t c50,
                        uint32_t c100, bool shadow, bool left)
{
    if (to < from) return;
    float wi = (float)(int)w, half = wi * 0.5f;
    uint32_t A = c0, B = c100 ? c50 : mix(c0, c50, 0x80 / 255.0f), C = c100 ? c100 : c50;
    float sx = (float)from * wi * 0.01f + x;
    float s1 = ((float)(to < 50 ? to : 50) - (float)from) * wi * 0.01f;
    s1 = s1 < 0 ? 0 : s1 > half ? half : s1;
    float s2 = ((float)to - (float)(from > 50 ? from : 50)) * wi * 0.01f;
    s2 = s2 < 0 ? 0 : s2 > half ? half : s2;
    int t1 = (int)(s1 * 255.0f / half), t2 = (int)(s2 * 255.0f / half);
    uint32_t e = C;
    if (left) {
        A = mix(A, B, (float)(int)(255.0f - s1 * 255.0f / half) / 255.0f);
        if (from < 1) B = mix(B, C, (float)(int)(255.0f - s2 * 255.0f / half) / 255.0f);
        else e = mix(B, C, (float)t2 / 255.0f);
    } else {
        if (from < 1) B = mix(A, B, (float)t1 / 255.0f);
        else A = mix(A, B, (float)(int)(255.0f - s1 * 255.0f / half) / 255.0f);
        e = mix(B, C, (float)t2 / 255.0f);
    }
    FE2D_DepthStep();
    if (s1 > 0) FE2D_Gouraud(sx, y, s1, h, A, B, A, B);
    if (s2 > 0) FE2D_Gouraud(s1 + sx, y, s2, h, B, e, B, e);
    float tw = s1 + s2;
    if (tw > 4.0f && shadow) {
        if (left) FEStyle_DropShadow(tw + sx, y, -tw, h, 0xff, -4, 4);
        else FEStyle_DropShadow(sx, y, tw, h, 0xff, 3, 4);
    }
}

static bool indicator_draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (!strcmp(c->id, "NFS5.CheckBox")) {
        /* FECheckBox (vtable 0x5b5b38; _v1 0x4c32b0, _v14 0x4c3300): its picture checkBoxPic the frame
           data.onIndex or data.offIndex by its data.boolAccessor, then drawn as any control */
        FEAccessor *a = FEAccess_Find(data_str(c, "boolAccessor"));
        if (a) part_frame(c, "checkBoxPic", data_int_of(c, FEAccess_Pos(a) ? "onIndex" : "offIndex"));
        return false;
    }
    if (FEMovieBox_Draw(c, ox, oy)) return true;
    if (FEChronicle_Draw(c, ox, oy)) return true;
    if (FECarLogo_Draw(c, ox, oy)) return true;
    if (FETrackMap_Draw(c, ox, oy)) return true;
    if (FEHudPreview_Draw(c, ox, oy)) return true;
    if (FECtrl_Draw(c, ox, oy)) return true;
    if (FEShowcase_Draw(c, ox, oy)) return true;
    if (!strcmp(c->id, "NFS5.FACTORYPIC") || !strcmp(c->id, "NFS5.FACTORYMAP")) return factory_draw(c, ox, oy);
    if (!strcmp(c->id, "NFS5.CarSettingSlider")) return setting_draw(c, ox, oy);
    if (!strcmp(c->id, "NFS5.ACCOUNTTHING")) return account_draw(c, ox, oy);
    if (!strcmp(c->id, "NFS5.WORDWRAPACCESSORWATCH") || !strcmp(c->id, "NFS5.WORDWRAPCONTROL")) return wordwrap_draw(c, ox, oy);
    if (!strcmp(c->id, "NFS5.ListBox")) return listbox_draw(c, ox, oy);
    if (!strcmp(c->id, "NFS5.INPUTBOX")) return inputbox_draw(c, ox, oy);
    if (!strcmp(c->id, "NFS5.PLAYERDRIVERPICTURE") || !strcmp(c->id, "NFS5.PLAYERPICTURE")) return picture_draw(c, ox, oy);
    if (!strcmp(c->id, "NFS5.KNOCKOUTTRACKLIST")) return ko_list_draw(c, ox, oy);
    if (is_colourbox(c)) {
        /* FEComboBox's draw (0x4c53e0) for a colour box (+0x1de): the bar, then its colour (its slot 0x74,
           the accessor's) as a 20 x 10 swatch 40 from its right and 4 down, outlined in black. (The custom
           colour picker, FEColorSelectBox_v17 0x4c3800, and the drop-down pend.) */
        if (c->feflags & 0x20) return true;
        if (FEColour_Open_On(c)) return true;   /* (_v17 0x4c3800: the picker instead, drawn last) */
        FEControl_Draw(c, ox, oy);
        FEAccessor *a = FEAccess_Find(data_str(c, "accessor"));
        if (!a) return true;
        FEAccess_AllowCustom(a, data_int_of(c, "allowcustom") != 0);   /* _v21 0x4c37c0 */
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
    /* FEGouraudIndicatorBar_v15 0x4ca170: its value (0x4ca270: the accessor's, or without one
       data.othersource for the car compare's stats), drawn 0 .. it, or going left 100 less it .. 100 */
    const char *path = data_str(c, "accessor");
    FEAccessor *a = path && *path ? FEAccess_Find(path) : NULL;
    int pct;
    if (a) {
        char arg[32] = "";
        const char *pp = strchr(path, '(');
        if (pp) {
            snprintf(arg, sizeof arg, "%s", pp + 1);
            char *e = strchr(arg, ')');
            if (e) *e = 0;
        }
        pct = FEAccess_Percent(a, arg);
    } else {
        const char *src = data_str(c, "othersource");
        if (!src || !*src) return true;
        pct = FEAccess_Percent(FEAccess_Find("CarCompare.Data"), src);
    }
    bool left = data_int_of(c, "goingleft") != 0;
    int32_t r[4];
    control_rect(c, r);
    gouraud_bar(left ? 100 - pct : 0, left ? 100 : pct, (float)(r[0] + ox), (float)(r[1] + oy), (float)(r[2] - r[0]),
                (float)(r[3] - r[1]), data_colour(c, "c0"), data_colour(c, "c50"), data_colour(c, "c100"),
                data_int_of(c, "hasshadow") != 0, left);
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

/* An input box's tick (FUN_004cc000): its accessor's string (stars for showstars) into hitext's text;
   one always focused (data.alwaysfocused, FEInputBox_v20 0x4cbe70) takes the focus (0x4c7940). */
static void inputbox_sync(FertControl *w, FertControl *c)
{
    if (data_int_of(c, "alwaysfocused") && !(c->feflags & 0x40)) {
        c->feflags |= 0x40;
        w->focus = c;
        refresh_state(c);
    }
    int k = FertControl_findPart(c, "hitext");
    if (k < 0) return;
    FertAttr *t = FertAttrList_find(&c->parts[k]->attrs, "text");
    FEAccessor *a = FEAccess_Find(data_str(c, "accessor"));
    char v[64];
    snprintf(v, sizeof v, "%s", a ? FEAccess_Text(a) : "BAD ACCESSOR!");   /* (0x5d8abc) */
    if (data_int_of(c, "showstars"))
        for (char *p = v; *p; p++) *p = '*';
    if (t && strcmp(FertAttr_str(t), v)) {
        FertAttr_set(t, v);
        FertControl_Update(c);
    }
}

/* Its draw (0x4cc390): its parts, the cursor blinking while focused (TIMER_ticks & 0x3f under 0x20),
   then the focused or hovered inset, else the drop shadow (but noshadow) and the shine. */
static bool inputbox_draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (c->feflags & 0x20) return true;
    FEControl_Draw(c, ox, oy);
    int32_t r[4];
    FertControl_rect(c, r);   /* (0x4c8240: autosized, its parts' bounds) */
    float x = (float)(r[0] + ox), y = (float)(r[1] + oy), w = (float)(r[2] - r[0]), h = (float)(r[3] - r[1]);
    bool focused = (c->feflags & 0x40) != 0;
    uint32_t ticks = (uint32_t)((uint64_t)app_ticks_ms() * 128u / 1000u);   /* TIMER_ticks */
    int k = FertControl_findPart(c, "cursor");
    if (focused && (ticks & 0x3f) < 0x20 && k >= 0) {
        int32_t pos[2] = {0, 0};
        FertAttr *pa = FertAttrList_find(&c->attrs, "pos");
        if (pa) FertAttr_point(pa, pos);
        FE2D_DepthStep();
        FEPart_Draw(c->parts[k], pos[0] + ox, pos[1] + oy);
    }
    if (!focused && !(c->feflags & 4)) {
        if (!data_int_of(c, "noshadow")) FEStyle_DropShadow(x, y, w, h, 0xff, 4, 4);
        FEStyle_Shine(x, y, w, h, 0xff);
        return true;
    }
    FEStyle_Inset(x, y, w, h, 0xff, 3);
    return true;
}

/* The create player screen's pictures (FEPlayerDriverPicture, vtable 0x5b64b0): the driver chosen
   (0x5d8f2c, its data.ournumber taken on a press, _v9 0x4cf370) glows 5 out (_v15 0x4cf1f0), a hovered
   one is inset, the others cast a shadow. The select name screen's (FEPlayerPicture_v15 0x4cf630): the
   chosen player's driver, its stem's digit (0 .. 8) of ALLdrivr.fsh, at its place when there are players
   (FE_LoadDriverPictures 0x4db650 converts it, or the player's own picture "<name>.<ext>", into
   SaveData\pic16.fsh with gimme.dll first). */

static bool picture_draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (c->feflags & 0x20) return true;
    int32_t r[4];
    FertControl_rect(c, r);   /* (0x4c8240: autosized, its parts' bounds) */
    float x = (float)(r[0] + ox), y = (float)(r[1] + oy), w = (float)(r[2] - r[0]), h = (float)(r[3] - r[1]);
    if (!strcmp(c->id, "NFS5.PLAYERPICTURE")) {
        FEAccessor *a = FEAccess_Find("UserList.UserList");
        if (!a || !FEAccess_Count(a)) return true;
        const char *stem = FESave_PlayerStem(FEAccess_Pos(a));
        PENDING_NOTE(0x4db650, "a player's own picture (<name>.<ext> through gimme.dll's ConvertFile)");
        int n = stem[0] - '0';
        n = n < 1 ? 0 : n > 8 ? 8 : n;
        char path[300];
        FE_ResolvePath("ALLdrivr.fsh", path, sizeof path);   /* ("%sAllDrivr.fsh" 0x5d9824 of 0x65b334) */
        FEShapes_Load(path);
        FeImage *img = FEShapes_Image(path, n);
        if (!img) return true;
        FE2D_DepthStep();
        FE2D_Image(img, x, y, (float)img->w, (float)img->h, 0xffffffffu, 0, false);
        return true;
    }
    FEControl_Draw(c, ox, oy);   /* (0x4c7e30) */
    bool chosen = s_driver_pic == data_int_of(c, "ournumber");
    FE2D_DepthStep();
    if (chosen) FEStyle_Glow(x, y, w, h, 0xff, 5);
    if (c->feflags & 4) FEStyle_Inset(x, y, w, h, 0xff, 3);
    else if (!chosen) FEStyle_DropShadow(x, y, w, h, 0xff, 4, 4);
    return true;
}

static bool picture_click(FertControl *c)
{
    if (strcmp(c->id, "NFS5.PLAYERDRIVERPICTURE")) return false;
    s_driver_pic = data_int_of(c, "ournumber");
    return true;
}

/* The keys to the focused input box (FEInputBox_v4 0x4cc660, the characters Windows makes of them): the
   keypad's digits, backspace, Enter (the focus let go, 0x4c7970, and the accessor's change, slot 3),
   the characters 0x20 .. 0xfe but %. */
static FertControl *focused_input(FertControl *w)
{
    for (int i = 0; w && i < w->nchildren; i++) {
        FertControl *c = w->children[i];
        if (c->sub) {
            FertControl *f = focused_input(c->sub);
            if (f) return f;
        }
        if (!strcmp(c->id, "NFS5.INPUTBOX") && (c->feflags & 0x40) && !(c->feflags & 0x30)) return c;
    }
    return NULL;
}


static void inputbox_keys(FertControl *c, const uint8_t *k, int nk)
{
    FEAccessor *a = FEAccess_Find(data_str(c, "accessor"));
    uint8_t held[KEY_COUNT];
    bool shift = plat_keys(held) && (held[0x2a] || held[0x36]);
    for (int i = 0; i < nk && a; i++) {
        int ch = Input_KeyChar(k[i], shift);
        if (!ch || (ch != 8 && ch != 13 && (ch < 0x20 || ch > 0xfe || ch == '%'))) continue;
        if (ch == 13) {
            if (!data_int_of(c, "alwaysfocused")) {   /* (0x4c7970; an always focused box takes it back) */
                c->feflags &= ~0x40u;
                refresh_state(c);
            }
        } else {
            FEAccess_TypeChar(a, ch);
        }
        Audio_UI(9);   /* 0x4aab80 */
    }
}

/* The accessor watches' texts (FEAccessorWatch's tick: the accessor's text for the path's argument into
   its label), in the screen and its panels. */
static void update_watches(FertControl *w)
{
    for (int i = 0; w && i < w->nchildren; i++) {
        FertControl *c = w->children[i];
        if (c->sub) update_watches(c->sub);
        if (!strcmp(c->id, "NFS5.INPUTBOX")) {
            inputbox_sync(w, c);
            continue;
        }
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

/* The controls under the mouse asking for the pointer's hand (FEControl's tick 0x4c7b20: hovered, not
   disabled or hidden, its slot 24 0x4c7b10; the 3D car control's own, front/fecar3d.c). */
static void hover_pointer(FertControl *w)
{
    for (int i = 0; w && i < w->nchildren; i++) {
        FertControl *c = w->children[i];
        if (c->sub && !(c->feflags & 0x20)) hover_pointer(c->sub);
        if ((c->feflags & 4) && !(c->feflags & 0x30) && strcmp(c->id, "NFS5.3DCAR")) FEPointer_Want(1);
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
        /* (data.nolabel, +0x1fc: 0 the value right-aligned past the label; 1 the value at the left in
           the label's place; 2 and 3 no value, the box shows its label only (the CD player's); 4's
           own centred layout pends, drawn as 0) */
        int nl = data_int_of(c, "nolabel");
        if (nl == 2 || nl == 3) continue;
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
        if (nl == 1) FE2D_Text(font, (float)(pos[0] + 5), (float)(pos[1] + 2), 0, 0xffffffffu, copy, 1.0f, 1.0f);
        else FE2D_Text(font, (float)(pos[0] + width - 13), (float)(pos[1] + 2), 2, 0xffffffffu, copy, 1.0f, 1.0f);
    }
}

/* A combo box's selection box (FEComboBox 0x4c4960, open: 0x4c79a0): the accessor's rows, 14 pixels each
   (FEPositionableAccessor_v26 0x4bc2e0), below the bar (part selectionbox: at (0, data.height),
   data.width wide), as tall as the rows up to data.selectionboxmaxheight, with a scroll bar when they
   don't fit (+0x1dc). It slides open (and shut) over 16 timer ticks (0x4c5030). Its back is a gouraud
   from 0xffffbb00 to that blended with 0xb282a8b0 by its height of the maximum, framed in black; the row
   under the mouse is on the hover colour (data.highlightcolorhover, else 0xffbc0000) in white, the others
   in the text colour (data.textcolor, else black), each 3 in and 2 down (0x4bbf30). One is open at a
   time. (The selection box's slide direction and the label's offset from the click are not ported.) */
static struct {
    FertControl *c;
    bool open;
    uint32_t t0;          /* +0x1d4: the timer at the last open or close */
    int hover;            /* +0x1a8, -1 none */
    ListSlider slider;    /* +0x1bc */
    bool scroll;          /* +0x1dc */
} s_combo;

typedef struct { int x, y, w, h, maxh, n, shown; } ComboBox;

/* a to b by p of 255, each channel in integers (0x4d8300) */
static uint32_t blend255(uint32_t a, uint32_t b, int p)
{
    uint32_t o = 0, q = (uint32_t)(0xff - p);
    for (int sh = 0; sh < 32; sh += 8) o |= ((((a >> sh) & 0xff) * q + ((b >> sh) & 0xff) * (uint32_t)p) / 0xff) << sh;
    return o;
}

static void combo_box(FertControl *c, FEAccessor *a, ComboBox *b)
{
    int32_t r[4];
    control_rect(c, r);
    int sw = data_int_of(c, "selectionboxwidth");
    b->x = r[0];
    b->y = r[1] + data_int_of(c, "height");
    b->w = sw > 0 ? sw : data_int_of(c, "width");
    b->maxh = data_int_of(c, "selectionboxmaxheight");
    b->n = FEAccess_Count(a);
    int total = b->n * 14;   /* (v25 0x4bc2c0) */
    b->h = total < b->maxh ? total : b->maxh;   /* 0x4c4f70 */
    s_combo.scroll = b->maxh <= total;
    /* the bar's count: the rows from the first that the list can start at (0x4c5030) */
    int fit = b->h / 14;
    b->shown = fit;
    slider_setcount(&s_combo.slider, s_combo.scroll ? b->n - fit + 2 : 0);
}

static void combo_open(FertControl *c)
{
    FEAccessor *a = FEAccess_Find(data_str(c, "accessor"));
    if (!a) return;
    memset(&s_combo, 0, sizeof s_combo);
    s_combo.c = c;
    s_combo.open = true;
    s_combo.hover = -1;
    ComboBox b;
    combo_box(c, a, &b);
    slider_layout(&s_combo.slider, b.x + b.w - 0x10, b.y, b.maxh);
    combo_box(c, a, &b);
    if (s_combo.scroll) {   /* (the chosen row in view) */
        int p = FEAccess_Pos(a) - b.shown / 2;
        slider_setpos(&s_combo.slider, p);
        slider_refresh(&s_combo.slider);
    }
    Audio_UI(2);   /* 0x4aab80 */
    s_combo.t0 = timer_ticks();
}

static void combo_close(int sound)
{
    if (!s_combo.open) return;
    s_combo.open = false;
    s_combo.t0 = timer_ticks();
    if (s_slider_held == &s_combo.slider) s_slider_held = NULL;
    if (sound) Audio_UI(sound);
}

/* The height shown now: up to the box's over the 16 ticks after opening, down after closing. */
static int combo_height(int h)
{
    int e = (int)(timer_ticks() - s_combo.t0) * h / 16;
    if (h < e) e = h;
    return s_combo.open ? e : h - e;
}

/* The mouse with a combo box open (0x4c66c0): a press on its scroll bar drags it; a release on a row
   picks it (sound 5) and closes; a release on the bar closes it (sound 3); a press elsewhere closes it and
   goes on to what is under the mouse. */
static bool combo_mouse(bool down, int32_t mx, int32_t my)
{
    if (!s_combo.open || !s_combo.c) return false;
    FEAccessor *a = FEAccess_Find(data_str(s_combo.c, "accessor"));
    if (!a) {
        combo_close(0);
        return false;
    }
    ComboBox b;
    combo_box(s_combo.c, a, &b);
    int32_t r[4];
    control_rect(s_combo.c, r);
    bool in_list = mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h;
    bool in_bar = mx >= r[0] && mx < r[0] + data_int_of(s_combo.c, "width") && my >= r[1] && my < b.y;
    if (down) {
        if (in_list && s_combo.scroll && slider_down(&s_combo.slider, mx, my)) {
            s_slider_held = &s_combo.slider;
            return true;
        }
        if (in_list || in_bar) return true;
        combo_close(3);
        return false;
    }
    if (in_bar) {
        combo_close(3);
        return true;
    }
    if (!in_list) return false;
    int first = s_combo.scroll ? s_combo.slider.pos : 0;
    if (s_combo.scroll && mx >= b.x + b.w - 0x10) return true;   /* (the bar's) */
    int row = first + (my - b.y) / 14;
    if (row < b.n) {
        /* (0x4c66c0: a colour box's "custom" row opens the picker, 0x4c35c0, the place kept) */
        FertControl *c = s_combo.c;
        if (is_colourbox(c) && row == FEAccess_CustomRow(a)) FEColour_Open(c);
        else FEAccess_SetPos(a, row);   /* (slot 0x2c) */
        combo_close(5);
    }
    return true;
}

static void combo_draw(void)
{
    if (!s_combo.c) return;
    FEAccessor *a = FEAccess_Find(data_str(s_combo.c, "accessor"));
    if (!a) return;
    ComboBox b;
    combo_box(s_combo.c, a, &b);
    int e = combo_height(b.h);
    if (!s_combo.open && e <= 0) {
        s_combo.c = NULL;
        return;
    }
    if (b.maxh <= 0 || e <= 0) return;
    uint32_t top = 0xffffbb00u;
    uint32_t bot = blend255(top, 0xb282a8b0u, b.h * 0xff / b.maxh);
    float x = (float)b.x, y = (float)b.y, w = (float)b.w;
    FE2D_DepthStep();
    FE2D_Gouraud(x, y, w, (float)e, top, top, bot, bot);   /* 0x4d9160 */
    uint32_t hover_c = data_colour(s_combo.c, "highlightcolorhover"), text_c = data_colour(s_combo.c, "textcolor");
    if (!hover_c) hover_c = 0xffbc0000u;   /* 0x4d8260 */
    text_c |= 0xff000000u;
    int first = s_combo.scroll ? s_combo.slider.pos : 0;
    float rw = s_combo.scroll ? w - 16.0f : w;
    s_combo.hover = -1;
    if (s_combo.open && e == b.h && s_mouse[0] >= b.x && s_mouse[0] < b.x + (int)rw && s_mouse[1] >= b.y && s_mouse[1] < b.y + b.h)
        s_combo.hover = first + (s_mouse[1] - b.y) / 14;
    HudFont *f = list_font();
    FE2D_DepthStep();
    for (int i = first, yy = b.y; i < b.n && yy + 14 <= b.y + e; i++, yy += 14) {
        uint32_t fg = text_c;
        if (i == s_combo.hover) {
            FE2D_Gouraud(x + 1.0f, (float)yy, rw - 2.0f, 14.0f, hover_c, hover_c, hover_c, hover_c);
            fg = 0xffffffffu;   /* 0x4d8280 */
        }
        if (is_colourbox(s_combo.c)) {
            /* FEColourAccessor_v15 0x4b8f70: a 20 wide swatch framed in black, the "custom" row its
               text (0x263) */
            if (i == FEAccess_CustomRow(a)) {
                const char *t = FEStrings_Get(0x263);
                if (t) list_text(f, x + 1.0f + 3.0f, (float)yy + 2.0f, t, fg, 0);
            } else {
                FE2D_Rect(x + 1.0f + 3.0f, (float)yy + 2.0f, 20.0f, 10.0f, FEAccess_ColourAt(a, i) | 0xff000000u);
                FE2D_Frame(x + 1.0f + 3.0f, (float)yy + 2.0f, 20.0f, 10.0f, 0xff000000u);
            }
            continue;
        }
        list_text(f, x + 1.0f + 3.0f, (float)yy + 2.0f, FEAccess_TextAt(a, i), fg, 0);
    }
    if (s_combo.scroll && e == b.h) {
        slider_tick(&s_combo.slider);
        if (s_slider_held != &s_combo.slider) slider_hit(&s_combo.slider, s_mouse[0], s_mouse[1]);
        slider_refresh(&s_combo.slider);
        FE2D_DepthStep();
        slider_draw(&s_combo.slider, 0, 0, 0xff);
    }
    FE2D_DepthStep();
    FE2D_Frame(x, y, w, (float)e, 0xff000000u);   /* 0x4d8cb0 */
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

/* The callback lists whose update runs FECallBackList's (0x50c2f0): the base list's (ID_NONE, for screens
   without one, 0x4e6d60), the ones that keep it (knockout, track records, car compare, career first, dealer,
   Factory Driver's victory: their vtables' slot 1) and the ones that call it first (car setup 0x4f1130,
   used cars 0x4f6930, end of era 0x4fe730, victory 0x506380, chronicle 0x506e90, credits 0x50e350). */
static bool base_cbl(const char *cbl)
{
    static const char *const names[] = {"", "ID_NONE", "ID_KNOCKOUTSCREEN", "ID_TRACKRECORDSCREEN", "ID_CARCOMPARECBL",
                                        "ID_CAREERFIRST", "ID_DEALERCBL", "ID_FDVICTORYSCREEN", "ID_CARSETUP", "ID_USEDCARCBL",
                                        "ID_ENDOFERASCREEN", "ID_VICTORYSCREEN", "ID_CHRONICLE", "ID_CREDITS"};
    for (size_t i = 0; i < sizeof names / sizeof *names; i++)
        if (!strcmp(cbl, names[i])) return true;
    return false;
}

static void screen_update(FertControl *w)   /* the screen's callback list's update (its v1) */
{
    const char *cbl = screen_attr(w, "callbacklist");
    {
        /* FEMainScreen_v1 0x50faf0 (the main list's, which every screen's runs too): the create player's
           OK with a name typed, the select name's remove and continue with players (the latter where the
           former is). (Its multiplayer buttons and the menu music's turn are elsewhere or pend.) */
        FEAccessor *users = FEAccess_Find("UserList.UserList");
        bool any = users && FEAccess_Count(users) > 0;
        enable(w, "ID_CREATEPLAYEROKBUTTON", FEData_NameInput()[0] != 0);
        enable(w, "ID_REMOVEBUTTON", any);
        if (control_named(w, "ID_REMOVEBUTTON")) enable(w, "ID_CONTINUEBUTTON", any);
    }
    if (base_cbl(cbl)) {
        /* FECallBackList_v1 0x50c2f0 (the base list's; its derived ones' run it first): the colour box for
           a new car, the garage for an owned one; the replays' buttons with replays; damage but in the career and Factory Driver; the laps on a circuit. (Its dialogs,
           +0x270 / +0x274: the delete replay's in FEApp_Frame; the settings car's ABS, traction control and roof where the car has them (CARSETTINGS.CAR, +0x27c:
           its +0x2e2, +0x2e3, +0x1bf); the multiplayer's collision pends.) */
        bool owned = FEData_MergedOwned();
        hide(w, "ID_MERGEDLISTCOLOR", owned);
        hide(w, "ID_MERGEDLISTCARSETUP", !owned);
        FEAccessor *rl = FEAccess_Find("Replay.List");
        bool replays = rl && FEAccess_Count(rl) > 0;
        enable(w, "ID_LOADREPLAYBUTTON", replays);
        enable(w, "ID_REMOVEREPLAYBUTTON", replays);
        enable(w, "ID_DAMAGE", s_gametype != 4 && s_gametype != 9 && s_gametype != 3);   /* (3: 0x48c9d0, no session) */
        enable(w, "ID_LAPSSELECTOR", FEData_TrackLooped(s_gametype));
        const uint8_t *sc = FEData_SettingsCar();
        if (sc) {
            enable(w, "ID_ABS", sc[0x2e2] != 0);
            enable(w, "ID_TRACTION", sc[0x2e3] != 0);
            enable(w, "ID_ROOF", sc[0x1bf] != 0);
        }
    }
    if (!strcmp(cbl, "ID_GARAGECBL")) garage_update(w);
    else if (!strcmp(cbl, "ID_USEDCARCBL")) usedcar_update(w);
    else if (!strcmp(cbl, "ID_CARSETUP")) {
        /* FECarSetupCallbackList_v1 0x4f1130: install for a part in stock, uninstall for a fitted one,
           uninstall shown off with none. (The gear ratio panel for +0x1ac 0x1f8 pends.) */
        bool fitted = false, any = FEData_InstallState(&fitted);
        enable(w, "ID_PARTSDESC", any);
        if (!any) {
            hide(w, "ID_INSTALLBUTTON", true);
            hide(w, "ID_UNINSTALLBUTTON", false);
            enable(w, "ID_UNINSTALLBUTTON", false);
        } else {
            enable(w, "ID_UNINSTALLBUTTON", true);
            hide(w, "ID_INSTALLBUTTON", fitted);
            hide(w, "ID_UNINSTALLBUTTON", !fitted);
        }
    }
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
    else if (!strcmp(cbl, "ID_ENDOFERASCREEN")) FEEndOfEra_Update();   /* _v1 0x4fe730 */
    else if (!strcmp(cbl, "ID_CHRONICLE")) {
        /* FEChronicleCallBackList_v1 0x506e90: the era's button selected, the others not */
        static const uint32_t ids[5] = {0x5dbb24, 0x5dbb14, 0x5dbb04, 0x5dbaf4, 0x5dbae4};
        for (int i = 0; i < 5; i++) set_flag(w, exe_str(ids[i]), 8, i == FEChronicle_Era());
    }
    else if (!strcmp(cbl, "ID_FACTORYCBL")) {
        /* FEFactoryDriverCBL_v1 0x519270: a changed Factory.MissionCheat makes that many missions done
           (0x51aa20) */
        FEAccessor *a = FEAccess_Find("Factory.MissionCheat");
        int v = a ? FEAccess_Pos(a) : 0;
        if (v != s_fd_cheat) {
            s_fd_cheat = v;
            FEFactory_Cheat(v + 1, false);
        }
    }
}

/* The menus' music (FEMainScreen_v1 0x50faf0): a song of "Menu*" (0x5dca14) every 0x1e00 ticks (a minute),
   the one before faded out, at the options' level (MUSICLEVEL). */
static void menu_music(void)
{
    if (!s_sound) return;
    /* (FEScreenOptions_v1 0x5111d0 runs the base list's update, not the main screen's: no menu songs
       there, the CD player's instead; its first tick chooses the song shown, which plays it) */
    if (!strcmp(screen_attr(FEApp_Top(), "callbacklist"), "ID_OPTIONSSCREEN")) {
        FEAccessor *ti = FEAccess_Find("CDPlayer.Title");
        if (!s_cd_started && ti) FEAccess_SetPos(ti, FEAccess_Pos(ti));
        s_cd_started = true;
        return;
    }
    int32_t now = (int32_t)((uint64_t)app_ticks_ms() * 128u / 1000u);   /* TIMER_ticks */
    if (s_music_at < 0 || s_music_at < now - 0x1e00) {
        Music_FadeOut(0);   /* 0x4ae190 */
        Music_SetVolume((int32_t)((exe_u32(0x5d14b0) * (uint32_t)Options_Int(0x5d618c, (int)exe_u32(0x5d1740))) >> 7));
        Music_Play(exe_str(0x5dca14), 0);
        s_music_at = now;
    }
}

/* A screen of the stack, its watches and controls ticked; the top one's own ticks, pointer and lists. */
static void draw_screen(FertControl *s, FertControl *top)
{
    if (s != top) {
        update_watches(s);
        update_all(s);
        FEWindow_Draw(s);
        return;
    }
    screen_update(s);
    update_watches(s);
    FECtrl_Tick(s);   /* (the controller setups' ticks) */
    update_all(s);   /* (the controls' ticks) */
    hover_pointer(s);
    FEWindow_Draw(s);
    if (s == FEApp_Top() && FEEndOfEra_On()) FEEndOfEra_Draw();   /* (its list's _v15 0x4fe7a0) */
    draw_combos(s);
    if (s == FEApp_Top()) combo_draw();
    if (s == FEApp_Top() && FEColour_Box()) FEColour_Draw(FEColour_Box(), 0, 0);
}

void FEApp_Frame(void)
{
    poll();
    if (s_dialog.purpose == DLG_FOR_REMOVE && !s_dialog.up) {
        /* FEMainScreen_v1 0x50faf0: yes deletes the chosen player's files (0x4de350; 0x324 when it
           can't), the list made anew */
        s_dialog.purpose = DLG_FOR_NONE;
        FEAccessor *users = FEAccess_Find("UserList.UserList");
        if (s_dialog.done == 1 && users && FEAccess_Count(users)) {
            if (!FESave_Delete(FESave_PlayerStem(FEAccess_Pos(users)))) FEApp_Message(0x324);
            else FESave_ListPlayers();
        }
    }
    if (s_dialog.purpose == DLG_FOR_KILLREPLAY && !s_dialog.up) {
        /* FECallBackList_v1 0x50c2f0: yes deletes the chosen replay, the list made anew (FE_ListReplays
           0x50ca80) */
        s_dialog.purpose = DLG_FOR_NONE;
        FEAccessor *rl = FEAccess_Find("Replay.List");
        if (s_dialog.done == 1 && rl && FEAccess_Count(rl)) FESave_DeleteReplay(FEAccess_Pos(rl));
    }
    if (s_dialog.purpose == DLG_FOR_FORFEIT && !s_dialog.up) {
        /* (0x505a50: yes, the tournament's races open again, 0x5033f0, and one screen back) */
        s_dialog.purpose = DLG_FOR_NONE;
        if (s_dialog.done == 1) {
            FEData_TourForfeit();
            FEApp_Back();
        }
    }
    if (s_dialog.purpose == DLG_FOR_TOUR && !s_dialog.up) {
        /* FEApplication_v1 0x4d25d0: ok goes on with the race (0x4d5200 again) */
        s_dialog.purpose = DLG_FOR_NONE;
        if (s_dialog.done == 1) {
            FEData_TourProceed();
            callback("PLAYGAME");
        }
    }
    menu_music();
    FEMovieBox_Update();
    FertControl *w = FEApp_Top();
    TexManager *tm = g_texgraphics ? g_texgraphics->textures : NULL;
    FEPointer_Begin();   /* (0x4d2980: 0x4e1460 first) */
    sync_room(w);
    Render_BeginFrame(g_render);
    if (tm) TexManager_begin(tm);
    /* 0x4d2980: the room first in the 3D camera; the base screen's 2D and the bottom bar over it
       (FEBIGOLOVALENABLED 0, the default); the z-buffer cleared and the screen's control cars (the
       second 3D view); the 2D cleared again (FEZBUFFERCLEARENABLED 0) and the popups, dialog and
       pointer over all */
    s_camera->clear = FE3D_Draw() ? 0 : 0xff000000u;
    View_Begin(g_render, s_camera);
    RCamera_Setup2D(s_camera);
    g_fe_depth = 0.99998474f;   /* 0x4d2980 */
    FE2D_Clear();
    if (s_bar) {
        /* the play button enabled with the screen's canplaygame (0x4d7c70 -> 0x4c79c0 / 0x4c79e0) */
        for (int i = 0; i < s_bar->nchildren; i++) {
            FertControl *c = s_bar->children[i];
            if (!strcmp(c->name, "ID_BACKBUTTON")) {
                /* (0x4d7c70: off while locked, 0x4d5df0; its other conditions pend) */
                uint32_t f = c->feflags;
                if (!s_back_locked) c->feflags &= ~0x10u;
                else if (FertControl_findState(c, "disabled") >= 0) c->feflags |= 0x10;
                if (f != c->feflags) refresh_state(c);
                continue;
            }
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
    /* (a popup over its screen: the screens under the popups drawn first) */
    int base = s_depth - 1;
    while (w && base > 0 && is_popup(s_stack[base])) base--;
    if (w) draw_screen(s_stack[base], w);
    if (s_bar) {
        for (int i = 0; i < s_bar->nchildren; i++) {
            FertControl_Update(s_bar->children[i]);
            if (!strcmp(s_bar->children[i]->id, "NFS5.BOTTOMBARBUTTON")) anim_update(s_bar->children[i]);
        }
        hover_pointer(s_bar);
        FEWindow_Draw(s_bar);   /* (0x4d2980: the bottom bar over the base screen) */
    }
    FE2D_Flush();
    FE2D_Clear();
    View_End(g_render);
    FE3D_DrawControlCars();
    s_camera->clear = 0;
    View_Begin(g_render, s_camera);
    RCamera_Setup2D(s_camera);
    FE2D_Clear();
    if (w)
        for (int i = base + 1; i < s_depth; i++) draw_screen(s_stack[i], w);
    dialog_draw();
    FEPointer_Draw();   /* (0x4d2980, after the windows: 0x4e1470 at the mouse) */
    FE2D_Flush();
    View_End(g_render);
    if (tm) TexManager_end(tm);
    Render_EndFrame(g_render);
}
