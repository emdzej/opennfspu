/* The sound library's banks (snd/snd_pt.c): every patch of the game's .bnk banks parses into layers
   whose samples lie inside the bank, at EA-XA's size (15 bytes for 28 samples a channel) or more.
   Skips (exit 0) without data. */
#include "exe.h"
#include "snd/snd.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>

static int fails, checks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static void bank(const char *name)
{
    size_t size = 0;
    uint8_t *b = vfs_read_all(name, &size);
    CHECK(b != NULL);
    if (!b) return;
    CHECK(b[0] == 'B' && b[1] == 'N' && b[2] == 'K' && b[3] == 'l');
    int n = b[6] | b[7] << 8, patches = 0, layers = 0, xa = 0;
    for (int i = 0; i < n; i++) {
        const uint8_t *pt = SND_BankPatch(b, i);
        if (!pt) continue;
        patches++;
        CHECK(pt[0] == 'P' && pt[1] == 'T');
        const uint8_t *p = pt + ((pt[3] & 2) ? 8 : 4);
        SndPatch s;
        bool more;
        do {
            more = SND_PatchParse(&p, &s);
            layers++;
            CHECK(s.channels >= 1 && s.channels <= 2);
            CHECK(s.rate >= 8000 && s.rate <= 48000);
            CHECK(s.nsamples > 0);
            CHECK(s.dataofs[0] > 0 && (size_t)s.dataofs[0] < size);
            if (s.codec == 10) xa++;
        } while (more);
    }
    printf("  %s: %d patches, %d layers, %d EA-XA\n", name, patches, layers, xa);
    CHECK(patches > 0 && xa == layers);
    free(b);
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("snd_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    bank("GameData/Sounds/SND_coll.bnk");
    bank("GameData/Sounds/fesfx.bnk");
    bank("GameData/Sounds/lilfesfx.bnk");
    bank("GameData/Sounds/LilSND_coll.bnk");
    printf("snd_test: %d checks: %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
