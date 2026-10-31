/* The sun and its lens flare (sky module 0x4600b0 .. 0x460640; sky_sun.h). */
#include "render/sky_sun.h"
#include "app.h"
#include "exe.h"
#include "vfs.h"
#include "render/render.h"
#include "render/thrash.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Sun *g_sun;   /* 0x627e5c */

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static float cfg_f(const SkyConfig *c, int off)
{
    float f;
    memcpy(&f, c->data + off, 4);
    return f;
}

static int32_t tick_count(void) { return (int32_t)app_ticks_ms(); }   /* GetTickCount */

/* The sprites' names: "anus" (the sun), then "bnus" .. "nnus" (0x5cf52c, a word each). */
static uint32_t sprite_name(int i) { return exe_u32(0x5cf52c + 4 * (uint32_t)i); }

void Sun_Create(const SkyConfig *config)
{
    Sun *s = calloc(1, sizeof *s);
    g_sun = s;
    if (!s) return;
    s->tick = tick_count();
    s->config = config;
    float x = cfg_f(config, 0xcdc), y = cfg_f(config, 0xce0);
    float lo = exe_f32(0x5b3f54), hi = exe_f32(0x5b3f50);   /* -0.02, 0.02 */
    s->enabled = x <= lo || hi <= x || y <= lo || hi <= y;
    static const float uv[8] = {0, 0, 1, 0, 1, 1, 0, 1};
    memcpy(s->uv, uv, sizeof s->uv);
    s->bpp = 2;
    for (int i = 0; i < SUN_SPRITES; i++) {
        s->blend[i] = SkyBlend_ctor(malloc(sizeof *s->blend[i]));
        s->mesh[i] = RMesh_ctor(malloc(sizeof *s->mesh[i]), 0x16, 4, 1, s->pos, 4);
        if (s->mesh[i]) RMesh_stage(s->mesh[i], 0, NULL, s->blend[i], NULL, NULL, 0);
    }
    Sky_SunPosition(s->world);   /* 0x460f50 */
    memcpy(s->screen, s->world, sizeof s->screen);
    char path[256];
    snprintf(path, sizeof path, exe_str(0x5cf564), "GameData\\Render\\");   /* "%sflarepos.dat" (0x65b30c) */
    size_t n = 0;
    uint8_t *d = vfs_read_all(path, &n);
    /* (the port's block is at least the 14 floats the flare reads) */
    s->flarepos = calloc(n < 0x38 ? 0x38 : n, 1);
    if (d && s->flarepos) {
        memcpy(s->flarepos, d, n);
    } else if (s->flarepos) {
        static const float def[9] = {1.0f, 0.75f, -0.75f, 0.5f, -0.5f, 0.25f, -0.25f, 0, -1.0f};
        memcpy(s->flarepos, def, sizeof def);
    }
    free(d);
}

void Sun_Free(void)
{
    Sun *s = g_sun;
    g_sun = NULL;
    if (!s) return;
    free(s->flarepos);
    for (int i = 0; i < SUN_SPRITES; i++) {
        free(s->blend[i]);
        if (s->mesh[i]) {
            RMesh_dtor(s->mesh[i]);
            free(s->mesh[i]);
        }
    }
    free(s);
}

/* A sprite of the bank on first use (none if neither of its layers has a texture). */
static Sprite *sun_sprite(Sun *s, int i)
{
    if (!s->tried[i] && !s->sprite[i]) {
        Sprite *p = SpriteBank_Find(g_sprites, sprite_name(i));
        if (p && Sprite_Empty(p)) p = NULL;
        s->sprite[i] = p;
        s->tried[i] = 1;
    }
    return s->sprite[i];
}

void Sun_Draw(Sun *s)
{
    if (!s->enabled) return;
    Sprite *sp = sun_sprite(s, 0);
    RCamera *c = g_render->camera;
    EAGL_ViewProject(c->view, 1, s->world, s->screen);
    float x = s->screen[0], y = s->screen[1];
    s->onscreen = sp && !(x < 0) && x < (float)g_display.width && !(y < 0) && y < (float)g_display.height &&
                  s->screen[2] < 1.0f;
    if (!s->onscreen) return;
    RCamera_Setup2D(c);
    Sprite_Draw(sp, s->screen, 1.0f, s->blend[0]);
    RCamera_Setup(c);
    if (s->state != 0) return;
    /* (0x5deb18, the CPU's MMX flag, is set on any machine the port runs on) */
    if (THRASH.fn.readrect((int)lrintf(x) - 2, (int)lrintf(y) - 2, 4, 4, s->ref) && g_render_modalpha == 1) {
        s->state = 1;
        s->bpp = s->ref[15] == 0 ? 2 : 4;
        return;
    }
    s->state = 2;
}

void Sun_DrawFlare(Sun *s)
{
    if (!s->enabled || s->state == 2 || s->state == 0 || !s->onscreen || !g_renderopts.lensflare ||
        !*(const int32_t *)(s->config->data + 0xdd8))
        return;
    int32_t now = tick_count();
    if (s->tick + 200 < now) {
        s->tick = now;
        s->seen = 0;
        s->unchanged = 0;
        uint32_t px[16] = {0};
        if (!THRASH.fn.readrect((int)lrintf(s->screen[0]) - 2, (int)lrintf(s->screen[1]) - 2, 4, 4, px)) return;
        s->reads += 1.0f;
        s->readms += (float)(tick_count() - now);
        /* Reading back too slow (over 5 ms on average after 10 reads): no flare from now on. */
        if (exe_f32(0x5b241c) < s->reads && s->reads < exe_f32(0x5b2524) && exe_f32(0x5b2400) < s->readms / s->reads) {
            s->state = 2;
            return;
        }
        if (exe_f32(0x5b2524) < s->reads) s->reads = s->readms = 0;
        /* The pixels as the sun left them: 16-bit words (the first half of the block) or 32-bit ones. */
        const uint16_t *a16 = (const uint16_t *)px, *r16 = (const uint16_t *)s->ref;
        for (int i = 0; i < 16; i++)
            if ((s->bpp == 2 && a16[i] == r16[i]) || (s->bpp == 4 && px[i] == s->ref[i])) s->unchanged += 1.0f;
        if (exe_f32(0x5b2604) < s->unchanged) s->seen = 1;
    }
    float cx = (float)g_display.width * 0.5f, cy = (float)g_display.height * 0.5f;   /* 0x5b23f0 */
    float dx = s->screen[0] - cx, dy = s->screen[1] - cy;
    float a = s->unchanged * exe_f32(0x5b26a4);   /* 1/16 */
    RCamera *c = g_render->camera;
    RCamera_Setup2D(c);
    for (int i = 1; i < SUN_SPRITES; i++) {
        Sprite *sp = sun_sprite(s, i);
        if (!sp) continue;
        float f = s->flarepos[i];
        float p[4] = {dx * f + cx, dy * f + cy, 0, 1.0f};
        Sprite_Draw(sp, p, a, s->blend[i]);
    }
    RCamera_Setup(c);
}
