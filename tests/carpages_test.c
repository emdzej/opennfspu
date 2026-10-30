/* The car texture pages (render/carpages.c, real/window.c, real/movdfl.c) against every car of the game:
   each car's pages composed for its default style and four fixed colours, every upload to the driver
   recorded (size, format, CRC of the pixels), the pages' sizes powers of two, and every texture reference
   of the car's materials resolved to a texture. Prints one line per car with the CRC of its uploads, so
   two data sources (the folder, a disc image) can be compared: they must print the same. Skips without
   data. */
#include "eagl_fake_thrash.h"
#include "exe.h"
#include "real/texture.h"
#include "render/carpages.h"
#include "render/r3dcar.h"
#include "render/rmaterial.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ---- a driver that records the uploads ---- */

enum { MAXTEX = 4096 };
static struct { int w, h, format; } texinfo[MAXTEX + 1];
static int ntex;
static uint32_t upload_crc;   /* over the current car's uploads, in order */
static int uploads;

/* Bytes of a w x h image in a THRASH format (thrash.h codes: 1 P4, 2 P8, 3 1555, 4 565, 6 8888, 7 4444,
   12 DXT1, 13 DXT3). */
static size_t image_bytes(int w, int h, int format)
{
    switch (format) {
    case 1: return (size_t)w * h / 2;
    case 2: return (size_t)w * h;
    case 3: case 4: case 7: return (size_t)w * h * 2;
    case 6: return (size_t)w * h * 4;
    case 12: return (size_t)((w + 3) / 4) * ((h + 3) / 4) * 8;
    case 13: return (size_t)((w + 3) / 4) * ((h + 3) / 4) * 16;
    default: return 0;
    }
}

static ThrashTexture rec_talloc(int width, int height, int format, int clut, uint32_t mipstage)
{
    (void)clut; (void)mipstage;
    if (ntex >= MAXTEX) return 0;
    ntex++;
    texinfo[ntex].w = width;
    texinfo[ntex].h = height;
    texinfo[ntex].format = format;
    return (ThrashTexture)(uintptr_t)ntex;
}

static ThrashTexture rec_tupdate(ThrashTexture t, const void *pixels, const uint32_t *clut)
{
    (void)clut;
    uintptr_t i = (uintptr_t)t;
    if (i >= 1 && i <= (uintptr_t)ntex && pixels) {
        size_t n = image_bytes(texinfo[i].w, texinfo[i].h, texinfo[i].format);
        uint32_t hdr[3] = {(uint32_t)texinfo[i].w, (uint32_t)texinfo[i].h, (uint32_t)texinfo[i].format};
        upload_crc = crc32(hdr, sizeof hdr) ^ (upload_crc * 31u);
        upload_crc ^= crc32(pixels, n);
        uploads++;
    }
    return t;
}

/* ---- every car ---- */

static char names[128][64];
static int nnames;

static void collect(const char *name, bool is_dir, void *user)
{
    size_t l = strlen(name);
    if (is_dir || l < 5 || strcasecmp(name + l - 4, ".crp") || nnames >= 128) return;
    snprintf(names[nnames], sizeof names[nnames], "%.*s", (int)(l - 4), name);
    nnames++;
}

static int cmp_names(const void *a, const void *b) { return strcasecmp(a, b); }

static uint32_t total_crc;
static int total_pages, total_refs, cars;

static void check_car(const char *base)
{
    static R3DCarState state;
    static R3DCarSpec spec;
    static R3DCar car;
    memset(&state, 0, sizeof state);
    memset(&spec, 0, sizeof spec);
    state.rot[0] = state.rot[4] = state.rot[8] = 1;
    spec.colours[0] = 0xffb8bcc0u;
    spec.colours[1] = 0xff202020u;
    spec.colours[2] = 0xff303030u;
    spec.colours[3] = 0xffc0c0c0u;
    spec.racenumber = -1;
    for (int i = 0; i < CAR_NOPTIONS; i++) spec.options[i] = -1;
    state.spec = &spec;
    R3DCar_Init(&car, &state);
    R3DCar_SetColours(&car, 0);
    char path[96];
    snprintf(path, sizeof path, "GameData\\CarModel\\%s", base);
    upload_crc = 0;
    uploads = 0;
    CHECK(R3DCar_LoadCrp(&car, path));
    if (!car.loaded) return;
    /* The textures reach the driver when the manager goes live for a frame (TexManager_begin 0x535dc0). */
    TexManager *tm = g_texgraphics->textures;
    TexManager_begin(tm);
    TexManager_end(tm);
    for (uint32_t i = 0; i < car.npages; i++) {
        const CarPage *p = &car.pages[i];
        CHECK(p->texwidth > 0 && (p->texwidth & (p->texwidth - 1)) == 0);
        CHECK(p->texheight > 0 && (p->texheight & (p->texheight - 1)) == 0);
        CHECK(p->shared || p->texture != NULL);
    }
    /* Every texture reference (info 0x54) of the materials found its texture. */
    int refs = 0, missing = 0;
    const R3DMaterialSet *ms = car.materials;
    for (uint32_t m = 0; ms && m < ms->count; m++) {
        const R3DMaterial *mat = ms->mat[m];
        if (!mat) continue;
        for (uint32_t r = 0; r < mat->nrefs; r++)
            if (mat->refs[r].info == 0x54) {
                refs++;
                if (!mat->refs[r].data) missing++;
            }
    }
    CHECK(missing == 0);
    if (missing) printf("  %s: %d of %d texture references unresolved\n", base, missing, refs);
    printf("%-16s pages %2u uploads %3d crc %08x\n", base, car.npages, uploads, upload_crc);
    total_crc = total_crc * 33u ^ upload_crc;
    total_pages += (int)car.npages;
    total_refs += refs;
    cars++;
    R3DCar_Free(&car);
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) {
        printf("carpages_test: no game data, skipped\n");
        return 0;
    }
    char err[256];
    if (!exe_init(err, sizeof err)) {
        printf("carpages_test: %s\n", err);
        return 1;
    }
    fake_install_graphics();
    THRASH.fn.talloc = rec_talloc;
    THRASH.fn.tupdate = rec_tupdate;
    R3DCar_LoadCommon();
    vfs_list("GameData/CarModel", collect, NULL);
    qsort(names, (size_t)nnames, sizeof names[0], cmp_names);
    for (int i = 0; i < nnames; i++) check_car(names[i]);
    CHECK(cars > 40);
    printf("carpages_test: %d cars, %d pages, %d texture references, crc %08x, %d checks: %s\n", cars,
           total_pages, total_refs, total_crc, checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
