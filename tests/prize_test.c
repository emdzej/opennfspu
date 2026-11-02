/* The victory room's prize race (front/feaccess.c) on the default player's save: the era's prize
   tournament (0x503f40, kind 2), its car through PrizeCars.Car (FEPrizeCarAccessor), its race's track,
   laps and traffic (0x4fb630's types 10, 11), and Tournament.Data's ENDERAHEADER / ENDERADESCRIP texts.
   Skips (exit 0) without data. */
#include "exe.h"
#include "front/fe_parts.h"
#include "front/feaccess.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int rec16(const uint8_t *p, int o) { int16_t v; memcpy(&v, p + o, 2); return v; }
static int rec32(const uint8_t *p, int o) { int32_t v; memcpy(&v, p + o, 4); return v; }

int main(void)
{
    char err[256];
    if (!vfs_mount_default() || !vfs_exists("SaveData/XXDefXX.sav") || !exe_init(err, sizeof err)) {
        printf("prize_test: no game data, skipped\n");
        return 0;
    }
    FEStrings_Load(0);
    FEData_Init();

    /* a new career: no era done, the classic era's (0x209) prize tournament */
    const uint8_t *r = FEData_PrizeTournament();
    CHECK(r != NULL);
    if (!r) return 1;
    CHECK(rec32(r, 4) == 0x209 + FEData_RaceEra() && rec16(r, 0xe4) == 2);

    /* its car, the accessor's */
    FEAccessor *pa = FEAccess_Find("PrizeCars.Car");
    CHECK(pa && FEAccess_Count(pa) == 1);
    const uint8_t *car = FEAccess_CarRecord(pa);
    CHECK(car && car == FEData_CarById(rec16(r, 0x10c)));
    const char *file = FEAccess_CarFile(pa);
    printf("prize: era %x, car %d '%s'\n", rec32(r, 4), rec16(r, 0x10c), file ? file : "");
    CHECK(file && *file);

    /* its race */
    int laps, traffic;
    bool rev, mir;
    const char *t = FEData_PrizeRace(&laps, &rev, &mir, &traffic);
    printf("prize race: %s, %d laps, reverse %d, mirror %d, traffic %d\n", t ? t : "(none)", laps, rev, mir, traffic);
    CHECK(t && *t && laps >= 1 && traffic >= 0 && traffic <= 8);

    /* the message's texts */
    FEAccessor *td = FEAccess_Find("Tournament.Data");
    CHECK(td != NULL);
    const char *h = FEAccess_TextArg(td, "ENDERAHEADER"), *d = FEAccess_TextArg(td, "ENDERADESCRIP");
    printf("header '%s' (%d), descrip '%.40s' (%d)\n", h, rec32(r, 0x24), d, rec32(r, 0x28));
    CHECK(h && *h && strcmp(h, "Not Found") && d && *d && strcmp(d, "Not Found"));
    CHECK(!strcmp(FEAccess_TextArg(td, "NOSUCH"), "NOT FOUND"));

    printf(fails ? "prize_test: %d failures\n" : "prize_test: ok\n", fails);
    return fails ? 1 : 0;
}
