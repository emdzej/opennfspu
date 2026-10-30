/* The sky layer descriptions (render/sky_text.c) of every track at every sky level: blocks found, their
   types known, cursors inside the file. Skips (exit 0) without data. */
#include "exe.h"
#include "game/track.h"
#include "render/sky_text.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <string.h>

static int fails, files, records, types[0x1000];

static void check(const char *name)
{
    for (int level = 0; level < 4; level++) {
        SkyText t;
        Sky_LoadText(&t, name, level);
        if (!t.buf) { printf("FAIL %s: no sky text\n", name); fails++; return; }
        for (int i = 0; i < t.count; i++) {
            SkyRecord *r = SkyText_record(&t, i);
            if (r->start < t.buf || r->start >= t.end) { printf("FAIL %s level %d record %d out of the file\n", name, level, i); fails++; }
            if (r->flags == SKYREC_UNKNOWN) { printf("FAIL %s level %d record %d: unknown type\n", name, level, i); fails++; }
            types[r->flags & 0xfff]++;
            records++;
        }
        if (level == 3) printf("%-10s amazing: %d layers\n", name, t.count);
        SkyText_free(&t);
    }
    files++;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("sky_text_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    static const char *names[] = {"alps", "autobahn", "canyon", "castle", "coastal", "farmland", "foothills", "forest",
                                  "industrial", "monaco", "monaco1", "skidpad", "nosuchtrack"};
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) check(names[i]);
    printf("types:");
    for (int f = 0; f < 0x1000; f++) if (types[f]) printf(" %#x:%d", f, types[f]);
    printf("\nsky_text_test: %d files, %d records: %s\n", files, records, fails ? "FAILED" : "ok");
    return fails != 0;
}
