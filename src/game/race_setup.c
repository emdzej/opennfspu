/* The race's car descriptions (see race_setup.h). */
#include "game/race_setup.h"
#include "game/options.h"
#include "game/car.h"
#include "game/car_damage.h"
#include "game/hud_config.h"
#include "game/race_progress.h"
#include "real/loc.h"
#include "real/random.h"
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
int32_t g_race_car_settings[4] = {1, 0, 0, 0};
int32_t g_race_ramps[3] = {-1, -1, -1};
int32_t g_race_fe_damage = 1;
char g_race_driver_name[32];
int g_race_grid_pers[8], g_race_grid_start[9];
bool g_race_grid_set;
int16_t g_race_traffic_slots[7];

RaceOpponent g_race_opp[8];
bool g_race_opp_set;
float g_race_skill = 1.0f;
int32_t g_race_cameras[4] = {0, 1, 2, 3};
char g_race_player[64];
int32_t g_track_records[8], g_lap_records[8];
char g_record_names[8][32], g_record_cars[8][32];
RaceTournament g_race_tour;
int g_race_ntraffic;

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

uint32_t g_race_cheats;

/* The cheats' fields (0x4ee430, for every class but traffic): the 993 Coupe 3.6's driving (3: SPECNAME
   "993coupe36", AISPECNAME "993") and TINYCAR (1, +0x518) and GLASSCAR (2, +0x51c; no code turns it on). */
static void cheat_fields(uint8_t *d)
{
    if (g_race_cheats >> 3 & 1) {
        put_str(d, 0x268, exe_str(0x5da71c));
        put_str(d, 0x2a8, exe_str(0x5d0f98));
    }
    if (g_race_cheats >> 1 & 1) put_i(d, 0x518, 1);
    if (g_race_cheats >> 2 & 1) put_i(d, 0x51c, 1);
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

/* Parts added to the car's (0x4ecf10): the same part (its kind +0x26 and name +0x28: the port, the
   same id) taken out of the list first, the new one at its end; a zero id ends them. */
static void add_parts(uint8_t *d, const int16_t *ids, int n)
{
    int32_t list[20];
    memcpy(list, d + 0x28, sizeof list);
    int np = 0;
    while (np < 20 && list[np] != 0) np++;
    for (int k = 0; k < n && ids[k] != 0; k++) {
        for (int j = 0; j < np; j++)
            if (list[j] == ids[k]) {
                memmove(list + j, list + j + 1, sizeof list[0] * (size_t)(np - j - 1));
                np--;
                break;
            }
        if (np < 20) list[np++] = ids[k];
    }
    for (int j = np; j < 20; j++) list[j] = 0;
    memcpy(d + 0x28, list, sizeof list);
}

bool RaceSetup_SingleCar(const char *name)
{
    g_race_cops = 0;
    g_race_ncops = 0;
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
    put_str(d, 0x17c, g_race_driver_name);                /* DRIVERNAME: the player's name (0x4ee430's argument, 0x500820) */
    put_str(d, 0x1c0, g_race_driver_name);                /* LICENSE: the same */
    car_fields(d, rec);
    cheat_fields(d);
    /* PARTINDEX (+0x28, 20): the car's parts (the front end hands the owned car's installed ones over,
       0x4ee430; else the record's stock list, +0x3bc ids from +0x3be, as a new car gets them, 0x4ee030) */
    stock_parts(d, rec);
    if (g_race_parts_set)
        for (int i = 0; i < 20; i++) put_i(d, 0x28 + 4 * i, g_race_parts[i]);
    put_i(d, 0x0, 0xb);                                   /* TYPE */
    put_i(d, 0x8, 1);                                     /* CLASS: the human player */
    put_i(d, 0x200, 0xe);                                 /* SPEECHCOLOUR */
    for (int k = 0; k < 4; k++) put_i(d, 0x208 + 4 * k, g_race_cameras[k]);   /* _CAMERA (0x148) */
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
    /* TRANSMISSION, ABS, TRACTION, CABRIODOWN (the settings screen's accessors write their tags) */
    put_i(d, 0xc, g_race_car_settings[0]);
    put_i(d, 0x24, g_race_car_settings[1]);
    put_i(d, 0x20, g_race_car_settings[2]);
    put_i(d, 0x50c, g_race_car_settings[3]);
    /* RAMPS, RAMPG, RAMPB (options 0x222 .. 0x224, the player's: fe.txt's car[0].ramps .., then the front
       end's controls over them, a digital steering, gas or brake 1): the steering eased in (car_drive.c) */
    static const char *const ramp[3] = {"car[0].ramps", "car[0].rampg", "car[0].rampb"};
    for (int i = 0; i < 3; i++) put_i(d, 0x14 + 4 * i, g_race_ramps[i] < 0 ? Options_Get(ramp[i], 0) : g_race_ramps[i]);
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
    g_race_damage = g_race_fe_damage;   /* DAMAGE (0x657a2c, the front end's) */
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
            cheat_fields(d);
            stock_parts(d, cr);
            add_parts(d, g_race_opp[i - 1].parts, g_race_opp[i - 1].nparts);
            memcpy(d + 0x218, g_race_opp[i - 1].colours, 16);
            put_i(d, 0x510, g_race_opp[i - 1].racenum);
            put_i(d, 0x514, g_race_opp[i - 1].stripe);
        }
        if (pi < 0 || pi >= nper) pi = i - 1;
        const uint8_t *r = per + 0x2c * pi;
        int16_t id;
        memcpy(&id, r, 2);
        float k = g_race_skill;   /* the event's difficulty (the tournament race's +0x3cc, 0x4eb430) */
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

/* The track's record in nfs5.trk (0x4a8 bytes; its name at +8), NULL if none. */
static const uint8_t *find_track(const uint8_t *db, size_t n, const char *name)
{
    for (size_t i = 0; i + 0x4a8 <= n; i += 0x4a8)
        if (!strcasecmp((const char *)db + i + 8, name)) return db + i;
    return NULL;
}

int g_race_ncops;

/* A road car's description after the racers' (0x4ee430 for the police and the traffic): car 0's with
   the record's car, its stock parts, the class and the colours (+0x218 order). */
static uint8_t *road_desc(const uint8_t *rec, int cls, const char *name, const uint32_t col[4], bool cheats)
{
    uint8_t *d = g_cardesc[g_nracecars];
    memcpy(d, g_cardesc[0], CARDESC_SIZE);
    put_i(d, 0x150, 0);                               /* DRIVER: rand() & 7 in the original */
    put_str(d, 0x17c, name);                          /* DRIVERNAME */
    put_str(d, 0x1c0, name);                          /* LICENSE */
    car_fields(d, rec);
    put_i(d, 0x518, 0);
    put_i(d, 0x51c, 0);
    if (cheats) cheat_fields(d);                      /* (not for traffic) */
    stock_parts(d, rec);
    put_i(d, 0x8, cls);                               /* CLASS */
    put_i(d, 0x14c, 0);                               /* PERSONALITY */
    put_i(d, 0x174, 0);                               /* STARTINGPOS */
    memcpy(d + 0x218, col, 16);
    put_i(d, 0x510, -1);
    put_i(d, 0x514, 0);
    g_nracecars++;
    return d;
}

bool RaceSetup_Cops(int n, const char *track)
{
    g_race_ncops = 0;
    g_race_cops = 0;
    if (CAR_MAX < g_nracecars + n) n = CAR_MAX - g_nracecars;
    if (n <= 0) return false;
    size_t tn = 0, cn = 0;
    uint8_t *trk = vfs_read_all("FEData\\Data\\nfs5.trk", &tn);
    uint8_t *db = vfs_read_all("FEData\\Data\\nfs5.car", &cn);
    const uint8_t *tr = trk ? find_track(trk, tn, track) : NULL;
    /* The police's records (0x6919f8: the database's kind 2 cars), by their id. */
    const uint8_t *first = NULL;
    for (size_t i = 0; db && i + FECAR_SIZE <= cn && !first; i += FECAR_SIZE) {
        int16_t kind;
        memcpy(&kind, db + i + 0x1f4, 2);
        if (kind == 2) first = db + i;
    }
    if (!tr || !first) {
        free(trk);
        free(db);
        return false;
    }
    static const uint32_t col[4] = {0xffffffffu, 0xff000000u, 0xffffff00u, 0xffffffffu};
    for (int i = 0; i < n; i++) {
        int16_t id;
        memcpy(&id, tr + 0x8a + 2 * (i % 5), 2);
        const uint8_t *rec = NULL;   /* 0x4f3a20: "Cop not found: %d" and the first */
        for (size_t k = 0; k + FECAR_SIZE <= cn && !rec; k += FECAR_SIZE) {
            int16_t kind, rid;
            memcpy(&kind, db + k + 0x1f4, 2);
            memcpy(&rid, db + k, 2);
            if (kind == 2 && rid == id) rec = db + k;
        }
        if (!rec) rec = first;
        road_desc(rec, 8, exe_str(exe_u32(0x5da6bc)), col, true);
        g_race_ncops++;
    }
    g_race_cops = 1;   /* COPS (option 0x15) */
    free(trk);
    free(db);
    return true;
}

bool RaceSetup_Traffic(int n, const char *track)
{
    g_race_ntraffic = 0;
    if (9 < n + g_nracecars) n = 9 - g_nracecars;   /* (the racers and the police before them) */
    if (n <= 0 || CAR_MAX < g_nracecars + n) return false;
    size_t tn = 0, cn = 0;
    uint8_t *trk = vfs_read_all("FEData\\Data\\nfs5.trk", &tn);
    uint8_t *db = vfs_read_all("FEData\\Data\\nfs5.car", &cn);
    const uint8_t *tr = trk ? find_track(trk, tn, track) : NULL;
    if (!tr || !db) {
        free(trk);
        free(db);
        return false;
    }
    /* The traffic's records (0x6919fc: the database's kind 1 cars, +0x1f4), by their id (+0). */
    const uint8_t *first = NULL;
    for (size_t i = 0; i + FECAR_SIZE <= cn && !first; i += FECAR_SIZE) {
        int16_t kind;
        memcpy(&kind, db + i + 0x1f4, 2);
        if (kind == 1) first = db + i;
    }
    /* The career race's own cars: its slots in turn up to n (at most seven), the empty ones skipped (the
       original leaves their descriptions' places unwritten), red. */
    bool own = false;
    for (int i = 0; i < 7; i++) own |= g_race_traffic_slots[i] != 0;
    int slot = 0, last = own ? (n < 7 ? n : 7) : n;
    for (int i = 0; i < last; i++) {
        int16_t id;
        if (own) {
            if (!g_race_traffic_slots[i]) continue;
            id = g_race_traffic_slots[i];
        } else {
            memcpy(&id, tr + 0x7c + 2 * (slot % 7), 2);
        }
        slot++;
        const uint8_t *rec = first;   /* 0x4f3a80: the first when none has the id */
        for (size_t k = 0; k + FECAR_SIZE <= cn; k += FECAR_SIZE) {
            int16_t kind, rid;
            memcpy(&kind, db + k + 0x1f4, 2);
            memcpy(&rid, db + k, 2);
            if (kind == 1 && rid == id) {
                rec = db + k;
                break;
            }
        }
        if (!rec) break;
        /* One colour at random for all four (0x537db0 four times: its low bytes, the first the top); the
           career's own are 0xffff0000. */
        uint32_t col = 0xffff0000u;
        if (!own) {
            uint32_t a = REAL_rand(), r = REAL_rand(), g = REAL_rand(), b = REAL_rand();
            col = (b & 0xff) | (g & 0xff) << 8 | (r & 0xff) << 16 | a << 24;
        }
        const uint32_t cols[4] = {col, col, col, col};
        road_desc(rec, 4, exe_str(exe_u32(0x5da6c0)), cols, false);   /* "Traffic" */
        g_race_ntraffic++;
    }
    free(trk);
    free(db);
    return g_race_ntraffic > 0;
}
