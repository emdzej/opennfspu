/* The used car market (front/femarket.c) on the default player's save: a few years of updates fill it
   with used cars of the racers out two years before (their mileage, parts, wear and value as their
   price), at most 25; a player's car put up gets an offer (at most its price) or a sale, and taking it
   pays the offer and takes the car out. Skips (exit 0) without data. */
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

static int32_t i32(const uint8_t *r, int off) { int32_t v; memcpy(&v, r + off, 4); return v; }
static int16_t i16(const uint8_t *r, int off) { int16_t v; memcpy(&v, r + off, 2); return v; }

int main(void)
{
    char err[256];
    if (!vfs_mount_default() || !vfs_exists("SaveData/XXDefXX.sav") || !exe_init(err, sizeof err)) {
        printf("market_test: no game data, skipped\n");
        return 0;
    }
    FEData_Init();
    Save *sv = FESave_Current();
    CHECK(sv != NULL);
    if (!sv) return 1;
    for (int y = 1960; y <= 1964; y++) Market_Update(y);
    printf("market: %d used cars in %d\n", sv->nused, sv->usedinfo);
    CHECK(sv->nused > 0 && sv->nused <= 25 && sv->usedinfo == 1964);
    for (int i = 0; i < sv->nused; i++) {
        const uint8_t *r = sv->used[i].rec;
        int first = i16(r + 4, 0x1f8);
        if (i < 4)
            printf("  %s ('%d): %d miles, %d parts fitted, price %d\n", (const char *)r + 4 + 2, first, i32(r, 0x698),
                   sv->used[i].nother, i32(r, 0x68c));
        CHECK(first < 1964 && r[0x6a0] == 0);
        CHECK(i32(r, 0x68c) == FEData_CarValue(&sv->used[i], 1964));   /* repriced at the last update */
        CHECK(sv->used[i].nother > 0);
    }
    /* A car of the player's put up (one of the market's, as bought): an offer or a sale at the update. */
    if (sv->nused > 0) {
        SaveCar *nc = realloc(sv->cars, (size_t)(sv->ncars + 2) * sizeof *nc);
        CHECK(nc != NULL);
        if (nc) {
            sv->cars = nc;
            for (int k = 0; k < 2; k++) {
                sv->cars[sv->ncars] = sv->used[0];
                sv->cars[sv->ncars].parts = NULL;
                sv->cars[sv->ncars].ninstalled = sv->cars[sv->ncars].nother = 0;
                sv->cars[sv->ncars].rec[0x6a0] = 1;
                sv->ncars++;
            }
            int32_t money = sv->money;
            Market_List(0, 2000000);
            CHECK(Market_Listed() == 1 && sv->cars[0].rec[0x691] == 1);
            Market_Update(1964);
            int o = Market_FirstOffer();
            CHECK(o == 0);
            int32_t offer = i32(sv->cars[0].rec, 0x694);
            printf("  offer %d for an asking price of 2000000\n", offer);
            CHECK(offer >= 0 && offer <= 2000000);
            Market_Accept(0);
            CHECK(sv->ncars == 1 && sv->money == money + offer && Market_Listed() == 0);
        }
    }
    printf("market_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
