/* The MAD decoder (real/mad.c) on every movie in FEData\Movies: each video chunk decodes (key frames
   from nothing, the others from the last reference), the frames move (consecutive frames differ), the
   2x display fills a 640 x 480 16-bit window. MAD_TEST_DUMP=<dir> writes <movie>-<frame>.ppm of the
   frames 0, 30, 60, ... of each. Skips (exit 0) without data. */
#include "exe.h"
#include "real/mad.h"
#include "real/window.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

typedef struct { char n[32][64]; int k; } Names;
static void collect(const char *name, bool is_dir, void *user)
{
    Names *l = user;
    size_t len = strlen(name);
    if (!is_dir && len > 4 && !strcasecmp(name + len - 4, ".mad") && l->k < 32) snprintf(l->n[l->k++], 64, "%s", name);
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void dump(const char *dir, const char *movie, int frame, const Window *w)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%.*s-%03d.ppm", dir, (int)(strlen(movie) - 4), movie, frame);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w->width, w->height);
    for (int y = 0; y < w->height; y++)
        for (int x = 0; x < w->width; x++) {
            uint16_t v;
            memcpy(&v, w->pixels + y * w->stride + x * 2, 2);
            uint8_t rgb[3] = {(uint8_t)((v >> 11) * 255 / 31), (uint8_t)((v >> 5 & 63) * 255 / 63), (uint8_t)((v & 31) * 255 / 31)};
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("mad_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("%s\n", err); return 1; }
    const char *dumpdir = getenv("MAD_TEST_DUMP");
    static Names l;
    vfs_list("FEData\\Movies", collect, &l);
    CHECK(l.k >= 12);
    Window *win = WINDOW_new(640, 480, 16, 0);
    CHECK(win != NULL);
    if (!win) return 1;
    for (int i = 0; i < l.k; i++) {
        char path[128];
        snprintf(path, sizeof path, "FEData\\Movies\\%s", l.n[i]);
        size_t n = 0;
        uint8_t *d = vfs_read_all(path, &n);
        CHECK(d != NULL);
        if (!d) continue;
        int frames = 0, keys = 0, moved = 0, w = 0, h = 0;
        uint8_t *buf[2] = {NULL, NULL}, *ref = NULL;
        int cur = 0;
        for (size_t p = 0; p + 8 <= n;) {
            const uint8_t *c = d + p;
            uint32_t sz = rd32(c + 4);
            if (sz < 8 || sz > n - p) break;
            p += sz;
            if (memcmp(c, "MAD", 3) != 0) continue;
            if (!w) {
                w = c[0x10] | c[0x11] << 8;
                h = c[0x12] | c[0x13] << 8;
                CHECK(w % 16 == 0 && h % 16 == 0 && w <= 320 && h <= 240);
                buf[0] = calloc(1, (size_t)w * h * 2 + (size_t)w * 160);
                buf[1] = calloc(1, (size_t)w * h * 2 + (size_t)w * 160);
            }
            if (c[3] == 'k') keys++;
            CHECK(frames > 0 || c[3] == 'k');
            uint8_t *dest = buf[cur] + w * 40;
            MAD_decode(c, sz, ref, dest);
            if (ref && memcmp(ref, dest, (size_t)w * h * 2)) moved++;
            if (c[3] != 'e') {
                ref = dest;
                cur ^= 1;
            }
            if (frames % 30 == 0) {
                WINDOW_select(win);
                memset(win->pixels, 0, (size_t)win->stride * win->height);
                CHECK(MAD_blit(0, 0, dest, w, h, 3) == 0);
                if (dumpdir) dump(dumpdir, l.n[i], frames, win);
                WINDOW_deselect();
            }
            frames++;
        }
        printf("%s: %d frames, %d key, %d moved\n", l.n[i], frames, keys, moved);
        CHECK(frames > 10 && keys >= 1 && moved > frames / 4);
        free(buf[0]);
        free(buf[1]);
        free(d);
    }
    printf("mad_test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
