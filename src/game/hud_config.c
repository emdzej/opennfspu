/* The HUD's layouts (see hud_config.h). */
#include "game/hud_config.h"
#include "exe.h"
#include "pending.h"
#include "platform.h"
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
int32_t g_hud_layout;
int32_t g_hud_splits = 1;   /* (the front end's default) */
int32_t g_hud_damage_display = 1;   /* (fe.txt's) */

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

/* A SaveData file: the user's own (the port's user files are flat), else the game's. */
static uint8_t *read_savedata(const char *file, size_t *n)
{
    uint8_t *d = plat_load_user_file(file, n);
    if (d) return d;
    char path[300];
    snprintf(path, sizeof path, "%s%s", save_dir, file);
    return vfs_read_all(path, n);
}

/* The player's file, else the default ("%s%s%s" 0x5ccb98, "%s%s" 0x5cc588); the name read into used. */
static uint8_t *read_file(const char *player, const char *own, const char *fallback, size_t *n, char *used, size_t cap)
{
    char f[200];
    snprintf(f, sizeof f, "%s%s", player, own);
    uint8_t *d = read_savedata(f, n);
    snprintf(used, cap, "%s", d ? f : fallback);
    return d ? d : read_savedata(fallback, n);
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

/* The name of a layout ("Config 1" .. "Config 4", the table 0x5ccb38). */
static const char *config_name(int config) { return exe_str(exe_u32(0x5ccb38 + 4 * (uint32_t)config)); }

/* Both files' records, then the one for the screen: its choices' +0x28, +0x2c the screen's size and its
   name the layout's. 0x41f7c0 */
static bool load_layouts(const char *player, int config)
{
    free(looks);
    free(choices);
    looks = choices = NULL;
    nconfigs = 0;
    size_t n = 0, m = 0;
    char used[200];
    looks = read_file(player, "1.hud", "XXhud1XX.hud", &n, used, sizeof used);
    if (!looks) return false;
    nconfigs = (int)(n / LOOK);
    choices = read_file(player, "2.hud", "XXhud2XX.hud", &m, used, sizeof used);
    if (!choices || (int)(m / CHOICE) < nconfigs) return false;
    for (int i = 0; i < nconfigs; i++) {
        const uint8_t *c = choices + CHOICE * i;
        if (rd32(c + 0x28) != screen_w || rd32(c + 0x2c) != screen_h) continue;
        if (strcmp((const char *)looks + LOOK * i, config_name(config))) continue;
        current = i;
        to_pixels();
        return true;
    }
    return false;
}

/* A place made for the screen from its place on 640 x 480 (0x41e4a0): above 640 wide, a box across the
   middle is centred, one right of it keeps its distance from the right edge, one below the middle (y
   over 240) from the bottom; then kept on the screen. */
static void fit(int32_t *px, int32_t *py, int32_t w, int32_t h)
{
    int32_t x = *px, y = *py;
    if (0x280 < screen_w) {
        bool down = false;
        if (x < 0x140 && 0x140 < x + w) {
            x = screen_w / 2 - w / 2;
            down = 0xf0 < y;
        } else if (x <= 0x140) {
            down = 0xf0 < y;
        } else if (y <= 0xf0) {
            x = screen_w - 0x280 + x;
        } else {
            x = screen_w - 0x280 + x;
            down = true;
        }
        if (down) y = screen_h - 0x1e0 + y;
    }
    if (screen_w < x + w) x = screen_w - w;
    if (screen_h < h + y) y = screen_h - h;
    *px = x, *py = y;
}

/* A fraction pair at f for the screen, by the box size at sz (0x41f010's steps). */
static void refit(uint8_t *f, const uint8_t *sz)
{
    int32_t x = (int32_t)((double)rdf(f) * 640.0), y = (int32_t)((double)rdf(f + 4) * 480.0);   /* 0x5b2a8c, 0x5b2a88 */
    fit(&x, &y, rd32(sz), rd32(sz + 4));
    float v[2] = {(float)((double)x / (double)screen_w), (float)((double)y / (double)screen_h)};
    memcpy(f, v, 8);
}

/* The places of a new layout record from 640 x 480 to the screen: the look's eleven (the first one twice,
   as the original), then the choices' two. 0x41f010 */
static void refit_layout(uint8_t *l, uint8_t *c)
{
    static const uint16_t sizes[10] = {0x94, 0x9c, 0xa4, 0xac, 0xb4, 0xbc, 0xc4, 0xd4, 0xe4, 0xf4};
    for (int k = 0; k < 10; k++) refit(l + 0x188 + 8 * k, l + sizes[k]);
    refit(l + 0x188, l + 0x94);
    refit(l + 0x1d8, l + 0xfc);
    refit(c + 0xc, c);
    refit(c + 0x20, c + 0x14);
}

/* No record for the screen (0x41ec10, also the HUD editor's new layout): both files (the player's, else
   the defaults) get a copy of the layout's record at their end, named the layout's and for the screen's
   size with its places fitted, and are written back whole (over the defaults when the player has none,
   made writable first, 0x59df60). */
static void add_layout(const char *player, int config)
{
    size_t n = 0, m = 0;
    char f1[200], f2[200];
    uint8_t *l = read_file(player, "1.hud", "XXhud1XX.hud", &n, f1, sizeof f1);
    uint8_t *c = read_file(player, "2.hud", "XXhud2XX.hud", &m, f2, sizeof f2);
    uint8_t *nl = l ? realloc(l, n + LOOK) : NULL, *nc = c ? realloc(c, m + CHOICE) : NULL;
    if (!nl || !nc || (size_t)LOOK * (size_t)(config + 1) > n || (size_t)CHOICE * (size_t)(config + 1) > m) {
        free(nl ? nl : l);
        free(nc ? nc : c);
        return;   /* (the original goes on with its record pointers unset) */
    }
    uint8_t *look = nl + n / LOOK * LOOK, *choice = nc + m / CHOICE * CHOICE;
    memcpy(look, nl + LOOK * config, LOOK);
    snprintf((char *)look, LOOK, "%s", config_name(config));
    memcpy(choice, nc + CHOICE * config, CHOICE);
    memcpy(choice + 0x28, &screen_w, 4);
    memcpy(choice + 0x2c, &screen_h, 4);
    refit_layout(look, choice);
    plat_save_user_file(f1, nl, n + LOOK);
    plat_save_user_file(f2, nc, m + CHOICE);
    free(nl);
    free(nc);
}

void HudConfig_Load(const char *player, int config, int w, int h)
{
    screen_w = w, screen_h = h;   /* 0x60c900, 0x60c81c; the layout 0x60c958 */
    if (loads == 0) {
        current = 0;
        char path[300];
        snprintf(path, sizeof path, "%shud.loc", g_hud_dir);   /* "%shud.loc" 0x5ccb78 */
        size_t n = 0;
        loc = vfs_read_all(path, &n);
        if (loc) LOC_setlanguage(loc, 0, 0);   /* (Options_Language: English) */
        if (!player) player = "";
        if (!load_layouts(player, config)) {
            add_layout(player, config);
            load_layouts(player, config);
        }
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

void HudConfig_setlookf(int off, float v)
{
    uint8_t *l = (uint8_t *)HudConfig_look();
    if (l) memcpy(l + off, &v, 4);
}

void HudConfig_setchoice(int off, int32_t v)
{
    uint8_t *c = (uint8_t *)HudConfig_choice();
    if (!c) return;
    memcpy(c + off, &v, 4);
    if (off == 0x58) HudConfig_trays(v == 0);   /* 0x420f50: the trays' alpha with the translucency */
}

/* The places back to fractions of the screen (0x41fef0), each only for a visual the choices show: the
   gear (k 0, by +0x64 == 1), the speedometer's two (1, 7: +0x30 == 1, 2), the time (2, always), the
   damage's (3: +0x3c 2 or 3; 5: 1), the standings (4: +0x48), the tachometer's (6: +0x38 == 2; 12: 1),
   the map (8: +0x40), 9 by +0x54, the mirror (10: +0x5c), the split times (11: +0x4c). */
static void to_fractions(void)
{
    uint8_t *l = (uint8_t *)HudConfig_look(), *c = (uint8_t *)HudConfig_choice();
    if (!l || !c) return;
    int32_t sp = rd32(c + 0x30), tach = rd32(c + 0x38), dmg = rd32(c + 0x3c);
    bool show[13] = {rd32(c + 0x64) == 1, sp == 1, true, dmg == 2 || dmg == 3, rd32(c + 0x48) != 0, dmg == 1,
                     tach == 2, sp == 2, rd32(c + 0x40) != 0, rd32(c + 0x54) != 0, rd32(c + 0x5c) != 0,
                     rd32(c + 0x4c) != 0, tach == 1};
    for (int k = 0; k < 13; k++) {
        if (!show[k]) continue;
        uint8_t *at = k < 11 ? l + 0x188 + 8 * k : c + (k == 11 ? 0xc : 0x20);
        float f[2] = {(float)pos[k][0] / (float)screen_w, (float)pos[k][1] / (float)screen_h};
        memcpy(at, f, 8);
    }
}

/* Written back (HUD_LoadLayout 0x41ea70, a save despite its name): the places as fractions, then both
   files whole to the player's own, else over the defaults (made writable first, 0x59df60). */
void HudConfig_Save(const char *player)
{
    if (!loads || !looks || !choices) return;
    to_fractions();
    char f[200];
    size_t n = 0;
    snprintf(f, sizeof f, "%s%s", player ? player : "", "1.hud");
    uint8_t *own = read_savedata(f, &n);
    free(own);
    plat_save_user_file(own ? f : "XXhud1XX.hud", looks, (size_t)LOOK * (size_t)nconfigs);
    snprintf(f, sizeof f, "%s%s", player ? player : "", "2.hud");
    own = read_savedata(f, &n);
    free(own);
    plat_save_user_file(own ? f : "XXhud2XX.hud", choices, (size_t)CHOICE * (size_t)nconfigs);
}
