/* The knockout's run (front/feaccess.c, 0x4dd9e0 .. 0x4ddc00): four opponents, four races; each race the
   places after the kept ones knock their racers out, a player knocked out ends it, the grid of the next
   race is the racers' places. Skips (exit 0) without data. */
#include "exe.h"
#include "front/feaccess.h"
#include "vfs.h"
#include "vfs_host.h"
#include <string.h>
#include <stdio.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    char err[256];
    if (!vfs_mount_default() || !vfs_exists("FEData/Data/nfs5.trk") || !exe_init(err, sizeof err)) {
        printf("knockout_test: no game data, skipped\n");
        return 0;
    }
    FEData_Init();
    CHECK(FEData_KOOpponents() == 4);
    FEData_KnockoutStart();
    CHECK(FEData_KnockoutOn());
    char track[64];
    int laps, pers[8], start[8];
    int n = FEData_KnockoutRace(track, sizeof track, &laps, pers, start);
    printf("race 1: %s, %d laps, %d opponents, the player from %d\n", track, laps, n, start[0]);
    CHECK(n == 4 && laps == 3 && start[0] == 4 && pers[0] == 1 && start[1] == 0);
    /* Each racer a personality of its own (0x4e2940), in OpponentCar.Car's car (at 0: the player's). */
    RaceOpponent o[4];
    for (int i = 0; i < 4; i++) {
        FEData_KnockoutOpponent(pers[i], &o[i]);
        CHECK(o[i].spec[0] != 0);
        for (int j = 0; j < i; j++) CHECK(o[i].pers != o[j].pers);
    }
    printf("racer 1: personality %d, car %s\n", o[0].pers, o[0].spec);
    /* The player wins; the opponents 1 .. 4 finish 2nd .. 5th: the last (racer 4) is out. */
    int r1[5] = {1, 2, 3, 4, 5};
    FEData_KnockoutResult(r1, 5);
    CHECK(FEData_KnockoutOn());
    n = FEData_KnockoutRace(track, sizeof track, &laps, pers, start);
    CHECK(n == 3 && start[0] == 0 && pers[2] == 3 && start[3] == 3);
    /* Second: the player 3rd of four, racer 1 4th: out. */
    int r2[4] = {3, 4, 1, 2};
    FEData_KnockoutResult(r2, 4);
    n = FEData_KnockoutRace(track, sizeof track, &laps, pers, start);
    CHECK(FEData_KnockoutOn() && n == 2 && pers[0] == 2 && pers[1] == 3 && start[0] == 2);
    /* Third: the player last of three: knocked out, the knockout over. */
    int r3[3] = {3, 1, 2};
    FEData_KnockoutResult(r3, 3);
    CHECK(!FEData_KnockoutOn());
    /* The other races' opponents (0x4eb230) with OpponentCar.Car at each choice: a car each. */
    for (int m = 0; m < 5; m++) {
        FEAccess_SetPos(FEAccess_Find("OpponentCar.Car"), m);
        RaceOpponent q[8];
        int k = FEData_RaceOpponents(3, q);
        CHECK(k == 3);
        for (int i = 0; i < k; i++) CHECK(q[i].spec[0] != 0);
        printf("%s: %s %s %s\n", FEAccess_Text(FEAccess_Find("OpponentCar.Car")), q[0].spec, q[1].spec, q[2].spec);
    }
    /* The career race's opponents (0x4eb430): the tournament's racers in their cars, before any race
       the player last on the grid and racer k k-th (0x502c10). */
    {
        RaceOpponent to[8];
        int st[9];
        float sk = 0;
        int tn = FEData_TourOpponents(to, st, &sk);
        printf("tournament: %d racers, skill %.3f, racer 1 %s (personality %d, colour %08x, number %d, %d parts)\n", tn, sk,
               to[0].spec, to[0].pers, to[0].colours[0], to[0].racenum, to[0].nparts);
        CHECK(tn == 7 && sk > 0);
        CHECK(st[0] == 7 && st[1] == 0 && st[7] == 6);
        for (int i = 0; i < tn; i++) CHECK(to[i].spec[0] != 0);
    }
    /* Another tournament half run asks first (0x504110); ok resets it (0x504180). */
    Save *sv = FESave_Current();
    for (int i = 0; sv && i < sv->nsec; i++) {
        if (strcmp(sv->sec[i].name, "Tournament")) continue;
        uint8_t *t = sv->sec[i].data + 32;
        int nt = (int)((sv->sec[i].size - 32) / 0xd40);
        const uint8_t *cur = FEData_Tournament();
        int other = cur == t ? 1 : 0;
        CHECK(nt > 1 && !FEData_TourOtherUnfinished());
        uint8_t *r = t + 0xd40 * other;
        int16_t races;
        memcpy(&races, r + 0x39c, 2);
        CHECK(races > 1);
        r[0x3d0] = 1;
        CHECK(FEData_TourOtherUnfinished());
        printf("tournament check: %#x\n", FEData_TourCheck());
        FEData_TourProceed();
        CHECK(!FEData_TourOtherUnfinished() && r[0x3d0] == 0);
    }
    printf("knockout_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
