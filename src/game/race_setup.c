/* The race's car descriptions (see race_setup.h). */
#include "game/race_setup.h"
#include "game/car.h"
#include "game/car_damage.h"
#include "game/hud_config.h"
#include "game/race_progress.h"
#include "real/loc.h"
#include "exe.h"
#include "vfs.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { FECAR_SIZE = 0x670 };

static void put_str(uint8_t *desc, int off, const char *s)
{
    memset(desc + off, 0, 0x40);
    strncpy((char *)desc + off, s, 0x3f);
}

static void put_i(uint8_t *desc, int off, int32_t v) { memcpy(desc + off, &v, 4); }

uint32_t g_race_colours[4];
int32_t g_race_parts[20];
bool g_race_parts_set;
bool g_race_colours_set;
float g_race_setup[17];
bool g_race_setup_set;
int g_race_grid_pers[8], g_race_grid_start[9];
bool g_race_grid_set;

RaceOpponent g_race_opp[8];
bool g_race_opp_set;
char g_race_player[64];
int32_t g_track_records[8], g_lap_records[8];
char g_record_names[8][32], g_record_cars[8][32];
RaceTournament g_race_tour;

static const uint8_t *find_car(const uint8_t *db, size_t n, const char *name)
{
    for (size_t i = 0; i + FECAR_SIZE <= n; i += FECAR_SIZE) {
        const char *file = (const char *)db + i + 0x7f, *spec = (const char *)db + i + 0xb1;
        if (!strcasecmp(file, name) || !strcasecmp(spec, name)) return db + i;
    }
    return NULL;
}

/* The fields of a description 0x4ee430 takes from the car's record: its names, its body style. */
static void car_fields(uint8_t *d, const uint8_t *rec)
{
    put_str(d, 0x534, (const char *)rec + 0x34);          /* FEMULTIPLAYERCARNAME */
    put_str(d, 0x228, (const char *)rec + 0x7f);          /* FILENAME */
    put_str(d, 0x268, (const char *)rec + 0xb1);          /* SPECNAME */
    put_str(d, 0x2a8, (const char *)rec + 0x115);         /* AISPECNAME */
    put_str(d, 0x2e8, (const char *)rec + 0x179);         /* SOUNDNAME */
    int16_t body;
    memcpy(&body, rec + 0x1c0, 2);
    put_i(d, 0x328, body);                                /* BODYSTYLE */
    /* TACH0RPM, TACH1000RPM, SPEEDO0KM, SPEEDO100KM: the cockpit needles' zero and scale (turns) */
    memcpy(d + 0x524, rec + 0x2f0, 16);
}

static void stock_parts(uint8_t *d, const uint8_t *rec)
{
    int16_t np;
    memcpy(&np, rec + 0x3bc, 2);
    for (int i = 0; i < 20; i++) {
        int32_t id = 0;
        if (i < np) {
            int16_t v;
            memcpy(&v, rec + 0x3be + 2 * i, 2);
            id = v;
        }
        put_i(d, 0x28 + 4 * i, id);
    }
}

bool RaceSetup_SingleCar(const char *name)
{
    size_t n = 0;
    uint8_t *db = vfs_read_all("FEData\\Data\\nfs5.car", &n);
    if (!db) return false;
    const uint8_t *rec = find_car(db, n, name);
    if (!rec) {
        free(db);
        return false;
    }
    uint8_t *d = g_cardesc[0];
    memset(d, 0, CARDESC_SIZE);
    /* As 0x4ee430 writes them (the front end's choices it takes from its screens are their defaults). */
    put_i(d, 0x150, 0);                                   /* DRIVER: rand() & 7 in the original */
    put_str(d, 0x17c, "");                                /* DRIVERNAME */
    put_str(d, 0x1c0, "");                                /* LICENSE */
    car_fields(d, rec);
    /* PARTINDEX (+0x28, 20): the car's parts (the front end hands the owned car's installed ones over,
       0x4ee430; else the record's stock list, +0x3bc ids from +0x3be, as a new car gets them, 0x4ee030) */
    stock_parts(d, rec);
    if (g_race_parts_set)
        for (int i = 0; i < 20; i++) put_i(d, 0x28 + 4 * i, g_race_parts[i]);
    put_i(d, 0x0, 0xb);                                   /* TYPE */
    put_i(d, 0x8, 1);                                     /* CLASS: the human player */
    put_i(d, 0x200, 0xe);                                 /* SPEECHCOLOUR */
    {
        /* _CAMERA: the car's camera list (the front end's camera option; a stand-in until it is ported):
           the near chase, the far chase, the hood, in the car. */
        static const int32_t cams[4] = {2, 5, 1, 0};
        for (int k = 0; k < 4; k++) put_i(d, 0x208 + 4 * k, cams[k]);
    }
    {
        /* SKILLSLOW .. SKILLDEFAULT 1, their ranges 0 (the player's, as the front end writes them). */
        float sk[8] = {1, 1, 1, 1, 0, 0, 0, 0};
        memcpy(d + 0x154, sk, sizeof sk);
    }
    {
        /* The car's settings (0x4ee430): each float times 0x5b3e68's constant, truncated (0x4a51d0), under
           its tag (Options_Parse 0x4b0db0 fills the table of id, address, name that gives its field). The
           setup screen's order: the downforces, brake bias, ride height, suspension travel, toe, shock
           rate, tyre pressures, then the reverse, first to sixth and final ratios. SUSPENSIONSTIFFNESS
           (+0x130) is not among them and stays 0. */
        static const uint16_t field[17] = {0x114, 0x118, 0x11c, 0x120, 0x124, 0x128, 0x134, 0x13c, 0x140,
                                           0xf4, 0xfc, 0x100, 0x104, 0x108, 0x10c, 0x110, 0xf0};
        static const float fresh[17] = {50, 50, 50, 0, 0, 0, 1.0f, 30, 30, 0, 0, 0, 0, 0, 0, 0, 0};   /* 0x4ee230 */
        const float *set = g_race_setup_set ? g_race_setup : fresh;
        uint32_t kb = exe_u32(0x5b3e68);
        float k;
        memcpy(&k, &kb, 4);
        for (int i = 0; i < 17; i++) put_i(d, field[i], (int32_t)(set[i] * k));
    }
    put_i(d, 0xc, 1);                                     /* TRANSMISSION: automatic */
    /* The colours (+0x218 COLORCONFIG: body, stripe, interior, wheels; the car viewer's silver), no race
       number, colour package 0. */
    static const uint32_t colours[4] = {0xffb8bcc0u, 0xff202020u, 0xff303030u, 0xffc0c0c0u};
    memcpy(d + 0x218, g_race_colours_set ? g_race_colours : colours, sizeof colours);   /* (the front end's when it chose) */
    put_i(d, 0x510, -1);                                  /* RACENUMBER */
    put_i(d, 0x514, 0);                                   /* STRIPEPACKAGE: the colour scheme */
    /* The HUD's dials (0x4eadb0, the units option km/h). */
    int16_t g[0x12];
    memcpy(g, rec + 0x300, sizeof g);
    HudDials *h = &g_hud_dials;
    if (!g_hud_imperial) h->speedo_kmh = g[0], h->speedo_start = g[3], h->speedo_deg = g[4], h->speedo_units = g[1];
    else h->speedo_mph = g[6], h->speedo_start = g[9], h->speedo_deg = g[10], h->speedo_units = g[7];
    h->tach = g[12], h->tach_start = g[15], h->tach_deg = g[16], h->tach_units = g[13];
    h->needle = g[14], h->pivot = g[17];
    free(db);
    /* Damage on (0x657a2c, the front end's damage option; a stand-in until the options are ported). */
    g_race_damage = 1;
    g_race_laps = 2;   /* 0x6573f0: two laps (the front end's choice; a stand-in) */
    g_nracecars = 1;
    g_ntrafficcars = 0;
    return true;
}

bool RaceSetup_Opponents(int n)
{
    size_t size = 0;
    uint8_t *per = vfs_read_all("FEData\\Data\\nfs5.per", &size);
    if (!per) return false;
    int nper = (int)(size / 0x2c);
    size_t nloc = 0;
    uint8_t *loc = vfs_read_all("FEData\\Locale\\festrings.loc", &nloc);   /* the front end's strings */
    size_t ncar = 0;
    uint8_t *cars = g_race_opp_set ? vfs_read_all("FEData\\Data\\nfs5.car", &ncar) : NULL;
    if (CAR_MAX <= n) n = CAR_MAX - 1;
    if (nper < n) n = nper;
    for (int i = 1; i <= n; i++) {
        uint8_t *d = g_cardesc[i];
        memcpy(d, g_cardesc[0], CARDESC_SIZE);
        int pi = g_race_opp_set ? g_race_opp[i - 1].pers : g_race_grid_set ? g_race_grid_pers[i - 1] - 1 : i - 1;   /* (the knockout's racers) */
        const uint8_t *cr = g_race_opp_set && cars ? find_car(cars, ncar, g_race_opp[i - 1].spec) : NULL;
        if (cr) {
            /* Factory Driver's opponents in their own cars (0x4eb830): the record's fields, its stock
               parts, the colours and the race number the front end picked */
            car_fields(d, cr);
            stock_parts(d, cr);
            memcpy(d + 0x218, g_race_opp[i - 1].colours, 16);
            put_i(d, 0x510, g_race_opp[i - 1].racenum);
            put_i(d, 0x514, g_race_opp[i - 1].stripe);
        }
        if (pi < 0 || pi >= nper) pi = i - 1;
        const uint8_t *r = per + 0x2c * pi;
        int16_t id;
        memcpy(&id, r, 2);
        float k = 1.0f;   /* the event's difficulty (its record's +0x3cc, the front end's) */
        float sk[8];
        memcpy(sk, r + 0xc, sizeof sk);
        put_i(d, 0x8, 2);                                  /* CLASS: an opponent */
        put_i(d, 0x14c, i);                                /* PERSONALITY */
        put_i(d, 0x150, i);                                /* DRIVER */
        float v[8] = {k * sk[0], k * sk[2], k * sk[4], k * sk[6], sk[1], sk[3], sk[5], sk[7]};
        memcpy(d + 0x154, v, sizeof v);                    /* SKILLSLOW .. SKILLRANGEDEFAULT */
        put_i(d, 0x174, g_race_grid_set ? g_race_grid_start[i] : n - i);   /* STARTINGPOS (0x247) */
        int32_t name;
        memcpy(&name, r + 4, 4);
        const char *s = loc ? LOC_string(loc, name) : NULL;
        put_str(d, 0x17c, s ? s : "");                     /* DRIVERNAME (the record's string) */
        put_str(d, 0x1c0, s ? s : "");                     /* LICENSE */
        (void)id;
    }
    put_i(g_cardesc[0], 0x174, g_race_grid_set ? g_race_grid_start[0] : n);
    free(per);
    free(loc);
    free(cars);
    g_nracecars = n + 1;
    return true;
}
