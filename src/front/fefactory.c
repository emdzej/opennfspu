/* Factory Driver's missions (fefactory.h). */
#include "front/fefactory.h"
#include "front/feaccess.h"
#include "front/fe_parts.h"
#include "game/options.h"
#include "game/mission.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *s_ms;        /* 0x69c8cc: the records */
static int s_n;              /* 0x69c8d0 */
static uint8_t *s_own;       /* 0x69c8e8 when they're nfs5.fac's (the save's are worked in place) */
static uint8_t *s_last;      /* 0x69c8f0 */
static SaveCar s_car;        /* 0x69c8c8 */
static bool s_havecar;
static bool s_cheated;       /* 0x69c900 */
static bool s_loaded;

static int16_t m16(const uint8_t *m, int off) { int16_t v; memcpy(&v, m + off, 2); return v; }
static int32_t m32(const uint8_t *m, int off) { int32_t v; memcpy(&v, m + off, 4); return v; }
static void put16(uint8_t *m, int off, int v) { int16_t s = (int16_t)v; memcpy(m + off, &s, 2); }
static uint32_t swap32(uint32_t v) { return v >> 24 | (v >> 8 & 0xff00u) | (v << 8 & 0xff0000u) | v << 24; }

static uint8_t *rec(int i) { return s_ms + MISSION_SIZE * i; }

static int current(void)   /* the first mission not done, -1 when all are */
{
    for (int i = 0; i < s_n; i++)
        if (m16(rec(i), 0xe8) == 0) return i;
    return -1;
}

static void free_car(void)
{
    if (s_havecar) free(s_car.parts);
    memset(&s_car, 0, sizeof s_car);
    s_havecar = false;
}

/* The mission's parts on its car (0x51aac0): each one's nfs5.prt record in place of the first fitted
   part of its type (+0x26) and name (+0x28), at the end of the fitted list. */
static void fit_parts(SaveCar *c, int n, const uint8_t *ids)
{
    for (int k = 0; k < n; k++) {
        int16_t id = m16(ids, 2 * k);
        const uint8_t *p = FEData_PartRecord(id);
        if (!p) continue;
        int32_t (*parts)[2] = c->parts;
        for (int i = c->ninstalled; i < c->ninstalled + c->nother; i++) {
            const uint8_t *q = FEData_PartRecord(parts[i][0]);
            if (!q || m16(q, 0x26) != m16(p, 0x26) || strcmp((const char *)q + 0x28, (const char *)p + 0x28)) continue;
            memmove(&parts[i], &parts[i + 1], sizeof parts[0] * (size_t)(c->ninstalled + c->nother - i - 1));
            c->nother--;
            break;
        }
        int all = c->ninstalled + c->nother;
        void *np = realloc(c->parts, sizeof parts[0] * (size_t)(all + 1));
        if (!np) return;
        c->parts = np;
        c->parts[all][0] = id;
        c->parts[all][1] = FEData_PartValue(id);
        c->nother++;
    }
}

/* The current mission's car (0x51a2d0): a car object of its record by id (0x4f39e0), its colours
   (+0xa0 body, +0xb0 interior, both swapped; +0xbc the other two, +0xbe the race number), its parts. */
static void make_car(void)
{
    if (s_n <= 0) return;
    int i = current();
    const uint8_t *m = rec(i < 0 ? 0 : i);
    const uint8_t *r = FEData_CarById(m16(m, 0x70));
    free_car();
    if (!r) return;
    int32_t col[4] = {(int32_t)swap32((uint32_t)m32(m, 0xa0)), (int32_t)swap32((uint32_t)m32(m, 0xb0)), m16(m, 0xbc), m16(m, 0xbc)};
    FEData_NewCar(&s_car, r, col, 0, m16(m, 0xbe));
    s_havecar = true;
    fit_parts(&s_car, m16(m, 0x72), m + 0x74);
}

void FEFactory_Load(void)
{
    if (s_loaded) return;
    s_loaded = true;
    Save *sv = FESave_Current();
    for (int i = 0; sv && i < sv->nsec; i++) {
        SaveSection *x = &sv->sec[i];
        if (strcmp(x->name, "Missions") || x->size < 4) continue;
        uint32_t k;
        memcpy(&k, x->data, 4);
        if (k > x->size - 4) k = (uint32_t)(x->size - 4);
        s_ms = x->data + 4;
        s_n = (int)(k / MISSION_SIZE);
    }
    if (!s_ms) {
        size_t n = 0;
        s_own = vfs_read_all("FEData\\Data\\nfs5.fac", &n);   /* ("%s%s" 0x5cc588 of 0x65b348, 0x5dd35c) */
        s_ms = s_own;
        s_n = s_own ? (int)(n / MISSION_SIZE) : 0;
    }
    /* (0x51a170: the levels' lists 0x69c8f4 .. 0x69c8fc, which nothing the port has reads; the start-up
       option's missions done, 0x657e28; the car) */
    if (s_n > 0) {
        FEFactory_Cheat(Options_Get("CASTANET", 0), true);
        make_car();
    }
}

int FEFactory_Count(void) { FEFactory_Load(); return s_n; }

uint8_t *FEFactory_Mission(void)
{
    FEFactory_Load();
    if (s_n <= 0) return NULL;
    int i = current();
    return rec(i < 0 ? 0 : i);
}

int FEFactory_MissionIndex(void)
{
    FEFactory_Load();
    int i = current();
    return i < 0 ? 0 : i;
}

int FEFactory_Rank(void)
{
    FEFactory_Load();
    int i = current();
    if (i < 0) return 3;
    if (m16(rec(0), 0xe8) == 0) return -1;
    return m16(rec(i), 8) - 1;
}

bool FEFactory_AllDone(void) { FEFactory_Load(); return s_n > 0 ? current() < 0 : true; }

bool FEFactory_LevelDone(void)
{
    FEFactory_Load();
    if (!s_last) return false;
    int i = current();
    if (i >= 0 && m16(s_last, 8) == m16(rec(i), 8)) return false;
    return m32(s_last, 0xc) != -1;
}

static void result(int place, int time, int value, int pass)
{
    int i = current();
    s_last = rec(i < 0 ? 0 : i);
    put16(s_last, 0xea, place);
    put16(s_last, 0xec, time);
    put16(s_last, 0xee, value);
    put16(s_last, 0xe8, pass);
    make_car();
}

void FEFactory_Result(int place, int time, int value, int pass)
{
    FEFactory_Load();
    if (s_n > 0) result(place, time, value, pass);
}

void FEFactory_Skip(int pass)
{
    FEFactory_Load();
    /* The original loops until the mission last marked ends a level with its message: with a fail (0)
       nothing moves on and it never ends; the port stops after every mission was tried. */
    for (int k = 0; s_n > 0 && k <= s_n; k++) {
        result(0, 0, 0, pass);
        int i = current();
        if (i < 0 || m16(s_last, 8) != m16(rec(i), 8)) {
            if (m32(s_last, 0xc) != -1) return;
        }
    }
}

void FEFactory_Cheat(int n, bool once)
{
    if ((once && s_cheated) || n <= 0) return;
    if (s_n < n) n = s_n;
    n--;
    for (int i = 0; i < s_n; i++) put16(rec(i), 0xe8, i < n ? 1 : 0);
    s_cheated = true;
    make_car();
}

SaveCar *FEFactory_Car(void) { FEFactory_Load(); return s_havecar ? &s_car : NULL; }
const uint8_t *FEFactory_Last(void) { return s_last; }

void FEFactory_ClearEndOfLevel(void)
{
    if (!s_last) return;
    int32_t v = -1;
    memcpy(s_last + 0xc, &v, 4);
}

const char *FEFactory_Data(const char *arg, int profile)
{
    static char b[2048];
    const uint8_t *m = FEFactory_Mission();
    Save *sv = FESave_Current();
    const char *player = sv ? sv->player : "";
    if (!arg) arg = "";
    if (!strcmp(arg, "MISSONDESC")) {
        snprintf(b, sizeof b, m ? FEStrings_Get(m32(m, 0)) : "", player);
        return b;
    }
    if (!strcmp(arg, "NARRNAME")) {
        snprintf(b, sizeof b, "%s", m ? FEStrings_Get(m16(m, 0x1e) + 0x9c4) : "");
        return b;
    }
    if (!strcmp(arg, "ENDOFLEVEL")) {
        /* (the last raced mission's; the original reads it without a check) */
        snprintf(b, sizeof b, s_last ? FEStrings_Get(m32(s_last, 0xc)) : "", player);
        return b;
    }
    if (!strcmp(arg, "TRACKNAME")) {
        const uint8_t *t = m ? FEData_TrackById(m16(m, 0x20)) : NULL;
        return t ? FEStrings_Get(m32(t, 4) + 0x14) : "";
    }
    if (!strcmp(arg, "profilename")) return FEStrings_Get(profile * 4 + 0xdad);
    if (!strcmp(arg, "profileage")) return FEStrings_Get(profile * 4 + 0xdaf);
    if (!strcmp(arg, "profiletitle")) return FEStrings_Get(profile * 4 + 0xdae);
    if (!strcmp(arg, "profiledesc")) return FEStrings_Get(profile * 4 + 0xdb0);
    return "NOT FOUND";   /* (0x5d73bc) */
}

int FEFactory_Opponents(int pers[5], int cars[5])
{
    const uint8_t *m = FEFactory_Mission();
    int n = 0;
    for (int i = 0; m && i < 5; i++) {
        if (m16(m, 0xd4 + 4 * i) == 0) continue;
        pers[n] = m16(m, 0xd4 + 4 * i);
        cars[n] = m16(m, 0xd6 + 4 * i);
        n++;
    }
    return n;
}

void FEFactory_RaceOptions(void)
{
    /* 0x4eb6d0: the mission's conditions as race options (Options_Parse's 0xf4 .. 0xff), and 0x4ec480's
       skill test (0x1c, the mission's +0x24) */
    const uint8_t *m = FEFactory_Mission();
    if (!m) return;
    for (int i = 0; i < 3; i++) {
        g_mission_win[i] = m32(m, 0x58 + 4 * i);
        g_mission_lose[i] = m32(m, 0x64 + 4 * i);
    }
    g_mission_narrator = m16(m, 0x1e);
    g_mission_time = m[0xc1] ? m16(m, 0xc2) : -1;
    g_mission_damage = m[0xc4] ? m16(m, 0xc6) : -1;
    g_mission_cones = m[0xc8] && m16(m, 0xca) == 0 ? m16(m, 0xcc) : -1;
    g_mission_penalty = m[0xc8] && m16(m, 0xca) == 1 ? m16(m, 0xce) : -1;
    g_mission_place = m[0xd0] ? m16(m, 0xd2) : 0;
    g_skill_test = m16(m, 0x24);
}

const char *FEFactory_Track(int *laps)
{
    /* 0x4fb630's type 9: the track of the mission's id (+0x20; the first without one), its laps (+0x22,
       at least 1) */
    const uint8_t *m = FEFactory_Mission();
    const uint8_t *t = m ? FEData_TrackById(m16(m, 0x20)) : NULL;
    if (!t) return NULL;
    *laps = m16(m, 0x22) ? m16(m, 0x22) : 1;
    return (const char *)t + 8;
}
