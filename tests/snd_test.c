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

/* SND_coll's first patch played: sound comes, and ends with the sample (4133 samples at 16 kHz: about
   a quarter of a second). */
static void play(void)
{
    size_t size = 0;
    uint8_t *b = vfs_read_all("GameData/Sounds/SND_coll.bnk", &size);
    CHECK(SND_Init(32000, 2, 48, 100));
    int slot = SND_BankAdd(b);
    CHECK(slot == 0);
    SndParams p;
    SND_ParamsDefault(&p);
    int h = SND_Play(slot, 0, &p);
    CHECK(h >= 0);
    CHECK(SND_Playing(h));
    static int16_t out[32000 * 2];
    SND_Fill(out, 32000 / 4);
    if (getenv("SND_DUMP")) {   /* the first quarter second, raw 16-bit stereo at 32 kHz */
        FILE *f = fopen(getenv("SND_DUMP"), "wb");
        if (f) {
            fwrite(out, sizeof *out, 32000 / 4 * 2, f);
            fclose(f);
        }
    }
    long peak = 0, sum = 0;
    for (int i = 0; i < 32000 / 4 * 2; i++) {
        long a = out[i] < 0 ? -out[i] : out[i];
        if (a > peak) peak = a;
        sum += a;
    }
    printf("  SND_coll 0: peak %ld, mean %ld over 0.25 s\n", peak, sum / (32000 / 2));
    CHECK(peak > 1000 && peak < 32768);
    SND_Fill(out, 32000);
    CHECK(!SND_Playing(h));
    long tail = 0;
    for (int i = 32000; i < 64000; i++) tail += out[i] < 0 ? -out[i] : out[i];
    CHECK(tail == 0);
    /* With the reverb on (0x5650f0, the default preset) and group 0's send up (0x565150): a tail after
       the sample's end. */
    SND_SetFx(0x20, 0, 0x28, NULL);
    SND_SetGroupVolume(0x20, 100);
    h = SND_Play(slot, 0, &p);
    SND_Fill(out, 32000 / 4);
    SND_Fill(out, 32000 / 2);
    CHECK(!SND_Playing(h));
    long wet = 0, late = 0;
    for (int i = 0; i < 32000 / 2 * 2; i++) wet += out[i] < 0 ? -out[i] : out[i];
    for (int i = 32000 / 4 * 2; i < 32000 / 2 * 2; i++) late += out[i] < 0 ? -out[i] : out[i];
    printf("  with the reverb: %ld after the sample's end (%ld in its last quarter second)\n", wet, late);
    CHECK(wet > 0);
    long q[6];
    for (int k = 0; k < 6; k++) {   /* the tail falling away, quarter second by quarter second */
        SND_Fill(out, 32000 / 4);
        q[k] = 0;
        for (int i = 0; i < 32000 / 4 * 2; i++) q[k] += out[i] < 0 ? -out[i] : out[i];
        q[k] /= 32000 / 2;
    }
    printf("  its tail's means: %ld %ld %ld %ld %ld %ld\n", q[0], q[1], q[2], q[3], q[4], q[5]);
    CHECK(q[5] < q[0] / 4 + 1);
    SND_Shutdown();
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
    play();
    printf("snd_test: %d checks: %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
