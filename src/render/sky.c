/* See sky.h. */
#include "render/sky.h"
#include "exe.h"
#include "game/track.h"
#include "pending.h"
#include "real/file.h"
#include "real/mem.h"
#include "render/render.h"
#include "support/ufile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SkyConfig *g_sky_config;      /* 0x627e18 */
static const uint8_t **g_sky_light;  /* 0x627e1c */
static uint8_t *g_sky_fsh;           /* 0x627e50 */

SkyConfig *Sky_Config(void) { return g_sky_config; }
const uint8_t *Sky_Fsh(void) { return g_sky_fsh; }

void Sky_FreeFsh(void)
{
    MEM_purge(g_sky_fsh);
    g_sky_fsh = NULL;
}

static void put(uint8_t *b, int word, uint32_t v) { memcpy(b + word * 4, &v, 4); }
static void putf(uint8_t *b, int word, float v) { memcpy(b + word * 4, &v, 4); }
static void copy_words(uint8_t *b, int dst, int src, int n) { memmove(b + dst * 4, b + src * 4, (size_t)n * 4); }

void SkyConfig_Upgrade(SkyConfig *c, int v)
{
    /* The new block (MEM_alloc 'SkyCnfig'; the original's heap block isn't cleared: the port zeroes it
       so the bytes past the old block's size are defined) with the old one's contents. */
    uint8_t *b = MEM_alloc(exe_str(0x5ceedc), SKY_CONFIG_SIZE, 0);
    if (!b) return;
    memset(b, 0, SKY_CONFIG_SIZE);
    int old = c->data ? MEM_size(c->data) : 0;
    if (old > SKY_CONFIG_SIZE) old = SKY_CONFIG_SIZE;
    if (old > 0) memcpy(b, c->data, (size_t)old);
    put(b, 0, SKY_CONFIG_VERSION);
    if (v < 1) {
        put(b, 1, 0);
        put(b, 2, 0);
        put(b, 3, 2);
        put(b, 4, 2);
        putf(b, 5, -100.0f);
        putf(b, 6, 250.0f);
        memset(b + 0x1c, 0, 0x20);
        put(b, 0xf, 0);
        putf(b, 0x10, 1000.0f);
        putf(b, 0x12, 75.0f);
        put(b, 0x11, 0x39d1b717u);    /* 0.0004 */
    }
    if (v < 3) {
        put(b, 0x2cf, 0);
        putf(b, 0x2d0, 1000.0f);
        put(b, 0x2d1, 0xb951b717u);   /* -0.0002 */
        putf(b, 0x2d2, -75.0f);
    }
    if (v < 5) {
        put(b, 0x338, 0);
        put(b, 0x337, 0);
    }
    if (v < 6) put(b, 0x339, 0x3dcccccdu);   /* 0.1 */
    if (v < 7) {
        for (int k = 0; k < 4; k++) {           /* four records of 15 words from word 0x33a */
            int p = 0x342 + k * 15;
            static const int zero[] = {-8, -7, 0, -4, -5, -1, -2, 3, 2, 6, 5};
            for (size_t i = 0; i < sizeof zero / sizeof *zero; i++) put(b, p + zero[i], 0);
            putf(b, p - 6, 254.0f);
            putf(b, p - 3, 254.0f);
            putf(b, p + 1, 254.0f);
            putf(b, p + 4, 254.0f);
        }
        put(b, 0x33a, 1);
    }
    if (v < 8) put(b, 0x376, 1);
    if (v < 9) {
        if (v < 0) {
            for (int i = 0; i < 400; i++) { put(b, 0x377 + 2 * i, 0xffffffffu); put(b, 0x378 + 2 * i, 0); }
        } else {
            copy_words(b, 0x377, 0x13, 100);
        }
        if (v < 2) {
            for (int off = 0; off < 0x4b00; off += 0x30) {
                put(b, (0x1a5c + off) / 4, 0xffffffffu);
                for (int k = 1; k < 12; k++) put(b, (0x1a5c + off) / 4 + k, 0);
            }
        } else {
            copy_words(b, 0x697, 0x77, 600);
        }
        if (v < 4) {
            for (int i = 0; i < 400; i++) { put(b, 0x1957 + 2 * i, 0xffffffffu); b[(0x1958 + 2 * i) * 4] = 0; }
        } else {
            copy_words(b, 0x1957, 0x2d3, 100);
        }
    }
    MEM_purge(c->data);
    c->data = b;
}

/* 0x4594d0 (operator new 0x10c + Sky_LoadConfig 0x459530). */
SkyConfig *Sky_CreateConfig(const char *name)
{
    Sky_FreeConfig();
    SkyConfig *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->flag = 0;
    c->f108 = -1;
    snprintf(c->path, sizeof c->path, exe_str(0x5ceefc), g_track_dir, name);
    c->data = UTIL_loadfile(c->path, 0);
    if (!c->data) {
        snprintf(c->path, sizeof c->path, exe_str(0x5ceee8), g_track_dir);
        c->data = UTIL_loadfileabort(c->path, 0);
    }
    if (!c->data) {
        /* no file: an empty block (not cleared in the original) upgraded from version 0 */
        c->data = MEM_alloc(exe_str(0x5ceedc), SKY_CONFIG_SIZE, 0);
        if (c->data) memset(c->data, 0, SKY_CONFIG_SIZE);
        SkyConfig_Upgrade(c, 0);
    } else {
        int32_t v;
        memcpy(&v, c->data, 4);
        if (v < 9) SkyConfig_Upgrade(c, v);
    }
    g_sky_config = c;
    return c;
}

void Sky_FreeConfig(void)
{
    if (!g_sky_config) return;
    MEM_purge(g_sky_config->data);
    free(g_sky_config);
    g_sky_config = NULL;
}

/* 0x4598f0: from the weather down to 0, the first slot whose first word is set; else 0. */
int Sky_SlotFor(int weather)
{
    if (!g_sky_config || !g_sky_config->data) return 0;
    for (int i = weather; i >= 0; i--) {
        uint32_t first;
        memcpy(&first, g_sky_config->data + SKY_SLOT_BASE + i * SKY_SLOT_SIZE, 4);
        if (first) return i;
    }
    return 0;
}

void Sky_CreateLight(int weather)   /* 0x459820 */
{
    Sky_FreeLight();
    g_sky_light = malloc(sizeof *g_sky_light);
    if (!g_sky_light) return;
    *g_sky_light = g_sky_config && g_sky_config->data
                       ? g_sky_config->data + SKY_SLOT_BASE + Sky_SlotFor(weather) * SKY_SLOT_SIZE : NULL;
}

/* The light environment's slot again: the level's (0x4598f0), or slot 0's (0x4598b0, whose search
   starts and ends at 0). */
void Sky_SetLight(bool level)
{
    if (!g_sky_light) return;
    *g_sky_light = g_sky_config && g_sky_config->data
                       ? g_sky_config->data + SKY_SLOT_BASE + (level ? Sky_SlotFor(g_renderopts.skyrendering) : 0) * SKY_SLOT_SIZE
                       : NULL;
}

void Sky_FreeLight(void)            /* 0x459890 */
{
    free(g_sky_light);
    g_sky_light = NULL;
}

const uint8_t *Sky_LightSlot(void) { return g_sky_light ? *g_sky_light : NULL; }

/* Sky_LoadFsh 0x45da90: the texture file, then the sky's texture object (0x460910, a new one; on a
   reload 0x460b90 gives the existing one the new file) and the sky (0x45d6c0, its layers from
   Sky\<name>.txt; on a reload the old sky freed first). */
void Sky_LoadFsh(const char *name, bool reload)
{
    if (reload) {
        MEM_purge(g_sky_fsh);
        g_sky_fsh = NULL;
    }
    char path[256];
    snprintf(path, sizeof path, exe_str(0x5cf130), g_track_dir, name);
    g_sky_fsh = UTIL_loadfile(path, 0);
    if (!g_sky_fsh) {
        snprintf(path, sizeof path, exe_str(0x5cf11c), g_track_dir);
        g_sky_fsh = UTIL_loadfileabort(path, 0);
    }
    Sky_Create(name, reload);   /* the texture object (0x460910 / 0x460b90) and the sky (0x45d6c0) */
}
