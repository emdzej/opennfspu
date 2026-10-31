/* The player's save (front/fesave.c) against the game's default player, SaveData\XXDefXX.sav: every
   section read to the file's end, the player's money, and the bytes written again the same. Skips
   (exit 0) without data. */
#include "exe.h"
#include "front/fesave.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    char err[256];
    if (!vfs_mount_default() || !vfs_exists("SaveData/XXDefXX.sav") || !exe_init(err, sizeof err)) {
        printf("save_test: no game data, skipped\n");
        return 0;
    }
    size_t n = 0;
    uint8_t *d = vfs_read_all("SaveData/XXDefXX.sav", &n);
    Save s;
    bool ok = Save_Parse(&s, d, n);
    CHECK(ok);
    if (ok) {
        printf("sections %d, cars %d, money %d, used cars %d\n", s.nsec, s.ncars, s.money, s.nused);
        CHECK(s.nsec == 10);
        CHECK(!strcmp(s.sec[0].name, "Missions") && !strcmp(s.sec[s.nsec - 1].name, "accessor"));
        CHECK(s.money == 11000 && s.ncars == 0);
        size_t m = 0;
        uint8_t *b = Save_Serialize(&s, &m);
        CHECK(b && m == n && !memcmp(b, d, n));   /* the same bytes back */
        free(b);
        Save_Free(&s);
    }
    free(d);
    printf("save_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
