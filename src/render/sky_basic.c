/* The sky's dome levels, first part (see sky_levels.h): the dome's grid (0x460d80, 0x460e00), its quads
   (0x460c30), the sky map and its sources (0x45dc50 .. 0x45e6a0, with the lightning's bolts 0x45e290,
   0x45e410), the scroller of the cloud texture (Scroller_BasicSky 0x45e7a0 .. 0x45f280) and
   SkyLevel_BasicSky (0x45acb0, vtable 0x5b3e8c). */
#include "render/sky_levels.h"
#include "exe.h"
#include "pending.h"
#include "real/random.h"
#include "render/noise.h"
#include "render/render.h"
#include "render/sky_text.h"
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
static double exe_f64(uint32_t va)
{
    uint64_t u = (uint64_t)exe_u32(va + 4) << 32 | exe_u32(va);
    double d;
    memcpy(&d, &u, 8);
    return d;
}
static float cfg_f(const SkyConfig *c, int off) { float f; memcpy(&f, c->data + off, 4); return f; }
static double sin_turns(float t) { return sin((double)t * exe_f64(0x5bb2c0)); }   /* 0x532550 */
static double cos_turns(float t) { return cos((double)t * exe_f64(0x5bb2c0)); }   /* 0x532560 */
static int fround(double v) { return (int)lrint(v); }   /* fistp: to nearest, ties to even */

/* ---- the dome's grid (0x28 bytes) ---- */

/* The (n+1)^2 vertices of a paraboloid: rows from -size/2 along z, columns from +size/2 down along x,
   turned by the angle (turns), the height less the squared distance times the curve, all scaled.
   0x460e00 */
void SkyGrid_build(SkyGrid *g, float size, float curve, float height, float angle, float scale)
{
    g->size = size;
    float step = size / (float)g->n;
    g->curve = curve;
    g->angle = angle;
    g->scale = scale;
    g->height = height;
    g->step = step;
    float z = step * (float)g->n * -0.5f;   /* 0x5b2448 */
    int k = 0;
    for (int j = 0; j < g->n1; j++) {
        float x = (float)g->n * g->step * 0.5f;   /* 0x5b23f0 */
        for (int i = 0; i < g->n1; i++, k++) {
            double c = (float)cos_turns(angle), s = sin_turns(angle);
            float *v = g->verts[k];
            v[3] = 1.0f;
            v[0] = (float)((s * z + c * x) * scale);
            v[1] = (height - (x * x + z * z) * curve) * scale;
            v[2] = (float)((c * z - s * x) * scale);
            x -= g->step;
        }
        z += g->step;
    }
}

SkyGrid *SkyGrid_ctor(SkyGrid *g, int n, float size, float curve, float height, float angle, float scale)   /* 0x460d80 */
{
    g->n = n;
    g->n1 = n + 1;
    g->size = size;
    g->curve = curve;
    g->height = height;
    g->angle = angle;
    g->scale = scale;
    g->step = size / (float)n;
    g->count = g->n1 * g->n1;
    g->verts = calloc((size_t)g->count, sizeof *g->verts);
    if (g->verts) SkyGrid_build(g, size, curve, height, angle, scale);
    return g;
}

static void SkyGrid_dtor(SkyGrid *g) { free(g->verts); }   /* 0x460d70 */

/* ---- the quads (0x10 bytes) ---- */

/* Four vertex indices (bytes) a quad of the n x n grid, in blocks of block x block quads, the blocks
   row by row, so that each block's quads are a run of block^2 * 4 indices. Not built when mode is 1.
   0x460c30 (the scroller builds the same list itself) */
static void build_quads(uint8_t *p, int n, int block)
{
    int nb = n / block;
    for (int by = 0; by < nb; by++)
        for (int bx = 0; bx < nb; bx++)
            for (int r = by * block; r < by * block + block; r++) {
                uint8_t a = (uint8_t)(r * (n + 1)), b = (uint8_t)((r + 1) * (n + 1));
                for (int c = bx * block; c < bx * block + block; c++) {
                    *p++ = (uint8_t)(c + a);
                    *p++ = (uint8_t)(c + a + 1);
                    *p++ = (uint8_t)(c + b + 1);
                    *p++ = (uint8_t)(c + b);
                }
            }
}

static SkyQuads *SkyQuads_ctor(SkyQuads *q, int n, int block, int mode)
{
    q->n = n;
    q->n1 = n + 1;
    q->nbytes = n * n * 4;
    q->idx = calloc((size_t)q->nbytes, 1);
    if (mode != 1 && q->idx && n / block > 0) build_quads(q->idx, n, block);
    return q;
}

/* ---- the sky map's sources ---- */

static void src_texture_dtor(SkyMapSource *s, int free_self) { if (free_self) free(s); }   /* 0x45e4a0 */
static void src_texture_update(SkyMapSource *s, float t) { (void)s; (void)t; }           /* 0x525120 */
static const SkyMapSourceVtbl src_texture_vt = {src_texture_dtor, src_texture_update};    /* 0x5b3ef4 */

/* A shape of the sky file: its pixels (32-bit) after the 16-byte header. 0x45e520 */
static SkyMapSource *src_texture_new(int size, const uint8_t *shape)
{
    SkyMapSource *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->vt = &src_texture_vt;
    s->size = size;
    s->shape = shape;
    s->pixels = (uint8_t *)(uintptr_t)(shape + 0x10);
    return s;
}

/* The noise's values: (x + offset) * 0.3, y * 0.3 and t times the speed into the noise, times 0.8, plus
   1, times 127.5. x isn't reset at each row (the original's counter runs on through the rows). */
static void noise_fill(SkyMapSource *s, float t)
{
    float x = 0, y = 0;
    int32_t *v = (int32_t *)(void *)s->pixels;
    for (int j = 0; j < s->size; j++) {
        float fy = y * 0.3f;   /* 0x5b25f0 */
        for (int i = 0; i < s->size; i++) {
            float n = Noise3((x + s->offset) * 0.3f, fy, t * s->speed);
            v[j * s->size + i] = (int32_t)(((double)n * 0.8 + 1.0) * 127.5);   /* 0x5b3f00, 0x5b3efc; _ftol */
            x += 1.0f;
        }
        y += 1.0f;
    }
}

static void src_noise_dtor(SkyMapSource *s, int free_self)   /* SkyMapSource_Noise_v0 0x45e660 */
{
    free(s->pixels);
    if (free_self) free(s);
}

/* Refilled at t when the speed is above 0.1. 0x45e6a0 */
static void src_noise_update(SkyMapSource *s, float t)
{
    if (exe_f64(0x5b3f10) < (double)s->speed) noise_fill(s, t);
}
static const SkyMapSourceVtbl src_noise_vt = {src_noise_dtor, src_noise_update};   /* 0x5b3f08 */

/* "noise<speed>": the speed after the word, a random offset up to 100. 0x45e540 */
static SkyMapSource *src_noise_new(int size, float t, const char *tok)
{
    SkyMapSource *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->vt = &src_noise_vt;
    s->size = size;
    s->pixels = calloc((size_t)(size * size), 4);
    s->speed = (float)atof(tok + strlen(exe_str(0x5cefb8)));
    s->offset = (float)((double)REAL_randf() * 100.0);   /* 0x5b242c */
    if (s->pixels) noise_fill(s, t);
    return s;
}

/* Every pixel's alpha 0xff. 0x45e4c0 */
static bool src_opaque(const SkyMapSource *s)
{
    float n = (float)(s->size * s->size);
    for (int i = 0; (float)i < n; i++)
        if (s->pixels[i * 4 + 3] != 0xff) return false;
    return true;
}

/* ---- the sky map (0x24 bytes) and its lightning (0x1930 bytes) ---- */

/* A bolt's fresh state: a peak up to the strength, a step of twice it over the count times the
   spread (at least 2), at least 1; idle (-1). */
static void bolt_reset(SkyBolt *b, const SkyStorm *st)
{
    float peak = (float)((double)REAL_randf() * (double)st->strength);
    double d = (double)REAL_randf() * (double)st->count * (double)st->spread;
    if (d < 2.0) d = 2.0;   /* 0x5b2580 */
    float step = (float)(((double)peak + (double)peak) / d);
    if (step < 1.0f) step = 1.0f;
    b->step = fround(step);
    b->rgb[0] = b->rgb[1] = b->rgb[2] = b->rgb[3] = 0xff;
    b->peak = fround(peak);
    b->pixel = NULL;
    b->level = -1;
}

/* A lit bolt: its level up by its step (turning down at the peak); while above 0 its pixel brightened
   by the level times the ticks, each channel at most the cell's. 0x45e410 */
static void bolt_tick(SkyBolt *b, int ticks)
{
    if (b->level < 0) return;
    if (b->peak <= b->level && 0 < b->step) b->step = -b->step;
    b->level += b->step;
    if (b->level < 0 || !b->pixel) return;
    uint8_t *p = b->pixel;
    int v = b->level * ticks;
    int r = p[2] + v, g = p[1] + v, bl = p[0] + v;
    if (b->rgb[1] < r) r = b->rgb[1];
    if (b->rgb[2] < g) g = b->rgb[2];
    if (b->rgb[3] < bl) bl = b->rgb[3];
    p[0] = (uint8_t)bl;
    p[1] = (uint8_t)g;
    p[2] = (uint8_t)r;
}

/* The storm's frame (0x45e290): the strikes left counted down; idle bolts lit near the flash (offsets
   from the level's row of 0x5cf144), at most the per-tick rate times the ticks this frame; it ends when
   every bolt is done. */
static bool storm_tick(SkyStorm *st, int ticks)
{
    if (!st->active) return false;
    st->count--;
    int done = 0, lit = 0;
    for (int i = 0; i < st->nbolts; i++) {
        SkyBolt *b = &st->bolt[i];
        if (!Sky_LightningActive()) {
            b->level = 0;
            done++;
        } else if (b->level < 0) {
            if (st->rate * ticks <= lit || st->count < 1) {
                done++;
                continue;
            }
            lit++;
            int x, y, dx, dy;
            uint32_t row = 0x5cf144 + (uint32_t)st->level * 0x80;
            /* The retry test of the original: only a negative x is ever retried. */
            do {
                dx = (int32_t)exe_u32(row + (REAL_rand() & 0x1f) * 4);
                uint32_t r2 = REAL_rand();
                x = st->cx + dx;
                dy = (int32_t)exe_u32(row + (r2 & 0x1f) * 4);
                y = st->cy + dy;
            } while (x < 0 || (st->size <= x && y < 0 && st->size <= y));
            const int32_t *cell = Sky_ColourCell(dx, dy);
            memcpy(b->rgb, cell, sizeof b->rgb);
            /* (port: a pixel off the map, which the original would write anyway, isn't touched) */
            b->pixel = x < st->size && 0 <= y && y < st->size ? st->map + (st->size * y + x) * 4 : NULL;
            b->level = 0;
            if (b->step < 0) b->step = -b->step;
        } else {
            bolt_tick(b, ticks);
        }
    }
    if (st->nbolts <= done) st->active = 0;
    return st->active != 0;
}

/* The map's colours: the two regions' maps mixed by the blend (16.16), the alpha from the alpha sources
   (the first's alone, or both mixed) when there are any; nothing when both regions have one map.
   0x45e020 */
static void SkyMap_mix(SkyMap *m, float blend)
{
    if (m->alpha[0]) m->alpha[0]->vt->update(m->alpha[0], blend);
    if (m->alpha[1]) m->alpha[1]->vt->update(m->alpha[1], blend);
    uint32_t ia = (uint32_t)fround((1.0f - blend) * 65536.0f), ib = (uint32_t)fround(blend * 65536.0f);
    if (m->src[0] == m->src[1]) return;
    const uint8_t *a = m->src[0]->pixels, *b = m->src[1]->pixels;
    if (a == b) return;
    uint8_t *o = m->out, *end = m->out + m->size * m->size * 4;
    const int32_t *aa = m->alpha[0] ? (const int32_t *)(const void *)m->alpha[0]->pixels : NULL;
    const int32_t *ab = m->alpha[1] ? (const int32_t *)(const void *)m->alpha[1]->pixels : NULL;
    for (; o < end; o += 4, a += 4, b += 4) {
        if (!aa) o[3] = (uint8_t)((b[3] * ib + a[3] * ia) >> 16);
        else if (!ab) o[3] = (uint8_t)*aa++;
        else o[3] = (uint8_t)(((uint32_t)*ab++ * ib + (uint32_t)*aa++ * ia) >> 16);
        o[2] = (uint8_t)((b[2] * ib + a[2] * ia) >> 16);
        o[1] = (uint8_t)((b[1] * ib + a[1] * ia) >> 16);
        o[0] = (uint8_t)((a[0] * ia + b[0] * ib) >> 16);
    }
}

/* 0x45dc50 */
static SkyMap *SkyMap_ctor(SkyMap *m, SkyMapSource *a, SkyMapSource *b, SkyMapSource *alpha_a, SkyMapSource *alpha_b,
                           int size)
{
    m->src[0] = a;
    m->src[1] = b;
    m->alpha[0] = alpha_a;
    m->alpha[1] = alpha_b;
    m->size = size;
    m->count = size * size;
    m->out = calloc((size_t)m->count, 4);
    m->saved = calloc((size_t)m->count, 4);
    if (m->out && a && a->pixels) memcpy(m->out, a->pixels, (size_t)m->count * 4);
    if (m->out && a && b) SkyMap_mix(m, 0);
    SkyStorm *st = calloc(1, sizeof *st);
    if (st) {
        st->map = m->out;
        st->size = m->size;
        st->nbolts = 0x50;
        st->spread = 0.3f;   /* 0x3e99999a */
        st->strength = 0xff;
        st->rate = 5;
        for (int i = 0; i < 200; i++) bolt_reset(&st->bolt[i], st);
    }
    m->storm = st;
    return m;
}

static void SkyMap_dtor(SkyMap *m)   /* 0x45dde0 */
{
    free(m->storm);
    free(m->saved);
    free(m->out);
}

/* Each frame (0x45de10): during a storm the map restored and its bolts lit; else mixed, unless the
   lightning has just struck: then the map kept, the storm set from the flash (0x45cdb0) and started. */
static void SkyMap_update(SkyMap *m, float blend, int ticks)
{
    SkyStorm *st = m->storm;
    if (!st || !m->out) return;
    size_t bytes = (size_t)m->size * (size_t)m->size * 4;
    if (st->active) {
        memcpy(m->out, m->saved, bytes);
        storm_tick(st, ticks);
        return;
    }
    if (!Sky_LightningActive()) {
        if (m->src[0] && m->src[1]) SkyMap_mix(m, blend);
        return;
    }
    float pos[2];
    int32_t rec[5];
    int level;
    Sky_LightningFlash(pos, rec, &level);
    memcpy(m->saved, m->out, bytes);
    st->cx = (int)pos[0];
    st->cy = (int)pos[1];
    st->count = rec[0];
    st->nbolts = rec[1];
    memcpy(&st->spread, &rec[2], 4);
    st->strength = rec[3];
    st->level = level;
    st->rate = rec[4];
    for (int i = 0; i < 200; i++) bolt_reset(&st->bolt[i], st);
    st->active = 1;
}

/* Whether the map is opaque (0x45dfd0): one map, its alpha; else no alpha source and the first map
   opaque (the original tests the first map for the second as well). */
static bool SkyMap_opaque(const SkyMap *m)
{
    if (m->src[0] == m->src[1]) return m->src[0] && src_opaque(m->src[0]);
    if (m->alpha[0]) return false;
    if (m->src[0] && !src_opaque(m->src[0])) return false;
    if (m->src[1] && !src_opaque(m->src[0])) return false;
    return true;
}

/* ---- Scroller_BasicSky (0x170 bytes) ---- */

/* The texture coordinates (0x45eb00): per stage, the column and row times the scale, plus the warp at
   the vertex, the scroll and the offset. */
static void scroller_uvs(Scroller *s)
{
    for (int k = 0; k < s->nstages; k++) {
        float (*uv)[2] = s->uv[k];
        float sx = s->scale[k][4], sy = s->scale[k][5];
        for (int j = 0; j < s->n1; j++)
            for (int i = 0; i < s->n1; i++) {
                int v = s->n1 * j + i;
                uv[v][0] = (float)i * sx + s->warp[v][0] + s->scroll[k][0] + s->offset[k][0];
                uv[v][1] = (float)j * sy + s->warp[v][1] + s->scroll[k][1] + s->offset[k][1];
            }
    }
}

/* The scroll by the wind and the offset by the eye (0x45ec00). */
static void scroller_move(Scroller *s, const float pos[3], int ticks)
{
    for (int k = 0; k < s->nstages; k++) {
        if (s->moving[k]) {
            for (int c = 0; c < 2; c++) {
                float *v = &s->scroll[k][c];
                *v = (float)ticks * s->wind[k][4 + c] + *v;
                if (1.0f < *v) *v -= 1.0f;
                if (*v < 0.0f) *v += 1.0f;
            }
        }
        if (s->follow) {
            float f = s->scale[k][4] * s->ratio[2] * pos[0];
            s->offset[k][0] = -(f - floorf(f));
            float g = s->scale[k][5] * pos[2] * s->ratio[2];
            s->offset[k][1] = g - floorf(g);
        }
    }
}

static void next_rnd(Scroller *s)
{
    if (++s->rndi == 12) s->rndi = 0;
}
static float rnd_amp(Scroller *s) { float v = s->rnd[s->rndi] * s->warpamount[2]; return (v + v) - s->warpamount[2]; }

/* The warp (0x45ed40): new random numbers each frame; every 32 ticks new targets within the amount;
   every 4 ticks each vertex's velocity steered towards its target (within the speed); then the
   vertices moved by their velocities. */
static void scroller_warp(Scroller *s, int ticks)
{
    if (!s->warping) return;
    for (int i = 0; i < 13; i++) s->rnd[i] = REAL_randf();
    float t = (float)ticks;
    s->retarget += ticks;
    s->steer += ticks;
    float acc = (float)(ticks * 4) * s->warpspeed[2] * t * 0.033333335f;   /* 0x5b3f30 */
    if (0x1f < s->retarget) {
        s->retarget -= 0x20;
        for (int i = 0; i < s->ntarget; i++) {
            next_rnd(s);
            s->target[i][0] = rnd_amp(s);
            next_rnd(s);
            s->target[i][1] = rnd_amp(s);
        }
    }
    if (3 < s->steer) {
        s->steer -= 4;
        for (int i = 0; i < s->nvel; i++)
            for (int c = 0; c < 2; c++) {
                float *v = &s->vel[i][c];
                if (s->warp[i][c] <= s->target[i][c]) {
                    *v = acc + *v;
                    if (s->warpspeed[2] < *v) *v = s->warpspeed[2];
                } else {
                    *v = *v - acc;
                    if (*v < -s->warpspeed[2]) *v = -s->warpspeed[2];
                }
            }
    }
    for (int i = 0, k = 0; i < s->nwarp; i++) {
        s->warp[i][0] = t * s->vel[k][0] + s->warp[i][0];
        s->warp[i][1] = t * s->vel[k][1] + s->warp[i][1];
        if (++k == s->nvel) k = 0;
    }
}

/* The warp's start (0x45f280): random positions within the amount, no velocities, one step. */
static void scroller_warp_init(Scroller *s)
{
    for (int i = 0; i < 13; i++) s->rnd[i] = REAL_randf();
    for (int i = 0; i < s->nwarp; i++) {
        next_rnd(s);
        s->warp[i][0] = rnd_amp(s);
        next_rnd(s);
        s->warp[i][1] = rnd_amp(s);
    }
    memset(s->vel, 0, (size_t)s->nvel * sizeof *s->vel);
    scroller_warp(s, 1);
}

/* The two regions' parameters (0x45f070), 15 floats each: the move ratio, the warp's speed and amount,
   then per stage the X and Y scales, the wind's speed and direction (degrees). */
static void scroller_params(Scroller *s, const float *rec)
{
    for (int r = 0; r < 2; r++, rec += 15) {
        for (int k = 0; k < s->nstages; k++) {
            const float *p = rec + 3 + 4 * k;
            s->scale[k][2 * r] = 1.0f / p[0];
            s->scale[k][2 * r + 1] = 1.0f / p[1];
            float dir = p[3] * -0.0027777778f;   /* 0x5b3f3c */
            float speed = p[2];
            s->wind[k][2 * r] = (float)(sin_turns(dir) * speed) * 0.00025f;   /* 0x5b3f38 */
            s->wind[k][2 * r + 1] = (float)(cos_turns(dir) * speed * (double)0.00025f);
            if (0.25f < speed) s->moving[k] = 1;   /* 0x5b2468 */
        }
        s->ratio[r] = rec[0] == 0.0f ? 0.0f : 1.0f / rec[0];
        s->warpamount[r] = rec[2];
        s->warpspeed[r] = rec[1] * 0.0055555557f;   /* 0x5b3f34 */
    }
    rec -= 30;
    if (1.0f < rec[0] || 1.0f < rec[15]) s->follow = 1;
    if (exe_f64(0x5b2bc0) < (double)rec[1] || exe_f64(0x5b2bc0) < (double)rec[16]) s->warping = 1;
    s->warpspeed[2] = s->warpspeed[0];
    s->warpamount[2] = s->warpamount[0];
    for (int k = 0; k < s->nstages; k++) {
        s->wind[k][4] = s->wind[k][0];
        s->wind[k][5] = s->wind[k][1];
        s->scale[k][4] = s->scale[k][0];
        s->scale[k][5] = s->scale[k][1];
    }
    s->ratio[2] = s->ratio[0];
}

static void lerp2(float *v, float t)   /* 0x45f040: [4], [5] from [0], [1] to [2], [3] */
{
    v[4] = (v[2] - v[0]) * t + v[0];
    v[5] = (v[3] - v[1]) * t + v[1];
}

/* Scroller_BasicSky_v0 0x45ef60: the region blend of the warp, the wind, the scales and the ratio, then
   the moves; the coordinates again when anything moves. */
void Scroller_update(Scroller *s, const float pos[3], float blend, int ticks)
{
    s->warpspeed[2] = (s->warpspeed[1] - s->warpspeed[0]) * blend + s->warpspeed[0];
    s->warpamount[2] = (s->warpamount[1] - s->warpamount[0]) * blend + s->warpamount[0];
    for (int k = 0; k < s->nstages; k++) {
        lerp2(s->wind[k], blend);
        lerp2(s->scale[k], blend);
    }
    s->ratio[2] = (s->ratio[1] - s->ratio[0]) * blend + s->ratio[0];   /* 0x45f020 */
    scroller_move(s, pos, ticks);
    scroller_warp(s, ticks);
    if (s->moving[0] || s->moving[1] || s->moving[2] || s->follow || s->warping) scroller_uvs(s);
}

/* 0x45e7a0 */
Scroller *Scroller_ctor(Scroller *s, int n, int block, int nstages, const float *rec)
{
    memset(s, 0, sizeof *s);
    s->nstages = nstages;
    s->n = n;
    s->block = block;
    s->n1 = n + 1;
    s->retarget = 0x20;
    s->steer = 4;
    s->rndi = 0;
    for (int i = 0; i < 13; i++) s->rnd[i] = REAL_randf();
    scroller_params(s, rec);
    s->nverts = s->n1 * s->n1;
    float (*uv)[2] = calloc((size_t)(s->nverts * nstages), sizeof *uv);
    for (int k = 0; k < nstages; k++) s->uv[k] = uv ? uv + k * s->nverts : NULL;
    s->nwarp = s->ntarget = s->nvel = s->nverts;
    s->warp = calloc((size_t)s->nwarp, sizeof *s->warp);
    s->target = calloc((size_t)s->ntarget, sizeof *s->target);
    s->vel = calloc((size_t)s->nvel, sizeof *s->vel);
    s->nidx = n * n * 4;
    s->quads = calloc((size_t)s->nidx, 1);
    if (!uv || !s->warp || !s->target || !s->vel || !s->quads) return s;
    scroller_warp_init(s);
    scroller_uvs(s);
    if (n / block > 0) build_quads(s->quads, n, block);
    return s;
}

void Scroller_dtor(Scroller *s)   /* Scroller_BasicSky_v1 0x45eaa0 */
{
    free(s->quads);
    free(s->vel);
    free(s->target);
    free(s->warp);
    free(s->uv[0]);
}

/* ---- SkyLevel_BasicSky (0x5c bytes) ---- */

/* The view's two side planes in the ground plane (0x45a9b0): the half field of view (turns, at least
   0x5b3e80) plus a quarter turn either side of the camera's z row. */
static void basic_planes(SkyBasic *b)
{
    RCamera *c = g_render->camera;
    float a = c->halffov * 0.0027777778f;   /* 0x5b3e84 */
    if (a < exe_f32(0x5b3e80)) a = exe_f32(0x5b3e80);
    a += 0.25f;
    double s = sin_turns(a), co = cos_turns(a);
    float fx = c->frame.orient[6], fz = c->frame.orient[8];
    float x1 = (float)(fx * co), z1 = fz * (float)s;
    b->plane[0] = z1 + x1;
    b->plane[1] = (float)(co * fz - fx * s);
    b->plane[2] = x1 - z1;
    b->plane[3] = (float)(fx * s + co * fz);
}

/* Whether a block is in view (0x45aa70): any of its corners or its sides' midpoints behind both planes. */
static bool basic_visible(const SkyBasic *b, const float c[4][4])
{
    float pts[8][2] = {
        {c[0][0], c[0][2]}, {c[1][0], c[1][2]}, {c[2][0], c[2][2]}, {c[3][0], c[3][2]},
        {(c[1][0] + c[0][0]) * 0.5f, (c[1][2] + c[0][2]) * 0.5f}, {(c[3][0] + c[1][0]) * 0.5f, (c[3][2] + c[1][2]) * 0.5f},
        {(c[3][0] + c[2][0]) * 0.5f, (c[3][2] + c[2][2]) * 0.5f}, {(c[2][0] + c[0][0]) * 0.5f, (c[2][2] + c[0][2]) * 0.5f},
    };
    for (int i = 0; i < 8; i++)
        if (pts[i][0] * b->plane[0] + pts[i][1] * b->plane[1] < 0 && pts[i][0] * b->plane[2] + pts[i][1] * b->plane[3] < 0)
            return true;
    return false;
}

/* SkyLevel_BasicSky_v0 0x45b200: the map, then the scroller at region A's position (B's for water). */
static void basic_update(SkyLevel *l, const float a[3], const float b[3], float blend, int ticks)
{
    SkyBasic *s = (SkyBasic *)l;
    if (s->map) SkyMap_update(s->map, blend, ticks);
    if (s->scroller) Scroller_update(s->scroller, s->water ? b : a, blend, ticks);
}

/* SkyLevel_BasicSky_v1 0x45b240: the nine 5 x 5 blocks, 100 indices each; looking up (the camera's z row
   y at least 0.5) every block, else those in view, the middle one also above 0.4. */
static void basic_draw(SkyLevel *l, float heading)
{
    (void)heading;
    SkyBasic *s = (SkyBasic *)l;
    if (!s->mesh || !s->grid || !s->scroller || !s->quads) return;
    float up = g_render->camera->frame.orient[7];
    if (up < 0.5f) basic_planes(s);
    for (int k = 0; k <= 8; k++) {
        const uint8_t *q = s->scroller->quads + k * 100;
        float c[4][4];
        memcpy(c[0], s->grid->verts[q[0]], 16);
        memcpy(c[1], s->grid->verts[q[0x11]], 16);
        memcpy(c[2], s->grid->verts[q[0x53]], 16);
        memcpy(c[3], s->grid->verts[q[0x62]], 16);
        if (!(0.5f <= up || (k == 4 && exe_f32(0x5b261c) < up)) && !basic_visible(s, (const float(*)[4])c)) continue;
        for (int st = 0; st < s->mesh->nstages; st++) {
            RMesh_setIndexUVs(s->mesh, st, s->quads->idx + k * 100, 100);           /* 0x468730 */
            RMesh_setIndexColours(s->mesh, st, s->scroller->quads + k * 100, 100);  /* 0x468770 */
        }
        RMesh_setIndexPositions(s->mesh, s->scroller->quads + k * 100, 100);        /* 0x468700 */
        RMesh_draw(s->mesh, 100);
    }
}

static void basic_dtor(SkyLevel *l, int free_self)   /* SkyLevel_BasicSky_v2 0x45b130 -> 0x45b150 */
{
    SkyBasic *s = (SkyBasic *)l;
    if (s->scroller) {
        Scroller_dtor(s->scroller);
        free(s->scroller);
    }
    if (s->mesh) {
        RMesh_dtor(s->mesh);
        free(s->mesh);
    }
    free(s->blend);
    if (s->quads) {
        free(s->quads->idx);
        free(s->quads);
    }
    if (s->grid) {
        SkyGrid_dtor(s->grid);
        free(s->grid);
    }
    if (s->map) {
        SkyMap_dtor(s->map);
        free(s->map);
    }
    for (int i = 0; i < 4; i++)
        if (s->source[i]) s->source[i]->vt->dtor(s->source[i], 1);
    if (free_self) free(s);
}

/* SkyLevel_BasicSky_v3 0x45b3e0: the grid again from the configuration (the water's at +0xb3c ..). */
static void basic_rebuild(SkyLevel *l)
{
    SkyBasic *s = (SkyBasic *)l;
    const SkyConfig *c = s->config;
    if (!s->grid) return;
    if (s->water) SkyGrid_build(s->grid, cfg_f(c, 0xb40), cfg_f(c, 0xb44), cfg_f(c, 0xb48), cfg_f(c, 0xb3c), 1.0f);
    else SkyGrid_build(s->grid, cfg_f(c, 0x40), cfg_f(c, 0x44), cfg_f(c, 0x48), cfg_f(c, 0x3c), cfg_f(c, 0xce4));
}

static const SkyLevelVtbl basic_vt = {basic_update, basic_draw, basic_dtor, basic_rebuild};   /* 0x5b3e8c */

/* 0x45acb0: from the record's tokens: whether it blocks the sun ("no": drawn before the sun, flag 0x400),
   then for regions A and B the sky map, its alpha ("noise<speed>" or "none"), the cloud texture ("flat":
   none), the X and Y scales, the move ratio, the wind's speed and direction, the warp's speed and amount.
   Its mesh: the 15 x 15 grid, the scroller's coordinates, the map's colours. */
SkyBasic *SkyBasic_ctor(SkyBasic *s, SkyRecord *rec, SkyTextures *tex, SkyConfig *config, bool water)
{
    memset(s, 0, sizeof *s);
    s->base.vt = &basic_vt;
    s->base.flags = rec->flags;
    s->config = config;
    s->water = water;
    rec->cur = rec->start;
    if (*rec->cur == '#') SkyRecord_next(rec);
    const char *sun = rec->cur;
    /* Per region: [0] map, [1] alpha, [2] texture, then the floats in file order. */
    const char *tok[2][10];
    for (int r = 0; r < 2; r++)
        for (int i = 0; i < 10; i++) {
            SkyRecord_next(rec);
            tok[r][i] = rec->cur;
        }
    if (!strncmp(sun, exe_str(0x5cefc0), 2)) s->base.flags |= 0x400;
    for (int r = 0; r < 2; r++) {
        int slot = SkyTextures_find(tex, tok[r][0]);
        const uint8_t *shape = slot >= 0 ? SkyTextures_shape(tex, slot) : NULL;
        s->source[r] = shape ? src_texture_new(0x10, shape) : NULL;
    }
    for (int r = 0; r < 2; r++) {
        const char *a = tok[r][1];
        if (!strncmp(a, exe_str(0x5cefb8), 5)) s->source[2 + r] = src_noise_new(0x10, 0, a);
        else s->source[2 + r] = NULL;   /* "none" (anything else leaves the original's slot unset) */
    }
    s->map = malloc(sizeof *s->map);
    if (s->map) SkyMap_ctor(s->map, s->source[0], s->source[1], s->source[2], s->source[3], 0x10);
    s->grid = malloc(sizeof *s->grid);
    if (s->grid) {
        if (!water)
            SkyGrid_ctor(s->grid, 0xf, cfg_f(config, 0x40), cfg_f(config, 0x44), cfg_f(config, 0x48), cfg_f(config, 0x3c),
                         cfg_f(config, 0xce4));
        else
            SkyGrid_ctor(s->grid, 0xf, cfg_f(config, 0xb40), cfg_f(config, 0xb44), cfg_f(config, 0xb48),
                         cfg_f(config, 0xb3c), 1.0f);
    }
    s->quads = malloc(sizeof *s->quads);
    if (s->quads) SkyQuads_ctor(s->quads, 0xf, 5, 4);
    /* The scroller's parameters: the move ratio, the warp speed and amount, the X and Y scales, the
       wind's speed and direction. */
    float p[30] = {0};
    for (int r = 0; r < 2; r++) {
        float *q = p + 15 * r;
        q[0] = (float)atof(tok[r][5]);
        q[1] = (float)atof(tok[r][8]);
        q[2] = (float)atof(tok[r][9]);
        q[3] = (float)atof(tok[r][3]);
        q[4] = (float)atof(tok[r][4]);
        q[5] = (float)atof(tok[r][6]);
        q[6] = (float)atof(tok[r][7]);
    }
    s->scroller = malloc(sizeof *s->scroller);
    if (s->scroller) Scroller_ctor(s->scroller, 0xf, 5, 1, p);
    s->blend = malloc(sizeof *s->blend);
    if (s->blend) SkyBlend_ctor(s->blend);
    s->mesh = malloc(sizeof *s->mesh);
    if (s->mesh && s->grid && s->grid->verts && s->scroller && s->scroller->quads)
        RMesh_ctorIndexed(s->mesh, 0x16, 4, 1, s->grid->verts, s->scroller->quads, 0x100, 100);
    else {
        free(s->mesh);
        s->mesh = NULL;
    }
    bool opaque_tex = true;
    if (!strncmp(tok[0][2], exe_str(0x5cefa8), 4)) {
        s->tex = tex->blank;
        if (s->blend) s->blend->v[1] = 0;   /* 0x5377b0 */
    } else {
        int slot = SkyTextures_find(tex, tok[0][2]);
        opaque_tex = slot >= 0 && SkyTextures_opaque(tex, slot);
        s->tex = slot >= 0 ? SkyTextures_texture(tex, slot) : NULL;
    }
    if (s->mesh && s->quads && s->quads->idx && s->map && s->map->out)
        RMesh_stageIndexed(s->mesh, 0, s->tex, s->blend, s->scroller->uv[0], 100, s->quads->idx, s->map->out, 100,
                           s->scroller->quads, 0);
    if (s->map && SkyMap_opaque(s->map) && opaque_tex && s->blend) s->blend->v[1] = 0;
    return s;
}
