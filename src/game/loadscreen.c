/* The race's loading screen (see loadscreen.h). */
#include "game/loadscreen.h"
#include "game/car_drive.h"
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
    /* (a network game's peers are told the step, 0x4ed290 message 0x256: the port has no network game) */
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
    /* (a network game's step 0 and 5 put MLoading.fsh's three pieces at y 0x142, 0x9e high) */
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
    /* (and a network game's bars of each player's step) */
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
