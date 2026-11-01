/* The players (front/fesave.c, Save_Load 0x4de060, 0x4de460, Save_ListPlayers 0x4dea10, 0x4de350) and
   the cheat codes typed as a name (front/feaccess.c, 0x4d8130). The user files are the host's in memory,
   empty at the start. Skips (exit 0) without data. */
#include "exe.h"
#include "front/feaccess.h"
#include "front/fesave.h"
#include "platform.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static bool user_file(const char *name)
{
    size_t n = 0;
    uint8_t *d = plat_load_user_file(name, &n);
    free(d);
    return d != NULL;
}

int main(void)
{
    char err[256];
    if (!vfs_mount_default() || !vfs_exists("SaveData/XXDefXX.sav") || !exe_init(err, sizeof err)) {
        printf("player_test: no game data, skipped\n");
        return 0;
    }
    FEData_Init();
    /* The game's player.lst names a player with no save: the template's data, no player. */
    Save *sv = FESave_Current();
    CHECK(sv && !FESave_HavePlayer() && sv->player[0] == 0);
    CHECK(FESave_PlayerCount() == 0 && !FESave_Write());
    /* A new player: its save under its picture, its HUD layouts, then the current one. */
    CHECK(FESave_Create("Bob", 3));
    sv = FESave_Current();
    CHECK(FESave_HavePlayer() && !strcmp(FESave_Stem(), "3Bob"));
    CHECK(!strcmp(sv->player, "Bob") && !strcmp(sv->player2, "Bob") && sv->info == 3);
    CHECK(user_file("3Bob.sav") && user_file("Bob1.hud") && user_file("Bob2.hud") && user_file("XXhud1XX.hud"));
    CHECK(FESave_NameTaken("Bob") && FESave_NameTaken("") && !FESave_NameTaken("Al"));
    CHECK(!FESave_Create("Bob", 5));
    CHECK(FESave_Create("Al", 1));
    CHECK(FESave_PlayerCount() == 2 && !strcmp(FESave_PlayerStem(0), "1Al") && !strcmp(FESave_PlayerStem(1), "3Bob"));
    CHECK(FESave_PlayerChosen() == 0);
    printf("players: %s, %s (chosen %d)\n", FESave_PlayerStem(0), FESave_PlayerStem(1), FESave_PlayerChosen());
    /* The list's text: the name past the picture. */
    FEAccessor *users = FEAccess_Find("UserList.UserList");
    CHECK(users && FEAccess_Count(users) == 2 && !strcmp(FEAccess_Text(users), "Al"));
    /* A player's money kept through a switch. */
    int32_t money = FESave_Current()->money;
    FESave_Current()->money = money + 1234;
    CHECK(FESave_Write());
    CHECK(FESave_LoadStem("3Bob", true) && FESave_Current()->money == money);
    CHECK(FESave_LoadStem("1Al", true) && FESave_Current()->money == money + 1234);
    CHECK(!FESave_LoadStem("9Nobody", true) && !strcmp(FESave_Stem(), "1Al"));
    /* Removed: its save and layouts gone, the list without it. */
    CHECK(FESave_Delete("3Bob"));
    CHECK(!user_file("3Bob.sav") && !user_file("Bob1.hud"));
    FESave_ListPlayers();
    CHECK(FESave_PlayerCount() == 1);
    /* The name typed: 8 characters, no file name's forbidden ones. */
    FEAccessor *in = FEAccess_Find("UserList.NameInput");
    const char *typed = "ab/c%d:efghij";
    for (const char *p = typed; *p; p++) FEAccess_TypeChar(in, *p);
    CHECK(!strcmp(FEData_NameInput(), "abcdefgh"));
    FEAccess_TypeChar(in, 8);
    CHECK(!strcmp(FEData_NameInput(), "abcdefg"));
    /* The cheats: each code back to its name (the bits stood up again), each taken. */
    CHECK(!FEData_CheatCode("Bob"));
    for (int k = 0; k < 7; k++) {
        const uint8_t *e = exe_data(0x5d9618 + 0xc * (uint32_t)k, 0xc);
        CHECK(e != NULL);
        if (!e) break;
        char name[9] = {0};
        for (int i = 0; i < 8; i++)
            for (int j = 0; j < 8; j++) name[i] = (char)(name[i] | ((e[j] >> i) & 1) << j);
        int32_t id;
        memcpy(&id, e + 8, 4);
        int32_t m0 = FESave_Current()->money;
        CHECK(FEData_CheatCode(name));
        if (id == 4) CHECK(FESave_Current()->money == m0 - 1);
        else CHECK(FE_Cheat(id));
    }
    printf("cheats on: %08x\n", (unsigned)g_fe_cheats);
    printf("player_test: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
