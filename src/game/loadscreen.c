/* The race's loading screen (see loadscreen.h). */
#include "game/loadscreen.h"
#include "game/car_drive.h"
#include "game/net.h"
#include "game/option_rec.h"
#include "game/race_progress.h"
#include "game/options.h"
#include "game/race_car.h"
#include "game/track.h"
#include "front/fe_parts.h"
#include "front/feaccess.h"
#include "render/r3dcar.h"
#include "render/render.h"
#include "real/set3d.h"
#include "real/mem.h"
#include "real/shape.h"
#include "real/window.h"
#include "support/ufile.h"
#include "exe.h"
#include <stdio.h>
#include <stdlib.h>

static bool s_on;          /* 0x655a28: between step 0 and step 10 */
static Window *s_window;   /* 0x655a24 */

static int16_t sh16(const uint8_t *rec, int off) { return (int16_t)(rec[off] | rec[off + 1] << 8); }
static int absi(int v) { return v < 0 ? -v : v; }

/* The rows of tiles that make up a 640 x 480 picture (the shapes in order: twenty of 32 pixels, ten of 64,
   then three rows of five of 128). */
static void tiles(const uint8_t *fsh)
{
    static const int rows[5][3] = {{0, 32, 0}, {32, 64, 20}, {96, 128, 30}, {224, 128, 35}, {352, 128, 40}};
    for (int r = 0; r < 5; r++) {
        int size = rows[r][1], index = rows[r][2];
        for (int x = 0; x < 640; x += size, index++) {
            const uint8_t *shape = SHAPE_entry(fsh, index);
            if (shape) WINDOW_drawscaled(shape, x, rows[r][0], size, size);
        }
    }
}

/* Step 5's track art (FEData\TrackArt\<track>.fsh): its map (0, mirrored for a mirrored race), its
   name (1) and the direction's arrow (2, 3 for a reverse race), each at its centre's distance from the
   corner, at its own size through the filter. The arrow of a mirrored race is flipped and left so
   (the file is freed after), at the name's centre mirrored by its own. */
static void track_art(uint8_t *fsh)
{
    uint8_t *map = (uint8_t *)SHAPE_entry(fsh, 0);
    if (map) {
        int x = absi(sh16(map, 8)), y = absi(sh16(map, 0xa));
        if (g_r3d_mirror == 1) SHAPE_horizflip(map);
        WINDOW_drawfiltered(map, x, y, sh16(map, 4), sh16(map, 6));
        if (g_r3d_mirror == 1) SHAPE_horizflip(map);
    }
    const uint8_t *name = SHAPE_entry(fsh, 1);
    if (!name) return;
    WINDOW_drawfiltered(name, absi(sh16(name, 8)), absi(sh16(name, 0xa)), sh16(name, 4), sh16(name, 6));
    uint8_t *arrow = (uint8_t *)SHAPE_entry(fsh, g_race_reverse == 1 ? 3 : 2);
    if (!arrow) return;
    int ax = absi(sh16(arrow, 8)), x = ax;
    if (g_r3d_mirror == 1) {
        SHAPE_horizflip(arrow);
        x = absi(sh16(arrow, 8)) + sh16(name, 4) - 2 * (ax - absi(sh16(name, 8)));
    }
    WINDOW_drawfiltered(arrow, x, absi(sh16(arrow, 0xa)), sh16(arrow, 4), sh16(arrow, 6));
}

/* LoadingScreen 0x4a4850 */
void LoadingScreen(int step)
{
    if (step == 0) {
        s_on = true;
    } else {
        if (step == 10) {
            s_on = false;
            return;
        }
        if (!s_on) return;
    }
    if (!s_window) s_window = WINDOW_new(640, 480, WINDOW_defaultdepth, 0x10);
    if (Net_InGame() && 0 < step && s_on) {
        /* a network game's players told my step (LOADINGAMOUNT 0x256) */
        int32_t recs[8], *p = recs;
        Option_PutCar(&p, 0x256, g_race_local, step);
        *p = 0;
        if (p != recs) Net_AddOptions(recs);   /* 0x48d480 */
    }
    if (Options_Int(0x5d3bb8, 0) == 1) return;   /* NOLOADINGSCREEN */
    /* (the music stream's prebuffering waited for, 0x4ad440 against 0x4ad490 with the timer tasks: the
       port's stream is fed by the frames) */
    WINDOW_select(s_window);
    char path[300];
    if (step == 0 || (step == 5 && g_race_mode != 9)) {
        uint8_t *track = NULL;
        if (step == 5) {
            snprintf(path, sizeof path, exe_str(0x5cccc8), FE_DIR_TRACKART, g_track_name);   /* "%s%s.fsh" */
            char found[300];
            if (UTIL_searchpath(path, found)) track = UTIL_loadfileabort(path, 0);   /* 0x59d9e0, 0x59d690 */
        }
        int era = 0;   /* (a career race: its era's picture, 0x502880) */
        if (g_race_mode == 4) {
            int e = FEData_Era();
            era = e == 0x209 ? 1 : e == 0x20a ? 2 : e == 0x20b ? 3 : 0;
        }
        snprintf(path, sizeof path, exe_str(step == 0 ? 0x5d10c0 : 0x5d10b0), FE_DIR_ART, era);   /* "%sera%d.fsh", "%sera%dtr.fsh" */
        uint8_t *bg = UTIL_loadfileabort(path, 0);
        if (bg) tiles(bg);
        if (step == 5 && track) track_art(track);
        MEM_purge(bg);
        if (track) MEM_purge(track);
    }
    if (Net_InGame() && (step == 0 || step == 5)) {
        /* a network game's: MLoading.fsh's three pieces (from 0 at step 0, from 3 at 5) across at y 0x142 */
        snprintf(path, sizeof path, "%sMLoading.fsh", FE_DIR_ART);   /* ("%sMLoading.fsh" 0x5d10a0) */
        uint8_t *ml = UTIL_loadfileabort(path, 0);
        int k = step > 4 ? 3 : 0;
        static const int px[3] = {0, 0x100, 0x200}, pw[3] = {0x100, 0x100, 0x80};
        for (int i = 0; ml && i < 3; i++) {
            const uint8_t *s = SHAPE_entry(ml, k + i);
            if (s) WINDOW_drawscaled(s, px[i], 0x142, pw[i], 0x9e);   /* 0x5628c0 */
        }
        if (ml) MEM_purge(ml);
    }
    int language = Options_Int(0x5d61cc, 0);   /* LANGUAGE (0x657c84) */
    if ((step == 0 || step == 5) && language != 0) {
        /* "Loading" in the language: loadtext.fsh's shape language - 1 at minus its centre, keyed */
        uint8_t *text = NULL;
        snprintf(path, sizeof path, exe_str(0x5d1090), FE_DIR_ART);   /* "%sloadtext.fsh" */
        char found[300];
        if (UTIL_searchpath(path, found)) text = UTIL_loadfileabort(path, 0);
        const uint8_t *s = text ? SHAPE_entry(text, language - 1) : NULL;
        if (s) WINDOW_drawscaledkey(s, -sh16(s, 8), -sh16(s, 0xa), sh16(s, 4), sh16(s, 6));
        if (text) MEM_purge(text);
    }
    if (0 <= step) {
        union { uint32_t u; float f; } k = {exe_u32(0x5b4ac0)};   /* 12.4 pixels a step */
        WINDOW_fillrect(0x12e, 0x1d3, (int)((double)step * (double)k.f), 6, 0xffffbb00);
    }
    if (Net_InGame() && s_on) {
        /* a network game's players, by grid slot (STARTINGPOS) 0x4a apart from x 0x20: a black box for
           a seat without a racing player; a player's picture (its block 0, once whole) above the bar of
           its step (LOADINGAMOUNT of ten, 54 wide) */
        for (int i = 0; i < g_race_inputs && i < CAR_MAX; i++) {
            int32_t slot, amount;
            memcpy(&slot, g_cardesc[i] + 0x174, 4);
            memcpy(&amount, g_cardesc[i] + 0x57c, 4);
            int x = slot * 0x4a + 0x20;
            NetPlayer *pl = Net_PlayerByCar(i);   /* 0x48d650 */
            if (!pl || !Net_PlayerRacing(pl)) {
                WINDOW_fillrect(x, 0x16c, 0x36, 0x40, 0);
                continue;
            }
            const uint8_t *pic = Net_PlayerBlock(pl, 0);   /* 0x4903a0 */
            if (!pic) continue;
            uint32_t off;
            memcpy(&off, pic + 0x14, 4);
            const uint8_t *s = pic + off;
            WINDOW_drawscaledkey(s, x, 0x16c, sh16(s, 4), sh16(s, 6));   /* 0x561c90 */
            int done = amount * 0x36 / 10;
            WINDOW_fillrect(x + done, 0x1b4, 0x36 - done, 6, 0xff312000);
            WINDOW_fillrect(x, 0x1b4, done, 6, 0xffffbb00);
        }
    }
    THRASH.fn.window(2);
    THRASH.fn.clearwindow();
    THRASH.fn.sync(0);
    WINDOW_selectback();
    WINDOW_drawshape(s_window->shape, (g_display.width - 640) / 2, (g_display.height - 480) / 2);
    WINDOW_deselect();
    THRASH.fn.pageflip();
    /* (the window's free at step 10 is never reached: that step returns first, and the window stays for
       the next race) */
}
