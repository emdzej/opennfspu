/* Localised strings (real/loc.c) from hud.loc: every language table resolves the HUD's ids, the gear
   letters among them (0x46e1, 0x46e2). Skips (exit 0) without data. */
#include "real/loc.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("loc_test: no game data, skipped\n"); return 0; }
    size_t n = 0;
    uint8_t *loc = vfs_read_all("GameData\\DashHud\\hud.loc", &n);
    CHECK(loc && memcmp(loc, "LOCH", 4) == 0);
    if (!loc) return 1;
    uint16_t nlang;
    memcpy(&nlang, loc + 0xc, 2);
    for (int l = 0; l < nlang; l++) {
        LOC_setlanguage(loc, 0, l);
        const char *r = LOC_string(loc, 0x46e1), *nn = LOC_string(loc, 0x46e2);
        printf("  language %d: reverse \"%s\", neutral \"%s\"\n", l, r ? r : "(none)", nn ? nn : "(none)");
        CHECK(r && nn && *r && *nn);
    }
    free(loc);
    printf("loc_test: %d checks: %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
