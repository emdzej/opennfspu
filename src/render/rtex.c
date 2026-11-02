/* The game's textures: see rtex.h. */
#include "render/rtex.h"
#include "exe.h"
#include "real/depthconv.h"
#include "render/render.h"
#include "real/dtex.h"
#include "real/file.h"
#include "real/mem.h"
#include "real/shape.h"
#include "support/udata.h"
#include "support/ufile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int RTex_level;              /* 0x619770 */
bool RTex_forceclass5;       /* 0x619800 */
bool RTex_flagsallowed;      /* 0x6197a8 */
bool RTex_mipsallowed;       /* 0x6197ae */
static int detected_level;   /* 0x657d88: the level before the depth limit */

const uint8_t *RTex_quality(int texclass)
{
    int c = RTex_forceclass5 ? 5 : texclass;
    return exe_data(0x5ce59c + 4 * (uint32_t)(c + RTex_level * 6), 4);
}

/* ---- the machine ---- */

static int mbytes(int64_t m)
{
    int32_t v = m > 0x7fffffff ? 0x7fffffff : (int32_t)m;   /* a 32-bit count in the original */
    return v / 1024 / 1024;
}

int RTex_levelfor(const RTexMachine *m)
{
    int mhz = m->cpuhz / 1000000, mem = mbytes(m->memory);
    uint32_t vram = m->vram >> 20;
    if (vram < 3 || mem < 0x21 || mhz < 0xeb) return 0;
    if (vram < 9 || mem < 0x41) return 1;
    if (vram > 0xd && mem > 0x54 && mhz > 0x1c1 && m->depth > 0x10) return 3;
    return 2;
}

const RTexMachine RTex_machine = {1000000000, 256ll << 20, 64u << 20, 32};

int RTex_layoutfor(const RTexMachine *m)
{
    int mhz = m->cpuhz / 1000000, mem = mbytes(m->memory);
    uint32_t vram = m->vram >> 20;
    int r = vram < 4 || mhz < 0xfa || mem < 0x21 ? 3 : 0;
    if (vram > 3 && mhz > 400 && mem > 0x41) return r;
    return 2;
}

void RTex_detectquality(const RTexMachine *m)
{
    /* the display's depth is the open mode's (0x61978c), not the machine's */
    RTexMachine at = *m;
    at.depth = g_display.bpp;
    RTex_level = detected_level = RTex_levelfor(&at);
    if (RTex_level == 3 && at.depth < 0x20) RTex_level = 2;
}

/* ---- DTEX preference lists the game sets per class (exe 0x5ce48c..0x5ce54c, 0-terminated) ---- */

enum { L565_16 = 0, L8888_16A, L8888_16, L565_32, L8888_32, L565_DEF, L8888_DEF, NLISTS };
static const uint32_t list_addr[NLISTS] = {0x5ce48c, 0x5ce4ac, 0x5ce4cc, 0x5ce4ec, 0x5ce50c, 0x5ce52c, 0x5ce54c};
static uint32_t lists[NLISTS][8];
static bool lists_loaded;

static const uint32_t *list(int i)
{
    if (!lists_loaded) {
        lists_loaded = true;
        for (int l = 0; l < NLISTS; l++)
            for (uint32_t k = 0; k < 7; k++)
                if (!(lists[l][k] = exe_u32(list_addr[l] + 4 * k))) break;
    }
    return lists[i];
}

/* ---- shapes to textures ---- */

uint8_t *RTex_shrinkmip(const uint8_t *shape)
{
    int bpp = SHAPE_typevalid(shape);
    if (bpp == 15) bpp = 16;
    int w = (int16_t)(shape[4] | shape[5] << 8), h = (int16_t)(shape[6] | shape[7] << 8);
    uint32_t pos = (uint32_t)shape[12] | shape[13] << 8 | shape[14] << 16 | (uint32_t)shape[15] << 24;
    int full = h * w * (bpp / 8);
    int total = full + 0x10, level = full;
    for (uint32_t m = pos >> 28; m; m--) {
        level /= 4;
        total += level;
    }
    uint8_t *out = MEM_alloc("ShrinkMIP", total - full, 0x10);
    MEM_copy(out, shape, 0x10);
    MEM_copy(out + 0x10, shape + full + 0x10, (size_t)(total - full - 0x10));
    /* One mip level less; the rest of the position word kept. */
    pos = ((pos & 0xf0000000u) - 1) ^ (~pos & 0x0fffffffu);
    out[12] = (uint8_t)pos;
    out[13] = (uint8_t)(pos >> 8);
    out[14] = (uint8_t)(pos >> 16);
    out[15] = (uint8_t)(pos >> 24);
    int16_t nw = (int16_t)(w / 2), nh = (int16_t)(h / 2);
    out[4] = (uint8_t)nw;
    out[5] = (uint8_t)((uint16_t)nw >> 8);
    out[6] = (uint8_t)nh;
    out[7] = (uint8_t)((uint16_t)nh >> 8);
    return out;
}

RTexConv *RTex_convertshape(RTexConv *c, const uint8_t *shape, int texclass)
{
    c->copy = NULL;
    c->halved = NULL;
    c->changed = false;
    c->shape = c->original = shape;
    const uint8_t *q = RTex_quality(texclass);
    bool alpha = true;
    if (q[2] & RTEX_Q_ALPHACHECK) alpha = (shape[8] | shape[9] << 8) != 0;
    if (q[0] == 0x10) {
        DTEX_setprefs(4, list(L565_16));
        DTEX_setprefs(6, list(alpha ? L8888_16A : L8888_16));
    } else {
        DTEX_setprefs(4, list(L565_32));
        DTEX_setprefs(6, list(L8888_32));
    }
    int w = (int16_t)(shape[4] | shape[5] << 8);
    uint8_t h = q[1];
    if (((h & 1) && w == 0x200) || ((h & 2) && w == 0x100) || ((h & 4) && w == 0x80) || ((h & 8) && w == 0x40) ||
        ((h & 0x10) && w == 0x20)) {
        c->changed = true;
        c->halved = (q[2] & RTEX_Q_SHRINK) ? RTex_shrinkmip(c->shape) : DTEX_halve(c->shape);
        c->shape = c->halved;
    }
    if (q[2] & RTEX_Q_COPY) {
        c->changed = true;
        const uint8_t *s = c->shape;
        int bpp = SHAPE_typevalid(s);
        if (bpp == 15) bpp = 16;
        int bytes = (int16_t)(s[6] | s[7] << 8) * (int16_t)(s[4] | s[5] << 8) * (bpp / 8) + 0x14;
        /* Twice the size, only the first half copied: the shape's palette, if any, isn't (the copy's chain
           points into the uncopied half; cleared in the port, so no palette is found there). */
        c->copy = MEM_alloc("RTex:SHPconv", bytes * 2, 0x10);
        memset(c->copy, 0, (size_t)bytes * 2);
        MEM_copy(c->copy, s, (size_t)bytes);
        c->shape = c->copy;
    }
    c->mipmapped = (q[2] & RTEX_Q_SHRINK) != 0;
    if (!(q[2] & RTEX_Q_MIPS)) {
        c->mips = 0;
        return c;
    }
    int n = 0;
    for (int16_t s = (int16_t)((int16_t)(c->shape[4] | c->shape[5] << 8) >> 2); s > 0; s >>= 1) n++;
    c->mips = n;
    return c;
}

static void conv_free(RTexConv *c)
{
    if (c->copy) MEM_purge(c->copy);
    if (c->halved) MEM_purge(c->halved);
    DTEX_setprefs(4, list(L565_DEF));
    DTEX_setprefs(6, list(L8888_DEF));
}

Texture *RTex_make(const uint8_t *shape, uint32_t name, int texclass, TexManager *m, uint32_t flags)
{
    if (!RTex_flagsallowed) flags = 0;
    const uint8_t *q = RTex_quality(texclass);
    if (name < 0x20) name = name * 0x1000000u + 0x30524143u;   /* "CAR0" + n */
    RTexConv c;
    RTex_convertshape(&c, shape, texclass);
    char text[16] = {(char)name, (char)(name >> 8), (char)(name >> 16), (char)(name >> 24)};
    strncat(text, exe_str(0x5ce574), sizeof text - strlen(text) - 1);
    uint32_t mips = RTex_mipsallowed ? (uint32_t)c.mips : 0;
    Texture *t = malloc(sizeof *t);
    if (t) Texture_fromshape(t, c.shape, m, mips | flags, text);
    if (t && (q[2] & RTEX_Q_COMMIT)) Texture_commit(t);
    conv_free(&c);
    return t;
}

void RTex_reload(Texture *t, const uint8_t *shape, int texclass)
{
    RTexConv c;
    RTex_convertshape(&c, shape, texclass);
    Texture_reload(t, c.shape);
    conv_free(&c);
}

/* ---- upload priorities ---- */

static int mip_levels(int size)
{
    int n = 0;
    for (int s = size >> 2; s > 0; s >>= 1) n++;
    return n;
}

void RTex_setprios(const uint8_t *rows, int n)
{
    for (int i = 0; i < n; i++, rows += 6) {
        int size = (int16_t)(rows[0] | rows[1] << 8);
        int fmt = (int8_t)rows[2];
        int levels = rows[3] && RTex_mipsallowed ? mip_levels(size) : 0;
        uint32_t clut = rows[2] == 2 ? 4 : 0;
        if (rows[4]) DTEX_setprio(0, (int8_t)rows[4], size, size, (uint32_t)fmt, clut, (uint32_t)levels);
        if (rows[5]) DTEX_setprio(1, (int8_t)rows[5], size, size, (uint32_t)fmt, clut, (uint32_t)levels);
    }
}

void RTex_defaultprios(void) { RTex_setprios(exe_data(0x5ce71c, 6 * 0xc), 0xc); }

/* The per-quality tables (0x44de40, 0x44de50, 0x44de60; level 3 has none: 0x44de70 only returns). */
static void level_prios(int level)
{
    static const struct { uint32_t addr; int n; } t[3] = {{0x5ce658, 0xb}, {0x5ce69c, 10}, {0x5ce6d8, 0xb}};
    if (level >= 0 && level < 3) RTex_setprios(exe_data(t[level].addr, (size_t)(6 * t[level].n)), t[level].n);
}

/* The .dtx rows (0x5ce768, 11 rows of 9 ints): size, format (low byte; byte 5 = mipmapped), the counts of
   the five classes (filled from the file), two more, the base count. Copied from the exe once; the file's
   counts and the two class-dependent constants are written into the copy, as into the original's data. */
enum { NROWS = 11 };
static int32_t rows[NROWS][9];
static bool rows_loaded;

static bool read_dtx(const char *trackdir, const char *track)
{
    char name[100];
    snprintf(name, sizeof name, exe_str(0x5ce8f4), trackdir, track);   /* "%s%s.dtx" */
    if (!rows_loaded) {
        rows_loaded = true;
        for (uint32_t r = 0; r < NROWS; r++)
            for (uint32_t k = 0; k < 9; k++) rows[r][k] = (int32_t)exe_u32(0x5ce768 + 36 * r + 4 * k);
    }
    int c2 = RTex_forceclass5 ? 5 : 2, c3 = RTex_forceclass5 ? 5 : 3;
    const uint8_t *q2 = exe_data(0x5ce59c + 4 * (uint32_t)(c2 + RTex_level * 6), 4);
    const uint8_t *q3 = exe_data(0x5ce59c + 4 * (uint32_t)(c3 + RTex_level * 6), 4);
    rows[9][4] = (q2[1] & 2) ? 0xe : 0x15;              /* 0x5ce8bc: row 9, class 2 */
    rows[9][5] = (int32_t)((~q3[1] & 2) | 4) >> 1;     /* 0x5ce8c0: row 9, class 3 */
    char path[300];
    if (!UTIL_searchpath(name, path)) return false;
    uint8_t *d = FILE_loadfile(path, 0);              /* Util_LoadFile 0x59d690 */
    if (!d) return false;
    for (int r = 0; r < NROWS; r++) rows[r][2] = d[r];
    for (int r = 0; r < NROWS; r++) {
        int size = rows[r][0];
        int fmt = (int8_t)(rows[r][1] & 0xff);
        bool mipped = (rows[r][1] >> 8 & 0xff) != 0;
        int pass0 = 0, pass1 = rows[r][8];
        for (int k = 0; r > 0 && k < 5; k++) {
            int cls = RTex_forceclass5 ? 5 : k;
            const uint8_t *q = exe_data(0x5ce59c + 4 * (uint32_t)(cls + RTex_level * 6), 4);
            uint8_t halve = q[1];
            bool to_pass0 = (q[2] & 2) && mipped;
            int own = rows[r][2 + k];
            /* The count of the next bigger size (same format) when that size is halved down to this one. */
            int bigger = r + 2 < NROWS ? rows[r + 2][2 + k] : (int32_t)exe_u32(0x5ce7b8 + 4 * (uint32_t)(r * 9 + k));
            bool own_halved = (size == 0x100 && (halve & 2)) || (size == 0x80 && (halve & 4)) ||
                              (size == 0x40 && (halve & 8)) || (size == 0x20 && (halve & 0x10));
            if (!own_halved) {
                if (to_pass0) pass0 += own;
                else pass1 += own;
            }
            bool from_bigger = (size == 0x80 && (halve & 2)) || (size == 0x40 && (halve & 4)) ||
                               (size == 0x20 && (halve & 8)) || (size == 0x10 && (halve & 0x10));
            if (from_bigger) {
                if (to_pass0) pass0 += bigger;
                else pass1 += bigger;
            }
        }
        int levels = mipped ? mip_levels(size) : 0;
        uint32_t clut = fmt == 2 ? 4 : 0;
        DTEX_setprio(0, pass0, size, size, (uint32_t)fmt, clut, (uint32_t)levels);
        DTEX_setprio(0, pass1, size, size, (uint32_t)fmt, clut, 0);
    }
    MEM_purge(d);
    return true;
}

void RTex_setuppriorities(const char *trackdir, const char *track)
{
    DTEX_setpalettepref(1);
    if (!read_dtx(trackdir, track)) level_prios(RTex_level);
}

/* ---- banks ---- */

RTexBank *RTexBank_ctor(RTexBank *b, TexManager *m, int texclass)
{
    b->manager = m;
    b->texclass = texclass;
    b->entries = NULL;
    b->n = b->cap = 0;
    return b;
}

static void bank_release(RTexBank *b)
{
    for (int i = 0; i < b->n; i++) {
        RTexEntry *e = &b->entries[i];
        if (!e->managed) {
            if (e->tex) e->tex->vt->destroy(e->tex, 1);
        } else
            TexManager_release(b->manager, e->tex);
    }
    b->n = 0;
}

void RTexBank_clear(RTexBank *b) { bank_release(b); }

void RTexBank_dtor(RTexBank *b)
{
    bank_release(b);
    free(b->entries);
    b->entries = NULL;
    b->cap = 0;
}

static bool key_less(uint32_t i0, uint32_t n0, uint32_t i1, uint32_t n1) { return i0 < i1 || (i0 == i1 && n0 < n1); }

/* The first entry not less than the key (std::multimap::lower_bound). */
static int lower_bound(const RTexBank *b, uint32_t id, uint32_t name)
{
    int lo = 0, hi = b->n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (key_less(b->entries[mid].id, b->entries[mid].name, id, name)) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

/* Inserted after the equal keys, as the map's insert_equal does (0x44da40, 0x44db20). */
static void bank_add(RTexBank *b, uint32_t id, uint32_t name, Texture *t, uint8_t managed)
{
    int at = lower_bound(b, id, name);
    while (at < b->n && b->entries[at].id == id && b->entries[at].name == name) at++;
    if (b->n == b->cap) {
        b->cap = b->cap ? b->cap * 2 : 64;
        b->entries = realloc(b->entries, (size_t)b->cap * sizeof *b->entries);
    }
    memmove(b->entries + at + 1, b->entries + at, (size_t)(b->n - at) * sizeof *b->entries);
    b->entries[at] = (RTexEntry){id, name, t, 0, managed};
    b->n++;
}

Texture *RTexBank_find(const RTexBank *b, uint32_t id, uint32_t name)
{
    int at = lower_bound(b, id, name);
    return at < b->n && b->entries[at].id == id && b->entries[at].name == name ? b->entries[at].tex : NULL;
}

/* 0x44d420: a bank texture's states, with mipmapping as the class says. */
static void bank_states(const RTexBank *b, Texture *t, const TextureStates *s)
{
    if (!s) return;
    const uint8_t *q = RTex_quality(b->texclass);
    TextureStates st = *s;
    TextureStates_setmipmap(&st, (q[2] & RTEX_Q_SHRINK) != 0);
    Texture_setstates(t, &st);
}

Texture *RTexBank_get(RTexBank *b, const uint8_t *shape, uint32_t id, uint32_t name, const TextureStates *states,
                      int unused, bool nocommit, uint32_t flags)
{
    int at = lower_bound(b, id, name);
    if (at >= b->n || b->entries[at].id != id || b->entries[at].name != name) {
        Texture *t = RTex_make(shape, name, b->texclass, b->manager, flags);
        bank_states(b, t, states);
        if (!nocommit) Texture_commit(t);
        bank_add(b, id, name, t, 1);
        return t;
    }
    Texture *last = NULL;
    for (; at < b->n && b->entries[at].id == id && b->entries[at].name == name; at++) {
        last = b->entries[at].tex;
        if (!states || !memcmp(&last->states, states, sizeof *states)) return last;
    }
    Texture *t = malloc(sizeof *t);
    Texture_copy(t, last);
    char text[16] = {(char)name, (char)(name >> 8), (char)(name >> 16), (char)(name >> 24)};
    strncat(text, exe_str(0x5ce56c), sizeof text - strlen(text) - 1);   /* ":CLONE" */
    Texture_rename(t, text);
    bank_states(b, t, states);
    bank_add(b, id, name, t, 0);
    return t;
}

void RTexBank_insert(RTexBank *b, Texture *t, uint32_t id, uint32_t name, const TextureStates *states,
                     int unused, bool nocommit)
{
    Texture *c = malloc(sizeof *c);
    Texture_copy(c, t);
    if (states) Texture_setstates(c, states);
    if (!nocommit) Texture_commit(c);
    bank_add(b, id, name, c, 1);
}

bool RTexBank_loadfile(RTexBank *b, const char *fsh, uint32_t flags)
{
    UDataFile f;
    UDataFile_ctor(&f);
    UDataFile_load(&f, fsh, false);
    const uint8_t *shp = (const uint8_t *)UDataFile_root(&f);
    if (!shp) {
        UDataFile_dtor(&f);
        return false;
    }
    uint32_t n = SHAPE_count(shp);
    for (uint32_t i = 0; i < n; i++) RTexBank_get(b, SHAPE_entry(shp, (int)i), 0, SHAPE_entryname(shp, (int)i), NULL, 1, true, flags);
    UDataFile_dtor(&f);
    return true;
}

/* ---- the render module's own art (Render_LoadCommonFsh 0x448a40 and its end 0x448af0) ---- */

RTexBank *g_rtex_common[2];   /* 0x613e04: common.fsh, particle.fsh (class 4) */

void Render_LoadCommonFsh(TexManager *m)
{
    for (int i = 0; i < 2; i++) {
        g_rtex_common[i] = RTexBank_ctor(calloc(1, sizeof *g_rtex_common[i]), m, 4);
        char path[256];
        snprintf(path, sizeof path, "%s%s", "GameData\\Render\\", exe_str(exe_u32(0x5cdeac + 4 * (uint32_t)i)));   /* 0x65b30c */
        if (g_rtex_common[i]) RTexBank_loadfile(g_rtex_common[i], path, 0);
    }
}

void Render_FreeCommonFsh(void)
{
    for (int i = 0; i < 2; i++) {
        if (g_rtex_common[i]) {
            RTexBank_dtor(g_rtex_common[i]);
            free(g_rtex_common[i]);
        }
        g_rtex_common[i] = NULL;
    }
}
