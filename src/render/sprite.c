/* The screen sprites (render module 0x451360 .. 0x452370): flare.dat's effects and the quad mesh that
   draws them (sprite.h). */
#include "render/sprite.h"
#include "exe.h"
#include "vfs.h"
#include "render/render.h"
#include "render/thrash.h"
#include "render/rmesh.h"
#include "render/rtex.h"
#include "render/sky_levels.h"
#include "render/sky_tex.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int32_t g_sim_tick;   /* 0x606ac4 */

SpriteBank *g_sprites;               /* 0x61bcc0 */
static RMesh *s_mesh;                /* 0x61bcb0 */
static SkyBlend *s_blend;            /* 0x61bcb4 */
static float s_uv[8];                /* 0x61bc78: the quad's corners (0,0) (1,0) (1,1) (0,1) */
static const int32_t *s_clock;       /* 0x61bcac: the race's tick counter */
static Texture *s_blur;              /* 0x61bcb8 */

enum { NAME_NULL = 0x6e756c6c };     /* 'null' */

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
static double exe_f64(uint32_t va)
{
    uint64_t u = (uint64_t)exe_u32(va + 4) << 32 | exe_u32(va);
    double d;
    memcpy(&d, &u, 8);
    return d;
}
static double sin_turns(float t) { return sin((double)t * exe_f64(0x5bb2c0)); }   /* 0x532550 */
static double cos_turns(float t) { return cos((double)t * exe_f64(0x5bb2c0)); }   /* 0x532560 */

void Sprites_Init(void)
{
    s_mesh = RMesh_ctor(malloc(sizeof *s_mesh), 0xe, 4, 1, NULL, 4);
    RMesh_stage(s_mesh, 0, NULL, NULL, NULL, NULL, 0);
    s_blend = SkyBlend_ctor(malloc(sizeof *s_blend));
    static const float uv[8] = {0, 0, 1, 0, 1, 1, 0, 1};
    memcpy(s_uv, uv, sizeof s_uv);
    s_clock = &g_sim_tick;
    s_blur = g_rtex_common[0] ? RTexBank_find(g_rtex_common[0], 0, 0x52554c42u) : NULL;   /* 'BLUR' */
}

void Sprites_Free(void)
{
    if (s_mesh) {
        RMesh_dtor(s_mesh);
        free(s_mesh);
    }
    s_mesh = NULL;
    free(s_blend);
    s_blend = NULL;
}

/* The defaults of a record (0x452370's, and the inline ones of 0x452180). */
static void rec_init(SpriteRec *r, uint32_t name)
{
    memset(r, 0, sizeof *r);
    r->flags = 4;
    r->name = name;
    for (int i = 0; i < 2; i++) {
        r->texname[i] = NAME_NULL;
        r->size[i] = 100.0f;
        r->colour[i] = 0xffffffffu;
    }
}

/* A layer's texture by name (0x451ff0): one of the sky's textures (its shape name is the bytes the
   other way round: 0x4609d0), else common.fsh's (byte-swapped). */
static void sprite_resolve(Sprite *s)
{
    for (int i = 0; i < 2; i++) {
        uint32_t n = s->rec->texname[i];
        if (n == NAME_NULL) {
            s->tex[i] = NULL;
            continue;
        }
        char name[5] = {(char)(n >> 24), (char)(n >> 16), (char)(n >> 8), (char)n, 0};
        int k = g_skytex ? SkyTextures_find(g_skytex, name) : -1;
        if (k == -1) {
            uint32_t sw = n >> 24 | (n >> 8 & 0xff00u) | (n << 8 & 0xff0000u) | n << 24;
            s->tex[i] = g_rtex_common[0] ? RTexBank_find(g_rtex_common[0], 0, sw) : NULL;
        } else {
            s->tex[i] = SkyTextures_texture(g_skytex, k);
        }
    }
}

static Sprite *sprite_new(const SpriteRec *src, uint32_t name)
{
    Sprite *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->rec = malloc(sizeof *s->rec);
    if (s->rec) {
        rec_init(s->rec, name);
        if (src) memcpy(s->rec, src, sizeof *s->rec);
        sprite_resolve(s);
    }
    return s;
}

void SpriteBank_Load(void)
{
    SpriteBank *b = calloc(1, sizeof *b);
    g_sprites = b;
    if (!b) return;
    char path[256];
    snprintf(path, sizeof path, exe_str(0x5cec24), "GameData\\Render\\");   /* "%sflare.dat" (0x65b30c) */
    size_t n = 0;
    uint8_t *d = vfs_read_all(path, &n);
    if (!d) {
        b->slot[b->count++] = sprite_new(NULL, NAME_NULL);
    } else {
        for (size_t o = 0; o + sizeof(SpriteRec) <= n && b->count < 200; o += sizeof(SpriteRec))
            b->slot[b->count++] = sprite_new((const SpriteRec *)(d + o), 0);
        free(d);
    }
    b->entries = malloc((size_t)b->count * sizeof *b->entries);
    if (b->entries) memcpy(b->entries, b->slot, (size_t)b->count * sizeof *b->entries);
}

void SpriteBank_Free(void)
{
    SpriteBank *b = g_sprites;
    g_sprites = NULL;
    if (!b) return;
    for (int i = 0; i < b->count; i++) {
        if (!b->slot[i]) continue;
        free(b->slot[i]->rec);
        free(b->slot[i]);
    }
    free(b->entries);
    free(b);
}

Sprite *SpriteBank_Find(SpriteBank *b, uint32_t name)
{
    if (!b || !b->entries) return NULL;
    Sprite **p = b->entries, **end = b->entries + b->count;
    int len = b->count;
    while (0 < len) {
        int half = len >> 1;
        if (p[half]->rec->name < name) {
            p += half + 1;
            len -= half + 1;
        } else {
            len = half;
        }
    }
    return p != end && (*p)->rec->name == name ? *p : NULL;
}

bool Sprite_Empty(const Sprite *s) { return !s->tex[0] && !s->tex[1]; }

void Sprite_Draw(const Sprite *s, const float *pos, float a, void *blend)
{
    const SpriteRec *r = s->rec;
    if (r->off != 0 || r->on != 0) {
        int t = r->phase + *s_clock;
        if (r->on < t % (r->on + r->off)) return;
    }
    if (!s_mesh) return;
    float turn = 0;
    if (r->flags & 4) turn = (float)((pos[1] + pos[0]) * exe_f64(0x5b3d58));   /* 1/1440 */
    RMesh_setUVs(s_mesh, 0, s_uv);
    RMesh_setBlend(s_mesh, 0, blend);
    for (int i = 0; i < 2; i++) {
        Texture *tex = s->tex[i];
        if (!tex || a < exe_f32(0x5b26a0)) continue;   /* 0.05 */
        uint32_t c = r->colour[i];
        if (a < 1.0f) c = (c & 0xffffffu) | (uint32_t)(int32_t)((float)(int32_t)(c >> 24) * a) << 24;
        uint32_t col[4] = {c, c, c, c};
        float sz = r->size[i] * exe_f32(0x5b2604);   /* 0.1 */
        float hw = (float)g_display.width * sz * exe_f32(0x5b3d50);    /* 1/640 */
        float hh = (float)g_display.height * sz * exe_f32(0x5b3d4c);   /* 1/480 */
        static float v[4][4];
        const float corner[4][2] = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
        float sn = (float)sin_turns(turn), cs = (float)cos_turns(turn);
        for (int k = 0; k < 4; k++) {
            float x = corner[k][0], y = corner[k][1];
            v[k][0] = y * sn + x * cs + pos[0];
            v[k][1] = y * cs - x * sn + pos[1];
            v[k][2] = 0;
            v[k][3] = 1.0f;
        }
        RMesh_setColours(s_mesh, 0, col);
        RMesh_setPositions(s_mesh, v, 4);
        RMesh_setTexture(s_mesh, 0, tex, 0);
        RMesh_draw(s_mesh, -1);
    }
}
