/* BIG archive directories (real/locatbig.c) against every .viv of the game: count, records in bounds,
   lookup by name (any case) finds the same record as by index. Skips (exit 0) without data. */
#include "real/locatbig.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int fails, archives, entries;

static void check(const char *path)
{
    size_t n = 0;
    uint8_t *d = vfs_read_all(path, &n);
    if (!d) { printf("FAIL read %s\n", path); fails++; return; }
    if (BIG_kind(d) == BIG_NONE) { printf("FAIL %s: not a BIG archive\n", path); fails++; free(d); return; }
    uint32_t count = BIG_count(d);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off, size, off2, size2;
        const char *name = BIG_locateentry(d, NULL, (int)i, &off, &size);
        if (!name || (uint64_t)off + size > n) { printf("FAIL %s #%u out of bounds\n", path, i); fails++; break; }
        char upper[256];
        size_t k = 0;
        for (; name[k] && k < sizeof upper - 1; k++) upper[k] = (char)(name[k] >= 'a' && name[k] <= 'z' ? name[k] - 32 : name[k]);
        upper[k] = 0;
        const char *again = BIG_locateentry(d, upper, 0, &off2, &size2);
        /* Duplicate names resolve to the first record, as in the original's scan. */
        if (!again || (again == name && (off2 != off || size2 != size))) { printf("FAIL %s: %s by name\n", path, name); fails++; }
        entries++;
    }
    if (BIG_locateentry(d, "no such entry", 0, NULL, NULL)) { printf("FAIL %s: phantom entry\n", path); fails++; }
    archives++;
    free(d);
}

static char dirs[64][256];
static int ndirs;
static char vivs[256][256];
static int nvivs;
static const char *cur;

static void visit(const char *name, bool is_dir, void *user)
{
    char p[256];
    snprintf(p, sizeof p, "%s%s%s", cur, *cur ? "/" : "", name);
    size_t l = strlen(p);
    if (is_dir) { if (ndirs < 64) snprintf(dirs[ndirs++], 256, "%s", p); }
    else if (l > 4 && !strcasecmp(p + l - 4, ".viv") && nvivs < 256) snprintf(vivs[nvivs++], 256, "%s", p);
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("big_test: no game data, skipped\n"); return 0; }
    snprintf(dirs[ndirs++], 256, "%s", "");
    for (int i = 0; i < ndirs; i++) { cur = dirs[i]; vfs_list(cur, visit, NULL); }
    for (int i = 0; i < nvivs; i++) check(vivs[i]);
    printf("big_test: %d archives, %d entries: %s\n", archives, entries, fails ? "FAILED" : "ok");
    return fails != 0 || archives == 0;
}
