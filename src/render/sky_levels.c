/* The sky's pieces (see sky_levels.h). */
#include "render/sky_levels.h"
#include "exe.h"
#include "render/render.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static float cfg_f(const SkyConfig *c, int off) { float f; memcpy(&f, c->data + off, 4); return f; }
static int32_t cfg_i(const SkyConfig *c, int off) { int32_t v; memcpy(&v, c->data + off, 4); return v; }

/* Sine and cosine of an angle in turns (0x532550 / 0x532560: times 2 pi, in x87 precision). */
/* 2 pi as the double at 0x5bb2c0 (fmul qword). */
static double two_pi(void)
{
    uint32_t lo = exe_u32(0x5bb2c0), hi = exe_u32(0x5bb2c4);
    uint64_t u = (uint64_t)hi << 32 | lo;
    double d;
    memcpy(&d, &u, 8);
    return d;
}
static double sin_turns(float t) { return sin((double)t * two_pi()); }
static double cos_turns(float t) { return cos((double)t * two_pi()); }

/* x87 fistp: round to nearest, ties to even. */
static int fround(float v) { return (int)lrintf(v); }

SkyBlend *SkyBlend_ctor(SkyBlend *b)
{
    b->v[0] = 1;
    b->v[1] = 2;
    b->v[2] = b->v[3] = b->v[4] = 0;
    b->v[5] = -1;
    return b;
}

/* ---- SkyLevel_Base (0x45a960 / 0x45a9a0) ---- */

static void base_init(SkyLevel *l, const SkyLevelVtbl *vt, uint32_t flags)
{
    l->vt = vt;
    l->flags = flags;
}

/* ---- SkyHorizon ---- */

static void horizon_update(SkyLevel *l, const float a[3], const float b[3], float blend, int ticks) {}   /* 0x46bc30 */

/* The segments in view (0x45a060): the heading against the horizon's rotation (+0x04 of the
   configuration, in turns), the quads from a quarter turn left of it to a quarter turn right, one more
   on each side; drawn from the doubled arrays, so a range that wraps stays contiguous. */
static void horizon_draw(SkyLevel *l, float heading)
{
    SkyHorizon *h = (SkyHorizon *)l;
    if (!h->active) return;
    float t = heading - cfg_f(h->config, 4);
    if (t < exe_f32(0x5b23f8)) t += exe_f32(0x5b24a8);
    float q = exe_f32(0x5b272c), segs = exe_f32(0x5b26a8);
    int a = fround((t - q) * segs), b = fround((t + q) * segs);
    int lo = a - 1, hi = b + 1;
    if (lo < 0) {
        lo = a + 15;
        hi = b + 17;
    }
    RMesh_setPositions(h->mesh, h->pos[lo * 4], (hi - lo) * 4);
    RMesh_setUVs(h->mesh, 0, h->uv[lo * 4]);
    RMesh_setColours(h->mesh, 0, &h->col[lo * 4]);
    RMesh_draw(h->mesh, -1);
}

static void horizon_dtor(SkyLevel *l, int free_self)   /* 0x459b80 */
{
    SkyHorizon *h = (SkyHorizon *)l;
    if (h->active) {
        if (h->mesh) {
            RMesh_dtor(h->mesh);
            free(h->mesh);
        }
        free(h->col);
        free(h->uv);
        free(h->blend);
        free(h->pos);
    }
    if (free_self) free(h);
}

/* The colours, texture coordinates and positions (0x459bf0). */
static void horizon_rebuild(SkyLevel *l)
{
    SkyHorizon *h = (SkyHorizon *)l;
    if (!h->active) return;
    SkyConfig *c = h->config;
    /* 'hzcl': a 16 x 2 image; per segment the upper row's colours for the top edge and the lower row's for
       the bottom, starting at the segment the configuration's +0x08 says. The second copy (from vertex 64)
       takes its second vertex's colour without that offset, as the original does. */
    const uint8_t *hz = SkyTextures_shape(h->textures, SkyTextures_find(h->textures, exe_str(0x5cef74)));
    int32_t rot = cfg_i(c, 8);
    for (int i = 0; i < 16; i++) {
        uint32_t k = (uint32_t)(rot + i) & 15, k1 = (uint32_t)(rot + i + 1) & 15, k2 = (uint32_t)(1 - rot + rot + i) & 15;
        uint32_t top0, top1, top2, bot0, bot1;
        memcpy(&top0, hz + 0x10 + 4 * k, 4);
        memcpy(&top1, hz + 0x10 + 4 * k1, 4);
        memcpy(&top2, hz + 0x10 + 4 * k2, 4);
        memcpy(&bot1, hz + 0x50 + 4 * k1, 4);
        memcpy(&bot0, hz + 0x50 + 4 * k, 4);
        uint32_t *a = &h->col[4 * i], *b = &h->col[64 + 4 * i];
        a[0] = b[0] = top0;
        a[1] = top1;
        b[1] = top2;
        a[2] = b[2] = bot1;
        a[3] = b[3] = bot0;
    }
    /* Texture coordinates: the tile of each segment (the byte at +0x1c + 2 k of a grid of +0x0c columns
       and +0x10 rows; the next byte flips it), the rows inset 0.01 at the top and to 0.98 at the bottom. */
    int cols = cfg_i(c, 0xc);
    float du = exe_f32(0x5b24a8) / (float)cols, dv = exe_f32(0x5b24a8) / (float)cfg_i(c, 0x10);
    float top = exe_f32(0x5b27a8), bottom = exe_f32(0x5b25f4), one = exe_f32(0x5b24a8);
    for (int q = 0; q < h->nuv / 4; q++) {
        int k = q & 15;
        uint8_t tile = c->data[0x1c + 2 * k], flip = c->data[0x1d + 2 * k];
        float col = (float)(tile % cols), row = (float)(tile / cols);
        float u0, u1, v0 = (row + top) * dv, v1 = (row + bottom) * dv;
        if (!flip) {
            u0 = col * du;
            u1 = (col + one) * du;
        } else {
            u0 = (col + one) * du;
            u1 = col * du;
        }
        float (*uv)[2] = &h->uv[4 * q];
        uv[0][0] = u0, uv[0][1] = v0;
        uv[1][0] = u1, uv[1][1] = v0;
        uv[2][0] = u1, uv[2][1] = v1;
        uv[3][0] = u0, uv[3][1] = v1;
    }
    /* The ring: 16 points 400 from the centre (0x5b25c4), from the configuration's angle (+0x04, turns), a
       sixteenth of a turn apart (0x3d800000); the band from +0x14 up by +0x18. */
    float ang = cfg_f(c, 4);
    float ring[16][4];
    float x = (float)(sin_turns(ang) * (double)exe_f32(0x5b25c4));
    float z = (float)(cos_turns(ang) * 400.0 - sin_turns(ang) * 0.0);
    for (int i = 0; i < 16; i++) {
        ring[i][0] = x;
        ring[i][1] = 0;
        ring[i][2] = z;
        ring[i][3] = 1;
        double s = sin_turns(0.0625f), co = cos_turns(0.0625f);
        float nx = (float)(co * (double)x + (double)(float)(s * (double)z));
        float nz = (float)((double)(float)(co * (double)z) - s * (double)x);
        x = nx;
        z = nz;
    }
    float ytop = cfg_f(c, 0x18) + cfg_f(c, 0x14), ybot = cfg_f(c, 0x14);
    for (int q = 0; q < h->npos / 4; q++) {
        int k = q & 15, k1 = (k + 1) & 15;
        float (*p)[4] = &h->pos[4 * q];
        memcpy(p[0], ring[k], sizeof ring[k]);
        p[0][1] = ytop;
        memcpy(p[1], ring[k1], sizeof ring[k1]);
        p[1][1] = ytop;
        memcpy(p[2], ring[k1], sizeof ring[k1]);
        p[2][1] = ybot;
        memcpy(p[3], ring[k], sizeof ring[k]);
        p[3][1] = ybot;
    }
}

static const SkyLevelVtbl horizon_vtbl = {horizon_update, horizon_draw, horizon_dtor, horizon_rebuild};

SkyHorizon *SkyHorizon_ctor(SkyHorizon *h, SkyTextures *textures, SkyConfig *config)
{
    memset(h, 0, sizeof *h);
    base_init(&h->base, &horizon_vtbl, 0x80);
    h->config = config;
    h->textures = textures;
    const char *hzcl = exe_str(0x5cef74), *horz = exe_str(0x5cef6c);
    const char *level = exe_str(0x5cef14 + 6u * (unsigned)g_renderopts.skyrendering);   /* "hor0".."hor3" */
    if (!textures || SkyTextures_find(textures, hzcl) == -1 ||
        (SkyTextures_find(textures, horz) == -1 && SkyTextures_find(textures, level) == -1)) {
        h->active = 0;
        return h;
    }
    h->active = 1;
    h->npos = 0x80;
    h->pos = calloc(0x80, sizeof *h->pos);
    h->nuv = 0x80;
    h->uv = calloc(0x80, sizeof *h->uv);
    h->ncol = 0x80;
    h->col = calloc(0x80, sizeof *h->col);
    h->blend = SkyBlend_ctor(malloc(sizeof *h->blend));
    int t = SkyTextures_find(textures, level);
    if (t == -1) t = SkyTextures_find(textures, horz);
    h->tex = SkyTextures_texture(textures, t);
    h->mesh = RMesh_ctor(calloc(1, sizeof *h->mesh), 0x16, 4, 1, h->pos, 0x40);
    RMesh_stage(h->mesh, 0, h->tex, h->blend, h->uv, h->col, 1);
    horizon_rebuild(&h->base);
    return h;
}
