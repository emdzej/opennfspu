/* The sky's configuration (render/sky.c) for every sky in GameData\Track\Sky: loads, ends at version 9,
   the version-7 files (default, Monaco) get the version-8/9 fields, weather slots resolve inside the
   block, the texture file loads. Skips (exit 0) without data. */
#include "exe.h"
#include "game/track.h"
#include "real/mem.h"
#include "render/sky.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

typedef struct { char n[64][64]; int k; } Names;
static void collect(const char *name, bool is_dir, void *user)
{
    Names *l = user;
    size_t len = strlen(name);
    if (!is_dir && len > 4 && !strcasecmp(name + len - 4, ".bin") && l->k < 64 && !strcasestr(name, "lightning"))
        snprintf(l->n[l->k++], 64, "%.*s", (int)(len - 4), name);
}

static uint32_t word(const uint8_t *b, int i) { uint32_t v; memcpy(&v, b + i * 4, 4); return v; }

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("sky_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("%s\n", err); return 1; }
    char dir[260];
    snprintf(dir, sizeof dir, "%sSky", g_track_dir);
    static Names l;
    vfs_list(dir, collect, &l);
    CHECK(l.k > 10);
    for (int i = 0; i < l.k; i++) {
        char path[300];
        snprintf(path, sizeof path, "%s\\%s.bin", dir, l.n[i]);
        size_t n = 0;
        uint8_t *raw = vfs_read_all(path, &n);
        int32_t ver = -1;
        if (raw && n >= 4) memcpy(&ver, raw, 4);
        SkyConfig *c = Sky_CreateConfig(l.n[i]);
        CHECK(c && c->data);
        if (!c || !c->data) continue;
        CHECK(MEM_size(c->data) == SKY_CONFIG_SIZE);
        CHECK(word(c->data, 0) == 9);
        if (ver == 9) CHECK(raw && n == SKY_CONFIG_SIZE && !memcmp(raw, c->data, n));   /* taken as it is */
        if (ver == 7 && raw) {
            CHECK(!memcmp(raw + 4, c->data + 4, n - 4));      /* the old contents kept */
            CHECK(word(c->data, 0x376) == 1);                  /* version 8's field */
            CHECK(!memcmp(c->data + 0x377 * 4, c->data + 0x13 * 4, 400));   /* version 9's copies */
            CHECK(!memcmp(c->data + 0x697 * 4, c->data + 0x77 * 4, 2400));
            CHECK(!memcmp(c->data + 0x1957 * 4, c->data + 0x2d3 * 4, 400));
        }
        int set = 0;
        for (int wthr = 0; wthr < 4; wthr++) {
            int s = Sky_SlotFor(wthr);
            CHECK(s >= 0 && s <= wthr);
            Sky_CreateLight(wthr);
            const uint8_t *slot = Sky_LightSlot();
            CHECK(slot == c->data + SKY_SLOT_BASE + s * SKY_SLOT_SIZE);
            CHECK(slot + SKY_SLOT_SIZE <= c->data + SKY_CONFIG_SIZE);
            set += s == wthr;
        }
        Sky_FreeLight();
        Sky_LoadFsh(l.n[i], i > 0);
        CHECK(Sky_Fsh() != NULL && !memcmp(Sky_Fsh(), "SHPI", 4));
        printf("  %-12s v%d -> 9, %d of 4 weather slots set\n", l.n[i], ver, set);
        free(raw);
    }
    Sky_FreeConfig();
    printf("sky_test: %d skies, %d checks: %s\n", l.k, checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
