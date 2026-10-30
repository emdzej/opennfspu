/* File layer against the user's data: the installed folder or a disc image of it (OPENNFSPU_DATA). Checks
   the long mixed-case names, a file read end to end, listing, and the exe check. Skips (exit 0) without
   data. */
#include "exe.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void count(const char *name, bool is_dir, void *user) { (void)name; (void)is_dir; ++*(int *)user; }

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("vfs_test: no game data, skipped\n"); return 0; }
    printf("mounted: %s\n", vfs_describe());
    CHECK(vfs_exists("GameData/CarModel/cop356_germanD.fsh"));      /* longer than 8.3 */
    CHECK(vfs_exists("gamedata\\carmodel\\COP356_GERMAND.FSH"));     /* any case, either separator */
    CHECK(!vfs_exists("GameData/CarModel"));                         /* a directory is not a file */
    int64_t sz = vfs_size("GameData/Track/alps.crp");
    CHECK(sz > 0);
    size_t n = 0;
    uint8_t *d = vfs_read_all("GameData/Track/alps.crp", &n);
    CHECK(d && (int64_t)n == sz);
    if (d) printf("alps.crp: %zu bytes, crc %08x\n", n, crc32(d, n));
    free(d);
    int k = 0;
    CHECK(vfs_list("GameData/CarModel", count, &k) && k > 50);
    char err[256];
    bool ok = exe_init(err, sizeof err);
    CHECK(ok);
    if (!ok) printf("%s\n", err);
    printf("vfs_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
