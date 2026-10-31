/* The screen sprites (render module 0x451360 .. 0x452370): flare.dat's effects and the quad mesh that
   draws them (sprite.h). */
#include "render/sprite.h"
#include "exe.h"
#include "vfs.h"
#include "render/render.h"
#include "render/r3dcar.h"
#include "real/random.h"
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
static Texture *s_blur;              /* 0x61bcb8 (the glows of 0x451c30, to come) */

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
    if (s_blend) s_blend->v[2] = 1;   /* 0x5377c0 (Ghidra's FERT_EventPump_v2, the same code): additive */
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

/* ---- billboards (RBillBoard.cpp, 0x44f5b0) ---- */

void RBillBoard_CreateQuads(int n, const RBillBoard *in, float (*pos)[4], uint32_t *colours)
{
    EaglView *v = g_render->device->current;
    EAGL_ViewUpdate(v);
    EaglMatrix inv;
    EAGL_MatInvert(&v->modelview, &inv);   /* (left as it was when singular) */
    static const float ax[3][4] = {{1.0f, 0, 0, 0}, {0, 1.0f, 0, 0}, {0, 0, 1.0f, 0}};
    float right[4], up[4], fwd[4];
    EAGL_MatTransform4(&inv, 1, ax[0], 16, right, 16);
    EAGL_MatTransform4(&inv, 1, ax[1], 16, up, 16);
    EAGL_MatTransform4(&inv, 1, ax[2], 16, fwd, 16);
    for (int i = 0; i < n; i++, in++, pos += 4, colours += 4) {
        float r[4], u[4];
        if (in->angle == 0) {
            memcpy(r, right, sizeof r);
            memcpy(u, up, sizeof u);
        } else {
            EaglMatrix m;
            EAGL_MatAxisAngle(&m, in->angle, fwd[0], fwd[1], fwd[2]);
            EAGL_MatTransform4(&m, 1, right, 16, r, 16);
            EAGL_MatTransform4(&m, 1, up, 16, u, 16);
        }
        float h = in->size * 0.5f;   /* 0x5b23f0 */
        const float *p = in->pos;
        for (int k = 0; k < 3; k++) {
            pos[0][k] = (u[k] - r[k]) * h + p[k];
            pos[1][k] = (u[k] + r[k]) * h + p[k];
            pos[2][k] = (r[k] * h - u[k] * h) + p[k];
            pos[3][k] = (-(r[k] * h) - u[k] * h) + p[k];
        }
        for (int k = 0; k < 4; k++) {
            pos[k][3] = p[3];
            colours[k] = in->colour;
        }
    }
}

/* ---- the track's glows (0x451810) ---- */

static uint8_t s_fog;          /* 0x61bca8: fog was on at the last glow */
static float s_fogdensity;     /* 0x61bca0 */
static uint32_t s_fogcolour;   /* 0x61bc9c */

/* The glow's alpha a and size by its flags (0x4514c0): 8 flickers (at most 190, plus up to 64), 1 and 2
   fade it by the facing f (1: f; 2: from 0.95 up, x 20), 0x20 and 0x40 scale the size the same ways;
   under fog it fades over 300 units. False when it ends up too faint (0.1) or small (1). */
static bool glow_fade(const Sprite *s, float f, float *size, float *a, float dist)
{
    uint32_t fl = s->rec->flags;
    if (fl & 8) {
        if (exe_f32(0x5b3d48) < *a) *a = 190.0f;
        *a = (float)((double)REAL_randf() * (double)exe_f32(0x5b24e4) + (double)*a);
    }
    float lo = exe_f32(0x5b2604), k = exe_f32(0x5b2610), x = exe_f32(0x5b2470);   /* 0.1, 0.95, 20 */
    if (fl & 1) {
        if (f < lo) return false;
        *a = f * *a;
    }
    if (fl & 2) {
        if (f < lo) return false;
        *a = (f - k) * *a * x;
        if (*a < lo) return false;
    }
    if (s->rec->flags & 0x60) {
        if (s->rec->flags & 0x20) *size = f * *size;
        if (s->rec->flags & 0x40) *size = (f - k) * *size * x;
        if (*size < 1.0f) return false;
    }
    if (s_fog) {
        *a = (1.0f - dist * exe_f32(0x5b3d44)) * *a;   /* 1/300 */
        if (*a < lo) return false;
    }
    return true;
}

/* The dot product of two points (0x556080): scaled by a's w over b's when they differ. */
static float dot4(const float *a, const float *b)
{
    if (a[3] == b[3]) return (float)((double)a[2] * b[2] + (double)a[0] * b[0] + (double)a[1] * b[1]);
    double r = (double)a[3] / (double)b[3];
    return (float)((double)b[2] * r * a[2] + r * b[0] * a[0] + (double)b[1] * r * a[1]);
}

void Sprite_DrawGlow(const float *g)
{
    uint32_t name;
    memcpy(&name, &g[7], 4);
    Sprite *s = SpriteBank_Find(g_sprites, name);
    if (!s || !s_mesh) return;
    const SpriteRec *r = s->rec;
    if (r->off != 0 || r->on != 0) {
        float ph = (r->flags & 0x10) ? g[8] : 0;
        uint32_t t = (uint32_t)(r->phase + (int32_t)ph + *s_clock);
        if (r->on < (int32_t)(t % (uint32_t)(r->on + r->off))) return;
    }
    RenderState *rs = g_render->device->rs;
    RCamera *c = g_render->camera;
    const EaglMatrix *model = EAGL_StackTop(&g_render->device->current->model);
    /* Its facing: the normal through the model matrix against the camera's back row. (The normal's w is
       0 and the row's 1, so the product is scaled by 0: always 0.) */
    float n[4] = {g[4], g[5], g[6], 0};
    EAGL_MatTransform4(model, 1, n, 16, n, 16);
    float back[4] = {c->frame.orient[6], c->frame.orient[7], c->frame.orient[8], 1.0f};
    float f = -dot4(n, back);
    float p[4] = {g[0], g[1], g[2], 1.0f};
    EAGL_MatTransform4(model, 1, p, 16, p, 16);
    const float *eye = c->frame.eye;
    float d2 = (p[0] - eye[0]) * (p[0] - eye[0]) + (p[1] - eye[1]) * (p[1] - eye[1]) + (p[2] - eye[2]) * (p[2] - eye[2]);
    /* Pulled towards the eye by a depth bias that grows as it nears. */
    s_blend->v[4] = (int32_t)lrintf(g_render->depthfactor * exe_f32(0x5b3d68) / d2);   /* -250000 */
    float angle = 0, dist = 0;
    if ((r->flags & 4) || s_fog) {   /* (the fog flag of the last glow) */
        uint32_t u;
        memcpy(&u, &d2, 4);
        u = (uint32_t)((int32_t)u >> 1) + 0x1fc00000u;   /* a quick square root */
        memcpy(&dist, &u, 4);
        angle = dist * exe_f32(0x5b3d64) + f * exe_f32(0x5b3d60);   /* 3.6, 120 (degrees) */
    }
    int z39 = rs->state39;
    rs->state39 = 0;
    EAGL_RSSet(0x39, 0);
    if (rs->state15 == 0) {
        s_fog = 0;
    } else {
        s_fog = 1;
        memcpy(&s_fogdensity, &rs->state0e, 4);
        s_fogcolour = (uint32_t)rs->state0f;
        RenderState_SetFogMode(rs, 0);
    }
    for (int i = 0; i < 2; i++) {
        Texture *tex = s->tex[i];
        if (!tex) continue;
        RBillBoard b = {{g[0], g[1], g[2], g[3]}, r->size[i] * exe_f32(0x5b2604), r->colour[i], 0};
        if (s_fog || (r->flags & 0x6b)) {
            float a = (float)(int32_t)(float)(b.colour >> 24);
            if (!glow_fade(s, f, &b.size, &a, dist)) continue;
            b.colour = (uint32_t)lrintf(a) << 24 | (b.colour & 0xffffffu);
        }
        b.angle = angle;
        static float v[4][4];
        static uint32_t col[4];
        RBillBoard_CreateQuads(1, &b, v, col);
        RMesh_setPositions(s_mesh, v, 4);
        RMesh_setUVs(s_mesh, 0, s_uv);
        RMesh_setColours(s_mesh, 0, col);
        RMesh_setTexture(s_mesh, 0, tex, 0);
        RMesh_setBlend(s_mesh, 0, s_blend);
        RMesh_draw(s_mesh, -1);
    }
    if (s_fog) {
        RenderState_SetFogMode(rs, 4);
        RenderState_SetFogMode(rs, 1);
        RenderState_SetFogDensity(rs, s_fogdensity);
        RenderState_SetFogColour(rs, s_fogcolour);
    }
    rs->state39 = z39;
    EAGL_RSSet(0x39, (uint32_t)z39);
}
