/* The HUD's layouts (see hud_config.h). */
#include "game/hud_config.h"
#include "exe.h"
#include "pending.h"
#include "real/loc.h"
#include "render/hudart.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { LOOK = 0x1e0, CHOICE = 0x68 };

/* The save directory (0x65b360: an entry of the path table the game fills at startup, not ported). */
static const char *save_dir = "SaveData\\";

HudDials g_hud_dials;
int32_t g_hud_imperial;

static int loads;            /* 0x60c978 */
static uint8_t *loc;         /* 0x60c974 */
static uint8_t *looks;       /* 0x60c964 */
static uint8_t *choices;     /* 0x60c968 */
static int nconfigs;         /* 0x60c960 */
static int current;          /* 0x60c95c */
static int screen_w, screen_h;   /* 0x60c900, 0x60c81c */
static int32_t pos[HUDPOS_COUNT][2];   /* 0x60c828 */

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static float rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }

/* The player's file, else the default ("%s%s%s" 0x5ccb98, "%s%s" 0x5cc588). */
static uint8_t *read_file(const char *player, const char *own, const char *fallback, size_t *n)
{
    char path[300];
    snprintf(path, sizeof path, "%s%s%s", save_dir, player, own);
    if (!vfs_exists(path)) snprintf(path, sizeof path, "%s%s", save_dir, fallback);
    return vfs_read_all(path, n);
}

/* The places in pixels. 0x41fb60 */
static void to_pixels(void)
{
    const uint8_t *l = HudConfig_look(), *c = HudConfig_choice();
    if (!l || !c) return;
    float w = (float)screen_w, h = (float)screen_h;
    for (int k = 0; k < 11; k++) {
        pos[k][0] = (int32_t)(w * rdf(l + 0x188 + 8 * k));
        pos[k][1] = (int32_t)(h * rdf(l + 0x18c + 8 * k));
    }
    pos[11][0] = (int32_t)(w * rdf(c + 0xc)), pos[11][1] = (int32_t)(h * rdf(c + 0x10));
    pos[12][0] = (int32_t)(w * rdf(c + 0x20)), pos[12][1] = (int32_t)(h * rdf(c + 0x24));
}

/* Both files' records. 0x41f7c0 */
static bool load_layouts(const char *player)
{
    free(looks);
    free(choices);
    looks = choices = NULL;
    size_t n = 0, m = 0;
    looks = read_file(player, "1.hud", "XXhud1XX.hud", &n);
    if (!looks) return false;
    nconfigs = (int)(n / LOOK);
    choices = read_file(player, "2.hud", "XXhud2XX.hud", &m);
    if (!choices || (int)(m / CHOICE) < nconfigs) return false;
    to_pixels();
    return true;
}

void HudConfig_Load(const char *player, int config, int w, int h)
{
    screen_w = w, screen_h = h;
    (void)config;   /* 0x60c958: the record the defaults copy (0x41ec10, the HUD editor's) */
    if (loads == 0) {
        current = 0;
        char path[300];
        snprintf(path, sizeof path, "%shud.loc", g_hud_dir);   /* "%shud.loc" 0x5ccb78 */
        size_t n = 0;
        loc = vfs_read_all(path, &n);
        if (loc) LOC_setlanguage(loc, 0, 0);   /* (Options_Language: English) */
        if (!load_layouts(player ? player : ""))
            PENDING_NOTE(0x41ec10, "the HUD layouts' defaults, written when the files don't load");
    }
    loads++;
}

void HudConfig_Free(void)
{
    if (loads > 0 && --loads == 0) {
        free(loc);
        free(looks);
        free(choices);
        loc = looks = choices = NULL;
        nconfigs = 0;
    }
}

const uint8_t *HudConfig_loc(void) { return loc; }
const uint8_t *HudConfig_look(void) { return looks && current < nconfigs ? looks + LOOK * current : NULL; }
const uint8_t *HudConfig_choice(void) { return choices && current < nconfigs ? choices + CHOICE * current : NULL; }

int32_t HudConfig_i32(int off)
{
    const uint8_t *l = HudConfig_look();
    return l ? rd32(l + off) : 0;
}

int32_t HudConfig_choicei32(int off)
{
    const uint8_t *c = HudConfig_choice();
    return c ? rd32(c + off) : 0;
}

const char *HudConfig_font(int off)
{
    const uint8_t *l = HudConfig_look();
    return l ? (const char *)l + off : "";
}

uint32_t HudConfig_scheme(void)
{
    switch (HudConfig_choicei32(0x50)) {
    case 1: return 0xccff0606u;
    case 2: return 0xcc1ee605u;
    default: return 0xccfab400u;
    }
}

bool HudConfig_notrays(void) { return ((uint32_t)HudConfig_i32(0x14) & 0xff000000u) == 0; }

void HudConfig_trays(bool on)
{
    uint8_t *l = (uint8_t *)HudConfig_look();
    if (!l) return;
    uint32_t c;
    memcpy(&c, l + 0x14, 4);
    c = (c << 8) >> 8;
    if (on) c |= exe_u32(0x5ccb34) << 24;
    memcpy(l + 0x14, &c, 4);
}

void HudConfig_pos(int k, int32_t *x, int32_t *y)
{
    *x = pos[k][0];
    *y = pos[k][1];
}

void HudConfig_setpos(int k, int32_t x, int32_t y)
{
    pos[k][0] = x;
    pos[k][1] = y;
}
