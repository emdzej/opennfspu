/* The front end's data accessors (feaccess.h). */
#include "front/feaccess.h"
#include "front/fe_parts.h"
#include "exe.h"
#include "pending.h"
#include "game/options.h"
#include "front/fesave.h"
#include "vfs.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { TRK_SIZE = 0x4a8, CAR_SIZE = 0x670 };

typedef enum { ACC_INT, ACC_TRACK, ACC_CAR, ACC_LIST, ACC_DEALER, ACC_OWNED, ACC_TDATA, ACC_MERGED, ACC_TOURNY, ACC_RTRACK } AccKind;

struct FEAccessor {
    char name[64];
    AccKind kind;
    int pos, count, min;
    uint32_t list;   /* ACC_LIST: the exe's zero-ended string ids (FEListAccessor 0x4bb260) */
    uint32_t key;    /* the option it keeps (its key's address), 0 none */
    int owned;       /* ACC_MERGED: the owned cars it counted last */
};

enum { NACC = 64 };
static FEAccessor s_acc[NACC];   /* 0x65b3a8 */
static int s_nacc;
static uint8_t *s_trk, *s_car;
static int s_tracks[32], s_ntracks;   /* the records with a model (0x691d00) */
static int s_cars[160], s_ncars;      /* (0x6919f0) */
static int s_dealer[160], s_ndealer;  /* 0x691a00: the cars the dealer sells this year */

static FEAccessor *add(const char *name, AccKind kind, int count, int min, int pos)
{
    if (s_nacc == NACC) return NULL;
    FEAccessor *a = &s_acc[s_nacc++];
    snprintf(a->name, sizeof a->name, "%s", name);
    a->kind = kind;
    a->count = count;
    a->min = min;
    a->pos = pos < 0 ? 0 : pos >= count ? (count ? count - 1 : 0) : pos;
    return a;
}

static bool fe_key(const char *key, char *out, size_t cap)
{
    size_t n = 0, kl = strlen(key);
    char *t = (char *)vfs_read_all("fe.txt", &n);
    bool found = false;
    if (!t) return false;
    char *b = realloc(t, n + 1);
    if (!b) {
        free(t);
        return false;
    }
    b[n] = 0;
    for (char *l = b; l && *l;) {
        char *e = strpbrk(l, "\r\n");
        if (e) *e++ = 0;
        while (*l == ' ') l++;
        if (!strncasecmp(l, key, kl) && (l[kl] == ' ' || l[kl] == '=')) {
            char *q = strchr(l, '=');
            if (q) {
                q++;
                while (*q == ' ') q++;
                snprintf(out, cap, "%s", q);
                found = true;
            }
        }
        l = e;
    }
    free(b);
    return found;
}

static const char *trk_name(int i) { return (const char *)s_trk + TRK_SIZE * s_tracks[i] + 8; }
static const char *car_file(int i) { return (const char *)s_car + CAR_SIZE * s_cars[i] + 0x7f; }

/* An option's value from fe.txt by its key in the options' table (Options_Parse 0x4b0db0 fills it with
   (id, target, key) triples: the keys' addresses are its instruction operands). */
static int option(uint32_t key, int def) { return Options_Int(key, def); }

static int list_count(uint32_t list)
{
    int n = 0;
    while (n < 64 && exe_u32(list + 4u * (unsigned)n)) n++;
    return n;
}

static void add_list(const char *name, uint32_t list, uint32_t key)   /* FEListAccessor 0x4bb260 */
{
    FEAccessor *a = add(name, ACC_LIST, list_count(list), 0, option(key, 0));
    if (a) a->list = list, a->key = key;
}

/* The audio data area (FEdaAudio_v0 0x4e3fb0): the five levels 0..127 (FEIntAccessor 0x4b9ef0, options
   0x37 .. 0x3b), the 3D mode and the quality (FEListAccessor 0x4bb260, options 0x36, 0x35, the string
   lists 0x5b6c1c, 0x5b6c08). (The CD player's accessors and the levels' callbacks into the sound pend.) */
static void audio(void)
{
    static const struct { const char *name; uint32_t key; } lv[5] = {
        {"Audio.MusicLevel", 0x5d618c}, {"Audio.SfxLevel", 0x5d6180}, {"Audio.EngineLevel", 0x5d6174},
        {"Audio.SpeechLevel", 0x5d6168}, {"Audio.AmbientLevel", 0x5d6158}};
    for (int i = 0; i < 5; i++) {
        FEAccessor *a = add(lv[i].name, ACC_INT, 0x80, 0, option(lv[i].key, 0x7f));
        if (a) a->key = lv[i].key;
    }
    add_list("Audio.Mode", 0x5b6c1c, 0x5d6198);
    add_list("Audio.Quality", 0x5b6c08, 0x5d61a0);
}


/* The display data area (FEdaDisplay_v0 0x4e4610): the HUD's choices (options 0x5b .. 0x69). */
static void display(void)
{
    static const struct { const char *name; uint32_t list, key; } d[14] = {
        {"Display.ScreenLayout", 0x5b6d18, 0x5d5fbc}, {"Display.Speedometer", 0x5b6d08, 0x5d5fb0},
        {"Display.SpeedUnits", 0x5b6cfc, 0x5d5fa4},   {"Display.Tach", 0x5b6cec, 0x5d5f94},
        {"Display.Damage", 0x5b6cd8, 0x5d5f84},       {"Display.Map", 0x5b6cc4, 0x5d5f80},
        {"Display.MapScale", 0x5b6cac, 0x5d5f74},     {"Display.OppList", 0x5b6c9c, 0x5d5f64},
        {"Display.SplitTimes", 0x5b6c88, 0x5d5f58},   {"Display.DispCols", 0x5b6c78, 0x5d5f48},
        {"Display.Translucency", 0x5b6c6c, 0x5d5f28}, {"Display.RearView", 0x5b6c60, 0x5d5f18},
        {"Display.RecordTime", 0x5b6c54, 0x5d5f0c},   {"Display.Gear", 0x5b6c48, 0x5d5f04}};
    for (int i = 0; i < 14; i++) add_list(d[i].name, d[i].list, d[i].key);
}

/* The graphics data area (FEdaGraphics_v0 0x4e4e00): the renderer's choices (options 0x44 .. 0x54) and the
   gamma (0x4d, 0..400 from 100). (Graphics.Resolutions, the display modes' list 0x4bc2f0, pends.) */
static void graphics(void)
{
    static const struct { const char *name; uint32_t list, key; } g[11] = {
        {"Graphics.WideScreen", 0x5b6da4, 0x5d60ec}, {"Graphics.CarChrome", 0x5b6d94, 0x5d60e0},
        {"Graphics.CarDetail", 0x5b6d94, 0x5d60d4},  {"Graphics.SpecialFX", 0x5b6d80, 0x5d60c8},
        {"Graphics.ViewDistance", 0x5b6d80, 0x5d6094}, {"Graphics.TripleBuffer", 0x5b6da4, 0x5d60ac},
        {"Graphics.Headlights", 0x5b6d74, 0x5d6088}, {"Graphics.LensFlare", 0x5b6da4, 0x5d607c},
        {"Graphics.TextureDetail", 0x5b6d5c, 0x5d6050}, {"Graphics.Reflections", 0x5b6d94, 0x5d6014},
        {"Graphics.SkyRendering", 0x5b6d4c, 0x5d6004}};
    for (int i = 0; i < 11; i++) add_list(g[i].name, g[i].list, g[i].key);
    FEAccessor *a = add("Graphics.GammaCorrection", ACC_INT, 401, 0, option(0x5d606c, 100));
    if (a) a->key = 0x5d606c;
}

static int16_t rec16(const uint8_t *r, int off) { int16_t v; memcpy(&v, r + off, 2); return v; }
static int32_t rec32(const uint8_t *r, int off) { int32_t v; memcpy(&v, r + off, 4); return v; }

int32_t FEData_PartValue(int32_t id)   /* (0x49c3e0's record) */
{
    static uint8_t *prt;
    static size_t nprt;
    if (!prt) prt = vfs_read_all("FEData\\Data\\nfs5.prt", &nprt);   /* (0x5d0c4c) */
    for (size_t i = 0; prt && (i + 1) * 0x168 <= nprt; i++)
        if (rec16(prt + 0x168 * i, 0) == id) return rec32(prt + 0x168 * i, 0x164);
    return 0;
}

/* The save's tournaments (its Tournament section: seven ints, then 0xd40 bytes a tournament). */
static const uint8_t *tour_section(int32_t *head, int *n)
{
    Save *sv = FESave_Current();
    for (int i = 0; sv && i < sv->nsec; i++)
        if (!strcmp(sv->sec[i].name, "Tournament") && sv->sec[i].size >= 32) {
            if (head) memcpy(head, sv->sec[i].data, 28);
            int32_t bytes;
            memcpy(&bytes, sv->sec[i].data + 28, 4);
            if (bytes < 0 || (size_t)bytes > sv->sec[i].size - 32) bytes = 0;
            *n = bytes / 0xd40;
            return sv->sec[i].data + 32;
        }
    *n = 0;
    return NULL;
}

static int s_tourny[64], s_ntourny;   /* the list shown (0x504970's choice of the lists 0x502150 makes) */

/* The tournaments of the event type (0x504970; the lists by +4, 0x502150): type 0 the era's (0x691e50:
   0x209 .. 0x20b), else the special events (1 0x20c, 2 0x20d, 3 0x20e; when 0x691e54 is set, 0 .. 2);
   +0xe4 2 kept out. */
static void tourny_list(int type)
{
    int32_t head[7] = {0};
    int n = 0;
    const uint8_t *t = tour_section(head, &n);
    int32_t want = head[3];   /* 0x691e50 */
    if (head[6] == 0) {
        if (type == 1) want = 0x20c;
        else if (type == 2) want = 0x20d;
        else if (type == 3) want = 0x20e;
    } else {
        if (type == 0) want = 0x20c;
        else if (type == 1) want = 0x20d;
        else if (type == 2) want = 0x20e;
    }
    s_ntourny = 0;
    for (int i = 0; t && i < n && s_ntourny < 64; i++) {
        const uint8_t *r = t + 0xd40 * i;
        if (rec16(r, 0xe4) == 2) continue;
        if (rec32(r, 4) == want) s_tourny[s_ntourny++] = i;
    }
}

static int s_remain[10], s_nremain;   /* 0x691e78: the tournament's races not run yet (their indices) */

/* The races of the chosen tournament not run yet (0x504720): of its +0x39c races (at most 10, 0x502150),
   0xa4 bytes each from +0x3a0 (+0x3a0 the track's id, +0x3a4 the prizes of the places, +0x3d0 run). */
static void remain_list(void)
{
    const uint8_t *r = FEData_Tournament();
    s_nremain = 0;
    if (!r) return;
    int n = rec16(r, 0x39c);
    if (n > 10) n = 10;
    for (int i = 0; i < n; i++)
        if (!r[0x3d0 + 0xa4 * i]) s_remain[s_nremain++] = i;
}

/* The nfs5.trk record of a track id (0x4fb4b0; the first one if none has it). */
static const uint8_t *track_by_id(int id)
{
    for (int i = 0; i < s_ntracks; i++)
        if (rec16(s_trk + TRK_SIZE * s_tracks[i], 0) == id) return s_trk + TRK_SIZE * s_tracks[i];
    return s_ntracks ? s_trk + TRK_SIZE * s_tracks[0] : NULL;
}

int FEData_TourRace(void)
{
    FEAccessor *a = FEAccess_Find("Tournament.RemainingTracks");
    if (a) FEAccess_Count(a);
    if (!a || a->pos >= s_nremain) return -1;
    return s_remain[a->pos];
}

int FEData_TourLaps(void)   /* (0x4fb630 for the career: the race's +0x3a2, 1 when 0) */
{
    const uint8_t *r = FEData_Tournament();
    int race = FEData_TourRace();
    int laps = r && race >= 0 ? rec16(r, 0x3a2 + 0xa4 * race) : 0;
    return laps ? laps : 1;
}

const char *FEData_TourTrack(void)
{
    const uint8_t *r = FEData_Tournament();
    int race = FEData_TourRace();
    const uint8_t *t = r && race >= 0 ? track_by_id(rec16(r, 0x3a0 + 0xa4 * race)) : NULL;
    return t ? (const char *)t + 8 : NULL;
}

static void refresh(const FEAccessor *c);

const uint8_t *FEData_Tournament(void)
{
    FEAccessor *a = FEAccess_Find("Tournament.Tournament");
    if (a) refresh(a);
    int n = 0;
    const uint8_t *t = tour_section(NULL, &n);
    if (!a || !t || a->pos >= s_ntourny) return NULL;
    return t + 0xd40 * s_tourny[a->pos];
}

/* The career's year (0x502870: the save's Tournament section's first int, 0x5db75c). */
static int career_year(void)
{
    Save *sv = FESave_Current();
    for (int i = 0; sv && i < sv->nsec; i++)
        if (!strcmp(sv->sec[i].name, "Tournament") && sv->sec[i].size >= 4) {
            int32_t y;
            memcpy(&y, sv->sec[i].data, 4);
            return y;
        }
    return 1950;
}

/* 0x4f13c0: by +0x1b0, then +0x1ac, then the first year. */
static bool car_before(const uint8_t *a, const uint8_t *b)
{
    if (rec32(a, 0x1b0) != rec32(b, 0x1b0)) return rec32(a, 0x1b0) < rec32(b, 0x1b0);
    if (rec32(a, 0x1ac) != rec32(b, 0x1ac)) return rec32(a, 0x1ac) < rec32(b, 0x1ac);
    return rec16(a, 0x1f8) < rec16(b, 0x1f8);
}

/* The dealer's cars (FE_LoadCarLists 0x4f34c0): the racing ones (+0x1f4 kind 0) made in a span of
   years (+0x1f8 .. +0x202) that has this year, not kept from sale (+0x204 not 2 or 3); in order
   (0x4f05c0: a[i] and a[j] swapped while a[j] goes before). */
static void dealer_list(size_t n)
{
    int year = career_year();
    s_ndealer = 0;
    for (size_t i = 0; s_car && (i + 1) * CAR_SIZE <= n && s_ndealer < 160; i++) {
        const uint8_t *r = s_car + CAR_SIZE * i;
        int av = rec16(r, 0x204);
        if (rec16(r, 0x1f4) == 0 && rec16(r, 0x1f8) <= year && year <= rec16(r, 0x202) && av != 2 && av != 3)
            s_dealer[s_ndealer++] = (int)i;
    }
    for (int i = 0; i < s_ndealer; i++)
        for (int j = i + 1; j < s_ndealer; j++)
            if (car_before(s_car + CAR_SIZE * s_dealer[j], s_car + CAR_SIZE * s_dealer[i])) {
                int t = s_dealer[i];
                s_dealer[i] = s_dealer[j];
                s_dealer[j] = t;
            }
}

/* A car's price this year (0x4ed8b0): its price (+0x208), after its first year that times its year's
   factor (+0x20c, one a year) over 100. */
static int32_t car_price(const uint8_t *r)
{
    int year = career_year(), first = rec16(r, 0x1f8);
    int32_t base = rec32(r, 0x208);
    if (first < year) {
        float f;
        memcpy(&f, r + 0x20c + 4 * (year - first), 4);
        return (int32_t)((float)base * f) / 100;
    }
    return base;
}

void FEData_Init(void)
{
    if (s_nacc) return;
    size_t n = 0;
    s_trk = vfs_read_all("FEData\\Data\\nfs5.trk", &n);
    for (size_t i = 0; s_trk && (i + 1) * TRK_SIZE <= n && s_ntracks < 32; i++) {
        char path[128];
        snprintf(path, sizeof path, "GameData\\Track\\%s.crp", (const char *)s_trk + TRK_SIZE * i + 8);   /* "%s.crp" */
        if (vfs_exists(path)) s_tracks[s_ntracks++] = (int)i;
    }
    s_car = vfs_read_all("FEData\\Data\\nfs5.car", &n);
    for (size_t i = 0; s_car && (i + 1) * CAR_SIZE <= n && s_ncars < 160; i++) {
        const uint8_t *r = s_car + CAR_SIZE * i;
        int16_t kind;
        memcpy(&kind, r + 0x1f4, 2);   /* 0 racer, 1 traffic, 2 cop */
        char path[128];
        snprintf(path, sizeof path, "GameData\\CarModel\\%s.crp", (const char *)r + 0x7f);   /* "%s%s.crp" */
        if (kind == 0 && vfs_exists(path)) s_cars[s_ncars++] = (int)i;
    }
    char v[64];
    int opp = fe_key("numopponentracecars", v, sizeof v) ? atoi(v) : 4;
    add("Game.NumOpponents", ACC_INT, 8, 0, opp);   /* 0x4b9ef0(name, 0, 4, 0, 7, ...) */
    int tp = 0;
    if (fe_key("track_name", v, sizeof v))
        for (int i = 0; i < s_ntracks; i++)
            if (!strcasecmp(trk_name(i), v)) tp = i;
    add("Track1.Track", ACC_TRACK, s_ntracks, 0, tp);
    int cp = 0;
    for (int i = 0; i < s_ncars; i++)
        if (!strcasecmp(car_file(i), "964")) cp = i;
    /* FEMergedCarListAccessor (0x4f3c40, vtable 0x5b773c): the owned cars, then the generic ones
       (0x4f3f10: a position below the first list's count is its) */
    add("zMergedList.Car", ACC_MERGED, s_ncars, 0, cp);
    audio();
    display();
    graphics();
    add("OpponentCar.Car", ACC_CAR, s_ncars, 0, cp);
    dealer_list(n);
    add("DealerCar.Car", ACC_DEALER, s_ndealer, 0, 0);   /* FEdaCar_v0 0x4f1520 -> 0x4f2560 */
    add("OwnedCars.Car", ACC_OWNED, 0, 0, 0);
    add("Tournament.Data", ACC_TDATA, 1, 0, 0);          /* FETournamentDataAccessor (0x5b81f0) */
    add_list("Tournament.EventType", 0x5b81a4, 0);          /* FETournamentEventTypeAccessor (0x506140) */
    add("Tournament.Tournament", ACC_TOURNY, 0, 0, 0);    /* FETournyAccessor (0x505bc0, vtable 0x5b88f0) */
    add("Tournament.RemainingTracks", ACC_RTRACK, 0, 0, 0);   /* FETrackAccessor (0x4f9a90 with 0x504720) */
}

FEAccessor *FEAccess_Find(const char *path)
{
    char name[64];
    snprintf(name, sizeof name, "%s", path ? path : "");
    char *paren = strchr(name, '(');
    if (paren) *paren = 0;
    for (int i = 0; i < s_nacc; i++)
        if (!strcmp(s_acc[i].name, name)) return &s_acc[i];
    return NULL;
}

static int owned_count(void)
{
    Save *sv = FESave_Current();
    return sv ? sv->ncars : 0;
}

static void refresh(const FEAccessor *c)
{
    FEAccessor *a = (FEAccessor *)c;
    if (a->kind == ACC_RTRACK) {
        remain_list();
        a->count = s_nremain;
        if (a->pos >= a->count) a->pos = a->count ? a->count - 1 : 0;
        return;
    }
    if (a->kind == ACC_TOURNY) {
        FEAccessor *e = FEAccess_Find("Tournament.EventType");
        tourny_list(e ? e->pos : 0);
        a->count = s_ntourny;
        if (a->pos >= a->count) a->pos = a->count ? a->count - 1 : 0;
        return;
    }
    if (a->kind == ACC_MERGED) {
        int o = owned_count();
        if (o != a->owned) {   /* (the same car kept chosen when the owned list changes) */
            a->pos += o - a->owned;
            if (a->pos < 0) a->pos = 0;
            a->owned = o;
        }
        a->count = o + s_ncars;
        return;
    }
    if (a->kind != ACC_OWNED) return;
    Save *sv = FESave_Current();
    a->count = sv ? sv->ncars : 0;
    if (a->pos >= a->count) a->pos = a->count ? a->count - 1 : 0;
}

int FEAccess_Count(const FEAccessor *a) { refresh(a); return a->count; }
int FEAccess_Pos(const FEAccessor *a) { return a->pos; }

void FEAccess_SetPos(FEAccessor *a, int pos)
{
    refresh(a);
    if (!a->count) return;
    a->pos = ((pos % a->count) + a->count) % a->count;
    if (a->key) Options_SetInt(a->key, a->min + a->pos);   /* (the accessor's option, slot 11 0x4bbe20) */
}

const char *FEAccess_Text(const FEAccessor *a)
{
    static char b[64];
    refresh(a);
    if (!a->count) return "";
    switch (a->kind) {
    case ACC_INT:
        snprintf(b, sizeof b, "%d", a->min + a->pos);
        return b;
    case ACC_TRACK: {
        int32_t id;
        memcpy(&id, s_trk + TRK_SIZE * s_tracks[a->pos] + 4, 4);
        return FEStrings_Get(id);
    }
    case ACC_CAR:
        snprintf(b, sizeof b, "%s", (const char *)s_car + CAR_SIZE * s_cars[a->pos] + 0x34);
        return b;
    case ACC_LIST:
        return FEStrings_Get((int32_t)exe_u32(a->list + 4u * (unsigned)a->pos));
    case ACC_DEALER:
        snprintf(b, sizeof b, "%s", (const char *)s_car + CAR_SIZE * s_dealer[a->pos] + 0x34);
        return b;
    case ACC_MERGED: {
        const uint8_t *r = FEAccess_CarRecord(a);
        snprintf(b, sizeof b, "%s", r ? (const char *)r + 0x34 : "");
        return b;
    }
    case ACC_OWNED: {
        Save *sv = FESave_Current();
        snprintf(b, sizeof b, "%s", sv ? (const char *)sv->cars[a->pos].rec + 4 + 0x34 : "");
        return b;
    }
    case ACC_TDATA:
        return "";
    case ACC_RTRACK: {   /* the track's name (its record's string, +4) */
        const uint8_t *r = FEData_Tournament();
        const uint8_t *t = r ? track_by_id(rec16(r, 0x3a0 + 0xa4 * s_remain[a->pos])) : NULL;
        const char *n = t ? FEStrings_Get(rec32(t, 4)) : NULL;
        return n ? n : "";
    }
    case ACC_TOURNY: {   /* SHORT: the name (+0x20) */
        const uint8_t *r = FEData_Tournament();
        const char *n = r ? FEStrings_Get(rec32(r, 0x20)) : NULL;
        return n ? n : "";
    }
    }
    return "";
}

const uint8_t *FEAccess_CarRecord(const FEAccessor *a);

const char *FEAccess_TextArg(const FEAccessor *a, const char *arg)
{
    static char b[16];
    if (a->kind == ACC_TDATA) {   /* 0x5058a0 */
        Save *sv = FESave_Current();
        if (arg && !strcmp(arg, "CASH")) return FE_MoneyText(sv ? sv->money : 0);
        if (arg && !strcmp(arg, "DEALERCARPRICE")) {
            FEAccessor *d = FEAccess_Find("DealerCar.Car");
            return d && d->count ? FE_MoneyText(car_price(s_car + CAR_SIZE * s_dealer[d->pos])) : "";
        }
        return "";   /* (the other data: pending) */
    }
    if (arg && !strcmp(arg, "CLASSLISTING")) {   /* the car accessors' (0x4f0130 ..): "%s %s" 0x5da730 */
        const uint8_t *r = FEAccess_CarRecord(a);
        static char cb[96];
        if (!r) return "";
        const char *c1 = FEStrings_Get(rec32(r, 0x1b0) - 10), *c2 = FEStrings_Get(rec32(r, 0x1ac) + 0xd);
        snprintf(cb, sizeof cb, "%s %s", c1 ? c1 : "", c2 ? c2 : "");
        return cb;
    }
    if (a->kind == ACC_TOURNY && arg && *arg && strcmp(arg, "SHORT")) {   /* FETournyAccessor_v5 0x505cd0 */
        const uint8_t *r = FEData_Tournament();
        static char tb[32];
        if (!r) return "";
        if (!strcmp(arg, "DESCRIPTION")) return FEStrings_Get(rec32(r, 0x28)) ? FEStrings_Get(rec32(r, 0x28)) : "";
        if (!strcmp(arg, "CARRESTRICTION")) return FEStrings_Get(rec32(r, 0x2c)) ? FEStrings_Get(rec32(r, 0x2c)) : "";
        if (!strcmp(arg, "ENTRYFEE")) return FE_MoneyText(rec32(r, 0xdc));
        if (!strcmp(arg, "PRIZEMONEY")) return FE_MoneyText(rec32(r, 0xec));
        if (!strcmp(arg, "TIMESCOMPLETED")) { snprintf(tb, sizeof tb, "%d", rec16(r, 0xd3c)); return tb; }
        if (!strcmp(arg, "LOCATION")) return FEAccess_Text(FEAccess_Find("Tournament.RemainingTracks"));
        PENDING_NOTE(0x505cd0, "the tournament's difficulty and record time (the remaining tracks)");
        return "";
    }
    if (a->kind == ACC_OWNED && arg) {   /* FEOwnedCarAccessor's (0x4f8xxx) */
        refresh(a);
        Save *sv = FESave_Current();
        if (!sv || !a->count) return "";
        if (!strcmp(arg, "PURCHASEPRICE")) return FE_MoneyText(rec32(sv->cars[a->pos].rec, 0x6a4));
        if (!strcmp(arg, "CURRENTVALUE")) {
            PENDING_NOTE(0x4f83f0, "the owned car's value (mileage, damage and parts tables)");
            return "";
        }
        if (strcmp(arg, "SHORT")) return "";   /* (the damages and the rest: pending) */
    }
    if (!arg || !*arg || !strcmp(arg, "SHORT")) return FEAccess_Text(a);
    if (!strcmp(arg, "PERCENT")) {   /* 0x4bc0e0 */
        int p = a->count < 2 ? 100 : a->pos * 100 / (a->count - 1);
        snprintf(b, sizeof b, "%d", p > 99 ? 100 : p);
        return b;
    }
    return arg;
}

const uint8_t *FEAccess_CarRecord(const FEAccessor *a)
{
    if (!a) return NULL;
    refresh(a);
    if (!a->count) return NULL;
    if (a->kind == ACC_MERGED) {
        Save *sv = FESave_Current();
        if (a->pos < a->owned && sv) return sv->cars[a->pos].rec + 4;
        return s_car + CAR_SIZE * s_cars[a->pos - a->owned];
    }
    if (a->kind == ACC_CAR) return s_car + CAR_SIZE * s_cars[a->pos];
    if (a->kind == ACC_DEALER) return s_car + CAR_SIZE * s_dealer[a->pos];
    if (a->kind == ACC_OWNED) {
        Save *sv = FESave_Current();
        return sv ? sv->cars[a->pos].rec + 4 : NULL;
    }
    return NULL;
}

const char *FEAccess_CarFile(const FEAccessor *a)
{
    if (!a) return NULL;
    refresh(a);
    if (!a->count) return NULL;
    if (a->kind == ACC_MERGED) {
        const uint8_t *r = FEAccess_CarRecord(a);
        return r ? (const char *)r + 0x7f : NULL;
    }
    if (a->kind == ACC_CAR) return car_file(a->pos);
    if (a->kind == ACC_DEALER) return (const char *)s_car + CAR_SIZE * s_dealer[a->pos] + 0x7f;
    if (a->kind == ACC_OWNED) {
        Save *sv = FESave_Current();
        return sv ? (const char *)sv->cars[a->pos].rec + 4 + 0x7f : NULL;
    }
    return NULL;
}

/* The tournament's standings (0x502c10): each racer's points (+0x3d2, shorts), times (+0x3e4) and
   earnings (+0x404 + +0x424) summed over the races run (+0x3d0); the player's rank among the eight (1 ..;
   8 when none is run). The port ranks by points, then time (the original's order is a sort of these). */
static int tour_rank(const uint8_t *r)
{
    int n = rec16(r, 0x39c), run = 0;
    if (n > 10) n = 10;
    int32_t pts[8] = {0}, tim[8] = {0};
    for (int i = 0; i < n; i++) {
        const uint8_t *x = r + 0xa4 * i;
        if (!x[0x3d0]) continue;
        run++;
        for (int k = 0; k < 8; k++) {
            pts[k] += rec16(x, 0x3d2 + 2 * k);
            tim[k] += rec32(x, 0x3e4 + 4 * k);
        }
    }
    if (!run) return 8;
    int rank = 1;
    for (int k = 1; k < 8; k++)
        if (pts[k] > pts[0] || (pts[k] == pts[0] && tim[k] && tim[k] < tim[0])) rank++;
    return rank;
}

/* A tournament's end (0x503080, for the era's tournaments): every race run and the player in the first
   three of the standings, once more completed (+0xd3c); the first time, the year to the next of the table
   0x5b8670; when the era has no tournament left not completed, the next era (0x209 .. 0x20b; 0x691e54 at
   the last). Below the first three the races open again (the port's: the original's 0x503263 path). */
static void tour_end(uint8_t *r)
{
    int n = rec16(r, 0x39c);
    if (n > 10) n = 10;
    for (int i = 0; i < n; i++) if (!r[0x3d0 + 0xa4 * i]) return;
    Save *sv = FESave_Current();
    SaveSection *ts = NULL;
    for (int i = 0; sv && i < sv->nsec; i++) if (!strcmp(sv->sec[i].name, "Tournament")) ts = &sv->sec[i];
    if (!ts) return;
    int32_t head[7];
    memcpy(head, ts->data, sizeof head);
    if (rec32(r, 4) >= 0x20c || tour_rank(r) <= 3) {
        int16_t t = (int16_t)(rec16(r, 0xd3c) + 1);
        memcpy((uint8_t *)r + 0xd3c, &t, 2);
        if (t == 1 && rec32(r, 4) < 0x20c) {
            for (int i = 0; i + 1 < 17; i++)
                if ((int32_t)exe_u32(0x5b8670 + 4u * (unsigned)i) == head[0]) {
                    head[0] = (int32_t)exe_u32(0x5b8674 + 4u * (unsigned)i);
                    break;
                }
            int tn = 0;
            const uint8_t *all = tour_section(NULL, &tn);
            bool left = false;
            for (int i = 0; i < tn; i++) {
                const uint8_t *q = all + 0xd40 * i;
                if (rec32(q, 4) == head[3] && rec16(q, 0xe4) != 2 && rec16(q, 0xd3c) == 0) left = true;
            }
            if (!left) {
                if (head[3] == 0x20b) head[6] = 1;
                if (head[3] == 0x209) head[3] = 0x20a;
                else head[3] = 0x20b;
                PENDING_NOTE(0x4fcaa0, "the era's end (FEEndOfEra_Run: its screens and movies)");
            }
            memcpy(ts->data, head, sizeof head);
        }
    }
    for (int i = 0; i < n; i++) {   /* the races open again (0x503080's clearing of their results) */
        uint8_t *x = (uint8_t *)r + 0xa4 * i;
        x[0x3d0] = 0;
        memset(x + 0x3d2, 0, 16);
        memset(x + 0x3e4, 0, 32);
        memset(x + 0x404, 0, 32);
        memset(x + 0x424, 0, 32);
    }
}

/* A career race's results into the save (the original's race-over update pends; the port's, on the
   race's fields the standings read): +0x3d0 run; for each of the eight racers its points (+0x3d2: 9 less
   its place, 8 the winner's as the debug "all won" 0x503a00 gives), its time (+0x3e4) and its prize
   (+0x424: the places' prizes from +0x3a8); the player's prize into the money; then the tournament's end
   (0x503080). */
void FEData_TourResult(int race, const int *places, const int *times, int n)
{
    Save *sv = FESave_Current();
    uint8_t *r = (uint8_t *)FEData_Tournament();
    if (!sv || !r || race < 0) return;
    uint8_t *x = r + 0xa4 * race;
    x[0x3d0] = 1;
    for (int k = 0; k < 8; k++) {
        int p = k < n ? places[k] : 0;
        int16_t pts = (int16_t)(1 <= p && p <= 8 ? 9 - p : 0);
        int32_t t = k < n ? times[k] : 0, won = 1 <= p && p <= 8 ? rec32(x, 0x3a8 + 4 * (p - 1)) : 0;
        memcpy(x + 0x3d2 + 2 * k, &pts, 2);
        memcpy(x + 0x3e4 + 4 * k, &t, 4);
        memcpy(x + 0x424 + 4 * k, &won, 4);
        if (k == 0) sv->money += won;
    }
    tour_end(r);
    FESave_Write();
}

/* Buying the dealer's car (FEDealershipCallbackList_v55 0x50e5f0 -> 0x5012a0): not with too little money
   (the message 0x322) or 80 cars already (0x3d); else a new car of the record (0x4eda40 -> 0x4ee230: the
   lowest id no car has, the record, its colours, its state, its stock parts installed, 0x4ee030), the
   price it cost at +0x6a4, the cars in order (0x4f0550), the money less the price. */
int FEData_BuyCar(void)
{
    FEAccessor *d = FEAccess_Find("DealerCar.Car");
    Save *sv = FESave_Current();
    if (!d || !d->count || !sv) return -1;
    const uint8_t *r = s_car + CAR_SIZE * s_dealer[d->pos];
    int32_t price = car_price(r);
    if (sv->money < price) return 0x322;
    if (sv->ncars >= 0x50) return 0x3d;
    SaveCar *nc = realloc(sv->cars, sizeof *nc * (size_t)(sv->ncars + 1));
    if (!nc) return -1;
    sv->cars = nc;
    SaveCar *c = &sv->cars[sv->ncars];
    memset(c, 0, sizeof *c);
    int32_t id = 0;   /* the lowest id of no car (the owned and the used ones: 0x69188c) */
    for (bool again = true; again;) {
        again = false;
        for (int i = 0; i < sv->ncars && !again; i++) if (rec32(sv->cars[i].rec, 0) == id) id++, again = true;
        for (int i = 0; i < sv->nused && !again; i++) if (rec32(sv->used[i].rec, 0) == id) id++, again = true;
    }
    memcpy(c->rec, &id, 4);
    memcpy(c->rec + 4, r, CAR_SIZE);
    /* +0x674 .. +0x688: the accessor's colours (slots 0x74 .. 0x88: pending, 0) */
    c->rec[0x6a0] = 1;
    memcpy(c->rec + 0x6a4, &price, 4);
    static const float f50 = 50.0f, f1 = 1.0f, f30 = 30.0f;
    memcpy(c->rec + 0x6d0, &f50, 4);
    memcpy(c->rec + 0x6d4, &f50, 4);
    memcpy(c->rec + 0x6d8, &f50, 4);
    memcpy(c->rec + 0x6e8, &f1, 4);
    memcpy(c->rec + 0x6ec, &f30, 4);
    memcpy(c->rec + 0x6f0, &f30, 4);
    memcpy(c->rec + 0x714, c->rec + 0x6d0, 0x11 * 4);
    /* the stock parts (the record's list +0x3bc), each with its nfs5.prt value (+0x164), as both the
       car's parts and its installed ones */
    int np = rec16(r, 0x3bc);
    if (np < 0 || np > 64) np = 0;
    c->parts = np ? calloc((size_t)np * 2, sizeof *c->parts) : NULL;
    if (np && !c->parts) np = 0;
    for (int i = 0; i < np; i++) {
        int32_t pid = rec16(r, 0x3be + 2 * i);
        int32_t v = FEData_PartValue(pid);
        c->parts[i][0] = c->parts[np + i][0] = pid;
        c->parts[i][1] = c->parts[np + i][1] = v;
    }
    c->ninstalled = c->nother = np;
    sv->ncars++;
    for (int i = 0; i < sv->ncars; i++)   /* 0x4f0550 */
        for (int j = i + 1; j < sv->ncars; j++)
            if (car_before(sv->cars[j].rec + 4, sv->cars[i].rec + 4)) {
                SaveCar t = sv->cars[i];
                sv->cars[i] = sv->cars[j];
                sv->cars[j] = t;
            }
    int32_t cost = price;   /* (clamped to 999999999 each way) */
    if (cost > 999999999) cost = 999999999;
    if (cost < -999999999) cost = -999999999;
    sv->money -= cost;
    FESave_Write();
    return 0;
}

const char *FEData_Track(void)
{
    FEAccessor *a = FEAccess_Find("Track1.Track");
    return a && a->count ? trk_name(a->pos) : "coastal";
}

const char *FEData_Car(void)
{
    const char *f = FEAccess_CarFile(FEAccess_Find("zMergedList.Car"));
    return f ? f : "964";
}

int FEData_Opponents(void)
{
    FEAccessor *a = FEAccess_Find("Game.NumOpponents");
    return a ? a->min + a->pos : 0;
}
