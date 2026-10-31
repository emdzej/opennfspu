/* The HUD's art (see hudart.h). */
#include "render/hudart.h"
#include "pending.h"
#include "real/shape.h"
#include "real/window.h"
#include "render/hud2d.h"
#include "render/render.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The HUD directory (0x65b310: an entry of the path table the game fills at startup, not ported). */
const char *g_hud_dir = "GameData\\DashHud\\";

/* The pages made so far, by file name (the map at 0x60c99c). */
typedef struct Page {
    struct Page *next;
    char name[64];
    const uint8_t *file;
    Texture *texture;
    int32_t bucket, size;
} Page;
static Page *pages;
static int32_t next_bucket;   /* 0x60c9a0 */

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static int16_t rd16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }

/* An image's place in its file's page: the low twelve bits of its +0xc, and bits 16 to 27 (signed). */
static int packed_x(const uint8_t *s) { return (int32_t)((uint32_t)rd32(s + 0xc) << 20) >> 20; }
static int packed_y(const uint8_t *s) { return (int32_t)((uint32_t)rd32(s + 0xc) << 4) >> 20; }

static const uint8_t *image(const uint8_t *file, int i) { return file + rd32(file + 0x14 + 8 * i); }

/* The window depth for a shape type (the first image's). */
static int window_depth(const uint8_t *s)
{
    switch (s[0]) {
    case 0x6d: return 0x115c;
    case 0x78: return 0x10;
    case 0x7b: return SHAPE_typevalid(SHAPE_clut(s));
    case 0x7d: return 0x20;
    case 0x7e: return 0xf;
    default: return 0;
    }
}

static Page *make_page(const char *name, const char *path, int index, bool single)
{
    size_t n = 0;
    uint8_t *file = vfs_read_all(path, &n);
    if (!file) return NULL;
    Page *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    snprintf(p->name, sizeof p->name, "%s", name);
    p->file = file;
    int depth = window_depth(image(file, index));
    int count = (int)SHAPE_count(file), ext = 0, ey = 0;
    if (!single) {
        for (int i = 0; i < count; i++) {
            const uint8_t *s = image(file, i);
            int xe = packed_x(s) + rd16(s + 4), ye = packed_y(s) + rd16(s + 6);
            if (ext < xe) ext = xe;
            if (ey < ye) ey = ye;
        }
    } else {
        const uint8_t *s = image(file, index);
        int xe = packed_x(s) + rd16(s + 4), ye = packed_y(s) + rd16(s + 6);
        if (0 < xe) ext = xe;
        if (0 < ye) ey = ye;
    }
    if (ext <= ey) ext = ey;
    /* The side: the power of two up to 0x200 at least the extent (none past 0x200). */
    for (int side = 2; side <= 0x200; side *= 2)
        if (ext < side + 1) {
            p->size = side;
            break;
        }
    Window *w = WINDOW_create(p->size, p->size, depth, 0, 0);   /* 0x5356c0 */
    if (w) {
        WINDOW_select(w);
        WINDOW_clear(0);   /* (the colour match of black, 0x537c40) */
        if (!single)
            for (int i = 0; i < count; i++) {
                const uint8_t *s = image(file, i);
                WINDOW_drawshape(s, packed_x(s), packed_y(s));   /* 0x537a90 */
            }
        else {
            const uint8_t *s = image(file, index);
            WINDOW_drawshape(s, packed_x(s), packed_y(s));
        }
        char tname[80];
        snprintf(tname, sizeof tname, "HUD:%s", name);
        TexManager *tm = g_render ? g_render->textures : NULL;
        Texture *t = calloc(1, sizeof *t);
        if (t && tm) p->texture = Texture_fromshape(t, w->shape, tm, 0, tname);
        else free(t);
        TextureStates s;
        TextureStates_init(&s);
        TextureStates_set(&s, 1, 1);
        TextureStates_settile(&s, 0);
        if (p->texture) Texture_setstates(p->texture, &s);
        WINDOW_deselect();
        WINDOW_destroy(w);
    }
    p->bucket = next_bucket++;
    p->next = pages;
    pages = p;
    return p;
}

HudArt *HudArt_load(const char *name, int index, int x, int y, int w, int h, bool now, bool centred, bool single)
{
    HudArt *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->x = x, a->y = y, a->w = w, a->h = h;
    a->now = now;
    a->centred = centred;
    for (int k = 0; k < 4; k++) a->colour[k] = 0xffffffffu;
    char path[300];
    snprintf(path, sizeof path, "%s%s.fsh", g_hud_dir, name);
    Page *p = pages;
    while (p && strcmp(p->name, name)) p = p->next;
    if (!p) p = make_page(name, path, index, single);
    if (!p) return a;
    if (a->now) PENDING_NOTE(0x59ee20, "the art's own material (drawn at once)");
    a->file = p->file;
    a->texture = p->texture;
    a->bucket = p->bucket;
    a->size = p->size;
    a->shape = image(a->file, index);
    float side = (float)a->size;
    HudArt_place(a, x, y);
    HudArt_setuv(a, (float)packed_x(a->shape) / side, (float)packed_y(a->shape) / side, (float)rd16(a->shape + 4) / side,
                 (float)rd16(a->shape + 6) / side);
    return a;
}

void HudArt_free(HudArt *a) { free(a); }

void HudArt_place(HudArt *a, int x, int y)
{
    if (a->centred) {
        x -= a->w / 2;
        y -= a->h / 2;
    }
    a->x = x, a->y = y;
    float z = 0;   /* (a depth the original computes once into 0x60c98c) */
    float c[4][2] = {{(float)x, (float)y}, {(float)(x + a->w), (float)y}, {(float)(x + a->w), (float)(a->h + y)}, {(float)x, (float)(a->h + y)}};
    for (int k = 0; k < 4; k++) {
        a->corner[4 * k] = c[k][0];
        a->corner[4 * k + 1] = c[k][1];
        a->corner[4 * k + 2] = z;
        a->corner[4 * k + 3] = 1.0f;
    }
}

void HudArt_setuv(HudArt *a, float u, float v, float du, float dv)
{
    float u1 = u + du, v1 = v + dv;
    float e = a->inset ? 0.5f / (float)a->size : 0.0f;
    e = e + e;
    float sets[4][8] = {
        {u, v, u1, v, u1, v1, u, v1},                  /* as is */
        {u1 - e, v, u - e, v, u - e, v1, u1 - e, v1},  /* mirrored in x */
        {u, v1 - e, u1, v1 - e, u1, v - e, u, v - e},  /* in y */
        {u1, v1, u, v1, u, v, u1, v},                  /* both (no inset, as the original) */
    };
    memcpy(a->uv, sets, sizeof sets);
}

void HudArt_setcolours(HudArt *a, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3)
{
    a->coloured = 1;
    uint32_t c[4] = {c0, c1, c2, c3};
    for (int k = 0; k < 4; k++) a->colour[k] = g_hud_opaque ? c[k] | 0xff000000u : c[k];
}

void HudArt_setcorners(HudArt *a, const float xyz[12])
{
    for (int k = 0; k < 4; k++) {
        a->corner[4 * k] = xyz[3 * k];
        a->corner[4 * k + 1] = xyz[3 * k + 1];
        a->corner[4 * k + 2] = xyz[3 * k + 2];
        a->corner[4 * k + 3] = 1.0f;
    }
}

void HudArt_draw(HudArt *a)
{
    if (!a || !a->texture) return;
    const float *uv = a->uv[(a->flipx != 0) + ((a->flipy != 0) ? 2 : 0)];
    Hud2D_TexQuad(a->bucket, a->texture, a->colour, a->corner, uv);
}
