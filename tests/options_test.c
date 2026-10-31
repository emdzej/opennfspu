/* The options (game/options.c) against the user's fe.txt: keys case-insensitive, read by their text in
   the exe (Options_Parse's triples), changed in place. Skips (exit 0) without data. */
#include "exe.h"
#include "game/options.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    char err[256];
    if (!vfs_mount_default() || !vfs_exists("fe.txt") || !exe_init(err, sizeof err)) {
        printf("options_test: no game data, skipped\n");
        return 0;
    }
    CHECK(!strcmp(exe_str(0x5d618c), "MUSICLEVEL"));   /* the key of option 0x37 */
    CHECK(!strcmp(exe_str(0x5d5f58), "SPLITTIMES"));   /* option 0x63 */
    int music = Options_Int(0x5d618c, -1);
    CHECK(0 <= music && music <= 127);
    CHECK(Options_Get("MusicLevel", -2) == music);     /* any case */
    CHECK(Options_Get("no_such_option", 42) == 42);
    Options_SetInt(0x5d618c, 33);
    CHECK(Options_Int(0x5d618c, -1) == 33);
    CHECK(Options_Str("musiclevel") && !strcmp(Options_Str("musiclevel"), "33"));
    Options_SetInt(0x5d5f04, 1);                       /* GEAR: not in every fe.txt, added */
    CHECK(Options_Int(0x5d5f04, -1) == 1);
    printf("options_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
