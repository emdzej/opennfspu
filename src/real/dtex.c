/* \real\patch3\cmn\dyntexture.c: see dtex.h. */
#include "real/dtex.h"
#include "exe.h"
#include "real/depthconv.h"
#include "real/mem.h"
#include "real/real.h"
#include "real/set3d.h"
#include "real/shape.h"
#include <stdlib.h>
#include <string.h>

static const char dyntexture_c[] = "\\real\\patch3\\cmn\\dyntexture.c";

DtexType DTEX_types[32];          /* 0x6a33f8 */
int DTEX_ntypes;                  /* 0x6a3a44 */
int DTEX_nobjects;                /* 0x6a3a40 */
bool DTEX_uploaded;               /* 0x6a3a48 */
int DTEX_destroycount;             /* 0x6a3a64 */
static const ThrashAbout *caps;   /* 0x6a3a54 */
uintptr_t DTEX_current;            /* 0x6a3a58 */
static void (*upload_hook)(const DtexHookInfo *, const DtexHookInfo *);   /* 0x6a3a50 */
static int (*score_hook)(uint32_t, uint32_t, uint32_t, uint32_t) = DTEX_score;   /* 0x5df2c8 */

/* ---- the memory pool: the port's blocks, freed together ---- */

static uint32_t pool_size = 0xc00000;   /* 0x5df2c4 */
static bool pool_open;                  /* 0x6a3a5c (the VirtualAlloc'd base) */
static void **pool_blocks;
static size_t pool_n, pool_cap;

int DTEX_initpool(uint32_t size)
{
    if (size == 0) {
        pool_size = 0xc00000;
        return 0;
    }
    if (pool_open) DTEX_destroyall();
    pool_open = true;
    pool_size = size;
    return 1;
}

/* 0x5548b0: the pool at the size last asked for (12 MiB by default); the original retries smaller sizes
   while VirtualAlloc fails. */
static bool pool_ensure(void)
{
    return pool_open || DTEX_initpool(pool_size ? pool_size : 0xc00000);
}

static void *pool_alloc(const char *name, int size, bool clear)
{
    if (!pool_ensure()) return NULL;
    void *p = MEM_alloc(name, size, 0);
    if (!p) return NULL;
    if (clear) memset(p, 0, (size_t)size);
    if (pool_n == pool_cap) {
        pool_cap = pool_cap ? pool_cap * 2 : 256;
        pool_blocks = realloc(pool_blocks, pool_cap * sizeof *pool_blocks);
    }
    pool_blocks[pool_n++] = p;
    return p;
}

static void pool_free(void *p)
{
    if (!p) return;
    for (size_t i = pool_n; i-- > 0;)
        if (pool_blocks[i] == p) {
            pool_blocks[i] = pool_blocks[--pool_n];
            break;
        }
    MEM_purge(p);
}

void DTEX_freepool(void)
{
    if (!pool_open) return;
    for (size_t i = 0; i < pool_n; i++) MEM_purge(pool_blocks[i]);
    pool_n = 0;
    pool_open = false;
}

/* ---- format tables (exe) ---- */

/* Per THRASH format (0x5df15c, 0x14 bytes): alpha bits, red, green, blue, bits per pixel. Per palette
   format (0x5df0cc, 0x10 bytes): alpha, red, green, blue bits. Index bits above the format number wrap
   away in the original's 32-bit address arithmetic: the port masks them. */
static uint32_t fmt_alpha(uint32_t f) { return exe_u32(0x5df15c + 0x14 * (f & 0xffff)); }
static uint32_t fmt_bpp(uint32_t f) { return exe_u32(0x5df15c + 0x14 * (f & 0xffff) + 0x10); }
static uint32_t clut_alpha(uint32_t c) { return exe_u32(0x5df0cc + 0x10 * (c & 0xffff)); }

/* Preference lists: the exe's (0x5def98 per format, 0x5df0a8 per palette format) copied at first use,
   so the game can change them. */
enum { NFMT = 14, NLIST = 12 };
static uint32_t fmt_lists[NFMT][NLIST], clut_lists[NFMT][NLIST];
static const uint32_t *fmt_pref[NFMT], *clut_pref[NFMT];
static bool lists_loaded;

static void load_list(uint32_t table, uint32_t i, uint32_t *dst, const uint32_t **ptr)
{
    uint32_t p = exe_u32(table + 4 * i);
    *ptr = NULL;
    if (!p) return;
    for (int k = 0; k < NLIST - 1; k++) {
        dst[k] = exe_u32(p + 4 * (uint32_t)k);
        if (!dst[k]) break;
    }
    dst[NLIST - 1] = 0;
    *ptr = dst;
}

static void lists_init(void)
{
    if (lists_loaded) return;
    lists_loaded = true;
    for (uint32_t i = 1; i < NFMT; i++) load_list(0x5def98, i, fmt_lists[i], &fmt_pref[i]);
    for (uint32_t i = 1; i < 9; i++) load_list(0x5df0a8, i, clut_lists[i], &clut_pref[i]);
}

void DTEX_setprefs(uint32_t format, const uint32_t *list)
{
    lists_init();
    if (format < NFMT) fmt_pref[format] = list;
}

/* 0x554690 writes the head of the 8-bit list (0x5dedf4): 2 (stay 8-bit) or 4 (565). */
void DTEX_setpalettepref(int keep8bit)
{
    lists_init();
    fmt_lists[2][0] = keep8bit ? 2 : 4;
}

static const ThrashAbout *get_caps(void)
{
    if (!caps) caps = THRASH.fn.about();
    return caps;
}

static bool palettised(uint32_t f) { return f == 2 || f == 1 || f == 9; }

/* 0x554290: asks the driver whether state 10 (blending) can be set, restoring it: the answer is unused. */
static void probe_blend(void)
{
    intptr_t old = THRASH.fn.getstate(10);
    THRASH.fn.setstate(10, 1);
    THRASH.fn.setstate(10, (uintptr_t)old);
}

/* Position of v in a 0-terminated list (penalty 20 if absent). */
static int list_penalty(const uint32_t *list, uint32_t v)
{
    int found = 0, score = 0;
    for (int i = 0; list && list[i]; i++)
        if (list[i] == v) {
            found = 1;
            score += i;
        }
    return found ? score : 0x14;
}

int DTEX_score(uint32_t srcformat, uint32_t srcclut, uint32_t format, uint32_t clutformat)
{
    lists_init();
    uint32_t s = srcformat & 0xfffff0ffu, sc = srcclut & 0xfffff0ffu;
    format &= 0xfffff0ffu;
    clutformat &= 0xfffff0ffu;
    bool srcpal = palettised(s), dstpal = palettised(format);
    if (srcpal) {
        if (sc == 0 || (dstpal && clutformat == 0)) return 10000;
    } else if (dstpal)
        return 10000;
    probe_blend();
    const ThrashAbout *a = get_caps();
    int score = list_penalty(fmt_pref[s & 0xffff], format);
    /* Alpha the source has and the target can't keep (unless the source says it doesn't need it, bit 8). */
    if (fmt_alpha(s) && !(srcformat >> 8 & 1) && (!fmt_alpha(format) || (a->texformats[format & 0xffff] & 4) == 4))
        score += 0x14;
    if ((a->texformats[format & 0xffff] & 1) != 1) score += 0x14;
    if (srcpal) {
        if (!dstpal) return score + 10;
    } else if (!dstpal)
        return score;
    score += list_penalty(clut_pref[sc & 0xffff], clutformat);
    if (clut_alpha(sc) && !(srcclut >> 8 & 1) && (!clut_alpha(clutformat) || (a->cluts[clutformat & 0xffff] & 4) == 4))
        score += 0x14;
    if ((a->cluts[clutformat & 0xffff] & 1) != 1) score += 0x14;
    return score;
}

uint32_t DTEX_choose(uint32_t *format, uint32_t *clutformat)
{
    int best = 10000;
    uint32_t bf = 0, bc = 0;
    if (!format || !*format) return 0;
    const ThrashAbout *a = get_caps();
    if (a->texformats)
        for (int f = 1; f < a->ntexformats + 1; f++) {
            if (!a->texformats[f]) continue;
            int nc = palettised((uint32_t)f) ? a->ncluts + 1 : 1;
            for (int c = palettised((uint32_t)f) ? 1 : 0; c < nc; c++) {
                if (c != 0 && !a->cluts[c]) continue;
                int s = score_hook(*format, clutformat ? *clutformat : 0, (uint32_t)f, (uint32_t)c);
                if (s < best) {
                    best = s;
                    bf = (uint32_t)f;
                    bc = (uint32_t)c;
                }
            }
        }
    *format = bf;
    if (clutformat) *clutformat = bc;
    return bf;
}

int DTEX_create(int size, uint32_t format, uint32_t clutformat, uint32_t flags)
{
    int i = 0;
    if (DTEX_types[0].size > 0) {
        int found = -1;
        for (; i < 32; i++) {
            const DtexType *t = &DTEX_types[i];
            if (t->size == size && t->format == format && t->clutformat == clutformat && t->flags == flags) found = i;
            if (i + 1 >= 32 || DTEX_types[i + 1].size <= 0) {
                i++;
                break;
            }
        }
        if (found != -1) return found;
        if (i > 0x1f)
            REAL_abort(dyntexture_c, 0,
                       "[DTEXTURE] dtexture.c Error too many different texture types used\n Try using more textures of the same width/mipmap levels/ pixel format\n");
    }
    DTEX_ntypes++;
    DtexType *t = &DTEX_types[i];
    t->size = size;
    t->format = format;
    t->clutformat = clutformat;
    t->flags = flags;
    int bytes = (int)fmt_bpp(format) * size * size / 8;
    int total = bytes;
    for (uint32_t m = flags & 0xff; m; m--) {
        bytes /= 4;
        total += bytes;
    }
    t->bytes = total;
    return i;
}

/* 0x554830: the header of a texture's pixel buffer, a shape record of the texture's own format. */
static void buffer_header(uint8_t *s, int width, int height, uint32_t format, uint32_t mips)
{
    uint32_t type = 0;
    switch (format & 0x7fffffffu) {
    case 1: type = 0x7a; break;
    case 2: type = 0x7b; break;
    case 3: case 0xb: type = 0x7e; break;
    case 4: type = 0x78; break;
    case 5: type = 0x7f; break;
    case 6: type = 0x7d; break;
    case 7: type = 0x6d; break;
    case 0xc: type = 0xf8; break;
    case 0xd: type = 0xfd; break;
    }
    memset(s, 0, SHAPE_HEADER);   /* the original keeps bits 13 and 15 of the position word from the heap */
    s[0] = (uint8_t)type;
    s[4] = (uint8_t)width;
    s[5] = (uint8_t)(width >> 8);
    s[6] = (uint8_t)height;
    s[7] = (uint8_t)(height >> 8);
    s[15] = (uint8_t)(mips << 4);
}

DtexObject *DTEX_new(int width, int height, uint32_t srcformat, uint32_t srcclut, uint32_t flags, const char *name)
{
    const ThrashAbout *a = caps = THRASH.fn.about();
    if (width == 0 || width != height || (width & (width - 1))) return NULL;
    DtexObject *t = pool_alloc(name, (int)sizeof *t, true);
    if (!t) return NULL;
    for (int i = 0; i < 8; i++) {
        if (!name[i]) {
            t->name[i] = 0;
            break;
        }
        t->name[i] = name[i];
    }
    t->srcformat = srcformat;
    t->srcclut = srcclut;
    uint32_t fmt = srcformat, clut = srcclut;
    DTEX_choose(&fmt, &clut);
    fmt &= 0xfffff0ffu;
    clut &= 0xfffff0ffu;
    t->type = DTEX_create(width, fmt, clut, flags);
    DtexType *ty = &DTEX_types[t->type];
    t->shape = pool_alloc(name, ty->bytes + 0x13, false);
    t->pixels = t->shape + SHAPE_HEADER;
    buffer_header(t->shape, width, height, fmt, flags & 0xff);
    t->clut = (ty->format == 2 || ty->format == 1) ? pool_alloc(name, 0x400, false) : NULL;
    t->handle = 0;
    t->next = ty->objects;
    ty->objects = t;
    t->flags = (a->flags >> 4 & DTEX_F_GLOBALCLUT) | (t->flags & ~0xfu);
    t->serial = ty->count++;
    DTEX_nobjects++;
    return t;
}

void DTEX_free(DtexObject *t)
{
    DtexType *ty = &DTEX_types[t->type];
    if (ty->objects == t) ty->objects = t->next;
    else {
        DtexObject *p = ty->objects;
        while (p && p->next != t) p = p->next;
        if (!p) {
            REAL_abort(dyntexture_c, 0x6f1, "DTEX - DTEX_destroy error, texture not in list\n");
            return;
        }
        p->next = t->next;
    }
    DTEX_nobjects--;
    ty->count--;
    pool_free(t->shape);
    pool_free(t->clut);
    pool_free(t);
}

void DTEX_convert(uint32_t srcformat, uint32_t srcclut, uint32_t format, uint32_t clutformat, const void *src,
                  void *dst, const void *srcclutdata, void *clutdata, int bytes)
{
    format &= 0xfffff0ffu;
    srcformat &= 0xfffff0ffu;
    int bpp = (int)fmt_bpp(format);
    if (!bpp) return;   /* format 0 (no driver format fits): the original divides by zero here */
    int n = bytes * 8 / bpp;
    if (TEXTURE_converttype(srcformat, srcclut & 0xfffff0ffu, format, clutformat & 0xfffff0ffu, src, dst,
                            srcclutdata, clutdata, n))
        return;
    if (srcformat == 8 && format == 7) {
        /* 0x5554e0: 8-bit (4-bit alpha, 4-bit grey) to 4444: the byte high, its grey nibble twice low. */
        const uint8_t *s = src;
        uint8_t *d = dst;
        for (int i = 0; i < bytes >> 1; i++) {
            d[2 * i] = (uint8_t)((s[i] & 0xf) << 4 | (s[i] & 0xf));
            d[2 * i + 1] = s[i];
        }
        return;
    }
    if (srcformat == format && (srcclut & 0xfffff0ffu) == (clutformat & 0xfffff0ffu)) {
        MEM_copy(dst, src, (size_t)bytes);
        if (srcclutdata && clutdata) MEM_copy(clutdata, srcclutdata, 0x400);
    }
}

void DTEX_load(DtexObject *t, const void *pixels, const void *clut)
{
    if (pixels) {
        const DtexType *ty = &DTEX_types[t->type];
        DTEX_convert(t->srcformat, t->srcclut, ty->format, ty->clutformat, pixels, t->pixels, clut, t->clut, ty->bytes);
        if (clut && t->clut) t->flags |= DTEX_F_CLUT;
    }
    t->flags = (t->flags & ~(uint32_t)DTEX_F_ONCARD) | DTEX_F_LOADED;
}

int DTEX_info(const DtexObject *t, DtexInfo *out)
{
    if (!t) return 0;
    out->serial = t->serial;
    out->shape = t->shape;
    out->pixels = t->pixels;
    out->clut = (t->flags & DTEX_F_CLUT) ? t->clut : NULL;
    out->srcformat = t->srcformat;
    out->format = DTEX_types[t->type].format;
    return 1;
}

void DTEX_invalidate(DtexObject *t) { t->flags = (t->flags & ~(uint32_t)DTEX_F_ONCARD) | DTEX_F_LOADED; }

/* 0x554da0: the upload hook's view of the two textures. */
static void call_hook(const DtexObject *up, const DtexObject *down)
{
    if (!upload_hook) return;
    DtexHookInfo a = {0}, b = {0};
    if (up) a = (DtexHookInfo){DTEX_types[up->type].size, up->srcformat, up->name, up};
    if (down) b = (DtexHookInfo){DTEX_types[down->type].size, down->srcformat, down->name, down};
    upload_hook(up ? &a : NULL, down ? &b : NULL);
}

void DTEX_sethook(void (*hook)(const DtexHookInfo *, const DtexHookInfo *)) { upload_hook = hook; }

DtexObject *DTEX_upload(DtexObject *t)
{
    if (t->handle)
        REAL_abort(dyntexture_c, 0x8b5, "[DTEX] Error - trying to upload a texture thats already uploaded.\n Texture %s.\n", t->name);
    DtexType *ty = &DTEX_types[t->type];
    t->handle = THRASH.fn.talloc(ty->size, ty->size, (int)ty->format, (int)ty->clutformat, ty->flags);
    if (!t->handle) return NULL;
    call_hook(t, NULL);
    THRASH.fn.tupdate(t->handle, t->pixels, (t->flags & DTEX_F_CLUT) ? (const uint32_t *)(const void *)t->clut : NULL);
    t->flags |= DTEX_F_ONCARD;
    if (!ty->newest) ty->oldest = t;
    else ty->newest->newer = t;
    t->older = ty->newest;
    ty->newest = t;
    t->newer = NULL;
    return t;
}

int DTEX_prio(const DtexObject *t, int pass)
{
    return pass >= 0 && pass < 3 ? DTEX_types[t->type].prio[pass] : 1;
}

int DTEX_uploadall(void)
{
    int pass = -1, round = 0, done_count = 0, failures = 0;
    bool done[32];
    DtexObject *next[32];
    if (DTEX_uploaded) return -1;
    for (int i = 0; i < 32; i++) {
        done[i] = i >= DTEX_ntypes;
        next[i] = i < DTEX_ntypes ? DTEX_types[i].objects : NULL;
    }
    while (DTEX_ntypes > 0 && done_count < DTEX_ntypes) {
        bool idle = true;
        for (int i = 0; i < DTEX_ntypes; i++) {
            if (done[i]) continue;
            DtexObject *t = next[i];
            if (!t) {
                done[i] = true;
                done_count++;
                continue;
            }
            if (round < DTEX_prio(t, pass)) {
                DtexObject *ok = DTEX_upload(t);
                next[i] = t->next;
                idle = false;
                if (!ok) {
                    failures++;
                    done[i] = true;
                    done_count++;
                }
            }
        }
        round++;
        if (idle) {
            round = 0;
            pass++;
        }
    }
    DTEX_uploaded = true;
    if (DTEX_ntypes <= failures)
        for (int i = 0; i < DTEX_ntypes; i++)
            if (DTEX_types[i].objects && !DTEX_types[i].objects->handle)
                REAL_abort(dyntexture_c, 0x991, "D3D Error - Not enough texture memory.\n");
    return pass;
}

void DTEX_resetall(void)
{
    if (!DTEX_uploaded) return;
    THRASH.fn.treset();
    for (int i = 0; i < DTEX_ntypes; i++) {
        DtexType *ty = &DTEX_types[i];
        ty->newest = ty->oldest = NULL;
        for (DtexObject *t = ty->objects; t; t = t->next) {
            t->handle = 0;
            t->older = t->newer = NULL;
        }
    }
    DTEX_uploaded = false;
    DTEX_current = 0;
}

void DTEX_destroyall(void)
{
    DTEX_resetall();
    DTEX_destroycount++;
    for (int i = 0; i < DTEX_ntypes; i++) memset(&DTEX_types[i], 0, sizeof DTEX_types[i]);
    DTEX_freepool();
    DTEX_ntypes = 0;
    DTEX_nobjects = 0;
    DTEX_uploaded = false;
}

void DTEX_touch(DtexObject *t)
{
    if (!t || !DTEX_uploaded) return;
    DtexType *ty = &DTEX_types[t->type];
    if (!t->handle) {
        /* No card memory: take the least recently used texture's of the same type. */
        DtexObject *victim = ty->oldest;
        call_hook(t, victim);
        t->handle = victim->handle;
        victim->handle = 0;
        DtexObject *after = victim->newer;
        if (!after) {
            ty->newest = ty->oldest = t;
            t->older = t->newer = NULL;
        } else {
            after->older = NULL;
            ty->oldest = after;
            t->older = ty->newest;
            ty->newest->newer = t;
            ty->newest = t;
            t->newer = NULL;
        }
        t->flags &= ~(uint32_t)DTEX_F_ONCARD;
    } else if (t->newer) {
        /* To the head of the upload order. */
        t->newer->older = t->older;
        if (!t->older) ty->oldest = t->newer;
        else t->older->newer = t->newer;
        t->older = ty->newest;
        ty->newest->newer = t;
        ty->newest = t;
        t->newer = NULL;
    }
    if (!(t->flags & DTEX_F_ONCARD)) {
        THRASH.fn.settexture(0);
        THRASH.fn.tupdate(t->handle, t->pixels, (t->flags & DTEX_F_CLUT) ? (const uint32_t *)(const void *)t->clut : NULL);
        t->flags |= DTEX_F_ONCARD;
    }
}

void DTEX_settexture(uintptr_t t)
{
    if (t > 0x10) {
        if (!DTEX_uploaded) {
            THRASH.fn.settexture(0);
            return;
        }
        DtexObject *o = (DtexObject *)t;
        DTEX_touch(o);
        THRASH.fn.settexture(o->handle);
    } else
        THRASH.fn.settexture(t);
    DTEX_current = t;
}

void DTEX_setprio(int pass, int prio, int width, int height, uint32_t format, uint32_t clutformat, uint32_t flags)
{
    if (width == 0 || width != height || (width & (width - 1)) || pass < 0 || pass >= 3) return;
    DTEX_choose(&format, &clutformat);
    DTEX_types[DTEX_create(width, format, clutformat, flags)].prio[pass] = prio;
}

uint8_t *DTEX_halve(const uint8_t *shape)
{
    int kind;
    switch (shape[0] & 0x7f) {
    case 0x03: case 0x42: case 0x50: case 0x68: case 0x7e: case 0x51: case 0x78: case 0x52: case 0x6d:
        kind = 2;
        break;
    case 0x05: case 0x6a: case 0x7d:
        kind = 4;
        break;
    default:
        kind = 0;
    }
    int w = (int16_t)(shape[4] | shape[5] << 8), h = (int16_t)(shape[6] | shape[7] << 8);
    uint8_t *out = SHAPE_create(w / 2, h / 2, SHAPE_typevalid(shape), 0);
    if (!out) return NULL;
    out[0] = shape[0];
    int ow = w / 2, oh = h / 2;
    if (!kind) {
        /* Other types aren't halved: the new shape's pixels stay as allocated (cleared in the port). */
        memset(out + SHAPE_HEADER, 0, (size_t)(SHAPE_rowbytes(out) * oh));
        return out;
    }
    int srow = (w + (w & 1)) * kind;   /* a source row, skipped every other row */
    const uint8_t *s = shape + SHAPE_HEADER;
    uint8_t *d = out + SHAPE_HEADER;
    for (int y = 0; y < oh; y++, s += srow)
        for (int x = 0; x < ow; x++, s += 2 * kind, d += kind) memcpy(d, s, (size_t)kind);
    /* The 32-bit case also copies one more word past the new shape's end in the original: left out. */
    return out;
}
