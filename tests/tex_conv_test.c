/* The texture path against every FSH image of the game: SHAPE_converttype to ARGB8888 (depthconv.c) and
   textures made through the texture manager and uploaded by DTEX (format choice, conversion, talloc and
   tupdate) into a recording THRASH that reports the GL driver's formats. Always: every image converts,
   every DTEX upload has the size and format DTEX chose (packed records: no format). With OPENNFSPU_TEXREF=<file> (tests/tex_ref.py)
   the pixels are compared with the reference decoder. Skips (exit 0) without data. */
#include "exe.h"
#include "real/depthconv.h"
#include "real/dtex.h"
#include "real/mem.h"
#include "real/set3d.h"
#include "real/shape.h"
#include "real/texture.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int fails, checked_img, checked_tex, images, uploads;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ---- a THRASH driver with the GL driver's texture formats, recording uploads ---- */

static uint32_t fake_texformats[16], fake_cluts[4];
static ThrashMode fake_modes[2];
static ThrashAbout fake_about;
typedef struct { int w, h, format, clut; uint32_t flags, crc; bool updated; } FakeTex;
static FakeTex fake_tex[8192];
static int fake_ntex;

static const ThrashAbout *f_about(void)
{
    if (!fake_about.size) {
        static const int ok[] = {1, 2, 3, 4, 6, 7, 12, 13};
        for (size_t i = 0; i < sizeof ok / sizeof *ok; i++) fake_texformats[ok[i]] = 1;
        fake_about.signature = 0x44334437;
        fake_about.size = 0xd0;
        fake_about.ntexformats = 0x10;
        fake_about.texformats = fake_texformats;
        fake_about.ncluts = 4;
        fake_about.cluts = fake_cluts;
        fake_about.nmodes = 1;
        fake_about.modes = fake_modes;
    }
    return &fake_about;
}

static uintptr_t f_setstate(uint32_t s, uintptr_t v) { return 1; }
static intptr_t f_getstate(uint32_t s) { return 0; }
static int f_settexture(ThrashTexture t) { return 1; }
static int f_treset(void) { fake_ntex = 0; return 1; }

static ThrashTexture f_talloc(int w, int h, int format, int clut, uint32_t flags)
{
    if (fake_ntex >= 8191) return 0;
    fake_tex[++fake_ntex] = (FakeTex){w, h, format, clut, flags, 0, false};
    return (ThrashTexture)fake_ntex;
}

static int bpp_of(int f) { return f == 6 ? 32 : f == 5 ? 24 : (f == 2 || f == 13) ? 8 : (f == 1 || f == 12) ? 4 : 16; }

static ThrashTexture f_tupdate(ThrashTexture t, const void *pixels, const uint32_t *clut)
{
    FakeTex *x = &fake_tex[t];
    x->crc = crc32(pixels, (size_t)(x->w * x->h * bpp_of(x->format) / 8));
    x->updated = true;
    return t;
}

static void install_fake(void)
{
    memset(&THRASH, 0, sizeof THRASH);
    THRASH.fn.about = f_about;
    THRASH.fn.setstate = f_setstate;
    THRASH.fn.getstate = f_getstate;
    THRASH.fn.settexture = f_settexture;
    THRASH.fn.talloc = f_talloc;
    THRASH.fn.tupdate = f_tupdate;
    THRASH.fn.treset = f_treset;
}

/* ---- the reference ---- */

typedef struct { char kind; char path[96]; int index, w, h; uint32_t crc; } Ref;
static Ref *refs;
static int nrefs;

static void load_refs(const char *file)
{
    FILE *f = fopen(file, "r");
    if (!f) return;
    char line[256], kind[8];
    Ref r;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "%7s\t%95[^\t]\t%d\t%d\t%d\t%x", kind, r.path, &r.index, &r.w, &r.h, &r.crc) == 6) {
            r.kind = kind[0];
            refs = realloc(refs, (size_t)(nrefs + 1) * sizeof *refs);
            refs[nrefs++] = r;
        }
    fclose(f);
}

static const Ref *find_ref(char kind, const char *path, int index)
{
    for (int i = 0; i < nrefs; i++)
        if (refs[i].kind == kind && refs[i].index == index && !strcasecmp(refs[i].path, path)) return &refs[i];
    return NULL;
}

/* ---- the walk ---- */

static char files[1024][128];
static int nfiles;
static char dirs[256][128];
static int ndirs;
static const char *curdir;

static void visit(const char *name, bool is_dir, void *user)
{
    char p[128];
    snprintf(p, sizeof p, "%s%s%s", curdir, *curdir ? "/" : "", name);
    size_t l = strlen(p);
    if (is_dir) {
        if (ndirs < 256) snprintf(dirs[ndirs++], 128, "%s", p);
    } else if (l > 4 && !strcasecmp(p + l - 4, ".fsh") && nfiles < 1024)
        snprintf(files[nfiles++], 128, "%s", p);
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static void check_file(const char *path, TexManager *m)
{
    uint8_t *shp = SHAPE_loadfile(path, 0, 0);
    if (!shp) return;   /* not a shape file (some .fsh are empty or other data) */
    Texture *made[1024];
    int idx[1024], nmade = 0;
    for (uint32_t i = 0; i < SHAPE_count(shp); i++) {
        const uint8_t *rec = SHAPE_entry(shp, (int)i);
        uint32_t t = rec[0] & 0x7f;
        if (t != 0x7b && t != 0x7d && t != 0x78 && t != 0x7e && t != 0x6d && t != 0x7f) continue;
        if (t == 0x7b) {
            const uint8_t *clut = SHAPE_clut(rec);
            if (!clut || (clut[0] != 0x2a && clut[0] != 0x29 && clut[0] != 0x2d)) continue;
        }
        int w = (int16_t)rd16(rec + 4), h = (int16_t)rd16(rec + 6);
        memset(DEPTH_clutbuf, 0, sizeof DEPTH_clutbuf);
        uint8_t *argb = SHAPE_converttype(rec, 0x7d, 0, 0);
        CHECK(argb && argb[0] == 0x7d && (int16_t)rd16(argb + 4) == w);
        images++;
        const Ref *r = find_ref('i', path, (int)i);
        if (argb && r) {
            uint32_t c = crc32(argb + 0x10, (size_t)(w * h * 4));
            if (c != r->crc) printf("FAIL img %s #%u %dx%d type %02x: %08x, reference %08x\n", path, i, w, h, rec[0], c, r->crc);
            fails += c != r->crc;
            checked_img++;
        }
        MEM_purge(argb);
        /* Packed records have no texture format (0x5563f0 knows only DXT among them): front-end art, drawn
           in 2D, never a texture. */
        if (rec[0] & 0x80) {
            CHECK(TEXTURE_formatof(rec) == 0);
            continue;
        }
        if (w == h && w && !(w & (w - 1)) && nmade < 1024 && t != 0x7f) {
            idx[nmade] = (int)i;
            made[nmade++] = m->vt->create(m, rec, 0, NULL);
        }
    }
    /* Upload all (as at the start of a race), then compare what the driver got. */
    if (nmade) {
        fake_ntex = 0;
        TexManager_begin(m);
        for (int k = 0; k < nmade; k++) {
            DtexObject *d = made[k]->dtex;
            CHECK(d && d->handle);
            if (!d || !d->handle) continue;
            const FakeTex *x = &fake_tex[d->handle];
            uploads++;
            CHECK(x->updated && x->w == made[k]->width && x->format == (int)DTEX_types[d->type].format);
            const uint8_t *rec = SHAPE_entry(shp, idx[k]);
            int want = (rec[0] & 0x7f) == 0x7b ? 7 : (rec[0] & 0x7f) == 0x7d ? 6 : (rec[0] & 0x7f) == 0x78 ? 4
                     : (rec[0] & 0x7f) == 0x7e ? 3 : 7;
            CHECK(x->format == want);
            const Ref *r = find_ref('t', path, idx[k]);
            if (r) {
                if (x->crc != r->crc) printf("FAIL tex %s #%d format %d: %08x, reference %08x\n", path, idx[k], x->format, x->crc, r->crc);
                fails += x->crc != r->crc;
                checked_tex++;
            }
        }
        TexManager_end(m);
        for (int k = 0; k < nmade; k++) TexManager_release(m, made[k]);
        DTEX_destroyall();
    }
    MEM_purge(shp);
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("tex_conv_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    const char *ref = getenv("OPENNFSPU_TEXREF");
    if (ref && *ref) load_refs(ref);
    install_fake();
    TEXTURE_initstates();
    static TexGraphics gfx;
    TexGraphics_register(&gfx);
    TexGraphics_setdevice(&gfx, NULL);
    snprintf(dirs[ndirs++], 128, "%s", "");
    for (int i = 0; i < ndirs; i++) {
        curdir = dirs[i];
        vfs_list(curdir, visit, NULL);
    }
    for (int i = 0; i < nfiles; i++) check_file(files[i], gfx.textures);
    printf("tex_conv_test: %d files, %d images, %d uploads; against the reference %d images, %d textures (%d refs): %s\n",
           nfiles, images, uploads, checked_img, checked_tex, nrefs, fails ? "FAILED" : "ok");
    TexGraphics_unregister(&gfx);
    return fails != 0 || images == 0;
}
