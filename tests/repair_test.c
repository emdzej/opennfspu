/* The garage's repairs (front/feaccess.c) on a used car of the default player's save made the player's:
   the advanced repair's shopping list (a part without wear not taken, a part taken twice, the total of
   repairs and replacements, the accept paying each in turn, the money short) and the quick fix's ticked
   categories. Skips (exit 0) without data. */
#include "exe.h"
#include "front/feaccess.h"
#include "front/femarket.h"
#include "front/fesave.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static float wear(const SaveCar *c, int i) { float w; memcpy(&w, &c->parts[i][1], 4); return w; }
static void set_wear(SaveCar *c, int i, float w) { memcpy(&c->parts[i][1], &w, 4); }
static int total(void) { return atoi(FEAccess_TextArg(FEAccess_Find("Parts.RepairShoppingList"), "TOTAL")); }

int main(void)
{
    char err[256];
    if (!vfs_mount_default() || !vfs_exists("SaveData/XXDefXX.sav") || !exe_init(err, sizeof err)) {
        printf("repair_test: no game data, skipped\n");
        return 0;
    }
    FEData_Init();
    Save *sv = FESave_Current();
    CHECK(sv != NULL);
    if (!sv) return 1;
    for (int y = 1960; y <= 1962 && !sv->nused; y++) Market_Update(y);
    CHECK(sv->nused > 0);
    if (!sv->nused) return 1;

    /* the market's first car the player's only one, its parts its own */
    if (!sv->ncars) sv->cars = calloc(1, sizeof *sv->cars);
    SaveCar *c = &sv->cars[0];
    *c = sv->used[0];
    size_t np = (size_t)(c->ninstalled + c->nother);
    c->parts = malloc(np * sizeof *c->parts);
    memcpy(c->parts, sv->used[0].parts, np * sizeof *c->parts);
    sv->ncars = 1;
    FEAccessor *owned = FEAccess_Find("OwnedCars.Car"), *od = FEAccess_Find("Parts.OwnedDamageRepairList"),
               *rl = FEAccess_Find("Parts.RepairShoppingList");
    CHECK(owned && od && rl);
    if (!owned || !od || !rl) return 1;
    FEAccess_SetPos(owned, 0);
    FEData_RepairOpen();
    CHECK(FEAccess_Count(od) == c->nother && c->nother >= 2);
    int a = c->ninstalled, b = c->ninstalled + 1;
    set_wear(c, a, 40.0f);
    set_wear(c, b, 0.0f);
    sv->money = 1000000;

    /* a part without wear isn't taken; one with wear twice (once to fix, once to replace) */
    FEAccess_SetPos(od, 1);
    CHECK(FEData_RepairCallback("fixpart") == 0 && FEAccess_Count(rl) == 0);
    FEAccess_SetPos(od, 0);
    CHECK(FEData_RepairCallback("fixpart") == 0 && FEData_RepairCallback("replacepart") == 0);
    CHECK(FEAccess_Count(rl) == 2);
    int t = total();
    printf("repair: list of 2, total %d, '%s'\n", t, FEAccess_TextAt(rl, 0));
    CHECK(t > 0);

    /* removed and taken again: the same total */
    FEAccess_SetPos(rl, 1);
    CHECK(FEData_RepairCallback("removeitem") == 0 && FEAccess_Count(rl) == 1);
    CHECK(FEData_RepairCallback("replacepart") == 0 && total() == t);

    /* short of money: the message, nothing paid */
    sv->money = t - 1;
    CHECK(FEData_RepairCallback("acceptlist") == 0x32a && wear(c, a) == 40.0f && FEAccess_Count(rl) == 2);

    /* paid in turn: the repair at its wear, then the replacement of a part without wear (20 per cent of
       its price): less than the total quoted */
    sv->money = 1000000;
    CHECK(FEData_RepairCallback("acceptlist") == 0);
    int32_t paid = 1000000 - sv->money;
    printf("repair: paid %d of %d quoted\n", paid, t);
    CHECK(wear(c, a) == 0.0f && FEAccess_Count(rl) == 0 && paid > 0 && paid < t);

    /* clearlist empties it */
    set_wear(c, a, 10.0f);
    CHECK(FEData_RepairCallback("fixpart") == 0 && FEData_RepairCallback("clearlist") == 0 && FEAccess_Count(rl) == 0);
    CHECK(FEData_RepairCallback("PLAYGAME") == -1);

    /* the quick fix: nothing ticked, nothing done; the whole car ticked, every part fixed */
    for (int i = a; i < a + c->nother; i++) set_wear(c, i, 30.0f);
    int32_t m0 = sv->money;
    FEData_QuickFixClear();
    CHECK(FEData_QuickFix() == 0 && sv->money == m0 && wear(c, a) == 30.0f);
    FEAccess_SetPos(FEAccess_Find("QUICKFIX.OVERALL"), 1);
    CHECK(FEData_QuickFix() == 0 && sv->money < m0);
    for (int i = a; i < a + c->nother; i++) CHECK(wear(c, i) == 0.0f);
    CHECK(FEAccess_Pos(FEAccess_Find("QUICKFIX.OVERALL")) == 0);   /* (the boxes off after) */
    printf("quick fix: paid %d\n", m0 - sv->money);

    printf(fails ? "repair_test: %d failures\n" : "repair_test: ok\n", fails);
    return fails ? 1 : 0;
}
