/* Textures and the texture manager: see texture.h. */
#include "real/texture.h"
#include "eagl/eagl.h"
#include "exe.h"
#include "real/set3d.h"
#include "real/shape.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TexGraphics *g_texgraphics;     /* 0x69ed0c */
bool TEXTURE_usedtex = true;    /* 0x5de6b8 */
bool TEXTURE_live;              /* 0x69dd1e */
bool TEXTURE_trilinear;         /* 0x69dd1d */
bool TEXTURE_jointaddress;      /* 0x69dd1c */

/* The stage-state cache (0x5de6bc): per stage seven (state id, value) pairs. */
static int32_t cache[2][TEXTURE_NSTATES][2];

/* 0x5357d0: mipmapping, raised to trilinear (3) when that is asked for. */
static void set_mipmap(uint32_t state, int32_t value)
{
    THRASH.fn.setstate(state, (uintptr_t)(TEXTURE_trilinear && value ? 3 : value));
}

/* 0x535800: an address mode; with one mode for both, state 0xd gets clamp (1) if either is clamped. */
static void set_address(uint32_t state, int32_t value)
{
    if (!TEXTURE_jointaddress) {
        THRASH.fn.setstate(state, (uintptr_t)value);
        return;
    }
    int stage = (int32_t)state >> 16;
    bool clamp = cache[stage][5][1] == 1 || cache[stage][6][1] == 1;
    THRASH.fn.setstate((state & 0xf0000) | 0xd, clamp ? 1 : (uintptr_t)value);
}

static void set_rs(uint32_t state, int32_t value) { EAGL_RSSet(state, (uintptr_t)value); }

/* The setter per state (0x5de72c, read from the exe: EAGL_RSSet 0x5401e0, 0x5357d0, 0x535800). */
typedef void (*Setter)(uint32_t, int32_t);
static Setter setters[TEXTURE_NSTATES];

static void setters_init(void)
{
    if (setters[0]) return;
    for (uint32_t i = 0; i < TEXTURE_NSTATES; i++) {
        switch (exe_u32(0x5de72c + 4 * i)) {
        case 0x5357d0: setters[i] = set_mipmap; break;
        case 0x535800: setters[i] = set_address; break;
        default: setters[i] = set_rs; break;   /* 0x5401e0 */
        }
    }
}

void TEXTURE_initstates(void)
{
    setters_init();
    /* The pairs as the exe has them, then 0x535710's assignments (the LOD bias default from 0x5de6b4). */
    for (uint32_t s = 0; s < 2; s++)
        for (uint32_t i = 0; i < TEXTURE_NSTATES; i++) {
            cache[s][i][0] = (int32_t)exe_u32(0x5de6bc + 0x38 * s + 8 * i);
            cache[s][i][1] = (int32_t)exe_u32(0x5de6c0 + 0x38 * s + 8 * i);
        }
    int32_t lod = (int32_t)exe_u32(0x5de6b4);
    static const int32_t ids[TEXTURE_NSTATES] = {5, 7, 0xc, 0xb, 8, 0x41, 0x42};
    static const int32_t values[TEXTURE_NSTATES] = {0, 1, 0, 0, 0, 0, 0};
    for (int i = 2; i < TEXTURE_NSTATES; i++) cache[0][i][1] = i == 4 ? lod : values[i];
    for (int i = 0; i < TEXTURE_NSTATES; i++) {
        cache[1][i][0] = 0x10000 | ids[i];
        cache[1][i][1] = i == 4 ? lod : values[i];
    }
    for (int i = 2; i < TEXTURE_NSTATES; i++) cache[0][i][0] = ids[i];
}

void TEXTURE_sendstates(void)
{
    setters_init();
    for (int i = 0; i < TEXTURE_NSTATES; i++) {
        setters[i]((uint32_t)cache[0][i][0], cache[0][i][1]);
        setters[i]((uint32_t)cache[1][i][0], cache[1][i][1]);
    }
}

void TEXTURE_settrilinear(bool on) { TEXTURE_trilinear = on; }

void TEXTURE_sendstage(const TextureStates *s, int stage)
{
    setters_init();
    for (int i = 0; i < TEXTURE_NSTATES; i++)
        if (s->v[i] != cache[stage][i][1]) {
            cache[stage][i][1] = s->v[i];
            setters[i]((uint32_t)cache[stage][i][0], s->v[i]);
        }
}

void TextureStates_init(TextureStates *s)
{
    memset(s, 0, sizeof *s);
    s->v[1] = 1;
}

void TextureStates_setmipmap(TextureStates *s, int on) { s->v[3] = on; }

/* ---- the graphics object ---- */

void TexGraphics_register(TexGraphics *g) { g_texgraphics = g; }

void TexGraphics_setdevice(TexGraphics *g, void *device)
{
    g->device = device;
    if (g->textures) g->textures->vt->destroy(g->textures, 1);
    TexManager *m = malloc(sizeof *m);
    g->textures = m ? TexManager_ctor(m, device) : NULL;
}

void TexGraphics_unregister(TexGraphics *g)
{
    if (g->textures) g->textures->vt->destroy(g->textures, 1);
    g_texgraphics = NULL;
}

/* ---- textures ---- */

static const TextureVtbl texture_vtbl = {Texture_dtor};

uint32_t TEXTURE_formatof(const uint8_t *rec)
{
    uint32_t f = 0;
    if (!rec) return 0;
    switch (rec[0]) {
    case 0x01: case 0x2d: case 0x40: case 0x7a: f = 1; break;
    case 0x02: case 0x23: case 0x29: case 0x41: case 0x7b: f = 2; break;
    case 0x03: case 0x42: case 0x7e: f = 3; break;
    case 0x04: case 0x43: case 0x7f: f = 5; break;
    case 0x05: case 0x20: case 0x7d: f = 6; break;
    case 0x21: case 0x2a: case 0x78: f = 4; break;
    case 0x24: return 0;
    case 0x6d: f = 7; break;
    case 0xf8: case 0xfd:
        /* Compressed records: DXT1 (0x18fb, also checked as 0xf8 falls through) or DXT3 (0x1afb). */
        if (rec[0] == 0xf8 && (rec[0x10] << 8 | rec[0x11]) == 0x18fb) f = 0xc;
        if ((rec[0x10] << 8 | rec[0x11]) == 0x1afb) f = 0xd;
        else if (f == 0) return 0;
        break;
    default: return 0;
    }
    if ((rec[0xd] << 8 | rec[0xc]) & 0x2000) f |= 0x80000000u;
    return f;
}

/* The name: the given one or "tex%d" (the manager's count), up to 19 characters. The original doesn't
   copy the terminator (the rest of the 20 bytes is whatever the new object held; the last is cleared):
   the port's objects start cleared. */
static void set_name(Texture *t, TexManager *m, const char *name)
{
    char buf[32];
    if (!name) {
        snprintf(buf, sizeof buf, "tex%d", m->count++);   /* 0x5de748 "tex%d" */
        name = buf;
    }
    for (int i = 0; i < 19 && name[i]; i++) t->name[i] = name[i];
    t->name[19] = 0;
}

static int32_t *new_refs(void)
{
    int32_t *r = malloc(sizeof *r);
    if (r) *r = 1;
    return r;
}

Texture *Texture_fromshape(Texture *t, const uint8_t *shape, TexManager *m, uint32_t flags, const char *name)
{
    memset(t, 0, sizeof *t);
    TextureStates_init(&t->states);
    t->vt = &texture_vtbl;
    t->format = TEXTURE_formatof(shape);
    const uint8_t *clut = SHAPE_clut(shape);
    t->clutformat = clut ? TEXTURE_formatof(clut) : 0;
    t->width = (int16_t)(shape[4] | shape[5] << 8);
    t->height = (int16_t)(shape[6] | shape[7] << 8);
    set_name(t, m, name);
    t->flags = flags;
    t->manager = m;
    t->dtex = DTEX_new(t->width, t->height, t->format, t->clutformat, flags, t->name);
    /* The conversion reads every mip level the flags ask for from after the pixels. A shape without them
       (a car page's window: one level) has other memory there in the original, levels the pages never
       sample (their mipmapping is off); the port reads zeros there instead of past the block. */
    uint8_t *pad = NULL;
    int want = (int)(flags & 0xff), own = shape[15] >> 4;
    if (want > own) {
        int bits = SHAPE_typevalid(shape);
        if (bits == 15) bits = 16;
        int size = t->width > t->height ? t->width : t->height;
        size_t total = 0x10, have = 0x10, b = (size_t)bits * (size_t)size * (size_t)size / 8;
        for (int l = 0; l <= want; l++, b /= 4) total += b;
        for (int l = 0, w = t->width, h = t->height; l <= own; l++, w = w > 1 ? w / 2 : 1, h = h > 1 ? h / 2 : 1)
            have += (size_t)bits * (size_t)w * (size_t)h / 8;
        pad = calloc(1, total > have ? total : have);
        if (pad) memcpy(pad, shape, have);
    }
    DTEX_load(t->dtex, (pad ? pad : shape) + 0x10, clut ? clut + 0x10 : NULL);
    free(pad);
    DtexInfo info;
    DTEX_info(t->dtex, &info);
    t->format = info.format;
    t->refs = new_refs();
    return t;
}

Texture *Texture_fromrecords(Texture *t, const uint8_t *pixels, const uint8_t *clut, TexManager *m, uint32_t flags,
                             const char *name)
{
    memset(t, 0, sizeof *t);
    TextureStates_init(&t->states);
    t->vt = &texture_vtbl;
    t->format = TEXTURE_formatof(pixels);
    t->clutformat = TEXTURE_formatof(clut);
    t->width = (int16_t)(pixels[4] | pixels[5] << 8);
    t->height = (int16_t)(pixels[6] | pixels[7] << 8);
    set_name(t, m, name);
    t->flags = flags;
    t->manager = m;
    t->dtex = DTEX_new(t->width, t->height, t->format, t->clutformat, flags, t->name);
    /* The original passes clut + 0x10 even without a palette record (address 0x10). */
    DTEX_load(t->dtex, pixels + 0x10, clut ? clut + 0x10 : NULL);
    DtexInfo info;
    DTEX_info(t->dtex, &info);
    t->format = info.format;
    t->refs = new_refs();
    return t;
}

Texture *Texture_empty(Texture *t, TexManager *m, uint32_t flags, const char *name)
{
    memset(t, 0, sizeof *t);
    TextureStates_init(&t->states);
    t->format = t->clutformat = 0xffffffffu;
    t->vt = &texture_vtbl;
    t->manager = m;
    t->flags = flags;
    set_name(t, m, name);
    t->refs = new_refs();
    return t;
}

Texture *Texture_copy(Texture *t, const Texture *src)
{
    *t = *src;
    t->vt = &texture_vtbl;
    ++*t->refs;
    return t;
}

void Texture_dtor(Texture *t, int free_it)
{
    if (--*t->refs == 0) {
        if (t->dtex) DTEX_free(t->dtex);
        free(t->refs);
    }
    if (free_it) free(t);
}

int Texture_info(const Texture *t, uint32_t *format, uint8_t **shape)
{
    DtexInfo info;
    if (!DTEX_info(t->dtex, &info)) return 0;
    *format = info.format;
    *shape = info.shape;
    return 1;
}

void Texture_invalidate(Texture *t) { DTEX_invalidate(t->dtex); }
void Texture_commit(Texture *t) {}

void Texture_setstates(Texture *t, const TextureStates *s)
{
    t->states = *s;
    TexManager *m = g_texgraphics ? g_texgraphics->textures : NULL;
    if (!m) return;
    if (m->stage[0] == t) TEXTURE_sendstage(&t->states, 0);
    else if (m->stage[1] == t) TEXTURE_sendstage(&t->states, 1);
}

void Texture_reload(Texture *t, const uint8_t *shape)
{
    t->format = TEXTURE_formatof(shape);
    const uint8_t *clut = SHAPE_clut(shape);
    t->clutformat = clut ? TEXTURE_formatof(clut) : 0;
    t->width = (int16_t)(shape[4] | shape[5] << 8);
    t->height = (int16_t)(shape[6] | shape[7] << 8);
    if (!t->dtex) t->dtex = DTEX_new(t->width, t->height, t->format, t->clutformat, t->flags, t->name);
    DTEX_load(t->dtex, shape + 0x10, clut ? clut + 0x10 : NULL);
}

void Texture_rename(Texture *t, const char *name)
{
    memset(t->name, 0, sizeof t->name);
    for (int i = 0; i < 19 && name[i]; i++) t->name[i] = name[i];
}

/* ---- the manager ---- */

static Texture *mgr_create(TexManager *m, const uint8_t *shape, uint32_t flags, const char *name)
{
    Texture *t = malloc(sizeof *t);
    if (!t) return NULL;
    return shape ? Texture_fromshape(t, shape, m, flags, name) : Texture_empty(t, m, flags, name);
}

static Texture *mgr_create_records(TexManager *m, const uint8_t *pixels, const uint8_t *clut, uint32_t flags,
                                   const char *name)
{
    Texture *t = malloc(sizeof *t);
    return t ? Texture_fromrecords(t, pixels, clut, m, flags, name) : NULL;
}

static const TexManagerVtbl manager_vtbl = {TexManager_dtor, mgr_create_records, mgr_create};

TexManager *TexManager_ctor(TexManager *m, void *device)
{
    m->vt = &manager_vtbl;
    m->count = 0;
    m->device = device;
    m->stage[0] = m->stage[1] = NULL;
    TEXTURE_jointaddress = !(THRASH.fn.setstate(0x41, 0) && THRASH.fn.setstate(0x42, 0));
    return m;
}

void TexManager_dtor(TexManager *m, int free_it)
{
    if (free_it) free(m);
}

void TexManager_begin(TexManager *m)
{
    m->stage[0] = m->stage[1] = NULL;
    if (TEXTURE_usedtex) {
        DTEX_uploadall();
        DTEX_settexture(0);
    }
    TEXTURE_live = true;
}

void TexManager_end(TexManager *m)
{
    m->stage[0] = m->stage[1] = NULL;
    DTEX_resetall();
    TEXTURE_live = false;
}

void TEXTURE_setdtex(bool on)
{
    TEXTURE_usedtex = on;
    if (on && TEXTURE_live) {
        DTEX_resetall();
        DTEX_uploadall();
        DTEX_settexture(0);
    }
}

bool TEXTURE_islive(void) { return TEXTURE_live; }

void TexManager_release(TexManager *m, Texture *t)
{
    if (t) t->vt->destroy(t, 1);
    m->count--;
}

static bool dtex_live(void) { return TEXTURE_usedtex && TEXTURE_live; }

void TEXTURE_select(TexManager *m, Texture *t0, Texture *t1)
{
    if (!t0 && !t1) {
        m->stage[1] = m->stage[0] = NULL;
        if (dtex_live()) DTEX_settexture(0);
        return;
    }
    if (t0 != m->stage[0]) {
        if (dtex_live()) DTEX_settexture(t0 ? (uintptr_t)t0->dtex : 1);
        m->stage[0] = t0;
    }
    if (t0) TEXTURE_sendstage(&t0->states, (int32_t)t0->flags >> 16);
    if (t1 != m->stage[1]) {
        if (dtex_live()) DTEX_settexture(t1 ? (uintptr_t)t1->dtex : 2);
        m->stage[1] = t1;
    }
    if (t1) TEXTURE_sendstage(&t1->states, (int32_t)t1->flags >> 16);
}

void TexManager_restore(TexManager *m)
{
    setters_init();
    for (int s = 0; s < 2; s++)
        if (m->stage[s])
            for (int i = 0; i < TEXTURE_NSTATES; i++) setters[i]((uint32_t)cache[s][i][0], cache[s][i][1]);
    if (!dtex_live()) return;
    if (!m->stage[0]) {
        if (!m->stage[1]) {
            DTEX_settexture(0);
            return;
        }
        DTEX_settexture(1);
    } else
        DTEX_settexture((uintptr_t)m->stage[0]->dtex);
    DTEX_settexture(m->stage[1] ? (uintptr_t)m->stage[1]->dtex : 2);
}
