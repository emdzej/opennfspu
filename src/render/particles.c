/* The particle systems: see particles.h. */
#include "render/particles.h"
#include "app.h"
#include "eagl/eagl.h"
#include "exe.h"
#include "real/texture.h"
#include "render/render.h"
#include "render/rtex.h"
#include "render/sprite.h"
#include "render/thrash.h"
#include "vfs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_render_particles;   /* 0x6197e0: the special effects' share of the particles (0, 100, 200, 256) */

static float f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

/* ---- the render's random numbers ---- */

static uint32_t s_seed = 0xf874af01u, s_mul = 1;   /* (the file's values until the race seeds them) */

void Render_SeedRand(uint32_t seed)   /* 0x44fbd0 (its result, a product of the steps, unused) */
{
    s_seed = 0x3ade68b1u;
    s_mul = 0x75bcd15u;
    for (uint32_t n = seed % 500; n; n--) s_seed = s_seed * (uint32_t)-0x32eb & 0xffff;
}

uint32_t Render_Rand(void)
{
    uint32_t u = s_mul * s_seed;
    s_seed = u & 0xffff;
    return (uint32_t)((int32_t)u >> 8) & 0xffff;
}

/* A spread: a random share of [0, 2) (2^-15 a unit, 0x5b3e10) of r, less r. */
static float spread(float r) { return (float)Render_Rand() * r * f32(0x5b3e10) - r; }

/* ---- the parameter sets (a map by tag: sorted, the first kept on a repeat) ---- */

typedef struct { uint32_t tag; PartParams p; } PartSet;
static PartSet *s_sets;
static int s_nsets;

static void set_insert(uint32_t tag, const PartParams *p)   /* 0x455850 */
{
    int i = 0;
    while (i < s_nsets && s_sets[i].tag < tag) i++;
    if (i < s_nsets && s_sets[i].tag == tag) return;
    PartSet *n = realloc(s_sets, (size_t)(s_nsets + 1) * sizeof *n);
    if (!n) return;
    s_sets = n;
    memmove(s_sets + i + 1, s_sets + i, (size_t)(s_nsets - i) * sizeof *s_sets);
    s_sets[i].tag = tag;
    s_sets[i].p = *p;
    s_nsets++;
}

/* By its tag, else the first (map end: 0x50db20, its begin). */
static const PartParams *set_find(uint32_t tag)
{
    for (int i = 0; i < s_nsets; i++)
        if (s_sets[i].tag == tag) return &s_sets[i].p;
    return s_nsets ? &s_sets[0].p : NULL;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* A .dat: a count, then per set its tag and its 0x7c bytes (a short file ends where it ends). */
static bool load_dat(const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "%s%s", "GameData\\Render\\", name);   /* 0x65b30c */
    size_t size;
    uint8_t *d = vfs_read_all(path, &size);
    if (!d) return false;
    uint32_t n = size >= 4 ? rd32(d) : 0;
    for (uint32_t i = 0; i < n && 4 + (size_t)(i + 1) * 0x80 <= size; i++) {
        PartParams p;
        memcpy(&p, d + 4 + (size_t)i * 0x80 + 4, sizeof p);
        set_insert(rd32(d + 4 + (size_t)i * 0x80), &p);
    }
    free(d);
    return true;
}

/* Particles_Load 0x455440: particle.dat (without it the exe's one set 0x5cecc8, tag 0), carpart.dat. */
static void load_sets(void)
{
    free(s_sets);
    s_sets = NULL;
    s_nsets = 0;
    if (!load_dat(exe_str(0x5cedd0) + 2)) {   /* "%sparticle.dat" */
        PartParams p;
        memcpy(&p, exe_data(0x5cecc8, sizeof p), sizeof p);
        set_insert(0, &p);
    }
    load_dat(exe_str(0x5cedbc) + 2);           /* "%scarpart.dat" */
}

/* ---- a system ---- */


static Particle *s_free;   /* the particles' pool (0x5e4fe8) */

static Particle *particle_new(void)
{
    Particle *q = s_free;
    if (q) s_free = q->next;
    else q = malloc(sizeof *q);
    return q;
}

static void particle_free(Particle *q)
{
    q->next = s_free;
    s_free = q;
}

static void cross(const float *a, const float *b, float *out)   /* 0x5325f0 */
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static float rough_sqrt(float v)
{
    uint32_t u;
    memcpy(&u, &v, 4);
    u = (uint32_t)((int32_t)u >> 1) + 0x1fc00000u;
    memcpy(&v, &u, 4);
    return v;
}

/* A new particle (0x4547b0): at the origin, spread in x and z; moving along the second axis by the
   set's speed and spread, and along the first and the third by their spreads; its fall and turn. */
static void emit(Particle *q, const float *o, const PartParams *p, const float *a1, const float *a2,
                 const float *a3)
{
    q->pos[0] = o[0];
    q->pos[1] = o[1];
    q->pos[2] = o[2];
    q->pos[0] = spread(p->emit[0]) + q->pos[0];
    float z = spread(p->emit[2]);
    q->pos[3] = 1.0f;
    q->pos[2] = z + q->pos[2];
    float s = (p->emit[1] - p->rand[1]) + (float)Render_Rand() * p->rand[1] * f32(0x5b3e10);
    for (int k = 0; k < 3; k++) q->vel[k] = a2[k] * s;   /* 0x532000 */
    s = spread(p->rand[0]);
    for (int k = 0; k < 3; k++) q->vel[k] += a1[k] * s;   /* 0x532070 */
    s = spread(p->rand[2]);
    for (int k = 0; k < 3; k++) q->vel[k] += a3[k] * s;
    q->age = 0;
    q->fall = ((p->weight - p->randweight) + (float)Render_Rand() * p->randweight * f32(0x5b3e10)) * f32(0x5b2738);
    q->spin = spread(p->randrot);
}

/* A step (0x454b80): in its emitting part of the period, the particles due by the rate (all within the
   set's share of the view distance, fewer further on) up to the most; past the period the next starts
   (a one-shot set stops there); then each particle moves, bounces at half its speed off the ground
   height, falls, slows, ages, and goes after its lifetime. */
void PartSystem_Step(PartSystem *s)
{
    if (!s->list && !s->active) return;
    const PartParams *p = s->p;
    const float *o = (s->flags & 1) ? s->emitter->pos : s->origin->pos;
    bool skip = false;
    if (!s->active || (p->periodoff != 0 && p->periodon < s->ticks)) {
        if (p->periodon + p->periodoff < s->ticks) {
            s->ticks = s->ticks - p->periodon - p->periodoff;
            if (p->flags & 2) s->active = false;
            skip = true;
        }
    } else {
        float vd = g_render_viewdist;   /* 0x6197cc */
        float f = 1.0f, r = vd * p->volscale;
        if (r * r < s->dist2) f = (vd - rough_sqrt(s->dist2)) / ((1.0f - p->volscale) * vd);
        s->due = f * s->k44 * s->rate + s->due;
        if (s->emitted + 1.0f < s->due) {
            do {
                if (s->alive < s->max) {
                    s->alive++;
                    Particle *q = particle_new();
                    if (q) {
                        q->next = s->list;
                        s->list = q;
                        emit(q, o, p, s->emitter->axis1, s->emitter->axis2, s->side);
                    }
                }
                s->emitted += 1.0f;
            } while (s->emitted + 1.0f < s->due);
        }
    }
    if (!skip) s->ticks++;
    Particle *prev = NULL;
    for (Particle *q = s->list; q;) {
        q->pos[0] += q->vel[0];
        float y = q->pos[1];
        q->pos[1] = q->vel[1] + y;
        q->pos[2] += q->vel[2];
        if ((p->flags & 1) && q->vel[1] + y < s->emitter->pos[3]) {
            q->pos[1] = s->emitter->pos[3];
            q->vel[1] *= -0.5f;   /* 0x5b3e18 */
        }
        q->vel[1] -= q->fall;
        if (p->friction < 1.0f) q->vel[0] *= p->friction;
        q->vel[2] = p->friction * q->vel[2];
        q->age += 1.0f;
        if (q->age <= (float)p->lifetime) {
            prev = q;
            q = q->next;
        } else {
            s->alive--;
            Particle *n = q->next;
            if (prev) prev->next = n;
            else s->list = n;
            particle_free(q);
            q = n;
        }
    }
}

/* The rate: the count over the lifetime, or over the emitting steps of the lifetime's periods. */
static float rate_of(const PartParams *p)
{
    int32_t life = (int32_t)p->lifetime, on = (int32_t)p->periodon;
    if (p->periodoff == 0 || life < on) return (float)(int32_t)p->count / (float)life;
    int32_t per = on + (int32_t)p->periodoff;
    if (per < life) return (float)(int32_t)p->count / (((float)life / (float)per) * (float)on);
    return (float)(int32_t)p->count / (float)on;
}

static Texture *texture_of(const PartParams *p)
{
    uint32_t m = p->matid;
    uint32_t name = ((m & 0xff0000u) | m >> 16) >> 8 | (m << 16 | (m & 0xff00u)) << 8;
    return g_rtex_common[1] ? RTexBank_find(g_rtex_common[1], 0, name) : NULL;   /* particle.fsh */
}

/* The constructor (0x454120): the set by the emitter's tag, its texture with its first state cleared,
   the most particles by the special effects, the rate (the count over the lifetime, or over the emitting
   steps of the lifetime's periods); primed (the third axis and the set's prime steps) unless movable. */
PartSystem *PartSystem_New(float d2, const PartEmitter *e, bool movable)
{
    PartSystem *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->life = 100;
    s->dist2 = d2;
    s->p = set_find(e->tag);
    s->emitter = e;
    s->active = true;
    if (!s->p) {
        free(s);
        return NULL;
    }
    const PartParams *p = s->p;
    s->tex = texture_of(p);
    s->k44 = s->k48 = 1.0f;
    s->max = (int)((p->count * (uint32_t)g_render_particles) >> 8);
    s->rate = rate_of(p);
    s->movable = movable;
    if (!movable) {
        s->origin = e;
        cross(e->axis1, e->axis2, s->side);
        for (uint32_t i = 0; i < p->prime; i++) PartSystem_Step(s);
    }
    if (p->startsize == 0 && p->endsize == 0) s->flags |= 2;
    if (p->flags & 2) s->active = false;
    if (s->tex) {
        TextureStates st = s->tex->states;
        TextureStates_set(&st, 0, 0);   /* 0x5377a0 */
        Texture_setstates(s->tex, &st);
    }
    if (movable) {   /* RGenericMovableParticleCreator 0x454f80: its own copy, then primed */
        s->own = *e;
        s->emitter = s->origin = &s->own;
        cross(s->own.axis1, s->own.axis2, s->side);
        for (uint32_t i = 0; i < p->prime; i++) PartSystem_Step(s);
    }
    return s;
}

void PartSystem_SetTag(PartSystem *s, uint32_t tag)   /* 0x454a10 */
{
    if (s->emitter->tag == tag) return;
    ((PartEmitter *)s->emitter)->tag = tag;   /* (the record's own tag changes) */
    const PartParams *p = set_find(tag);
    if (!p) return;
    s->tex = texture_of(p);
    int max = (int)((p->count * (uint32_t)g_render_particles) >> 8);
    if (p->count != s->p->count) {
        while (s->list) {
            Particle *n = s->list->next;
            particle_free(s->list);
            s->list = n;
        }
        s->alive = 0;
    }
    s->max = max;
    s->p = p;
    s->rate = rate_of(p);
}

void PartSystem_Reset(PartSystem *s)   /* 0x4542f0 */
{
    s->active = true;
    s->ticks = 0;
}

void PartSystem_Free(PartSystem *s)   /* 0x454300 */
{
    while (s->list) {
        Particle *n = s->list->next;
        particle_free(s->list);
        s->list = n;
    }
    free(s);
}

/* ---- the draw (0x454380) ---- */

static EaglMaterial s_mat;       /* the shared drawer (0x6221d8, 0x59ee20) */
static bool s_matready;
static int32_t s_rs[6];
static float s_pos[1024][4];     /* 0x61e1d8 */
static uint32_t s_col[1024];     /* 0x6221dc */
static RBillBoard s_bb[256];     /* 0x6231e8 */
static float s_uv[256 * 8];      /* 0x624de8: per quad (0, 0), (1, 0), (1, 1), (0, 1) */
static float s_streak[256][4];   /* 0x626df0 */
static float s_nouv[1024 * 2];   /* 0x6af340 */

static void drawer_init(void)
{
    EAGL_MaterialInit(&s_mat);
    EAGL_MaterialAddLayer(&s_mat, EAGL_CreateLayer(1, 7, 0));
    EAGL_MaterialAddLayer(&s_mat, EAGL_CreateLayer(6, 0, 0));
    EAGL_MaterialAddLayer(&s_mat, EAGL_CreateLayer(3, 0, 0));
    EAGL_MaterialAddLayer(&s_mat, EAGL_CreateLayer(4, 0, 0));
    EAGL_MaterialAddLayer(&s_mat, EAGL_CreateLayer(8, 0, 0));
    EAGL_MaterialAddLayer(&s_mat, EAGL_CreateLayer(0, 0, 0));
    EAGL_MaterialBind(&s_mat);
    s_rs[0] = 1, s_rs[1] = 2, s_rs[2] = s_rs[3] = s_rs[4] = 0, s_rs[5] = -1;   /* 0x537760, 0x537790(0), 0x5377d0(0) */
    for (int i = 0; i < 256; i++) {
        static const float q[8] = {0, 0, 1.0f, 0, 1.0f, 1.0f, 0, 1.0f};
        memcpy(s_uv + 8 * i, q, sizeof q);
    }
    s_matready = true;
}

static void drawer_draw(int prim, int n, float (*pos)[4], uint32_t *col, float *uv, Texture *t)   /* 0x59ef80 */
{
    EaglStream st[5] = {{(uint32_t)n * 16, pos}, {(uint32_t)n * 4, col}, {(uint32_t)n * 8, uv},
                        {0x54, t}, {0x18, s_rs}};
    EAGL_MaterialDraw(&s_mat, st, prim, n);
}

/* The streaks (0x454e10): per particle a triangle from where it is to where it was three steps back,
   the third corner 0.025 up, in its colour, untextured. */
static void draw_streaks(int n)
{
    for (int i = 0; i < n; i++) {
        s_col[3 * i] = s_col[3 * i + 1] = s_col[3 * i + 2] = s_bb[i].colour;
        memcpy(s_pos[3 * i], s_bb[i].pos, 16);
        memcpy(s_pos[3 * i + 1], s_streak[i], 16);
        memcpy(s_pos[3 * i + 2], s_bb[i].pos, 16);
        s_pos[3 * i][1] += f32(0x5b26dc);
    }
    for (int at = 0; 0 < n; n -= 0x50) {
        int k = n < 0x51 ? n : 0x50;
        drawer_draw(3, k * 3, s_pos + at, s_col + at, s_nouv, NULL);
        at += k * 3;
    }
}

/* Each particle's colour blended through the set's colours by its age (the bytes each its difference
   times the share, truncated), its place (about the emitter for the 1 flag), size and turn; the
   billboards 55 at a time in the set's blend and texture with z writes off, its depth bias by the
   distance; then the streaks. */
void PartSystem_Draw(PartSystem *s)
{
    if (!s->list) return;
    const PartParams *p = s->p;
    THRASH.fn.setstate(0x24, 1);
    int32_t bias = 0;
    if (p->zbias != 0) bias = (int32_t)lrintf((float)p->zbias * g_render->depthfactor / s->dist2);
    s_rs[4] = bias;   /* 0x5377d0 */
    float diff[3][4];
    const uint8_t *c = (const uint8_t *)p->colours;
    for (int k = 0; k + 1 < p->nstates && k < 3; k++) {
        diff[k][0] = (float)(c[4 * k + 7] - c[4 * k + 3]);
        diff[k][1] = (float)(c[4 * k + 6] - c[4 * k + 2]);
        diff[k][2] = (float)(c[4 * k + 5] - c[4 * k + 1]);
        diff[k][3] = (float)(c[4 * k + 4] - c[4 * k + 0]);
    }
    float grow = p->endsize - p->startsize;
    int n = 0;
    for (Particle *q = s->list; q && n < 256; q = q->next, n++) {
        RBillBoard *b = &s_bb[n];
        float t = q->age / (float)p->lifetime;
        uint32_t col;
        if (p->nstates == 2 || (2 < p->nstates && p->nstates < 5)) {
            int seg = 0;
            float f = t;
            if (p->nstates != 2) {
                float tt = t < 1.0f ? t : f32(0x5b3e0c);
                float x = (float)(p->nstates - 1) * tt;
                seg = (int)lrintf(x - 0.5f);
                f = x - (float)seg;
            }
            uint8_t v[4];
            memcpy(v, &p->colours[seg], 4);
            v[3] = (uint8_t)(v[3] + (int)(diff[seg][0] * f));
            v[2] = (uint8_t)(v[2] + (int)(diff[seg][1] * f));
            v[1] = (uint8_t)(v[1] + (int)(diff[seg][2] * f));
            v[0] = (uint8_t)(v[0] + (int)(diff[seg][3] * f));
            memcpy(&col, v, 4);
        } else {
            col = p->colours[0];
        }
        b->colour = col;
        memcpy(b->pos, q->pos, sizeof b->pos);
        if (s->flags & 1)
            for (int k = 0; k < 3; k++) b->pos[k] += s->origin->pos[k];
        b->size = (t * grow + p->startsize) * f32(0x5b241c);
        b->angle = t * q->spin;
        if (p->flags & 4) {
            memcpy(s_streak[n], b->pos, 16);
            for (int k = 0; k < 3; k++) s_streak[n][k] -= q->vel[k] * f32(0x5b2600);
        }
    }
    s_rs[2] = (int32_t)p->blend;   /* 0x5377c0 */
    RenderState_SetZWrite(g_render->device->rs, 0);   /* 0x5375e0(0) */
    if (!(s->flags & 2)) {
        for (int at = 0, left = n; 0 < left; left -= 0x37, at += 0x37) {
            int k = left < 0x38 ? left : 0x37;
            RBillBoard_CreateQuads(k, s_bb + at, s_pos, s_col);
            drawer_draw(4, k * 4, s_pos, s_col, s_uv + at * 8, s->tex);
        }
    }
    if (p->flags & 4) draw_streaks(n);
    RenderState_SetZWrite(g_render->device->rs, 1);
}

/* ---- the map of systems by emitter (0x627df0) ---- */

typedef struct { const void *key; PartSystem *s; } PartEntry;
static PartEntry *s_map;
static int s_nmap, s_capmap;
static bool s_ready;
static uint32_t s_last;   /* 0x628148: the timer at the last step */

void Particles_Init(void)   /* 0x455180 */
{
    Particles_Free();
    load_sets();
    if (!s_matready) drawer_init();
    s_ready = true;
    s_last = 0;
}

void Particles_Free(void)   /* 0x4552c0, 0x455300 */
{
    for (int i = 0; i < s_nmap; i++) PartSystem_Free(s_map[i].s);
    free(s_map);
    s_map = NULL;
    s_nmap = s_capmap = 0;
    free(s_sets);
    s_sets = NULL;
    s_nsets = 0;
    s_ready = false;
}

static void tick_all(void)   /* Particles_Tick 0x4550b0 */
{
    if (!g_render_particles) return;
    for (int i = 0; i < s_nmap;) {
        PartSystem *s = s_map[i].s;
        if (--s->life < 1) {
            PartSystem_Free(s);
            memmove(s_map + i, s_map + i + 1, (size_t)(s_nmap - i - 1) * sizeof *s_map);
            s_nmap--;
        } else {
            PartSystem_Step(s);
            i++;
        }
    }
}

void Particles_Frame(bool paused)
{
    if (!s_ready || paused) return;
    uint32_t now = (uint32_t)((uint64_t)app_ticks_ms() * 128u / 1000u);   /* TIMER_ticks */
    uint32_t d = now - s_last;
    if (d < 0xd) {
        if (d < 2) return;
    } else {
        s_last = now - 2;
        d = 2;
    }
    for (d >>= 1; d; d--) {
        s_last += 2;
        tick_all();
    }
}

void Particles_Draw(void)
{
    if (!s_ready || !g_render_particles) return;
    for (int i = 0; i < s_nmap; i++)
        if (s_map[i].s->life == 100) PartSystem_Draw(s_map[i].s);
}

void Particles_Ask(const PartEmitter *e, float d2)
{
    if (!s_ready || !g_render_particles) return;
    int i = 0;
    while (i < s_nmap && (uintptr_t)s_map[i].key < (uintptr_t)e) i++;
    PartSystem *s;
    if (i < s_nmap && s_map[i].key == e) {
        s = s_map[i].s;
    } else {
        s = PartSystem_New(d2, e, false);   /* RGenericParticleCreator 0x454f50 */
        if (!s) return;
        if (s_nmap == s_capmap) {
            int cap = s_capmap ? s_capmap * 2 : 64;
            PartEntry *m = realloc(s_map, (size_t)cap * sizeof *m);
            if (!m) {
                PartSystem_Free(s);
                return;
            }
            s_map = m;
            s_capmap = cap;
        }
        memmove(s_map + i + 1, s_map + i, (size_t)(s_nmap - i) * sizeof *s_map);
        s_map[i] = (PartEntry){e, s};
        s_nmap++;
    }
    s->life = 100;
    s->dist2 = d2;
}
