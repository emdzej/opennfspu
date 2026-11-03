/* Application states (see app.h). For now: the exe check and the render system brought up as the game
   does (Render_Init: the 3D Setup's driver "dx7z", mode 1 = 640x480 at 16 bits); the front end and the race come as they are
   ported (docs/re/port-plan.md, docs/guide/status.md). --param thrash_test=1 draws the driver's self-test
   scene instead of a blank frame, thrash_test=2 EAGL's; 10 the TCP/IP provider's, 11 the pings;
   car=<name> shows a car (CAR_selftest). */
#include "app.h"
#include "game/options.h"
#include "game/car.h"
#include "game/race_setup.h"
#include "game/race_car.h"
#include "game/replay_file.h"
#include "real/mouse.h"
#include "game/track.h"
#include "render/r3dcar.h"
#include "game/race_progress.h"
#include "game/mission.h"
#include "game/net.h"
#include "game/car_drive.h"
#include "front/fefactory.h"
#include "snd/snd.h"
#include "exe.h"
#include "pending.h"
#include "platform.h"
#include "eagl/eagl.h"
#include "real/set3d.h"
#include "render/car_selftest.h"
#include "render/track_selftest.h"
#include "render/drive_selftest.h"
#include "front/fe_selftest.h"
#include "front/feapp.h"
#include "front/feintro.h"
#include "front/fesave.h"
#include "front/fecd.h"
#include "front/fectrl.h"
#include "game/music.h"
#include "front/feaccess.h"
#include "real/thread.h"
#include "real/llpacket.h"
#include "real/conman.h"
#include "real/netgathr.h"
#include "real/icmp.h"
#include "real/tcp.h"
#include "real/udp.h"
#include "real/wsock.h"
#include "render/render.h"
#include "render/thrash.h"
#include "static_init.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int selftest;   /* --param thrash_test: 1 the THRASH scene, 2 the EAGL scene */
static char car_name[64];     /* --param car=<name>: that car in the viewer (GameData/CarModel/<name>.crp) */
static char track_name[32];   /* --param track=<name>: the track viewer (TRACK_selftest) */
static char drive_name[32];   /* --param drive=<track>: drive car=<name> (default 964) there (DRIVE_selftest) */
static char screen_name[64];  /* --param screen=<layout>: a front-end screen (FE_selftest), e.g. main.lay */
static char fe_cb[512];       /* --param fecb=<cb>[,<cb>..]: run at front-end frames 30, 40, .. (port addition, tests;
                                 "=<name>" types the name into UserList.NameInput, "^" goes back a screen, "@x:y"
                                 clicks there, "$acc:text" sets a text) */
static int fe_frames;
static char fe_start[64];     /* --param fe=<screen>: the front end from that screen (default gamesetup) */
static bool fe_on;
static int intro;             /* the intro movies before the front end (default start; --param intro=0 skips them):
                                 1 to start, 2 playing, 0 none */
static char race_track[64];   /* the race the front end started (GameMain 0x4b6840: FE, then the race) */

bool app_init(void)
{
    PORT_StaticInit();   /* the original's global constructors, before WinMain */
    char err[256], m[300];
    if (!exe_init(err, sizeof err)) {
        snprintf(m, sizeof m, "OpenNFSPU: %s", err);
        plat_log(m);
        return false;
    }
    /* The port's drivers, what LoadLibrary would find (THRASH_opendll 0x574d50), then the game's start:
       the options (fe.txt), the render system (Render_Init 0x4673b0). */
    THRASH_registerdll(&THRASH_dx7z);
    RenderOptions_Load();
    Options_Apply();
    Render_Init();
    if (!g_render || !g_render->device || !g_render->device->rs) {
        plat_log("OpenNFSPU: the render system could not start (OpenGL ES 3.0 needed)");
        return false;
    }
    char v[8];
    selftest = plat_param("thrash_test", v, sizeof v) ? atoi(v) : 0;
    if (!plat_param("car", car_name, sizeof car_name)) car_name[0] = 0;
    if (!plat_param("track", track_name, sizeof track_name)) track_name[0] = 0;
    if (!plat_param("drive", drive_name, sizeof drive_name)) drive_name[0] = 0;
    if (!plat_param("screen", screen_name, sizeof screen_name)) screen_name[0] = 0;
    if (!plat_param("fe", fe_start, sizeof fe_start)) fe_start[0] = 0;
    if (!plat_param("fecb", fe_cb, sizeof fe_cb)) fe_cb[0] = 0;
    {
        /* --param replay=<file>: the race of a replay file, replayed (as the front end's load replay does) */
        char rf[40];
        if (plat_param("replay", rf, sizeof rf) && Replay_Load(rf)) {
            Replay_Apply();
            snprintf(drive_name, sizeof drive_name, "%s", g_track_name);
            snprintf(car_name, sizeof car_name, "%s", (const char *)g_cardesc[0] + 0x228);   /* (FILENAME) */
        }
    }
    /* Without a viewer parameter the game starts as the original does: in the front end. */
    if (!fe_start[0] && !screen_name[0] && !drive_name[0] && !track_name[0] && !car_name[0] && !selftest) {
        snprintf(fe_start, sizeof fe_start, "gamesetup");
        /* GameMain 0x4b6840: FE_IntroMovies once, before the first FEApplication_Run (unless 0x65743c) */
        intro = !(plat_param("intro", v, sizeof v) && v[0] == '0');
    }
    return true;
}

bool app_frame(void)
{
    g_app_frames++;
    /* (Sys_Init 0x4a51e0: the network session; on the first frame, as gasm's threads, which the UDP
       provider starts, run from the frame loop) */
    if (g_app_frames == 1 && !g_net) Net_Construct();
    TIMER_run(app_ticks_ms());   /* (the timer's threads: their ticks up to the frame's time) */
    /* Every frame runs the game, the runner's catch-up frames too (gasm_gl_frame_shown 0: only the last
       of them is shown): the game's time is the frame count, so skipping them would stop it. */
    {
        if (race_track[0]) {
            DRIVE_selftest(race_track, car_name);
            /* The race loop's end (RaceLoop 0x415300): the menus' quit or the race-over's Escape back to the
               front end (GameMain's loop runs FEApplication_Run again); their restart loads the race anew
               (the original restarts in place, 0x606a88). */
            if (g_drive_result == 2) {
                DRIVE_Shutdown();
            } else if (g_drive_result == 3) {
                DRIVE_Shutdown();
                return false;
            } else if (g_drive_result == 1) {
                {
                    int places[8], times[8];
                    bool clean[8];
                    int n = DRIVE_Results(places, times, clean);
                    int32_t ids[20];
                    float wear[20];
                    int np = DRIVE_PartsWear(ids, wear);
                    FEApp_RaceWear(ids, wear, np);
                    char rn[32], rc[32];
                    int32_t best, time;
                    if (!g_race_quit && DRIVE_PlayerResult(rn, rc, &best, &time)) FEApp_RaceRecord(rn, rc, best, time);   /* (0x4ed590 skips a quit race, 0x657e34) */
                    int32_t ms[3] = {0, 0, 0};
                    DRIVE_MissionStats(&ms[0], &ms[1], &ms[2]);
                    FEApp_MissionStats(ms);
                    FEApp_RaceOver(places, times, clean, n, g_race_quit != 0);
                }
                DRIVE_Shutdown();
                if (fe_start[0]) {
                    race_track[0] = 0;
                    fe_on = false;
                } else {
                    return false;
                }
            }
        } else if (intro) {
            if (intro == 1) {
                FEIntro_Start();
                intro = 2;
            }
            if (!FEIntro_Frame()) intro = 0;
        } else if (fe_start[0]) {
            if (!fe_on) fe_on = FEApp_Init(fe_start);
            if (fe_on) {
                FEApp_Frame();
                if (fe_cb[0] && ++fe_frames >= 30 && fe_frames % 10 == 0) {
                    const char *p = fe_cb;
                    for (int k = (fe_frames - 30) / 10; k > 0 && p; k--) p = strchr(p, ',') ? strchr(p, ',') + 1 : NULL;
                    if (p) {
                        char one[64];
                        size_t len = strcspn(p, ",");
                        snprintf(one, sizeof one, "%.*s", (int)(len < sizeof one ? len : sizeof one - 1), p);
                        if (one[0] == '=') snprintf(FEData_NameInput(), 9, "%s", one + 1);   /* (a name typed) */
                        else if (one[0] == '^') FEApp_Back();
                        else if (one[0] == '!') {   /* (!<x>:<y> the mouse down there, ! up again: a dialog's click) */
                            static int32_t px, py;
                            const char *colon = strchr(one, ':');
                            if (colon) px = atoi(one + 1), py = atoi(colon + 1);
                            MOUSE_Override(true, px, py, colon ? 1u : 0u);
                        }
                        else if (one[0] == '$') {   /* ($<accessor>:<text>: a text typed into it) */
                            char *colon = strchr(one, ':');
                            if (colon) *colon = 0;
                            FEAccessor *acc = FEAccess_Find(one + 1);
                            FEAccess_SetText(acc, colon ? colon + 1 : "");   /* (a string's) */
                            FEAccess_ListAdd(acc, colon ? colon + 1 : "");   /* (a list's, chosen) */
                        }
                        else if (one[0] == '@') {   /* (@<x>:<y>: a click there) */
                            const char *colon = strchr(one, ':');
                            if (colon) FEApp_Click(atoi(one + 1), atoi(colon + 1));
                        }
                        else if (one[0] == '#') {   /* (#<accessor>:<row>: a list's row chosen) */
                            char *colon = strrchr(one, ':');
                            if (colon) *colon = 0;
                            FEAccessor *acc = FEAccess_Find(one + 1);
                            if (acc && colon) FEAccess_SetPos(acc, atoi(colon + 1));
                        }
                        else FEApp_Callback(one);
                    }
                }
                if (FEApp_Result() == FE_PLAY) {
                    FEApp_End();
                    {
                        const uint32_t *col = FEApp_Colours();
                        g_race_colours_set = col != NULL;
                        if (col) memcpy(g_race_colours, col, sizeof g_race_colours);
                        g_race_parts_set = FEApp_Parts(g_race_parts) >= 0;
                        g_race_setup_set = FEApp_Setup(g_race_setup);
                        static const char *const cs[4] = {"Car.Transmission", "Car.ABS", "Car.Traction", "Car.Roof"};
                        for (int i = 0; i < 4; i++) g_race_car_settings[i] = FEData_CarSetting(cs[i]);
                        FECtrl_Ramps(g_race_ramps);
                        g_race_fe_damage = FEApp_Damage();
                        Save *sv = FESave_Current();
                        snprintf(g_race_driver_name, sizeof g_race_driver_name, "%s", sv ? sv->player : "");
                    }
                    FEAccess_WriteResolution();   /* (the display mode's row, as it goes into the race's stream) */
                    Options_Save();        /* (fe.txt written back) */
                    RenderOptions_Load();  /* the options the race reads (Options_Parse's globals) */
                    Options_Apply();
                    /* the race's display (GameMain 0x4b6840: fe.txt's WIDTH, HEIGHT, DEPTH) */
                    Render_SetVideoMode(g_render, g_renderopts.width, g_renderopts.height, g_renderopts.depth, 2);
                    snprintf(race_track, sizeof race_track, "%s", FEApp_Track());
                    snprintf(car_name, sizeof car_name, "%s", FEApp_Car());
                    g_drive_race_type = FEApp_RaceType();
                    /* 0x4ecc60: the Dakar cheat (0) sets RALLY, fetherw8 (8) HEAVY; the derby cheat (7)
                       makes the race a lap knockout on the skidpad with damage, collisions on and the
                       DERBY option set */
                    g_race_cheats = g_fe_cheats;
                    g_race_rally = FE_Cheat(0);   /* RALLY (0x19) */
                    g_race_heavy = FE_Cheat(8);   /* HEAVY (0x1a) */
                    g_race_derby = FE_Cheat(7);
                    g_race_linear = FEData_TrackLinear(race_track);   /* LINEAR_TRACK (0x4ec480's 0x1d) */
                    if (g_race_derby) {
                        snprintf(race_track, sizeof race_track, "%s", exe_str(0x5da6d0));   /* "Skidpad" */
                        g_drive_race_type = 3;
                        g_race_linear = 0;   /* (its LINEAR_TRACK 0) */
                        g_race_fe_damage = 1;   /* (its DAMAGE 1) */
                        /* (and NOCOLLISION 0x29 at 0: the cars' collisions skip only in a network game
                           with it set, 0x657408 and 0x65740c in 0x498f60's pair test, so alone it is a no-op) */
                    }
                    /* (0x4ec590: Fuzzyfuz gives the quick race four police cars; a mission with police two) */
                    g_drive_cops = FE_Cheat(5) && FEApp_GameType() == 0 ? 4 : FEApp_GameType() == 9 ? FEApp_Cops() : 0;
                    if (!FEData_TrackRecords(race_track, g_track_records, g_lap_records, g_record_names, g_record_cars)) {
                        memset(g_track_records, 0, sizeof g_track_records);
                        memset(g_lap_records, 0, sizeof g_lap_records);
                    }
                    g_drive_laps = FEApp_Laps();
                    g_race_reverse = FEApp_Reverse();   /* (0x4ecc60's REVERSE and MIRROR) */
                    g_r3d_mirror = FEApp_Mirror();
                    g_drive_opponents = FEApp_Opponents();
                    g_drive_traffic = FEApp_Traffic(g_race_traffic_slots);
                    g_race_grid_set = FEApp_Grid(g_race_grid_pers, g_race_grid_start) >= 0;
                    g_race_opp_set = FEApp_OpponentCars(g_race_opp) >= 0;
                    g_race_skill = FEApp_Skill();
                    FEData_Cameras(g_race_cameras);   /* (0x4ee430: Camera.1 .. Camera.4) */
                    g_race_mode = FEApp_GameType();   /* (0x4eab10's option 0x30) */
                    if (g_race_mode == 4) FEData_TourRaceInfo(&g_race_tour);   /* (0x4ecc60 -> 0x4ebb70) */
                    else memset(&g_race_tour, 0, sizeof g_race_tour);
                    g_race_tour.era = FEData_RaceEra();   /* (0x4ecc60's 0xbb) */
                    {
                        /* 0x4be610: the CD player's chosen songs as the race's playlist (0x3c, 0x143) */
                        int n = FECD_SelCount();
                        g_music_nplaylist = n < 0x80 ? n : 0x80;
                        memcpy(g_music_playlist, FECD_Sel(), sizeof *g_music_playlist * (size_t)g_music_nplaylist);
                    }
                    {
                        Save *sv = FESave_Current();   /* (the description's player, 0x4eab10) */
                        snprintf(g_race_player, sizeof g_race_player, "%s", sv ? sv->player : "");
                    }
                    Mission_Reset();
                    if (g_race_mode == 9) FEFactory_RaceOptions();
                    /* The load replay screen's: the replay file's race (0x413830 at the race's start), or
                       with no file the race the options describe, as the original's (0x5e99f4 0) */
                    if (FEApp_ReplayFile() && Replay_Load(FEApp_ReplayFile())) {
                        Replay_Apply();
                        snprintf(race_track, sizeof race_track, "%s", g_track_name);
                        snprintf(car_name, sizeof car_name, "%s", (const char *)g_cardesc[0] + 0x228);   /* (FILENAME) */
                    }
                } else if (FEApp_Result() == FE_QUIT) {
                    return false;
                }
            }
        } else if (screen_name[0]) FE_selftest(screen_name);
        else if (drive_name[0]) DRIVE_selftest(drive_name, car_name);
        else if (track_name[0]) TRACK_selftest(track_name);
        else if (car_name[0]) CAR_selftest(car_name);
        else if (selftest == 1) THRASH_selftest();
        else if (selftest == 2) EAGL_selftest();
        else if (selftest >= 3 && selftest <= 11) {
            if (selftest == 3) WS_selftest();
            else if (selftest == 4) UDP_selftest();
            else if (selftest == 5) NETGATHR_selftest();
            else if (selftest == 6) PACKET_selftest();
            else if (selftest == 7) CONMAN_selftest();
            else if (selftest == 8) CONMAN_localtest();
            else if (selftest == 10) TCP_selftest();
            else if (selftest == 11) ICMP_selftest();
            else Net_Selftest();
            THRASH.fn.clearwindow();
            THRASH.fn.pageflip();
        }
        else {
            THRASH.fn.clearwindow();
            THRASH.fn.pageflip();
        }
    }
    THRASH_present();
    return true;
}

/* The sound library's device (DirectSound and its thread in the original): its fill, as 16-bit stereo
   (0x58f970), to the platform's floats. */
void app_audio(float *stereo, unsigned frames)
{
    static int16_t buf[4096 * 2];
    while (frames) {
        unsigned n = frames < 4096 ? frames : 4096;
        SND_Fill(buf, (int)n);
        for (unsigned i = 0; i < n * 2; i++) stereo[i] = (float)buf[i] * (1.0f / 32768.0f);
        stereo += n * 2;
        frames -= n;
    }
}

void app_exit(void) { Render_Shutdown(); }
