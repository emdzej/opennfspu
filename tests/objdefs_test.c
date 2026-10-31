/* The track's object definitions (game/objdefs.c) from GameData\Track\animdefs.txt: every SECTION:ANIM
   read, the collision kinds counted, a few definitions checked against the file. Skips (exit 0) without
   data. */
#include "exe.h"
#include "game/objdefs.h"
#include "vfs.h"
#include "vfs_host.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static uint32_t tag(const char *s)
{
    uint8_t t[4] = {0};
    for (int i = 0; i < 4 && s[i]; i++) t[i] = (uint8_t)s[i];
    uint32_t v;
    memcpy(&v, t, 4);
    return v;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("objdefs_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    WMidGround_LoadObjectDefs("GameData\\Track\\animdefs.txt");
    int n = WObjectDefs_count(), kinds[4] = {0};
    for (int i = 0; i < n; i++) kinds[WObjectDefs_at(i)->collidetype & 3]++;
    printf("  %d definitions: %d cylinders, %d boxes, %d smackable, %d without\n", n, kinds[1], kinds[2], kinds[3], kinds[0]);
    /* As the file's collideType lines say (kCylinder 12, kBox 4, kSmackable 37). */
    CHECK(kinds[1] == 12 && kinds[2] == 4 && kinds[3] == 37);
    const WObjectDef *endw = WObjectDefs_find(tag("ENDW")), *sign = WObjectDefs_find(tag("SIGN")), *gas = WObjectDefs_find(tag("GAS"));
    CHECK(endw && endw->tag == tag("ENDW") && endw->collidetype == 2);
    CHECK(sign && sign->collidetype == 3 && sign->audiotype == 1 && fabsf(sign->mass - 80) < 1e-4f && fabsf(sign->radius - 0.5f) < 1e-6f);
    CHECK(gas && gas->tag == tag("GAS") && gas->collidetype == 3 && fabsf(gas->mass - 90) < 1e-4f);
    CHECK(WObjectDefs_find(tag("ZZZZ")) == WObjectDefs_at(0));   /* an unknown type: the first */
    CHECK(WObjectDefs_find(0) == NULL);
    CHECK(n == 64);
    /* The audio section: TRAN's sound (PatchID 833, Range 300, FadeIn 17, Volume 100). */
    uint32_t tran;
    memcpy(&tran, "TRAN", 4);
    const WAudioDef *a = WObjectDefs_findaudio(tran);
    CHECK(a && a->n == 1 && a->sound[0].patch == 833 && a->sound[0].range == 300.0f && a->sound[0].fadein == 17.0f &&
          a->sound[0].volume == 100);
    printf("objdefs_test: %d checks: %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
