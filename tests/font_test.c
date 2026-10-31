/* The real library's fonts (real/font.c) over every font file of the game: each opens, finds its
   glyphs, lays out text (one quad a visible character, the pen advancing by the glyphs' advances), measures
   it, and draws it through the DTEX driver with texture coordinates inside its pages. Skips (exit 0)
   without data. */
#include "eagl_fake_thrash.h"
#include "exe.h"
#include "real/font.h"
#include "vfs.h"
#include "vfs_host.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static int drawn_quads, bad_uv;
static void capture(int count, const void *verts, const int *idx)
{
    const float *v = verts;
    for (int q = 0; q < count; q++)
        for (int k = 0; k < 4; k++) {
            const float *p = v + 8 * idx[4 * q + k];
            if (p[6] < -1e-4f || p[6] > 1.0001f || p[7] < -1e-4f || p[7] > 1.0001f) bad_uv++;
        }
    drawn_quads += count;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("font_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    fake_install_graphics();
    THRASH.fn.drawquadmesh = capture;
    static const char *files[] = {"GameData\\DashHud\\instrument.ffn", "GameData\\DashHud\\UniversC12b.ffn",
                                  "GameData\\DashHud\\unviers9b.ffn", "GameData\\DashHud\\unviers9bi.ffn",
                                  "GameData\\DashHud\\debug.ffn", "GameData\\CarModel\\license.ffn",
                                  "FEData\\Art\\Univers14b.ffn", "FEData\\Art\\Univers45b.FFN", "FEData\\Art\\UniversC11b.ffn"};
    for (size_t i = 0; i < sizeof files / sizeof *files; i++) {
        size_t n = 0;
        uint8_t *d = vfs_read_all(files[i], &n);
        if (!d) { printf("  %s: missing\n", files[i]); continue; }
        CHECK(memcmp(d, "FNTF", 4) == 0);
        Font *f = FONT_open(d);
        CHECK(f != NULL);
        const char *text = "LAP 2/3";
        int visible = 0;
        for (const char *p = text; *p; p++) visible += *p != ' ' && FONT_glyph(f, (uint8_t)*p) != NULL;
        float *v = malloc(sizeof *v * 8 * 4 * strlen(text));
        float endx = 0, minx = 0;
        int nv = FONT_build(f, v, 10, 20, text, &endx, &minx);
        float adv = 0;
        for (const char *p = text; *p; p++) {
            const uint8_t *g = FONT_glyph(f, (uint8_t)*p);
            if (g) adv += (int8_t)g[8];
        }
        CHECK(nv == 4 * visible);
        CHECK(fabsf(endx - (10 + f->penx + adv)) < 1e-3f);
        float ex, ey, ew, eh;
        FONT_extent(f, text, &ex, &ey, &ew, &eh);
        drawn_quads = bad_uv = 0;
        FONT_print(f, 10, 20, text);
        printf("  %-34s %3d glyphs, line %2d (ascent %d), \"%s\": %d quads, extent %.0f x %.0f, drew %d, %d pages\n", files[i],
               f->nglyphs, f->lineheight, f->ascent, text, nv / 4, ew, eh, drawn_quads, FONT_pages(f));
        CHECK(ew > 0 && eh > 0);
        CHECK(drawn_quads == visible && bad_uv == 0);
        free(v);
        FONT_close(f);
        free(d);
    }
    printf("font_test: %d checks: %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
