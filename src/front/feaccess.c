/* The front end's data accessors (feaccess.h). */
#include "front/feaccess.h"
#include "front/fe_parts.h"
#include "exe.h"
#include "pending.h"
#include "game/options.h"
#include "front/fesave.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { TRK_SIZE = 0x4a8, CAR_SIZE = 0x670 };

typedef enum { ACC_INT, ACC_TRACK, ACC_CAR, ACC_LIST, ACC_DEALER, ACC_OWNED, ACC_TDATA, ACC_MERGED } AccKind;

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
