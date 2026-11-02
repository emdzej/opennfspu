/* The front end's data accessors (feaccess.h). */
#include "front/feaccess.h"
#include "support/crt.h"
#include "real/random.h"
#include "front/fe_parts.h"
#include "exe.h"
#include "pending.h"
#include "game/options.h"
#include "front/fesave.h"
#include "front/femarket.h"
#include "front/fefactory.h"
#include "front/feshowcase.h"
#include "vfs.h"
#include "platform.h"
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void Audio_UI(int patch);   /* game/audio.h (whose car.h this file's CAR_SIZE clashes with) */
void HTime_Format(int t, char *out, bool sign);   /* game/hud.h (the same) */

enum { TRK_SIZE = 0x4a8, CAR_SIZE = 0x670 };

typedef enum { ACC_INT, ACC_TRACK, ACC_OPPCAR, ACC_LIST, ACC_DEALER, ACC_OWNED, ACC_TDATA, ACC_MERGED, ACC_TOURNY, ACC_RTRACK, ACC_COLOUR, ACC_INTCOLOUR, ACC_SETUP, ACC_ALLPARTS, ACC_BASKET, ACC_INSTALL, ACC_USEDDMG, ACC_FDATA, ACC_FDCAR, ACC_FDPRIZE, ACC_USERS, ACC_STRING, ACC_SHOWCASE } AccKind;

struct FEAccessor {
    char name[64];
    AccKind kind;
    int pos, count, min;
    uint32_t list;   /* ACC_LIST: the exe's zero-ended string ids (FEListAccessor 0x4bb260) */
    uint32_t key;    /* the option it keeps (its key's address), 0 none */
    int owned;       /* ACC_MERGED: the owned cars it counted last */
    float fmin, fstep; int dec, slot;   /* ACC_SETUP: FEFloatAccessor's range, decimals; the car's float */
    int src;         /* ACC_OWNED: 1 the market's cars; ACC_COLOUR / ACC_INTCOLOUR: 1 the garage car's */
    uint32_t cval;   /* FEColourAccessor's colour (+0x2c) for the garage car's (src 1) */
};

enum { NACC = 128 };
static FEAccessor s_acc[NACC];   /* 0x65b3a8 */
static int s_nacc;
static uint8_t *s_trk, *s_car;
static size_t s_trk_size;
static size_t s_car_size;
static int s_tracks[32], s_ntracks;   /* the records with a model (0x691d00) */
static int s_avail[32], s_navail;     /* 0x691d04: the ones to choose (0x4fb190) */
static int s_looped[32], s_nlooped;   /* 0x691d08: those of them looped (+0x6e 1) */
static int s_fe_gametype;             /* Game.GameType as the front end set it */
static int s_cars[160], s_ncars;      /* (0x6919f0) */
static void pick_opponent_cars(void);
static char s_nameinput[9];           /* UserList.NameInput's string (+0x14, 8 characters) */
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

/* An era's year (0x501f40: the classic 1970, the golden 1989, the modern 2000; others 0). */
static int era_year(int32_t era) { return era == 0x209 ? 1970 : era == 0x20a ? 1989 : era == 0x20b ? 2000 : 0; }

static const uint8_t *tour_section(int32_t *head, int *n);
static int career_year(void);
static int16_t rec16(const uint8_t *r, int off);
static int32_t rec32(const uint8_t *r, int off);

/* The track lists (0x4fb190): of the records with a model, all but the skidpad ("Skidpad" 0x5da6d0,
   any case); a bonus track (+0x6c 3) only in multiplayer (Game.GameType 3) or once a tournament of an
   era reached (its year at most the career's) races there (its +0xa ids from +0xc); the looped ones
   (+0x6e 1) a list of their own. */
static void track_lists(void)
{
    int32_t head[7];
    int nt = 0, year = career_year();
    const uint8_t *t = tour_section(head, &nt);
    s_navail = s_nlooped = 0;
    for (int i = 0; i < s_ntracks; i++) {
        const uint8_t *r = s_trk + TRK_SIZE * s_tracks[i];
        bool ok = rec16(r, 0x6c) != 3;
        for (int k = 0; !ok && t && k < nt; k++) {
            const uint8_t *tk = t + 0xd40 * k;
            if (era_year(rec32(tk, 4)) > year) continue;
            for (int j = 0; j < rec16(tk, 0xa) && !ok; j++) ok = rec16(tk, 0xc + 2 * j) == rec16(r, 0);
        }
        if (!(s_fe_gametype == 3 || ok) || !strcasecmp((const char *)r + 8, exe_str(0x5da6d0))) continue;
        s_avail[s_navail++] = s_tracks[i];
        if (rec16(r, 0x6e) == 1) s_looped[s_nlooped++] = s_tracks[i];
    }
}

void FEData_SetGameType(int type) { s_fe_gametype = type; }

/* A track accessor's record index: its list's entry (src 1 the looped, else the choosable). */
static int track_rec(const FEAccessor *a, int i)
{
    const int *l = a && a->src == 1 ? s_looped : s_avail;
    int n = a && a->src == 1 ? s_nlooped : s_navail;
    return n ? l[i < n ? i : n - 1] : 0;
}

static const char *trk_name_of(const FEAccessor *a, int i) { return (const char *)s_trk + TRK_SIZE * track_rec(a, i) + 8; }
static const char *car_file(int i) { return (const char *)s_car + CAR_SIZE * s_cars[i] + 0x7f; }
static const uint8_t *generic_rec(void);
const uint8_t *FEData_GenericCar(void) { return generic_rec(); }

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

extern uint32_t g_carnschemes;   /* 0x6115f8 (render/carpages.h): the last car's stripe packages */
static int career_year(void);
static bool car_before(const uint8_t *a, const uint8_t *b);
static SaveCar *garage_car(void);
static SaveCar *acc_car(const FEAccessor *a);
static int allparts_count(void);
typedef struct { bool fitted; int idx; } InstallRow;
static int install_rows(InstallRow *out, int cap);
static int fit_parts(const uint8_t *r, int year, const uint8_t **out, int cap);
static void allparts_row(int row, int *g, int *i);
static int s_nbasket;
static bool s_open_sale, s_open_stock;
int FEData_CareerYear(void) { return career_year(); }

static const uint8_t *part_rec(int32_t id)   /* 0x49c3e0: an nfs5.prt record by its id */
{
    static uint8_t *prt;
    static size_t nprt;
    if (!prt) prt = vfs_read_all("FEData\\Data\\nfs5.prt", &nprt);   /* (0x5d0c4c) */
    for (size_t i = 0; prt && (i + 1) * 0x168 <= nprt; i++)
        if (rec16(prt + 0x168 * i, 0) == id) return prt + 0x168 * i;
    return NULL;
}

/* A part's category string (0x518640: by its type, 6 and 7 one). */
static int part_category(const uint8_t *p)
{
    static const int k[11] = {0, 0x9fa, 0x9fb, 0x9fc, 0x9fd, 0x9fe, 0x9ff, 0x9ff, 0xa00, 0xa01, 0xa02};
    int t = rec16(p, 0x26);
    return t >= 1 && t <= 10 ? k[t] : 0;
}

int32_t FEData_PartValue(int32_t id)
{
    const uint8_t *p = part_rec(id);
    return p ? rec32(p, 0x164) : 0;
}

static float recf(const uint8_t *r, int off) { float v; memcpy(&v, r + off, 4); return v; }

/* A part's worth in a year (0x517b00): its price (+0x5c) grown by the yearly rates since its year (+0x46,
   the table 0x5ba4a4, at most 0x3e years), less its wear (its saved value, 0 .. 100 per cent). */
static int32_t part_worth(int32_t id, int32_t saved, int year)
{
    const uint8_t *p = part_rec(id);
    if (!p) return 0;
    int n = year - rec16(p, 0x46), sum = 0;
    for (int i = 0; i < n && i < 0x3e; i++) sum += (int32_t)exe_u32(0x5ba4a4 + 4u * (unsigned)i);
    int32_t price = rec32(p, 0x5c);
    float wf;
    memcpy(&wf, &saved, 4);
    int w = (int)wf;
    if (w > 100) w = 100;
    if (w < 0) w = 0;
    return (100 - w) * (price * sum / 100 + price) / 100;
}

/* The average wear (+0x164) of a car's installed parts of a type (+0x26; 0x4eebf0). */
static float wear_of(const SaveCar *c, int type)
{
    float sum = 0;
    int n = 0;
    for (int i = c->ninstalled; i < c->ninstalled + c->nother; i++) {
        const uint8_t *p = part_rec(c->parts[i][0]);
        if (!p || rec16(p, 0x26) != type) continue;
        float w;
        memcpy(&w, &c->parts[i][1], 4);
        sum += w;
        n++;
    }
    return 1 < n ? sum / (float)n : sum;
}

/* A part's repair in a year (0x518160): its price grown since its year as its worth, times its wear. */
static int32_t part_repair(int32_t id, int32_t saved, int year)
{
    const uint8_t *p = part_rec(id);
    if (!p) return 0;
    int n = year - rec16(p, 0x46), sum = 0;
    for (int i = 0; i < n && i < 0x3e; i++) sum += (int32_t)exe_u32(0x5ba4a4 + 4u * (unsigned)i);
    int32_t price = rec32(p, 0x5c);
    float wf;
    memcpy(&wf, &saved, 4);
    return ((price * sum) / 100 + price) * (int)wf / 100;
}

/* The repair screen's categories (FEOwnedCarAccessor's texts, 0x4f5xxx): their part types (+0x26) and,
   for the damage, the average wear (0x4eecb0 ..; bodywork 6 and 7 together, 0x4eee40) shown as the
   condition left ("%2.0f" of 100 less it); the cost the installed parts' repairs (0x4ef230 ..). */
static const struct { const char *name; int type, type2; } k_cats[9] = {
    {"ENGINE", 1, 0}, {"BRAKES", 2, 0}, {"TRANSMISSION", 3, 0}, {"SUSPENSION", 4, 0}, {"SHOCKS", 5, 0},
    {"BODYWORK", 6, 7}, {"SWAYBARS", 8, 0}, {"TIRES", 9, 0}, {"COSMETIC", 10, 0}};

static int32_t category_cost(const SaveCar *c, int type, int type2, int year)
{
    int32_t sum = 0;
    for (int i = c->ninstalled; i < c->ninstalled + c->nother; i++) {
        const uint8_t *p = part_rec(c->parts[i][0]);
        int t = p ? rec16(p, 0x26) : -1;
        if (t == type || (type2 && t == type2)) sum += part_repair(c->parts[i][0], c->parts[i][1], year);
    }
    return sum;
}

static void refresh(const FEAccessor *c);

int FEData_OwnedIndex(const char *accessor)
{
    FEAccessor *a = FEAccess_Find(accessor);
    if (!a) return -1;
    refresh(a);
    if (a->kind == ACC_OWNED) return a->count ? a->pos : -1;
    if (a->kind == ACC_MERGED) return a->pos < a->owned ? a->pos : -1;
    return -1;
}

int FEData_OwnedParts(int car, int32_t ids[20])
{
    Save *sv = FESave_Current();
    if (!sv || car < 0 || car >= sv->ncars) return 0;
    const SaveCar *c = &sv->cars[car];
    int n = 0;
    for (int i = c->ninstalled; i < c->ninstalled + c->nother && n < 20; i++) ids[n++] = c->parts[i][0];
    for (int i = n; i < 20; i++) ids[i] = 0;
    return n;
}

bool FEData_OwnedSetup(int car, float set[17])
{
    Save *sv = FESave_Current();
    if (!sv || car < 0 || car >= sv->ncars) return false;
    memcpy(set, sv->cars[car].rec + 0x6d0, 17 * sizeof(float));
    return true;
}

void FEData_AddWear(int car, const int32_t *ids, const float *wear, int n)
{
    Save *sv = FESave_Current();
    if (!sv || car < 0 || car >= sv->ncars) return;
    SaveCar *c = &sv->cars[car];
    for (int k = 0; k < n; k++) {
        if (!ids[k] || wear[k] <= 0) continue;
        for (int i = c->ninstalled; i < c->ninstalled + c->nother; i++) {
            if (c->parts[i][0] != ids[k]) continue;
            float w;
            memcpy(&w, &c->parts[i][1], 4);
            w += wear[k];
            if (w > 100.0f) w = 100.0f;
            memcpy(&c->parts[i][1], &w, 4);
            break;
        }
    }
}

/* QUICKFIX (FEQuickFixCallBackList 0x50e9f0): every category's repair (its check box: all here), not with
   too little money (the message 0x329); else the parts' wear to none (0x4efb00 ..), the money less it. */
int FEData_QuickFix(void)
{
    Save *sv = FESave_Current();
    FEAccessor *a = FEAccess_Find("OwnedCars.Car");
    if (!sv || !a || !FEAccess_Count(a)) return -1;
    SaveCar *c = &sv->cars[FEAccess_Pos(a)];
    int year = career_year();
    int32_t total = 0;
    for (int k = 0; k < 9; k++) total += category_cost(c, k_cats[k].type, k_cats[k].type2, year);
    if (sv->money < total) return 0x329;
    for (int i = c->ninstalled; i < c->ninstalled + c->nother; i++) c->parts[i][1] = 0;   /* (0.0f) */
    sv->money -= total;
    FESave_Write();
    return 0;
}

/* A car's damage in per cent (0x4eefd0): its parts' wear by type, 6 and 7 at half, over 9 (0x5b74b8). */
float FEData_CarDamage(const SaveCar *c)
{
    float a = (wear_of(c, 7) + wear_of(c, 6)) * 0.5f;
    return (a + wear_of(c, 1) + wear_of(c, 10) + wear_of(c, 9) + wear_of(c, 8) + wear_of(c, 5) + wear_of(c, 4) +
            wear_of(c, 3) + wear_of(c, 2)) * (1.0f / 9.0f);
}

/* An owned car's value (0x4f83f0), in the used car market's year (0x691b58): its price times that year's
   factor (+0x20c of the car object, the factors from +0x218 by the year, x 0.01); its mileage's (+0x698,
   the table 0x5b7c08) and its damage's (the table 0x5b7c48) per cents of that; its installed parts' worth
   less its stock ones'; +0x69c per cent of it; at least a twentieth of it (0x5b7b04). */
int32_t FEData_CarValue(const SaveCar *c, int year)
{
    const uint8_t *o = c->rec;
    int foff = 4 * year - 0x1c60;
    if (foff < 0 || foff + 4 > SAVE_CAR) return 0;
    int32_t base = (int32_t)((double)(float)((float)rec32(o, 0x20c) * recf(o, foff)) * 0.01);
    int32_t miles = rec32(o, 0x698), madj = 0, dadj = 0;
    for (uint32_t t = 0x5b7c08; t < 0x5b7c44; t += 12)
        if ((int32_t)exe_u32(t) <= miles && miles <= (int32_t)exe_u32(t + 4)) madj = (int32_t)exe_u32(t + 8) * base / 100;
    int dmg = (int)FEData_CarDamage(c);
    for (uint32_t t = 0x5b7c48; t < 0x5b7c84; t += 12)
        if ((int32_t)exe_u32(t) <= dmg && dmg <= (int32_t)exe_u32(t + 4)) dadj = (int32_t)exe_u32(t + 8) * base / 100;
    int32_t inst = 0, stock = 0;
    for (int i = c->ninstalled; i < c->ninstalled + c->nother; i++) inst += part_worth(c->parts[i][0], c->parts[i][1], year);
    for (int i = 0; i < c->ninstalled; i++) stock += part_worth(c->parts[i][0], c->parts[i][1], year);
    int32_t v = (inst - stock) + (rec32(o, 0x69c) * base) / 100 + madj + dadj + base;
    if ((float)v < (float)base * 0.05f) return (int32_t)((float)base * 0.05f);
    return v;
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

/* The career race's checks (0x4d5200 for Game.GameType 4): the garage car one the tournament allows
   (0x504020: its record's id among the shorts from +0x30, +0xda of them; else 0x32d); at its first race
   (none run, 0x502920 == 0x5038b0) the entry fee (+0xdc, 0x5029e0) paid, 0x359 without the money.
   Before the fee another tournament left unfinished asks 0x35a (0x504110), once (FEApplication +0x174). */
static bool s_tour_asked;   /* (FEApplication +0x174) */

/* The save's tournament records and the current one's index (0x691e6c, which 0x5042b0 sets from
   Tournament.Tournament's position). */
static uint8_t *tour_records(int *n, int *cur)
{
    const uint8_t *t = tour_section(NULL, n);
    const uint8_t *r = FEData_Tournament();
    *cur = t && r ? (int)((r - t) / 0xd40) : -1;
    return (uint8_t *)t;
}

static bool tour_unfinished(const uint8_t *r)   /* some of its races run, some not */
{
    int n = rec16(r, 0x39c), left = 0;
    for (int i = 0; i < n; i++) left += r[0x3d0 + 0xa4 * i] == 0;
    return 0 < left && left < n;
}

bool FEData_TourOtherUnfinished(void)   /* 0x504110 */
{
    int n = 0, cur;
    const uint8_t *t = tour_records(&n, &cur);
    for (int i = 0; t && i < n; i++)
        if (i != cur && tour_unfinished(t + 0xd40 * i)) return true;
    return false;
}

void FEData_TourProceed(void)
{
    /* the question's ok (FEApplication_v1 0x4d25d0): asked, and every other unfinished tournament reset
       (0x504180: each race (0xa4 bytes from +0x3a0) not run, its +0x32 shorts and three arrays of eight
       ints from +0x44 cleared) */
    s_tour_asked = true;
    int n = 0, cur;
    uint8_t *t = tour_records(&n, &cur);
    for (int i = 0; t && i < n; i++) {
        uint8_t *r = t + 0xd40 * i;
        if (i == cur || !tour_unfinished(r)) continue;
        for (int k = 0; k < rec16(r, 0x39c); k++) {
            uint8_t *race = r + 0x3a0 + 0xa4 * k;
            race[0x30] = 0;
            memset(race + 0x32, 0, 0x10);
            memset(race + 0x44, 0, 0x60);
        }
    }
}

int FEData_TourCheck(void)
{
    uint8_t *r = (uint8_t *)FEData_Tournament();
    SaveCar *gc = garage_car();
    Save *sv = FESave_Current();
    if (!r || !gc || !sv) return 0;
    int16_t car = rec16(gc->rec + 4, 0);
    bool ok = false;
    for (int i = 0; i < rec16(r, 0xda) && !ok; i++) ok = rec16(r, 0x30 + 2 * i) == car;
    if (!ok) return 0x32d;
    if (!s_tour_asked && FEData_TourOtherUnfinished()) return 0x35a;
    int n = rec16(r, 0x39c), left = 0;
    for (int i = 0; i < n; i++) left += r[0x3d0 + 0xa4 * i] == 0;
    if (left == n) {
        int32_t fee = rec32(r, 0xdc);
        if (sv->money < fee) return 0x359;
        sv->money -= fee;   /* (0x500890) */
        FESave_Write();
    }
    return 0;
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

int FEData_TourTraffic(int16_t slots[7])
{
    /* (0x4fb630 for the career: without the record's own traffic (+0x380) the count by its traffic
       level +0xe0 (0x5db430: none, light, medium, heavy), else its slots +0x382 that hold a car) */
    memset(slots, 0, 7 * sizeof *slots);
    const uint8_t *r = FEData_Tournament();
    if (!r) return 0;
    if (!r[0x380]) {
        int lvl = rec16(r, 0xe0);
        return 0 <= lvl && lvl < 4 ? (int)exe_u32(0x5db430 + 4u * (uint32_t)lvl) : 0;
    }
    int n = 0;
    for (int i = 0; i < 7; i++) {
        slots[i] = rec16(r, 0x382 + 2 * i);
        if (slots[i]) n++;
    }
    return n;
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

static uint32_t swap32(uint32_t v) { return v >> 24 | (v >> 8 & 0xff00u) | (v << 8 & 0xff0000u) | v << 24; }

/* A record's colours in the renderer's order (+0x218 .. of the description: the body, then the slots
   0x7c and 0x80, then the interior; 0x4bf9a0's order of the slots 0x74 .. 0x80): the body the colour
   entry ci's (v42: +0x3ec, 0x10 an entry), the interior entry ii's (v43: +0x530), the others the record's
   +0x3f0 and +0x3f4 (v31, v32); each stored byte-swapped. */
static void record_colours(const uint8_t *r, int ci, int ii, uint32_t out[4])
{
    int nc = rec16(r, 0x3e6), ni = rec16(r, 0x528);
    out[0] = ci < nc ? swap32((uint32_t)rec32(r, 0x3ec + 0x10 * ci)) : swap32((uint32_t)rec32(r, 0x3ec));
    out[1] = swap32((uint32_t)rec32(r, 0x3f0));
    out[2] = swap32((uint32_t)rec32(r, 0x3f4));
    out[3] = ii < ni ? swap32((uint32_t)rec32(r, 0x530 + 0x10 * ii)) : (ni ? swap32((uint32_t)rec32(r, 0x530)) : 0);
    for (int i = 0; i < 4; i++) out[i] |= 0xff000000u;   /* (0x4bfbb0: | 0xff000000) */
}

bool FEAccess_CarColours(const FEAccessor *a, uint32_t out[4])
{
    if (!a) return false;
    refresh(a);
    if (!a->count) return false;
    Save *sv = FESave_Current();
    if (a->kind == ACC_FDCAR || ((a->kind == ACC_OWNED || (a->kind == ACC_MERGED && a->pos < a->owned)) && sv)) {
        const SaveCar *oc = a->kind == ACC_FDCAR ? FEFactory_Car() : a->kind == ACC_OWNED ? acc_car(a) : &sv->cars[a->pos];
        if (!oc) return false;
        const uint8_t *o = oc->rec;   /* the car object's +0x674 .. (0x4ee230) */
        out[0] = (uint32_t)rec32(o, 0x674) | 0xff000000u;
        out[3] = (uint32_t)rec32(o, 0x678) | 0xff000000u;
        out[1] = (uint32_t)rec32(o, 0x67c) | 0xff000000u;
        out[2] = (uint32_t)rec32(o, 0x680) | 0xff000000u;
        return true;
    }
    const uint8_t *r = FEAccess_CarRecord(a);
    if (!r) return false;
    int ci = 0, ii = 0;
    if (a->kind == ACC_DEALER) {
        FEAccessor *c = FEAccess_Find("DealerCar.Colour"), *i = FEAccess_Find("DealerCar.IntColour");
        if (c) ci = c->pos;
        if (i) ii = i->pos;
    } else if (a->kind == ACC_MERGED) {   /* (a new car: GenericCar.Car's colour, its interior the first) */
        FEAccessor *c = FEAccess_Find("GenericCar.Colour");
        if (c) ci = c->pos;
    }
    record_colours(r, ci, ii, out);
    return true;
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

/* The car setup data area (FEdaCarSetup_v0 0x4f2020): FEFloatAccessor (0x4b9620: min, max, step,
   decimals) on the garage's car's settings, the seventeen floats from +0x6d0 of the car object (their
   defaults 0x4ee230 sets: the downforces and the brake bias 50, the ride, suspension and toe 0, the shocks
   1, the tyres 30, the eight gears' offsets 0). */
static void setup(void)
{
    static const struct { const char *name; float min, max, step; int dec, slot; } f[17] = {
        {"FDownForce", 0, 100, 1, 0, 0},          {"RDownForce", 0, 100, 1, 0, 1},
        {"BrakeBias", 30, 70, 1, 2, 2},           {"RideHeight", -20, 20, 1, 2, 3},
        {"Suspension", -30, 30, 1, 2, 4},         {"SteeringToe", -5, 5, 0.1f, 2, 5},
        {"ShockRate", 0.7f, 1.5f, 0.01f, 2, 6},   {"FTirePressure", 25, 45, 1, 0, 7},
        {"RTirePressure", 25, 45, 1, 0, 8},       {"Gear0", -0.5f, 0.5f, 0.05f, 2, 9},
        {"Gear1", -0.5f, 0.5f, 0.05f, 2, 10},     {"Gear2", -0.45f, 0.45f, 0.05f, 2, 11},
        {"Gear3", -0.4f, 0.405f, 0.05f, 2, 12},   {"Gear4", -0.35f, 0.35f, 0.05f, 2, 13},
        {"Gear5", -0.3f, 0.3f, 0.05f, 2, 14},     {"Gear6", -0.25f, 0.25f, 0.05f, 2, 15},
        {"Gear7", -0.5f, 0.5f, 0.05f, 2, 16}};
    for (int i = 0; i < 17; i++) {
        FEAccessor *a = add(f[i].name, ACC_SETUP, (int)((f[i].max - f[i].min) / f[i].step + 0.5f) + 1, 0, 0);
        if (!a) continue;
        a->fmin = f[i].min, a->fstep = f[i].step, a->dec = f[i].dec, a->slot = f[i].slot;
    }
}

static SaveCar *garage_car(void);

/* A car list accessor's cars (FEOwnedCarAccessor 0x4f5080 over its list's function): OwnedCars.Car the
   player's (0x501070), UsedCar.Car the market's own (0x4f85b0: the save's used cars, in its order). */
static SaveCar *acc_cars(const FEAccessor *a, int *n)
{
    Save *sv = FESave_Current();
    *n = 0;
    if (!sv) return NULL;
    if (a->src == 1) return *n = sv->nused, sv->used;
    return *n = sv->ncars, sv->cars;
}

static SaveCar *acc_car(const FEAccessor *a)
{
    int n;
    SaveCar *l = acc_cars(a, &n);
    return l && a->pos >= 0 && a->pos < n ? &l[a->pos] : NULL;
}

/* ---- the parts shop (FEPurchasePartsCBL 0x518ba0) ---- */

/* The player's parts in stock (0x69a0ec: the save's PartsMarket section, n then n parts of 0x168). */
static SaveSection *stock_section(void)
{
    Save *sv = FESave_Current();
    for (int i = 0; sv && i < sv->nsec; i++)
        if (!strcmp(sv->sec[i].name, "PartsMarket") && sv->sec[i].size >= 4) return &sv->sec[i];
    return NULL;
}

static int stock_count(void)
{
    SaveSection *x = stock_section();
    int32_t n = x ? rec32(x->data, 0) : 0;
    return x && n > 0 && (size_t)n * 0x168 + 4 <= x->size ? n : 0;
}

static uint8_t *stock_part(int i) { SaveSection *x = stock_section(); return x ? x->data + 4 + 0x168 * i : NULL; }

static void stock_add(const uint8_t *part)   /* (0x517ca0's copy) */
{
    SaveSection *x = stock_section();
    if (!x) return;
    int n = stock_count();
    uint8_t *d = realloc(x->data, 4 + (size_t)(n + 1) * 0x168);
    if (!d) return;
    x->data = d;
    memcpy(d + 4 + 0x168 * n, part, 0x168);
    n++;
    memcpy(d, &n, 4);
    x->size = 4 + (size_t)n * 0x168;
}

static void stock_remove(int i)
{
    SaveSection *x = stock_section();
    int n = stock_count();
    if (!x || i < 0 || i >= n) return;
    memmove(x->data + 4 + 0x168 * i, x->data + 4 + 0x168 * (i + 1), (size_t)(n - i - 1) * 0x168);
    n--;
    memcpy(x->data, &n, 4);
    x->size = 4 + (size_t)n * 0x168;
}

/* The stock without the body (0x518460: type 10 named "Body" kept out). */
static int stock_list(int *out, int cap)
{
    int n = 0, k = stock_count();
    const char *body = exe_str(0x5da740);
    for (int i = 0; i < k && n < cap; i++) {
        const uint8_t *p = stock_part(i);
        if (rec16(p, 0x26) == 10 && !strcmp((const char *)p + 0x28, body)) continue;
        out[n++] = i;
    }
    return n;
}

/* (s_open_sale, s_open_stock: the groups shown, +0x2c, +0x2d) */
static const uint8_t *s_sale[256];       /* (+0x38: the parts that fit the garage car this year, 0x5175f0) */
static int s_nsale, s_sale_car = -2, s_sale_year;

static void sale_list(void)   /* FEPartsAllPartsAccessor_v2 0x514c70's */
{
    SaveCar *gc = garage_car();
    int car = FEData_OwnedIndex("OwnedCars.Car"), year = career_year();
    if (!gc) {
        s_nsale = 0;
        s_sale_car = -2;
        return;
    }
    if (car == s_sale_car && year == s_sale_year) return;
    s_sale_car = car, s_sale_year = year;
    s_nsale = fit_parts(gc->rec + 4, year, s_sale, 256);
}

static int allparts_count(void)
{
    int n = 2, st[256];
    if (s_open_sale) sale_list(), n += s_nsale;
    if (s_open_stock) n += stock_list(st, 256);
    return n;
}

/* A row's group and index (0x514bb0): group 0 the headers (0 the parts for sale, 1 the stock), 1 a
   part for sale, 2 one in stock (its stock index). */
static void allparts_row(int row, int *g, int *i)
{
    *g = 0, *i = 0;
    if (row == 0) return;
    int h = 1;
    if (s_open_sale) {
        sale_list();
        h = s_nsale + 1;
        if (row < h) {
            *g = 1, *i = row - 1;
            return;
        }
    }
    if (row == h) {
        *i = 1;
        return;
    }
    int st[256], n = s_open_stock ? stock_list(st, 256) : 0;
    if (row - h - 1 < n) *g = 2, *i = st[row - h - 1];
}

static const uint8_t *allparts_part(int row, int *g)
{
    int i;
    allparts_row(row, g, &i);
    if (*g == 1) return i < s_nsale ? s_sale[i] : NULL;
    if (*g == 2) return stock_part(i);
    return NULL;
}

/* A part's price in a year (0x517aa0): its price (+0x5c) grown by the yearly rates since its year. */
static int32_t part_price(const uint8_t *p, int year)
{
    int n = year - rec16(p, 0x46), sum = 0;
    for (int i = 0; i < n && i < 0x3e; i++) sum += (int32_t)exe_u32(0x5ba4a4 + 4u * (unsigned)i);
    return rec32(p, 0x5c) * sum / 100 + rec32(p, 0x5c);
}

/* The basket (+0x24): parts to buy (buy, the part's record) or to sell (a stock index). */
typedef struct { uint8_t part[0x168]; bool buy; int stock; } BasketItem;
static BasketItem s_basket[64];   /* (s_nbasket of them) */

static int32_t basket_value(const BasketItem *b)   /* (a sale's worth 0x517b00, a purchase's price less) */
{
    int year = career_year();
    return b->buy ? -part_price(b->part, year) : part_worth(rec16(b->part, 0), rec32(b->part, 0x164), year);
}

/* A part into the basket (0x515990), the basket kept in order: by the parts' order (0x49c2c0), a
   purchase before a sale of the same. */
static void basket_add(const uint8_t *part, bool buy, int stock)
{
    if (s_nbasket >= 64) return;
    BasketItem *b = &s_basket[s_nbasket++];
    memcpy(b->part, part, 0x168);
    b->buy = buy;
    b->stock = stock;
    for (int i = 0; i < s_nbasket; i++)
        for (int j = i + 1; j < s_nbasket; j++) {
            const uint8_t *x = s_basket[j].part, *y = s_basket[i].part;
            bool before = rec16(x, 0x26) != rec16(y, 0x26) ? rec16(x, 0x26) < rec16(y, 0x26)
                        : rec32(x, 0x5c) != rec32(y, 0x5c) ? rec32(x, 0x5c) < rec32(y, 0x5c)
                        : rec16(x, 0) < rec16(y, 0);
            bool after = rec16(y, 0x26) != rec16(x, 0x26) ? rec16(y, 0x26) < rec16(x, 0x26)
                       : rec32(y, 0x5c) != rec32(x, 0x5c) ? rec32(y, 0x5c) < rec32(x, 0x5c)
                       : rec16(y, 0) < rec16(x, 0);
            if (before || (!after && s_basket[j].buy && !s_basket[i].buy)) {
                BasketItem t = s_basket[i];
                s_basket[i] = s_basket[j];
                s_basket[j] = t;
            }
        }
}

static bool in_basket(int stock)
{
    for (int i = 0; i < s_nbasket; i++)
        if (!s_basket[i].buy && s_basket[i].stock == stock) return true;
    return false;
}

void FEData_PartsOpen(void)   /* the screen made (0x518ba0): its groups shut (0x515570), a new car's basket emptied */
{
    s_open_sale = s_open_stock = false;
    static int last = -3;
    int car = FEData_OwnedIndex("OwnedCars.Car");
    if (car != last) s_nbasket = 0, last = car;
    FEAccessor *a = FEAccess_Find("Parts.AllParts");
    if (a) a->pos = 0;
}

int FEData_PartsAction(const char *cb)
{
    FEAccessor *a = FEAccess_Find("Parts.AllParts"), *b = FEAccess_Find("Parts.ShoppingList");
    if (!a || !b) return 0;
    refresh(a);
    refresh(b);
    int g, i;
    const uint8_t *p = allparts_part(a->pos, &g);
    allparts_row(a->pos, &g, &i);
    if (!strcmp(cb, "BUYPARTCALLBACK")) {   /* FEPurchasePartsCBL_v55 0x518e30 */
        if (p && g == 1) basket_add(p, true, -1);
    } else if (!strcmp(cb, "SELLPARTCALLBACK")) {   /* _v56 0x518e80 */
        if (p && g == 2 && !in_basket(i)) basket_add(p, false, i);
    } else if (!strcmp(cb, "CLEARLISTCALLBACK")) {   /* _v57 0x518ee0 -> 0x515ba0 */
        s_nbasket = 0;
    } else if (!strcmp(cb, "REMOVEITEMCALLBACK")) {   /* _v58 0x518ef0 -> 0x515b40 */
        if (b->pos < s_nbasket) {
            memmove(&s_basket[b->pos], &s_basket[b->pos + 1], (size_t)(s_nbasket - b->pos - 1) * sizeof *s_basket);
            s_nbasket--;
        }
    } else if (!strcmp(cb, "PURCHASECALLBACK")) {   /* _v59 0x518f30 */
        Save *sv = FESave_Current();
        int32_t total = 0;
        for (int k = 0; k < s_nbasket; k++) total += basket_value(&s_basket[k]);
        if (!sv) return 0;
        if (sv->money < -total) return 0x32a;
        for (int k = 0; k < s_nbasket; k++) {
            BasketItem *it = &s_basket[k];
            sv->money += basket_value(it);   /* (0x500890) */
            if (it->buy) {
                stock_add(it->part);   /* 0x517ca0 */
            } else {
                stock_remove(it->stock);   /* 0x517ba0 */
                for (int j = k + 1; j < s_nbasket; j++)
                    if (!s_basket[j].buy && s_basket[j].stock > it->stock) s_basket[j].stock--;
            }
        }
        s_nbasket = 0;
        FESave_Write();
    }
    refresh(a);
    refresh(b);
    return 0;
}

bool FEData_PartsState(bool *buy, bool *sell, bool *basket, bool *selected)
{
    FEAccessor *a = FEAccess_Find("Parts.AllParts"), *b = FEAccess_Find("Parts.ShoppingList");
    if (!a || !b) return false;
    refresh(a);
    refresh(b);
    int g, i;
    const uint8_t *p = allparts_part(a->pos, &g);
    allparts_row(a->pos, &g, &i);
    *buy = p && g == 1;
    *sell = p && g == 2 && !in_basket(i);
    *basket = s_nbasket > 0;
    *selected = b->pos < s_nbasket;
    return p != NULL;
}

/* The car setup's parts (FEPartsCarSetupInventoryAccessor's 0x515fa0): the garage car's fitted parts
   but its body, then the stock's that fit it (0x517850: each stock part once for each time its id is in
   the car's list, +0x32e ids from +0x330, but the body), in the parts' order (0x49c2c0). */
static const uint8_t *install_part(InstallRow r, int32_t *value)
{
    SaveCar *gc = garage_car();
    if (r.fitted) {
        if (!gc || r.idx >= gc->nother) return NULL;
        if (value) *value = gc->parts[gc->ninstalled + r.idx][1];
        return part_rec(gc->parts[gc->ninstalled + r.idx][0]);
    }
    const uint8_t *p = stock_part(r.idx);
    if (value && p) *value = rec32(p, 0x164);
    return p;
}

static bool part_before(const uint8_t *x, const uint8_t *y)
{
    if (rec16(x, 0x26) != rec16(y, 0x26)) return rec16(x, 0x26) < rec16(y, 0x26);
    if (rec32(x, 0x5c) != rec32(y, 0x5c)) return rec32(x, 0x5c) < rec32(y, 0x5c);
    return rec16(x, 0) < rec16(y, 0);
}

static int install_rows(InstallRow *out, int cap)
{
    InstallRow rows[256];
    int n = 0;
    SaveCar *gc = garage_car();
    if (!gc) return 0;
    const char *body = exe_str(0x5da740);
    for (int i = 0; i < gc->nother && n < 256; i++) {
        const uint8_t *p = part_rec(gc->parts[gc->ninstalled + i][0]);
        if (p && rec16(p, 0x26) == 10 && !strcmp((const char *)p + 0x28, body)) continue;
        rows[n++] = (InstallRow){true, i};
    }
    const uint8_t *r = gc->rec + 4;
    int nl = rec16(r, 0x32e), ns = stock_count();
    for (int i = 0; i < ns; i++) {
        const uint8_t *p = stock_part(i);
        for (int k = 0; k < nl && k < 0x46 && n < 256; k++) {
            if (rec16(r, 0x330 + 2 * k) != rec16(p, 0)) continue;
            if (rec16(p, 0x26) == 10 && !strcmp((const char *)p + 0x28, body)) continue;
            rows[n++] = (InstallRow){false, i};
        }
    }
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            const uint8_t *x = install_part(rows[j], NULL), *y = install_part(rows[i], NULL);
            if (x && y && part_before(x, y)) {
                InstallRow t = rows[i];
                rows[i] = rows[j];
                rows[j] = t;
            }
        }
    for (int i = 0; i < n && i < cap; i++) out[i] = rows[i];
    return n;
}

/* A fitted part into the stock (0x518020): its nfs5.prt record with its value (+0x164). */
static void unfit(SaveCar *gc, int i)
{
    const uint8_t *p = part_rec(gc->parts[gc->ninstalled + i][0]);
    if (p) {
        uint8_t rec[0x168];
        memcpy(rec, p, 0x168);
        memcpy(rec + 0x164, &gc->parts[gc->ninstalled + i][1], 4);
        stock_add(rec);
    }
    memmove(&gc->parts[gc->ninstalled + i], &gc->parts[gc->ninstalled + i + 1], (size_t)(gc->nother - i - 1) * sizeof *gc->parts);
    gc->nother--;
}

int FEData_InstallAction(const char *cb)
{
    FEAccessor *a = FEAccess_Find("Parts.InstallList");
    SaveCar *gc = garage_car();
    if (!a || !gc) return 0;
    InstallRow rows[256];
    int n = install_rows(rows, 256);
    if (a->pos >= n) return 0;
    InstallRow r = rows[a->pos];
    if (!strcmp(cb, "UNINSTALLPART")) {   /* 0x4f12b0 -> 0x518020 */
        if (r.fitted) unfit(gc, r.idx);
    } else if (!strcmp(cb, "INSTALLPART") && !r.fitted) {   /* 0x4f1300 -> 0x517df0 */
        const uint8_t *p = stock_part(r.idx);
        if (!p) return 0;
        uint8_t rec[0x168];
        memcpy(rec, p, 0x168);
        /* the fitted part of its type and category off into the stock first (the first one) */
        for (int i = 0; i < gc->nother; i++) {
            const uint8_t *q = part_rec(gc->parts[gc->ninstalled + i][0]);
            if (q && rec16(q, 0x26) == rec16(rec, 0x26) && !strcmp((const char *)q + 0x28, (const char *)rec + 0x28)) {
                unfit(gc, i);
                break;
            }
        }
        /* the part out of the stock onto the car */
        int k = -1, ns = stock_count();
        for (int i = 0; i < ns && k < 0; i++)
            if (!memcmp(stock_part(i), rec, 0x168)) k = i;
        if (k < 0) return 0;
        int tot = gc->ninstalled + gc->nother;
        int32_t (*np)[2] = realloc(gc->parts, (size_t)(tot + 1) * sizeof *np);
        if (!np) return 0;
        gc->parts = np;
        gc->parts[tot][0] = rec16(rec, 0);
        memcpy(&gc->parts[tot][1], rec + 0x164, 4);
        gc->nother++;
        stock_remove(k);
    }
    refresh(a);
    FESave_Write();
    return 0;
}

/* Whether the garage car has a part of each kind its stock parts are (0x4ef890: each stock part's
   name, +0x28, and type among the fitted ones'); else the message of the missing ones (string 0x28a,
   "%s  ", then their names, "%s %s" the first, "%s, %s" the rest). */
bool FEData_CarComplete(char *msg, size_t cap)
{
    SaveCar *gc = garage_car();
    if (!gc) return true;
    bool all = true, first = true;
    snprintf(msg, cap, "%s  ", FEStrings_Get(0x28a));
    for (int i = 0; i < gc->ninstalled; i++) {
        const uint8_t *s = part_rec(gc->parts[i][0]);
        if (!s) continue;
        bool have = false;
        for (int k = 0; k < gc->nother && !have; k++) {
            const uint8_t *q = part_rec(gc->parts[gc->ninstalled + k][0]);
            have = q && !strcmp((const char *)q + 0x28, (const char *)s + 0x28) && rec16(q, 0x26) == rec16(s, 0x26);
        }
        if (have) continue;
        all = false;
        char t[512];
        snprintf(t, sizeof t, first ? "%s %s" : "%s, %s", msg, FEStrings_Get(rec32(s, 4)));
        snprintf(msg, cap, "%s", t);
        first = false;
    }
    return all;
}

bool FEData_InstallState(bool *fitted)
{
    FEAccessor *a = FEAccess_Find("Parts.InstallList");
    InstallRow rows[256];
    int n = install_rows(rows, 256);
    if (!a || a->pos >= n) return false;
    *fitted = rows[a->pos].fitted;
    return true;
}

bool FEData_PartsRow(const FEAccessor *a, int row, FEPartsRow *out)
{
    memset(out, 0, sizeof *out);
    int year = career_year();
    if (a->kind == ACC_ALLPARTS) {   /* FEPartsAllPartsAccessor_v15 0x514ef0, _v26 0x515450 */
        int g, i;
        const uint8_t *p = allparts_part(row, &g);
        allparts_row(row, &g, &i);
        out->height = 0xe;
        if (g == 0) {
            out->header = true;
            out->open = i == 0 ? s_open_sale : s_open_stock;
            if (i == 0) {
                SaveCar *gc = garage_car();
                snprintf(out->text, sizeof out->text, FEStrings_Get(0x9f6), gc ? (const char *)gc->rec + 6 : "");
            } else {
                snprintf(out->text, sizeof out->text, "%s", FEStrings_Get(0x9f7));
            }
            return true;
        }
        if (!p) return false;
        snprintf(out->text, sizeof out->text, "%s %s", (const char *)p + 8, FEStrings_Get(rec32(p, 4)));
        out->price = g == 1 ? part_price(p, year) : part_worth(rec16(p, 0), rec32(p, 0x164), year);
        out->has_price = true;
        out->dim = g == 2 && in_basket(i);
        int pg;
        const uint8_t *prev = allparts_part(row - 1, &pg);
        out->category = part_category(p);
        if (!prev || part_category(prev) != out->category) out->height += 0xd, out->label = true;   /* (0x518780) */
        return true;
    }
    if (a->kind == ACC_INSTALL) {   /* FEPartsCarSetupInventoryAccessor_v15 0x516220, _v26 0x5163b0 */
        InstallRow rows[256];
        int n = install_rows(rows, 256);
        if (row < 0 || row >= n) return false;
        const uint8_t *p = install_part(rows[row], NULL);
        if (!p) return false;
        snprintf(out->text, sizeof out->text, "%s %s", (const char *)p + 8, FEStrings_Get(rec32(p, 4)));
        snprintf(out->status, sizeof out->status, "%s", FEStrings_Get(rows[row].fitted ? 0x9f8 : 0xa05));
        out->status_dim = !rows[row].fitted;   /* (at half alpha, 0x80) */
        out->status_pos = "ID_INSTALLYPOS";
        out->height = 0xe;
        out->category = part_category(p);
        const uint8_t *prev = row ? install_part(rows[row - 1], NULL) : NULL;
        if (!prev || part_category(prev) != out->category) out->height += 0xd, out->label = true;
        return true;
    }
    if (a->kind == ACC_USEDDMG) {   /* FEPartsDamageRepairAccessor_v15 0x516640 */
        const SaveCar *uc = acc_car(FEAccess_Find("UsedCar.Car"));
        if (!uc || row < 0 || row >= uc->nother) return false;
        const uint8_t *p = part_rec(uc->parts[uc->ninstalled + row][0]);
        if (!p) return false;
        float wear;
        memcpy(&wear, &uc->parts[uc->ninstalled + row][1], 4);
        snprintf(out->text, sizeof out->text, "%s %s", (const char *)p + 8, FEStrings_Get(rec32(p, 4)));
        snprintf(out->status, sizeof out->status, "%d %%", (int)wear);   /* ("%d %" 0x5dce5c of its wear) */
        out->status_pos = "ID_DAMAGEPOS";
        out->status_align = 2;
        out->dim = wear == 0.0f;   /* (none: at 0x40) */
        out->height = 0xe;
        out->category = part_category(p);
        const uint8_t *prev = row ? part_rec(uc->parts[uc->ninstalled + row - 1][0]) : NULL;
        if (!prev || part_category(prev) != out->category) out->height += 0xd, out->label = true;
        return true;
    }
    if (a->kind != ACC_BASKET) {   /* another accessor's: its item's text (slot 12), 14 high */
        if (row < 0 || row >= a->count) return false;
        FEAccessor t = *a;
        t.pos = row;
        snprintf(out->text, sizeof out->text, "%s", FEAccess_Text(&t));
        out->height = 0xe;
        return true;
    }
    if (a->kind == ACC_BASKET) {   /* FEPartsShoppingBasket_v15 0x515800, _v26 0x515930 */
        if (row < 0 || row >= s_nbasket) return false;
        const uint8_t *p = s_basket[row].part;
        snprintf(out->text, sizeof out->text, "%s %s", (const char *)p + 8, FEStrings_Get(rec32(p, 4)));
        out->price = basket_value(&s_basket[row]);
        out->has_price = true;
        out->height = 0xe;
        out->category = part_category(p);
        if (row == 0 || part_category(s_basket[row - 1].part) != out->category) out->height += 0xd, out->label = true;
        return true;
    }
    return false;
}

/* SAVESETTINGS (0x4f10a0 -> 0x4eeb50): the settings kept (+0x714); LOADSETTINGS (0x4f10c0 -> 0x4eeb30):
   the kept ones back. (The accessors read the car's settings, so they follow.) */
void FEData_KeepSetup(bool load)
{
    SaveCar *car = garage_car();
    if (!car) return;
    if (load) memmove(car->rec + 0x6d0, car->rec + 0x714, 17 * 4);
    else memmove(car->rec + 0x714, car->rec + 0x6d0, 17 * 4);
    FESave_Write();
}

void FEData_DefaultSetup(void)   /* DEFAULTSETTINGS (0x4f10e0 -> 0x4eeb70, as 0x4ee230 a new car's) */
{
    SaveCar *car = garage_car();
    if (!car) return;
    static const float d[17] = {50, 50, 50, 0, 0, 0, 1.0f, 30, 30, 0, 0, 0, 0, 0, 0, 0, 0};
    memcpy(car->rec + 0x6d0, d, sizeof d);
    FESave_Write();
}

void FEData_Init(void)
{
    if (s_nacc) return;
    size_t n = 0;
    /* FE_LoadTrackRecords 0x4fa3c0: the user's nfs5.trk (the records kept), else the game's (the original
       copies it there first; the port writes it once a record changes). The empty records ("n/a",
       0x5db32c) get the longest time, 359999. */
    s_trk = plat_load_user_file("nfs5.trk", &n);
    if (s_trk && (n == 0 || n % TRK_SIZE)) free(s_trk), s_trk = NULL;
    if (!s_trk) s_trk = vfs_read_all("FEData\\Data\\nfs5.trk", &n);
    s_trk_size = s_trk ? n - n % TRK_SIZE : 0;
    for (size_t i = 0; i < s_trk_size / TRK_SIZE; i++)
        for (int k = 0; k < 8; k++) {
            uint8_t *e = s_trk + TRK_SIZE * i + 0x1b0 + 0x4c * k;
            int32_t t;
            memcpy(&t, e + 0x48, 4);
            if (t != 0) continue;
            snprintf((char *)e, 0x14, "%s", exe_str(0x5db32c));
            t = 359999;
            memcpy(e + 0x48, &t, 4);
        }
    for (size_t i = 0; s_trk && (i + 1) * TRK_SIZE <= n && s_ntracks < 32; i++) {
        char path[128];
        snprintf(path, sizeof path, "GameData\\Track\\%s.crp", (const char *)s_trk + TRK_SIZE * i + 8);   /* "%s.crp" */
        if (vfs_exists(path)) s_tracks[s_ntracks++] = (int)i;
    }
    s_car = vfs_read_all("FEData\\Data\\nfs5.car", &n);
    s_car_size = s_car ? n : 0;
    for (size_t i = 0; s_car && (i + 1) * CAR_SIZE <= n && s_ncars < 160; i++) {
        const uint8_t *r = s_car + CAR_SIZE * i;
        int16_t kind;
        memcpy(&kind, r + 0x1f4, 2);   /* 0 racer, 1 traffic, 2 cop */
        char path[128];
        snprintf(path, sizeof path, "GameData\\CarModel\\%s.crp", (const char *)r + 0x7f);   /* "%s%s.crp" */
        if (kind == 0 && vfs_exists(path)) s_cars[s_ncars++] = (int)i;
    }
    for (int i = 0; i < s_ncars; i++)   /* (0x4f05c0, as FE_LoadCarList 0x4f30c0 sorts it) */
        for (int j = i + 1; j < s_ncars; j++)
            if (car_before(s_car + CAR_SIZE * s_cars[j], s_car + CAR_SIZE * s_cars[i])) {
                int t = s_cars[i];
                s_cars[i] = s_cars[j];
                s_cars[j] = t;
            }
    char v[64];
    int opp = fe_key("numopponentracecars", v, sizeof v) ? atoi(v) : 4;
    add("Game.NumOpponents", ACC_INT, 8, 0, opp);   /* 0x4b9ef0(name, 0, 4, 0, 7, ...) */
    int tp = 0;
    track_lists();
    if (fe_key("track_name", v, sizeof v))
        for (int i = 0; i < s_navail; i++)
            if (!strcasecmp(trk_name_of(NULL, i), v)) tp = i;
    add("Track1.Track", ACC_TRACK, s_navail, 0, tp);   /* (0x4f9a90 on 0x4f9490's list) */
    add("Game.NumKOOpponents", ACC_INT, 7, 1, 3);      /* FEIntAccessor 0x4b9ef0(.., 4, 1, 7): 4 of 1 .. 7 */
    /* FEdaUserList_v0 0x4e5410: the name typed (FEStringAccessor 0x4bc490, 8 characters, a file's) and the
       players (FESpecialUserListCharVectAccessor 0x4b8b50, Save_ListPlayers' list) */
    add("UserList.NameInput", ACC_STRING, 1, 0, 0);
    add("UserList.UserList", ACC_USERS, 0, 0, 0);
    FEAccessor *df = add("Game.Difficutly", ACC_LIST, list_count(0x5d9f08), 0, 1);   /* FEListAccessor 0x4bb260, no option; at 1 */
    if (df) df->list = 0x5d9f08;
    /* FEdaTrack (0x4f8d30): the seven slots' tracks, traffic (list 0x5b9628), direction (0x5b963c) and
       laps (0x5b9650: "1" .. "8"; the first slot's 3, its options 0xc .. 0xf in fe.txt: pending) */
    for (int k = 1; k <= 7; k++) {
        char nm[32];
        if (k > 1) {
            snprintf(nm, sizeof nm, "Track%d.Track", k);
            add(nm, ACC_TRACK, s_navail, 0, 0);
        }
        snprintf(nm, sizeof nm, "Track%d.Traffic", k);
        FEAccessor *x = add(nm, ACC_LIST, list_count(0x5b9628), 0, 0);
        if (x) x->list = 0x5b9628;
        snprintf(nm, sizeof nm, "Track%d.Direction", k);
        x = add(nm, ACC_LIST, list_count(0x5b963c), 0, 0);
        if (x) x->list = 0x5b963c;
        snprintf(nm, sizeof nm, "Track%d.Laps", k);
        x = add(nm, ACC_LIST, list_count(0x5b9650), 0, k == 1 ? 2 : 0);
        if (x) x->list = 0x5b9650;
    }
    add("Knockout.TrackNumber", ACC_INT, 7, 0, 0);      /* FEKnockoutTrackNumberAccessor (vtable 0x5b50a4) */
    {
        FEAccessor *lt = add("LoopedTrack.Track", ACC_TRACK, s_nlooped, 0, 0);   /* (on 0x4f94a0's) */
        if (lt) lt->src = 1;
    }
    int cp = 0;
    for (int i = 0; i < s_ncars; i++)
        if (!strcasecmp(car_file(i), "964")) cp = i;
    /* FEMergedCarListAccessor (0x4f3c40, vtable 0x5b773c): the owned cars, then the generic ones
       (0x4f3f10: a position below the first list's count is its) */
    add("zMergedList.Car", ACC_MERGED, s_ncars, 0, cp);
    audio();
    setup();
    display();
    graphics();
    /* FEOpponentCarAccessor (0x4f4480, vtable 0x5b77d8): which cars the opponents race, five choices at 0
       (four when 0x4e2db0 finds less than 33 MB of memory), its seven cars picked at once (0x4f45c0) */
    add("OpponentCar.Car", ACC_OPPCAR, 5, 0, 0);
    pick_opponent_cars();
    dealer_list(n);
    add("DealerCar.Car", ACC_DEALER, s_ndealer, 0, 0);   /* FEdaCar_v0 0x4f1520 -> 0x4f2560 */
    add("DealerCar.Colour", ACC_COLOUR, 1, 0, 0);        /* 0x4b8dd0 on the car's slots 0xa0 / 0xa8 */
    add("DealerCar.IntColour", ACC_INTCOLOUR, 1, 0, 0);  /* (slots 0xa4 / 0xac) */
    /* GenericCar.Colour (0x4b8dd0 with 0x4f0260 / 0x4f0290 on GenericCar.Car, the merged list's cars past
       the owned ones: FEMergedCarListAccessor_v11 0x4f4020 hands it the place) */
    FEAccessor *gcol = add("GenericCar.Colour", ACC_COLOUR, 1, 0, 0);
    if (gcol) gcol->src = 2;
    /* The paint shop's (FEdaGarage 0x4f2xxx): FEColourAccessor 0x4b8dd0 on the garage car's palettes, the
       stripe package (FEListAccessor of 0x5b7644), the race number (FEIntAccessor -1 .. 99). */
    {
        static const struct { const char *name; int kind, slot; } pc[4] = {
            {"OwnedCar.ExtColour", ACC_COLOUR, 0x3ec}, {"OwnedCar.IntColour", ACC_INTCOLOUR, 0x530},
            {"OwnedCar.S1Colour", ACC_COLOUR, 0x3f0}, {"OwnedCar.S2Colour", ACC_COLOUR, 0x3f4}};
        for (int i = 0; i < 4; i++) {
            FEAccessor *c = add(pc[i].name, (AccKind)pc[i].kind, 1, 0, 0);
            if (c) c->src = 1, c->slot = pc[i].slot;
        }
        FEAccessor *pk = add("OwnedCar.PackageNum", ACC_LIST, list_count(0x5b7644), 0, 0);
        if (pk) pk->list = 0x5b7644;
        add("OwnedCar.DecalNum", ACC_INT, 101, -1, 0);
    }
    add("Parts.AllParts", ACC_ALLPARTS, 2, 0, 0);       /* FEPartsAllPartsAccessor 0x514a30 */
    add("Parts.ShoppingList", ACC_BASKET, 0, 0, 0);     /* FEPartsShoppingBasket 0x5156c0 */
    add("Parts.InstallList", ACC_INSTALL, 0, 0, 0);     /* FEPartsCarSetupInventoryAccessor 0x515e70 */
    add("Parts.UsedDamageList", ACC_USEDDMG, 0, 0, 0);  /* FEPartsDamageRepairAccessor 0x5164e0 on UsedCar.Car */
    add("OwnedCars.Car", ACC_OWNED, 0, 0, 0);
    {
        FEAccessor *u = add("UsedCar.Car", ACC_OWNED, 0, 0, 0);   /* (0x4f5080(.., 0x33, 0, 0x4f85b0)) */
        if (u) u->src = 1;
    }
    add("Tournament.Data", ACC_TDATA, 1, 0, 0);          /* FETournamentDataAccessor (0x5b81f0) */
    add_list("Tournament.EventType", 0x5b81a4, 0);          /* FETournamentEventTypeAccessor (0x506140) */
    add("Tournament.Tournament", ACC_TOURNY, 0, 0, 0);    /* FETournyAccessor (0x505bc0, vtable 0x5b88f0) */
    add("Tournament.RemainingTracks", ACC_RTRACK, 0, 0, 0);   /* FETrackAccessor (0x4f9a90 with 0x504720) */
    /* FEdaFactoryDriver_v0 0x518f50: the mission's texts, the team's profile (FEPositionableAccessor
       0x4bbdd0 of 6), the missions cheat (FEIntAccessor 0x4b9ef0: 1 of 1 .. 0x22); the cars' (FEdaCar) */
    add("Factory.Data", ACC_FDATA, 1, 0, 0);
    add("Factory.ProfileNum", ACC_INT, 6, 0, 0);
    add("Factory.MissionCheat", ACC_INT, 0x22, 1, 0);
    add("FactoryDriver.Car", ACC_FDCAR, 1, 0, 0);
    add("FDPrizeCars.Car", ACC_FDPRIZE, 1, 0, 0);
    /* FEdaChronicle_v0 0x5080b0: the chronicle's year (FEIntAccessor 0x4b9ef0: 1948 of 1948 .. 1996; its
       range the era's, 0x4ba0d0); its Showcase.Text (FEShowcaseTextAccessor, vtable 0x5b8d64), the
       showcase's texts by number. */
    add("Chronicle.Year", ACC_INT, 0x7cc - 0x79c + 1, 0x79c, 0);
    add("Showcase.Text", ACC_SHOWCASE, 1, 0, 0);
}

FEAccessor *FEAccess_Find(const char *path)
{
    char name[64];
    snprintf(name, sizeof name, "%s", path ? path : "");
    char *paren = strchr(name, '(');
    if (paren) *paren = 0;
    if (!strncmp(name, "Knockout.", 9) && strcmp(name + 9, "TrackNumber")) {
        /* Knockout.Track, .Traffic, .Direction, .Laps: the slot's (FEAliasAccessor 0x4b8150 on
           Track<n>.*, n Knockout.TrackNumber's + 1) */
        int slot = 0;
        for (int i = 0; i < s_nacc; i++)
            if (!strcmp(s_acc[i].name, "Knockout.TrackNumber")) slot = s_acc[i].pos;
        char t[64];
        snprintf(t, sizeof t, "Track%d.%s", slot + 1, name + 9);
        snprintf(name, sizeof name, "%s", t);
    }
    for (int i = 0; i < s_nacc; i++)
        if (!strcmp(s_acc[i].name, name)) return &s_acc[i];
    return NULL;
}

static int owned_count(void)
{
    Save *sv = FESave_Current();
    return sv ? sv->ncars : 0;
}

static float recf(const uint8_t *r, int off);

static SaveCar *garage_car(void)
{
    Save *sv = FESave_Current();
    FEAccessor *o = FEAccess_Find("OwnedCars.Car");
    if (!sv || !o || !sv->ncars) return NULL;
    int i = o->pos < sv->ncars ? o->pos : 0;
    return &sv->cars[i];
}

static void refresh(const FEAccessor *c)
{
    FEAccessor *a = (FEAccessor *)c;
    if (a->kind == ACC_USERS) {
        FESave_Current();
        a->count = FESave_PlayerCount();
        a->pos = FESave_PlayerChosen() < 0 ? 0 : FESave_PlayerChosen();
        return;
    }
    if (a->kind == ACC_SETUP) {   /* the position from the car's value */
        SaveCar *car = garage_car();
        if (car) {
            float v = recf(car->rec, 0x6d0 + 4 * a->slot);
            int p = (int)((v - a->fmin) / a->fstep + (v >= a->fmin ? 0.5f : -0.5f));
            a->pos = p < 0 ? 0 : p >= a->count ? a->count - 1 : p;
        }
        return;
    }
    if (a->kind == ACC_COLOUR || a->kind == ACC_INTCOLOUR) {   /* the dealer car's (v40 +0x3e6, v41 +0x528) */
        FEAccessor *d = FEAccess_Find("DealerCar.Car");
        const uint8_t *r = d && d->count ? s_car + CAR_SIZE * s_dealer[d->pos] : NULL;
        if (a->src == 1) {   /* the garage car's palettes (0x4f0490, 0x4f0350) */
            SaveCar *gc = garage_car();
            r = gc ? gc->rec + 4 : NULL;
        } else if (a->src == 2) r = generic_rec();
        int n = r ? rec16(r, a->kind == ACC_COLOUR ? 0x3e6 : 0x528) : 0;
        a->count = n > 0 ? n : 1;
        if (a->pos >= a->count) a->pos = 0;
        return;
    }
    if (a->kind == ACC_TRACK) {
        track_lists();
        a->count = a->src == 1 ? s_nlooped : s_navail;
        if (a->pos >= a->count) a->pos = a->count ? a->count - 1 : 0;
        return;
    }
    if (a->kind == ACC_USEDDMG) {   /* (_v2 0x516520: the used car's fitted parts, 0x4efaf0) */
        const SaveCar *uc = acc_car(FEAccess_Find("UsedCar.Car"));
        a->count = uc ? uc->nother : 0;
        if (a->pos >= a->count) a->pos = a->count ? a->count - 1 : 0;
        return;
    }
    if (a->kind == ACC_ALLPARTS || a->kind == ACC_BASKET || a->kind == ACC_INSTALL) {
        a->count = a->kind == ACC_ALLPARTS ? allparts_count() : a->kind == ACC_BASKET ? s_nbasket : install_rows(NULL, 0);
        if (a->pos >= a->count) a->pos = a->count ? a->count - 1 : 0;
        return;
    }
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
    acc_cars(a, &a->count);
    if (a->pos >= a->count) a->pos = a->count ? a->count - 1 : 0;
}

int FEAccess_Count(const FEAccessor *a) { refresh(a); return a->count; }
int FEAccess_Pos(const FEAccessor *a) { return a->pos; }

/* The garage car's palette colour of a colour accessor (0x4f02d0 the body +0x3ec, 0x4f0410 the first
   stripe +0x3f0, 0x4f04d0 the second +0x3f4, 0x4f0390 the interior +0x530; 0x10 an entry, swapped). */
static uint32_t palette_colour(const FEAccessor *a, int i)
{
    SaveCar *gc = garage_car();
    if (!gc) return 0;
    int base = a->kind == ACC_INTCOLOUR ? 0x530 : a->slot;
    return swap32((uint32_t)rec32(gc->rec + 4, base + 0x10 * i));
}

void FEAccess_SetPos(FEAccessor *a, int pos)
{
    refresh(a);
    if (a->kind == ACC_SETUP) {   /* (clamped, not round; FEFloatAccessor) */
        SaveCar *car = garage_car();
        a->pos = pos < 0 ? 0 : pos >= a->count ? a->count - 1 : pos;
        if (car) {
            float v = a->fmin + a->fstep * (float)a->pos;
            memcpy(car->rec + 0x6d0 + 4 * a->slot, &v, 4);
            FESave_Write();
        }
        return;
    }
    if (a->kind == ACC_ALLPARTS) {   /* FEPartsAllPartsAccessor_v11 0x514d10: a header opens or shuts its group */
        if (pos < 0 || pos >= a->count) pos = 0;
        a->pos = pos;
        int g, i;
        allparts_row(pos, &g, &i);
        if (g == 0) {
            if (i == 1) s_open_stock = !s_open_stock;
            else s_open_sale = !s_open_sale;
            refresh(a);
        }
        return;
    }
    if (a->kind == ACC_USERS) {
        FESave_ChoosePlayer(pos);
        refresh(a);
        return;
    }
    if (!a->count) return;
    a->pos = ((pos % a->count) + a->count) % a->count;
    if (a->key) Options_SetInt(a->key, a->min + a->pos);   /* (the accessor's option, slot 11 0x4bbe20) */
    if ((a->kind == ACC_COLOUR || a->kind == ACC_INTCOLOUR) && a->src == 1) a->cval = palette_colour(a, a->pos);
}

int FEAccess_Int(const FEAccessor *a) { return a ? a->min + a->pos : 0; }   /* (FEIntAccessor's slot 0x6c) */

void FEAccess_SetRange(FEAccessor *a, int lo, int hi)   /* 0x4ba0d0 */
{
    if (!a || (a->min == lo && a->min + a->count - 1 == hi)) return;
    a->min = lo;
    a->count = hi - lo + 1;
    if (a->pos >= a->count) a->pos = 0;   /* (0x4bbef0: a place past the end back to 0) */
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
        memcpy(&id, s_trk + TRK_SIZE * track_rec(a, a->pos) + 4, 4);
        return FEStrings_Get(id);
    }
    case ACC_USERS:   /* (_v15 0x4b8bf0: the stem past its picture's digit) */
        snprintf(b, sizeof b, "%s", FESave_PlayerStem(a->pos)[0] ? FESave_PlayerStem(a->pos) + 1 : "");
        return b;
    case ACC_STRING:
        return s_nameinput;
    case ACC_OPPCAR: {   /* FEOpponentCarAccessor_v12 0x4f4d40 */
        static const int16_t id[5] = {0x280, 0x282, 0x283, 0x284, 0x281};
        return FEStrings_Get(id[a->pos < 5 ? a->pos : 0]);
    }
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
        const SaveCar *oc = acc_car(a);
        snprintf(b, sizeof b, "%s", oc ? (const char *)oc->rec + 4 + 0x34 : "");
        return b;
    }
    case ACC_SETUP: {
        SaveCar *car = garage_car();
        float v = car ? recf(car->rec, 0x6d0 + 4 * a->slot) : a->fmin + a->fstep * (float)a->pos;
        snprintf(b, sizeof b, "%.*f", a->dec, (double)v);
        return b;
    }
    case ACC_FDCAR:
    case ACC_FDPRIZE: {
        const uint8_t *r = FEAccess_CarRecord(a);
        snprintf(b, sizeof b, "%s", r ? (const char *)r + 0x34 : "");
        return b;
    }
    case ACC_FDATA:
    case ACC_TDATA:
    case ACC_SHOWCASE:
    case ACC_COLOUR:      /* (a colour box shows its swatch, not a text: FEColorBox pending) */
    case ACC_INTCOLOUR:
        return "";
    case ACC_ALLPARTS:    /* (the item's text, slot 12 0x515590) */
    case ACC_INSTALL:
    case ACC_USEDDMG:
    case ACC_BASKET: {
        FEPartsRow r;
        if (!FEData_PartsRow(a, a->pos, &r)) return "";
        snprintf(b, sizeof b, "%s", r.text);
        return b;
    }
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
bool FEAccess_CarColours(const FEAccessor *a, uint32_t out[4]);

const char *FEAccess_TextArg(const FEAccessor *a, const char *arg)
{
    static char b[16];
    if (a->kind == ACC_SHOWCASE) return FEShowcase_Text(Fert_ParseInt(arg ? arg : ""));   /* v5 0x508540 */
    if (a->kind == ACC_FDATA) {   /* FEFactoryDriverDataAccessor_v5 0x519620 */
        FEAccessor *p = FEAccess_Find("Factory.ProfileNum");
        return FEFactory_Data(arg, p ? p->pos : 0);
    }
    if ((a->kind == ACC_ALLPARTS || a->kind == ACC_INSTALL) && arg && *arg) {
        /* FEPartsAllPartsAccessor_v5 0x515630, FEPartsCarSetupInventoryAccessor_v5 0x516460: whatever the
           argument, the chosen row's part's description, the string after its name's by 500 (0x4e0270).
           On a header the original reads a part pointer it never set; the port gives nothing. */
        const uint8_t *p = NULL;
        refresh(a);
        if (a->kind == ACC_ALLPARTS) {
            int g;
            p = allparts_part(a->pos, &g);
        } else {
            InstallRow rows[256];
            int n = install_rows(rows, 256);
            if (a->pos >= 0 && a->pos < n) p = install_part(rows[a->pos], NULL);
        }
        if (!p) return "";
        const char *s = FEStrings_Get(rec32(p, 4) + 500);
        return s ? s : "";
    }
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
        /* DIFFICULTY and RECORDTIME: Tournament.RemainingTracks' track (0x4f9c30): its difficulty's
           string (+0x78, from 0x1c2), its record list's first time (0x4fa6c0: +0x1b0 +0x48) */
        int race = FEData_TourRace();
        const uint8_t *t = race >= 0 ? track_by_id(rec16(r, 0x3a0 + 0xa4 * race)) : NULL;
        if (!strcmp(arg, "DIFFICULTY")) {
            const char *d = t ? FEStrings_Get(rec16(t, 0x78) + 0x1c2) : NULL;
            return d ? d : "";
        }
        if (!strcmp(arg, "RECORDTIME")) {
            if (!t) return "";
            HTime_Format(rec32(t, 0x1b0 + 0x48), tb, false);
            return tb;
        }
        return exe_str(0x5d79a0);   /* "This is a bug" */
    }
    if (a->kind == ACC_INT && arg && !strcmp(a->name, "Knockout.TrackNumber")) {
        /* FEKnockoutTrackNumberAccessor_v5 0x4ba290: "<d>TRACKNUMBER" its number, "<d>TRACKNAME",
           "<d>TRACKLAPS", "<d>TRACKDIRECTION", "<d>TRACKTRAFFIC" slot d's, for the slots raced (fewer
           than Game.NumKOOpponents); "TRACKNAME", "DIFFICULTY", "RECORDTIME" the chosen slot's */
        static char kb[64];
        int d = arg[0] - '0', n = FEData_KOOpponents();
        const char *f = arg;
        if (d >= 0 && d <= 6) f = arg + 1;
        else d = a->pos;
        if (!strcmp(f, "DIFFICULTY") || !strcmp(f, "RECORDTIME")) {
            /* the slot's track (Track<d>.Track's, 0x4f9c30): its difficulty's string (+0x78, from
               0x1c2), its record list's first time (0x4fa6c0: +0x1b0 +0x48) */
            char nm[32];
            snprintf(nm, sizeof nm, "Track%d.Track", d + 1);
            FEAccessor *ta = FEAccess_Find(nm);
            if (!ta || !s_trk) return "";
            refresh(ta);
            const uint8_t *t = track_by_id(rec16(s_trk + TRK_SIZE * track_rec(ta, ta->pos), 0));
            if (!t) return "";
            if (f[0] == 'D') {
                const char *x = FEStrings_Get(rec16(t, 0x78) + 0x1c2);
                return x ? x : "";
            }
            HTime_Format(rec32(t, 0x1b0 + 0x48), kb, false);
            return kb;
        }
        if (f == arg && strcmp(f, "TRACKNAME")) return "";
        if (d >= n && f != arg) return "";
        if (!strcmp(f, "TRACKNUMBER")) { snprintf(kb, sizeof kb, "%d", d + 1); return kb; }
        const char *fld = !strcmp(f, "TRACKNAME") ? "Track" : !strcmp(f, "TRACKLAPS") ? "Laps"
                        : !strcmp(f, "TRACKDIRECTION") ? "Direction" : !strcmp(f, "TRACKTRAFFIC") ? "Traffic" : NULL;
        if (!fld) return "";
        char nm[32];
        snprintf(nm, sizeof nm, "Track%d.%s", d + 1, fld);
        snprintf(kb, sizeof kb, "%s", FEAccess_Text(FEAccess_Find(nm)));
        return kb;
    }
    if (a->kind == ACC_BASKET && arg && !strcmp(arg, "TOTAL")) {   /* FEPartsShoppingBasket_v5 0x515d30: "%d" */
        int32_t total = 0;
        for (int k = 0; k < s_nbasket; k++) total += basket_value(&s_basket[k]);
        snprintf(b, sizeof b, "%d", total);
        return b;
    }
    if (a->kind == ACC_OWNED && arg) {   /* FEOwnedCarAccessor's (0x4f8xxx) */
        refresh(a);
        Save *sv = FESave_Current();
        const SaveCar *c = acc_car(a);
        if (!sv || !c) return "";
        if (!strcmp(arg, "PURCHASEPRICE")) return FE_MoneyText(rec32(c->rec, 0x6a4));
        if (!strcmp(arg, "CURRENTVALUE")) return FE_MoneyText(FEData_CarValue(c, sv->usedinfo));
        if (!strcmp(arg, "ASKINGPRICE")) return FE_MoneyText(rec32(c->rec, 0x68c));   /* ("%s" of 0x4dba30, 0x4f5770) */
        if (!strcmp(arg, "MILEAGE")) return FE_MoneyText(rec32(c->rec, 0x698));
        static char db[16];
        if (!strcmp(arg, "OVERALLDAMAGE")) { snprintf(db, sizeof db, "%2.0f", 100.0 - FEData_CarDamage(c)); return db; }
        int32_t total = 0;
        for (int k = 0; k < 9; k++) {
            size_t nl = strlen(k_cats[k].name);
            int32_t cost = category_cost(c, k_cats[k].type, k_cats[k].type2, career_year());
            total += cost;
            if (strncmp(arg, k_cats[k].name, nl) || strncmp(arg + nl, "DAMAGE", 6)) continue;
            if (!strcmp(arg + nl + 6, "COST")) return FE_MoneyText(cost);
            if (!arg[nl + 6]) {
                float w = k_cats[k].type2 ? (wear_of(c, k_cats[k].type) + wear_of(c, k_cats[k].type2)) * 0.5f : wear_of(c, k_cats[k].type);
                snprintf(db, sizeof db, "%2.0f", 100.0 - w);   /* "%2.0f" 0x5dae18 */
                return db;
            }
        }
        if (!strcmp(arg, "OVERALLDAMAGECOST")) return FE_MoneyText(total);
        if (strcmp(arg, "SHORT")) return "";   /* (the damages and the rest: pending) */
    }
    if (a->kind == ACC_OPPCAR && arg && !strcmp(arg, "SHORT") && a->pos == 3) return FEStrings_Get(0x285);   /* (v13 0x4f4dc0) */
    if (!arg || !*arg || !strcmp(arg, "SHORT")) return FEAccess_Text(a);
    if (!strcmp(arg, "PERCENT")) {   /* 0x4bc0e0 */
        int p = a->count < 2 ? 100 : a->pos * 100 / (a->count - 1);
        snprintf(b, sizeof b, "%d", p > 99 ? 100 : p);
        return b;
    }
    return arg;
}

/* The paint shop's update (FEPaintShopCBL_v1 0x510af0): a new garage car's colours, stripes and race
   number into the accessors (0x4b9160: the colour itself, its palette place the first that has it); the
   accessors' into the car each frame (its setters 0x94 .. 0xa8: +0x674 .. +0x688), written when they
   changed. */
void FEData_PaintShop(void)
{
    static int last = -2;
    SaveCar *gc = garage_car();
    int idx = FEData_OwnedIndex("OwnedCars.Car");
    static const char *names[4] = {"OwnedCar.ExtColour", "OwnedCar.IntColour", "OwnedCar.S1Colour", "OwnedCar.S2Colour"};
    static const int field[4] = {0x674, 0x678, 0x67c, 0x680};
    FEAccessor *pk = FEAccess_Find("OwnedCar.PackageNum"), *dn = FEAccess_Find("OwnedCar.DecalNum");
    if (!gc) {
        last = -2;
        return;
    }
    if (idx != last) {
        last = idx;
        for (int i = 0; i < 4; i++) {
            FEAccessor *c = FEAccess_Find(names[i]);
            if (!c) continue;
            refresh(c);
            c->cval = (uint32_t)rec32(gc->rec, field[i]);
            c->pos = 0;
            for (int k = 0; k < c->count; k++)
                if ((palette_colour(c, k) & 0xffffffu) == (c->cval & 0xffffffu)) {
                    c->pos = k;
                    break;
                }
        }
        if (pk && pk->count) pk->pos = rec32(gc->rec, 0x684) % pk->count;
        if (dn) {
            int v = rec32(gc->rec, 0x688);
            dn->pos = (v < -1 ? -1 : v > 99 ? 99 : v) + 1;
        }
    }
    bool changed = false;
    for (int i = 0; i < 4; i++) {
        FEAccessor *c = FEAccess_Find(names[i]);
        if (c && (uint32_t)rec32(gc->rec, field[i]) != c->cval) memcpy(gc->rec + field[i], &c->cval, 4), changed = true;
    }
    int32_t pkv = pk ? pk->pos : 0, dv = dn ? dn->min + dn->pos : -1;
    if (rec32(gc->rec, 0x684) != pkv) memcpy(gc->rec + 0x684, &pkv, 4), changed = true;
    if (rec32(gc->rec, 0x688) != dv) memcpy(gc->rec + 0x688, &dv, 4), changed = true;
    if (changed) FESave_Write();
}

uint32_t FEAccess_Colour(const FEAccessor *a)
{
    if (!a || (a->kind != ACC_COLOUR && a->kind != ACC_INTCOLOUR)) return 0;
    refresh(a);
    if (a->src == 1) return a->cval;
    FEAccessor *d = FEAccess_Find("DealerCar.Car");   /* (the dealer car's palette entry) */
    const uint8_t *r = d && d->count ? s_car + CAR_SIZE * s_dealer[d->pos] : NULL;
    if (a->src == 2) r = generic_rec();
    if (!r) return 0;
    return swap32((uint32_t)rec32(r, (a->kind == ACC_INTCOLOUR ? 0x530 : 0x3ec) + 0x10 * a->pos));
}

int FEAccess_Percent(const FEAccessor *a, const char *arg)
{
    if (!a) return 0;
    refresh(a);
    if (a->kind == ACC_OWNED && arg && *arg) {   /* (FEOwnedCarAccessor's slot 0x18: a condition) */
        const SaveCar *c = acc_car(a);
        if (!c) return 0;
        if (!strcmp(arg, "OVERALLDAMAGE")) return (int)(100.0f - FEData_CarDamage(c));
        for (int k = 0; k < 9; k++) {
            size_t nl = strlen(k_cats[k].name);
            if (strncmp(arg, k_cats[k].name, nl) || strcmp(arg + nl, "DAMAGE")) continue;
            float w = k_cats[k].type2 ? (wear_of(c, k_cats[k].type) + wear_of(c, k_cats[k].type2)) * 0.5f : wear_of(c, k_cats[k].type);
            return (int)(100.0f - w);
        }
        return 0;
    }
    return a->count ? a->pos * 100 / a->count : 0;   /* (FEGouraudIndicatorBar's 0x4ca270 without one) */
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
    if (a->kind == ACC_DEALER) return s_car + CAR_SIZE * s_dealer[a->pos];
    if (a->kind == ACC_OWNED) {
        const SaveCar *c = acc_car(a);
        return c ? c->rec + 4 : NULL;
    }
    if (a->kind == ACC_FDCAR) {   /* FEFactoryDriverCarAccessor (0x519370): the mission's car object */
        const SaveCar *c = FEFactory_Car();
        return c ? c->rec + 4 : NULL;
    }
    if (a->kind == ACC_FDPRIZE) {   /* FEFactoryDriverPrizeCarAccessor_v36 0x519e10: the last mission's prize */
        const uint8_t *m = FEFactory_Last();
        if (!m) m = FEFactory_Mission();   /* (the original reads 0x69c8f0 unchecked) */
        return m ? FEData_CarById(rec16(m, 0x10)) : NULL;
    }
    return NULL;
}

const char *FEAccess_CarFile(const FEAccessor *a)
{
    if (!a) return NULL;
    refresh(a);
    if (!a->count) return NULL;
    if (a->kind == ACC_MERGED || a->kind == ACC_FDCAR || a->kind == ACC_FDPRIZE) {
        const uint8_t *r = FEAccess_CarRecord(a);
        return r ? (const char *)r + 0x7f : NULL;
    }
    if (a->kind == ACC_DEALER) return (const char *)s_car + CAR_SIZE * s_dealer[a->pos] + 0x7f;
    if (a->kind == ACC_OWNED) {
        const SaveCar *c = acc_car(a);
        return c ? (const char *)c->rec + 4 + 0x7f : NULL;
    }
    return NULL;
}

/* The tournament's standings place of racer k (0x502c10): each racer's points (+0x3d2, shorts), times
   (+0x3e4) and earnings (+0x404 + +0x424) summed over the races run (+0x3d0), bubble sorted eight times
   (0x501e30): by points, then earnings, then the shorter time; a rally (+0xe8) by the shorter time,
   then the earnings. 1 .. 8; before any race run, k itself (the player, 0, 8th). */
static int tour_standing(const uint8_t *r, int k)
{
    int n = rec16(r, 0x39c), run = 0;
    if (n > 10) n = 10;
    for (int i = 0; i < n; i++) run += r[0x3d0 + 0xa4 * i] != 0;
    if (run == 0) return k == 0 ? 8 : k;
    int32_t e[8][4] = {{0}};   /* points, time, earnings, racer */
    for (int j = 0; j < 8; j++) {
        e[j][3] = j;
        for (int i = 0; i < n; i++) {
            const uint8_t *x = r + 0xa4 * i;
            if (!x[0x3d0]) continue;
            e[j][0] += rec16(x, 0x3d2 + 2 * j);
            e[j][1] += rec32(x, 0x3e4 + 4 * j);
            e[j][2] += rec32(x, 0x424 + 4 * j) + rec32(x, 0x404 + 4 * j);
        }
    }
    bool rally = r[0xe8] != 0;
    for (int pass = 0; pass < 8; pass++)
        for (int j = 0; j < 7; j++) {
            int32_t *u = e[j], *v = e[j + 1];
            bool swap = rally ? (v[1] < u[1] || (v[1] == u[1] && v[2] < u[2]))
                              : (u[0] < v[0] || (v[0] == u[0] && (u[2] < v[2] || (v[2] == u[2] && v[1] < u[1]))));
            if (swap)
                for (int f = 0; f < 4; f++) {
                    int32_t t = u[f];
                    u[f] = v[f];
                    v[f] = t;
                }
        }
    for (int j = 0; j < 8; j++)
        if (e[j][3] == k) return j + 1;
    return k;
}

static int tour_rank(const uint8_t *r) { return tour_standing(r, 0); }

int FEData_TourOpponents(RaceOpponent out[8], int start[9], float *skill)
{
    /* 0x4eb430 with 0: the tournament's racers (+0x114, 0x58 bytes from +0x118): the personality
       (+0x118), the car by id (+0x11a, 0x4f39e0), its colours (+0x14c and +0x15c swapped, the short
       +0x168 twice) and race number (+0x16a); the skills scaled by the race's difficulty (+0x3cc);
       the grid (0x247) by the standings (0x502c10) */
    memset(out, 0, sizeof(RaceOpponent) * 8);
    const uint8_t *r = FEData_Tournament();
    int race = FEData_TourRace();
    if (!r || race < 0) return -1;
    int n = rec16(r, 0x114);
    if (n > 7) n = 7;
    if (n < 0) n = 0;
    for (int i = 0; i < n; i++) {
        const uint8_t *x = r + 0x118 + 0x58 * i;
        const uint8_t *c = FEData_CarById(rec16(x, 2));
        out[i].pers = FEData_PersonalityIndex(rec16(x, 0));
        if (c) snprintf(out[i].spec, sizeof out[i].spec, "%s", (const char *)c + 0xb1);
        out[i].colours[0] = swap32((uint32_t)rec32(x, 0x34));
        out[i].colours[1] = swap32((uint32_t)rec32(x, 0x44));
        out[i].colours[2] = out[i].colours[3] = (uint32_t)(int32_t)rec16(x, 0x50);
        out[i].stripe = 0;
        out[i].racenum = rec16(x, 0x52);
        int np = rec16(x, 4);   /* (its parts: +0x11c, the ids from +0x11e) */
        for (int k = 0; k < np && k < 20; k++) out[i].parts[k] = rec16(x, 6 + 2 * k);
        out[i].nparts = np < 20 ? (np < 0 ? 0 : np) : 20;
    }
    for (int k = 0; k < 9; k++) start[k] = k < 8 ? tour_standing(r, k) - 1 : 0;   /* (0-based) */
    float f;
    memcpy(&f, r + 0x3cc + 0xa4 * race, 4);
    *skill = f;
    return n;
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
   (+0x424: the places' prizes from +0x3a8), a clean car's safe-driving bonus (+0x404); the player's
   prize and bonus into the money; then the tournament's end
   (0x503080). */
static float exe_f32(uint32_t va)
{
    uint32_t v = exe_u32(va);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

void FEData_TourResult(int race, const int *places, const int *times, const bool *clean, int n)
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
        /* the safe-driving bonus (0x502f10's +0x404): a clean car's, the race's whole to the winner, half
           the second's, a quarter the third's */
        if (k < n && clean && clean[k]) {
            int32_t b = rec32(x, 0x3c8), safe = p == 1 ? b : p == 2 ? (int32_t)((float)b * exe_f32(0x5b23f0))
                                                   : p == 3 ? (int32_t)((float)b * exe_f32(0x5b2468)) : 0;
            memcpy(x + 0x404 + 4 * k, &safe, 4);
            if (k == 0) sv->money += safe;
        }
        if (k == 0) sv->money += won;
    }
    tour_end(r);
    FESave_Write();
}

/* A quit career race (0x4ed590 with 0x657e34 set): nothing recorded, only the tournament's end
   (0x503080; 0x503000's resets are the port's s_tour_race). */
void FEData_TourEnd(void)
{
    uint8_t *r = (uint8_t *)FEData_Tournament();
    if (!FESave_Current() || !r) return;
    tour_end(r);
    FESave_Write();
}

/* A new car object of a record (0x4eda40 -> 0x4ee230): its id, the record, the colours (+0x674 ..
   +0x680), the stripe package (+0x684) and race number (+0x688), the market's fields none, the player's
   (+0x6a0), the settings a new car's kept at +0x714 too, the record's stock parts (+0x3bc ids from +0x3be)
   both as its stock (+0x75c) and its fitted ones (+0x758), each with its nfs5.prt value (0x4ee030). The
   id: the lowest no car has (the original's loop compares the new object's own unset +0 against the
   count, so its id is what the heap left there: the port takes the intent). */
static void car_object(Save *sv, SaveCar *c, const uint8_t *r, const int32_t col[4], int32_t stripe, int32_t racenum)
{
    memset(c, 0, sizeof *c);
    int32_t id = 0;
    for (bool again = true; again;) {
        again = false;
        for (int i = 0; i < sv->ncars && !again; i++) if (rec32(sv->cars[i].rec, 0) == id) id++, again = true;
        for (int i = 0; i < sv->nused && !again; i++) if (rec32(sv->used[i].rec, 0) == id) id++, again = true;
    }
    memcpy(c->rec, &id, 4);
    memcpy(c->rec + 4, r, CAR_SIZE);
    memcpy(c->rec + 0x674, col, 16);
    memcpy(c->rec + 0x684, &stripe, 4);
    memcpy(c->rec + 0x688, &racenum, 4);
    c->rec[0x6a0] = 1;
    static const float f50 = 50.0f, f1 = 1.0f, f30 = 30.0f;
    memcpy(c->rec + 0x6d0, &f50, 4);
    memcpy(c->rec + 0x6d4, &f50, 4);
    memcpy(c->rec + 0x6d8, &f50, 4);
    memcpy(c->rec + 0x6e8, &f1, 4);
    memcpy(c->rec + 0x6ec, &f30, 4);
    memcpy(c->rec + 0x6f0, &f30, 4);
    memcpy(c->rec + 0x714, c->rec + 0x6d0, 0x11 * 4);
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
}

/* The parts that fit a car in a year (0x5175f0): for each id of its record's list (+0x32e ids from
   +0x330), the nfs5.prt records of that id (file order) out by then (+0x46), but not the body (type 10
   named "Body", 0x5da740); in order (a[i], a[j] swapped while 0x49c2c0 has a[j] first: type +0x26, then
   +0x5c, then the id). */
static int fit_parts(const uint8_t *r, int year, const uint8_t **out, int cap)
{
    static uint8_t *prt;
    static size_t nprt;
    if (!prt) prt = vfs_read_all("FEData\\Data\\nfs5.prt", &nprt);
    int n = 0, nl = rec16(r, 0x32e);
    const char *body = exe_str(0x5da740);
    for (int k = 0; k < nl && k < 0x46; k++) {
        int16_t id = rec16(r, 0x330 + 2 * k);
        for (size_t i = 0; prt && (i + 1) * 0x168 <= nprt; i++) {
            const uint8_t *p = prt + 0x168 * i;
            if (rec16(p, 0) != id || rec16(p, 0x46) > (int16_t)year) continue;
            if (rec16(p, 0x26) == 10 && !strcmp((const char *)p + 0x28, body)) continue;
            if (n < cap) out[n++] = p;
        }
    }
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            const uint8_t *a = out[j], *b = out[i];
            bool first = rec16(a, 0x26) != rec16(b, 0x26) ? rec16(a, 0x26) < rec16(b, 0x26)
                       : rec32(a, 0x5c) != rec32(b, 0x5c) ? rec32(a, 0x5c) < rec32(b, 0x5c)
                       : rec16(a, 0) < rec16(b, 0);
            if (first) {
                out[i] = a;
                out[j] = b;
            }
        }
    return n;
}

/* A part kept off a car for one it has (0x4f7b30's test): the same name and type, or two engines
   ("Engine" 0x5d0d1c, "Engine (Standard)" 0x5dafac). */
static bool part_clash(const uint8_t *p, const uint8_t *q)
{
    const char *a = (const char *)p + 0x28, *b = (const char *)q + 0x28;
    if (!strcmp(a, b) && rec16(p, 0x26) == rec16(q, 0x26)) return true;
    const char *e = exe_str(0x5d0d1c), *es = exe_str(0x5dafac);
    return (!strcmp(a, e) || !strcmp(a, es)) && (!strcmp(b, e) || !strcmp(b, es));
}

/* A number from one of the market's four-row tables (lo, hi, min, max: REAL_rand() % 100 in lo..hi gives
   min + REAL_rand() % (max - min); the last row that matches; none 0). */
static int32_t table_roll(uint32_t t)
{
    int32_t v = 0;
    uint32_t r = REAL_rand() % 100;
    for (int i = 0; i < 4; i++, t += 16) {
        if ((int32_t)exe_u32(t) <= (int32_t)r && (int32_t)r <= (int32_t)exe_u32(t + 4)) {
            uint32_t span = exe_u32(t + 12) - exe_u32(t + 8);
            v = (int32_t)((span ? REAL_rand() % span : 0) + exe_u32(t + 8));
        }
    }
    return v;
}

static void add_fitted(SaveCar *c, int32_t id, int32_t value)
{
    int n = c->ninstalled + c->nother;
    int32_t (*np)[2] = realloc(c->parts, (size_t)(n + 1) * sizeof *np);
    if (!np) return;
    c->parts = np;
    c->parts[n][0] = id;
    c->parts[n][1] = value;
    c->nother++;
}

static bool clashes(const SaveCar *c, const uint8_t *p)
{
    for (int i = c->ninstalled; i < c->ninstalled + c->nother; i++) {
        const uint8_t *q = part_rec(c->parts[i][0]);
        if (q && part_clash(p, q)) return true;
    }
    return false;
}

/* A used car's parts and wear (0x4f7b30): its fitted parts anew: of the parts that fit it in the
   market's year, a share (the table 0x5b7b08, x 0.01 of their count) picked at random from the ones
   left (the original draws from the first "left" of the list itself: the shuffled index it keeps
   isn't used), each but one that clashes with a part it has; then its stock parts that don't clash.
   Ten wear figures (+0x6a8, the table 0x5b7b88); the body's wear their average (x 0.1, 0x5b49b4), the
   other parts' each a figure of their own. */
static void used_parts(SaveCar *c, int year)
{
    c->nother = 0;
    const uint8_t *list[256];
    int n = fit_parts(c->rec + 4, year, list, 256);
    int32_t pct = table_roll(0x5b7b08);
    int k = (int)((double)(pct * n) * 0.01);   /* (0x5b2bc0) */
    if (n > 0) {
        int left = n;
        for (; k > 0; k--) {
            uint32_t j = left == 0 ? 0 : REAL_rand() % (uint32_t)left;
            if (j < (uint32_t)n && !clashes(c, list[j])) add_fitted(c, rec16(list[j], 0), rec32(list[j], 0x164));
            left--;
        }
    }
    for (int i = 0; i < c->ninstalled; i++) {
        const uint8_t *p = part_rec(c->parts[i][0]);
        if (p && !clashes(c, p)) add_fitted(c, c->parts[i][0], c->parts[i][1]);
    }
    float sum = 0;
    for (int i = 0; i < 10; i++) {
        float w = (float)table_roll(0x5b7b88);
        memcpy(c->rec + 0x6a8 + 4 * i, &w, 4);
        sum += w;
    }
    float avg = sum * 0.1f;
    const char *body = exe_str(0x5da740);
    for (int i = c->ninstalled; i < c->ninstalled + c->nother; i++) {
        const uint8_t *p = part_rec(c->parts[i][0]);
        float w = p && rec16(p, 0x26) == 10 && !strcmp((const char *)p + 0x28, body) ? avg : (float)table_roll(0x5b7b88);
        memcpy(&c->parts[i][1], &w, 4);
    }
}

bool FEData_UsedCar(const uint8_t *r, int year, SaveCar *out)   /* 0x4f7860 */
{
    Save *sv = FESave_Current();
    if (!sv || !r) return false;
    uint32_t body = 0xffff0000u, c3 = 0xffff0000u, c4 = 0xffff0000u, inside = 0xffffffffu;
    int nb = rec16(r, 0x3e6), ni = rec16(r, 0x528);
    if (nb > 0) {
        uint32_t k = REAL_rand() % (uint32_t)nb;
        body = swap32((uint32_t)rec32(r, 0x3ec + 16 * (int)k));
        c3 = swap32((uint32_t)rec32(r, 0x3f0 + 16 * (int)k));
        c4 = swap32((uint32_t)rec32(r, 0x3f4 + 16 * (int)k));
    }
    if (ni > 0) inside = swap32((uint32_t)rec32(r, 0x530 + 16 * (int)(REAL_rand() % (uint32_t)ni)));
    int32_t stripe = 0, racenum = -1;
    if (CRT_rand() % 100 > 50) stripe = g_carnschemes ? (int32_t)((uint32_t)CRT_rand() % g_carnschemes) : 0;   /* (0x6115f8: the last car's) */
    if (CRT_rand() % 100 > 80) racenum = CRT_rand() % 100 - 1;
    int32_t col[4] = {(int32_t)body, (int32_t)inside, (int32_t)c3, (int32_t)c4};
    car_object(sv, out, r, col, stripe, racenum);
    int32_t miles = table_roll(0x5b7b48);
    memcpy(out->rec + 0x698, &miles, 4);
    used_parts(out, year);
    int32_t x = table_roll(0x5b7bc8);
    memcpy(out->rec + 0x69c, &x, 4);
    out->rec[0x6a0] = 0;
    int32_t v = FEData_CarValue(out, year);
    memcpy(out->rec + 0x68c, &v, 4);
    return true;
}

int FEData_UsedPool(const uint8_t **out, int cap)   /* the records 0x4f7660 offers (0x6919f0) */
{
    int n = 0;
    for (int i = 0; i < s_ncars && n < cap; i++) out[n++] = s_car + CAR_SIZE * s_cars[i];
    return n;
}

/* Buying a used car (FEUsedCarCallbackList_v55 0x4f68f0 -> 0x501490): not with too little money (the
   message 0x322) or 80 cars (0x3d); else its asking price paid (+0x6a4 the price it cost, the money less
   it clamped), the car out of the market (0x4f88e0) into the garage, the garage in order (0x4f0550).
   (The purchase sound 0xb pends.) */
int FEData_BuyUsed(void)
{
    FEAccessor *u = FEAccess_Find("UsedCar.Car");
    Save *sv = FESave_Current();
    if (!u || !sv) return -1;
    refresh(u);
    SaveCar *c = acc_car(u);
    if (!c) return -1;
    int32_t price = rec32(c->rec, 0x68c);
    if (sv->money < price) return 0x322;
    if (sv->ncars >= 0x50) return 0x3d;
    SaveCar *nc = realloc(sv->cars, sizeof *nc * (size_t)(sv->ncars + 1));
    if (!nc) return -1;
    sv->cars = nc;
    SaveCar got;
    if (!Market_TakeUsed(u->pos, &got)) return -1;
    memcpy(got.rec + 0x6a4, &price, 4);
    int32_t cost = price;
    if (cost > 999999999) cost = 999999999;
    if (cost < -999999999) cost = -999999999;
    sv->money -= cost;
    sv->cars[sv->ncars++] = got;
    for (int i = 0; i < sv->ncars; i++)   /* 0x4f0550 */
        for (int j = i + 1; j < sv->ncars; j++)
            if (car_before(sv->cars[j].rec + 4, sv->cars[i].rec + 4)) {
                SaveCar t = sv->cars[i];
                sv->cars[i] = sv->cars[j];
                sv->cars[j] = t;
            }
    Audio_UI(0xb);   /* 0x4aab80 */
    refresh(u);
    FESave_Write();
    return 0;
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
    {   /* +0x674 .. +0x680: the accessor's colours (slots 0x74 .. 0x80; 0x84, 0x88 pend: 0) */
        uint32_t col[4];
        FEAccess_CarColours(d, col);
        int32_t v[4] = {(int32_t)col[0], (int32_t)col[3], (int32_t)col[1], (int32_t)col[2]};
        car_object(sv, c, r, v, 0, 0);
    }
    memcpy(c->rec + 0x6a4, &price, 4);
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

bool FEData_MergedOwned(void)   /* 0x4f4420 on zMergedList.Car */
{
    FEAccessor *a = FEAccess_Find("zMergedList.Car");
    if (!a) return false;
    refresh(a);
    return a->pos < a->owned;
}

bool FEData_TrackLooped(int type)   /* 0x4fb4f0 */
{
    char nm[32];
    const char *acc;
    switch (type) {
    case 0:
    case 3:   /* (a multiplayer client's the session's track, 0x691d00: no multiplayer) */
        acc = "Track1.Track";
        break;
    case 1:
        acc = "LoopedTrack.Track";
        break;
    case 2: {
        FEAccessor *k = FEAccess_Find("Knockout.TrackNumber");
        snprintf(nm, sizeof nm, "Track%d.Track", (k ? k->pos : 0) + 1);   /* "Track%d.Track" 0x5db4f0 */
        acc = nm;
        break;
    }
    default:
        return false;
    }
    FEAccessor *a = FEAccess_Find(acc);
    if (!a || !a->count) return false;
    return rec16(s_trk + TRK_SIZE * track_rec(a, a->pos), 0x6e) == 1;   /* (0x4f9c30's record) */
}

const char *FEData_Track(void)
{
    FEAccessor *a = FEAccess_Find("Track1.Track");
    return a && a->count ? trk_name_of(a, a->pos) : "coastal";
}

/* The track's eight records for the race (0x4ec200: 0x4fa6c0's +0x1b0 of the race's track, 0x4c bytes
   each: +0x00 the driver, +0x14 the car, +0x48 the time; the same times as the race's and the lap's
   records). false without the track. */
bool FEData_TrackRecords(const char *track, int32_t race[8], int32_t lap[8], char names[8][32], char cars[8][32])
{
    if (!s_trk || !track) return false;
    for (int i = 0; i < s_ntracks; i++) {
        const uint8_t *r = s_trk + TRK_SIZE * s_tracks[i];
        if (strcasecmp((const char *)r + 8, track)) continue;
        for (int k = 0; k < 8; k++) {
            const uint8_t *e = r + 0x1b0 + 0x4c * k;
            int32_t t;
            memcpy(&t, e + 0x48, 4);
            race[k] = lap[k] = t;
            snprintf(names[k], 32, "%.19s", (const char *)e);
            snprintf(cars[k], 32, "%.31s", (const char *)e + 0x14);
        }
        return true;
    }
    return false;
}

void FEData_TourRaceInfo(RaceTournament *t)
{
    memset(t, 0, sizeof *t);
    const uint8_t *r = FEData_Tournament();
    int race = FEData_TourRace();
    if (!r || race < 0) return;
    int n = rec16(r, 0x39c);
    if (n > 10) n = 10;
    t->rally = r[0xe8] != 0;                                   /* 0xd6 */
    for (int k = 0; k < 8; k++) {
        t->race_points[k] = 8 - k;                             /* 0xa8 .. 0xaf */
        t->prize[k] = rec32(r, 0x3a4 + 0xa4 * race + 4 * (k + 1));   /* 0x503540 */
        t->bonus[k] = rec32(r, 0xe8 + 4 * (k + 1));            /* 0x502a30 */
        for (int i = 0; i < n; i++) {
            const uint8_t *x = r + 0xa4 * i;
            if (!x[0x3d0]) continue;
            t->total_points[k] += rec16(x, 0x3d2 + 2 * k);     /* 0x502a90 */
            t->total_time[k] += rec32(x, 0x3e4 + 4 * k);       /* 0x502b10 */
            t->earnings[k] += rec32(x, 0x404 + 4 * k) + rec32(x, 0x424 + 4 * k);   /* 0x502b90 */
        }
    }
    t->safe_bonus = rec32(r, 0x3c8 + 0xa4 * race);             /* 0x5035c0 */
    t->race = race;                                            /* 0x5028e0 */
    int left = 0;
    for (int i = 0; i < n; i++) left += !r[0x3d0 + 0xa4 * i];
    t->last_race = left == 1;                                  /* 0xb9: 0x5038b0 */
    t->runs = n - left;                                        /* 0xf3: 0x502920 less 0x5038b0 */
    /* the player's winnings (0xdf ..: 0x503630, +0x424) and safe-driving bonus (0xe9 ..: 0x5036a0,
       +0x404) by race: the original's eight options, whatever the race count */
    for (int i = 0; i < 8; i++) {
        t->won[i] = rec32(r, 0x424 + 0xa4 * i);
        t->safe_won[i] = rec32(r, 0x404 + 0xa4 * i);
    }
    /* 0xba: the era's last open tournament (0x503d60 counts the era's not kept out, +0xe4 2, and never
       completed, +0xd3c), this one never completed (0x503020) nor of kind +0xe2 1 (0x5040b0), at its
       last race */
    int32_t head[7] = {0};
    int nt = 0;
    const uint8_t *all = tour_section(head, &nt);
    int open = 0;
    for (int i = 0; all && i < nt; i++) {
        const uint8_t *x = all + 0xd40 * i;
        if (rec32(x, 4) == head[3] && rec16(x, 0xe4) != 2 && rec16(x, 0xd3c) == 0) open++;
    }
    t->end_of_era = rec16(r, 0xd3c) < 1 && rec16(r, 0xe2) != 1 && open == 1 && left == 1;
}

int FEData_RaceEra(void)
{
    /* 0x503db0: 0x20b when the modern era has no open tournament, else 0x20a when the golden has none,
       else 0x209 when the classic has none; with all open the original returns a register's leftover
       (0x4ecc60 then passes that on): the port 0. Its option 0xbb: 0x209 0, 0x20a 1, 0x20b 2. */
    int32_t head[7] = {0};
    int nt = 0;
    const uint8_t *all = tour_section(head, &nt);
    bool open[3] = {false, false, false};
    for (int i = 0; all && i < nt; i++) {
        const uint8_t *x = all + 0xd40 * i;
        int e = rec32(x, 4) - 0x209;
        if (0 <= e && e < 3 && rec16(x, 0xe4) != 2 && rec16(x, 0xd3c) == 0) open[e] = true;
    }
    return !open[2] ? 2 : !open[1] ? 1 : 0;
}

void FEData_TrackRecordEnter(const char *track, const char *name, const char *car, int32_t t)
{
    if (!s_trk || !track) return;
    uint8_t *r = NULL;
    for (size_t i = 0; i < s_trk_size / TRK_SIZE && !r; i++)
        if (!strcasecmp((const char *)s_trk + TRK_SIZE * i + 8, track)) r = s_trk + TRK_SIZE * i;
    if (!r) r = s_trk;   /* (0x4fa560: the first track's without it) */
    uint8_t *e = r + 0x1b0;
    int i = 0;
    for (;; i++) {
        if (7 < i) return;
        int32_t v;
        memcpy(&v, e + 0x4c * i + 0x48, 4);
        if (t < v) break;
    }
    for (int k = 7; k > i; k--) {
        uint8_t *d = e + 0x4c * k, *from = d - 0x4c;
        memcpy(d + 0x48, from + 0x48, 4);
        memcpy(d, from, 0x14);
        memcpy(d + 0x14, from + 0x14, 0x34);
    }
    uint8_t *d = e + 0x4c * i;
    memcpy(d + 0x48, &t, 4);
    snprintf((char *)d, 0x14, "%s", name ? name : "");
    memset(d + 0x14, 0, 0x11);
    strncpy((char *)d + 0x14, car ? car : "", 0x11);   /* (17 letters at most) */
    d[0x25] = 0;
    plat_save_user_file("nfs5.trk", s_trk, s_trk_size);   /* 0x4fa320 */
}

/* The knockout's state (0x690410, 0x84 bytes; the save's "Knockout" section, which its loader 0x4dd810
   copies raw, so the port works the section in place, else its own):
     +0x00 running (a byte)   +0x04 the race   +0x08 the opponents
     +0x0c a byte per racer still in (0 the player; 12 bytes)
     +0x18 the racers' personality ids (+0x18 + 4i, racers 1 .. n)
     +0x3c their places (+0x3c the player's)   +0x60 their places in the last race */
enum { KO_ON = 0, KO_RACE = 4, KO_N = 8, KO_IN = 0xc, KO_PERS = 0x18, KO_PLACE = 0x3c, KO_LAST = 0x60, KO_SIZE = 0x84 };
static uint8_t s_ko_own[KO_SIZE];

static uint8_t *ko(void)
{
    Save *sv = FESave_Current();
    for (int i = 0; sv && i < sv->nsec; i++)
        if (!strcmp(sv->sec[i].name, "Knockout") && sv->sec[i].size >= KO_SIZE) return sv->sec[i].data;
    return s_ko_own;
}

static int32_t ko_get(int off) { return rec32(ko(), off); }
static void ko_set(int off, int32_t v) { memcpy(ko() + off, &v, 4); }

bool FEData_KnockoutOn(void) { return ko()[KO_ON] != 0; }

/* The personalities of n opponents (0x4e2940): srand(time(0)) (0x537e80; the port's clock in seconds,
   so runs repeat), then n picked at random (0x537db0) without repeats from nfs5.per's records of the
   difficulty (+0x8; all of them for -1); their ids (0x6907d8). (The original divides by zero when it
   runs out; the port stops.) */
static int pick_personalities(int n, int difficulty, int16_t ids[8])
{
    static uint8_t *per;
    static size_t nper;
    if (!per) per = vfs_read_all("FEData\\Data\\nfs5.per", &nper);
    REAL_srand(app_ticks_ms() / 1000u);
    int cand[64], nc = 0;
    for (int i = 0; per && i < (int)(nper / 0x2c) && nc < 64; i++)
        if (difficulty == -1 || rec16(per, 0x2c * i + 8) == difficulty) cand[nc++] = i;
    int k = 0;
    for (; k < n && k < 8 && nc > 0; k++) {
        int j = (int)(REAL_rand() % (uint32_t)nc);
        ids[k] = rec16(per, 0x2c * cand[j]);
        memmove(&cand[j], &cand[j + 1], sizeof cand[0] * (size_t)(nc - j - 1));
        nc--;
    }
    return k;
}

static int difficulty(void)   /* Game.Difficutly's position (FEListAccessor 0x4bb260 on 0x5d9f08) */
{
    FEAccessor *d = FEAccess_Find("Game.Difficutly");
    return d ? d->pos : 1;
}

/* A car's colours as an opponent gets them (0x4f45c0, 0x4eb830): one of its colour sets and interiors
   at random (0x537db0; none: red and white), stripes half the time and a race number a fifth of it
   (rand), in the race description's order (+0x674 body, +0x678 interior, +0x67c, +0x680). */
static void opp_colours(const uint8_t *r, uint32_t col[4], int32_t *stripe, int32_t *racenum)
{
    uint32_t body = 0xffff0000u, c3 = 0xffff0000u, c4 = 0xffff0000u, inside = 0xffffffffu;
    int nb = rec16(r, 0x3e6), ni = rec16(r, 0x528);
    if (nb > 0) {
        uint32_t k = REAL_rand() % (uint32_t)nb;
        body = swap32((uint32_t)rec32(r, 0x3ec + 16 * (int)k));
        c3 = swap32((uint32_t)rec32(r, 0x3f0 + 16 * (int)k));
        c4 = swap32((uint32_t)rec32(r, 0x3f4 + 16 * (int)k));
    }
    if (ni > 0) inside = swap32((uint32_t)rec32(r, 0x530 + 16 * (int)(REAL_rand() % (uint32_t)ni)));
    *stripe = 0;
    *racenum = -1;
    if (CRT_rand() % 100 > 50) *stripe = g_carnschemes ? (int32_t)((uint32_t)CRT_rand() % g_carnschemes) : 0;   /* (0x6115f8) */
    if (CRT_rand() % 100 > 80) *racenum = CRT_rand() % 100 - 1;
    col[0] = body | 0xff000000u;
    col[1] = c3 | 0xff000000u;
    col[2] = c4 | 0xff000000u;
    col[3] = inside | 0xff000000u;
}

/* OpponentCar.Car's seven car objects (FEOpponentCarAccessor +0x24, 0x760 bytes each: 0x4f4e70). */
static struct { const uint8_t *rec; uint32_t col[4]; int32_t stripe, racenum; } s_oppcar[7];

/* OpponentCar.Car's cars picked (0x4f45c0) by its position: 0 the player's car (zMergedList.Car's),
   1 .. 3 a car of its era (+0x1b0), its class (+0x1ac) or both: the k-th such walking the list
   (0x6919f0) round from its start, k random; else (4, or no car) any car of the list. */
static void pick_opponent_cars(void)
{
    FEAccessor *oa = FEAccess_Find("OpponentCar.Car");
    int mode = oa ? oa->pos : 0;
    const uint8_t *mine = FEAccess_CarRecord(FEAccess_Find("zMergedList.Car"));
    for (int k = 0; k < 7; k++) {
        const uint8_t *r = NULL;
        if (mine && mode == 0) {
            r = mine;
        } else if (mine && mode >= 1 && mode <= 3 && s_ncars > 0) {
            uint32_t u = REAL_rand() % (uint32_t)s_ncars;
            int i = 0, hits = 0;
            do {
                if (i >= s_ncars) i = 0;
                const uint8_t *c = s_car + CAR_SIZE * s_cars[i++];
                bool era = rec32(c, 0x1b0) == rec32(mine, 0x1b0), cls = rec32(c, 0x1ac) == rec32(mine, 0x1ac);
                if ((mode == 1 && era) || (mode == 2 && cls) || (mode == 3 && era && cls)) hits++;
                r = c;
            } while (hits <= (int)u);
        } else if (s_ncars > 0) {
            r = s_car + CAR_SIZE * s_cars[REAL_rand() % (uint32_t)s_ncars];
        }
        s_oppcar[k].rec = r;
        if (r) opp_colours(r, s_oppcar[k].col, &s_oppcar[k].stripe, &s_oppcar[k].racenum);
    }
}

static void slot_car(int k, RaceOpponent *o)
{
    const uint8_t *r = k >= 0 && k < 7 ? s_oppcar[k].rec : NULL;
    o->spec[0] = 0;
    if (!r) return;
    snprintf(o->spec, sizeof o->spec, "%s", (const char *)r + 0xb1);
    memcpy(o->colours, s_oppcar[k].col, sizeof o->colours);
    o->stripe = s_oppcar[k].stripe;
    o->racenum = s_oppcar[k].racenum;
}

int FEData_RaceOpponents(int n, RaceOpponent out[8])
{
    /* 0x4eb230's other race kinds: OpponentCar.Car's cars (0x4f45c0), the personalities of the
       difficulty (0x4e2940); opponent i in car i */
    pick_opponent_cars();
    int16_t ids[8];
    int m = pick_personalities(n, difficulty(), ids);
    memset(out, 0, sizeof(RaceOpponent) * 8);
    for (int i = 0; i < m; i++) {
        out[i].pers = FEData_PersonalityIndex(ids[i]);
        slot_car(i, &out[i]);
    }
    return m;
}

void FEData_KnockoutStart(void)   /* 0x4dd9e0 */
{
    pick_opponent_cars();
    uint8_t *k = ko();
    k[KO_ON] = 1;
    ko_set(KO_RACE, 0);
    int n = FEData_KOOpponents();
    ko_set(KO_N, n);
    memset(k + KO_IN, 0, 12);
    for (int i = 0; i < 9; i++) ko_set(KO_PLACE + 4 * i, -1 - i), ko_set(KO_LAST + 4 * i, -1);
    ko_set(KO_PLACE, n + 1);   /* (the player behind them) */
    k[KO_IN] = 1;
    for (int i = 1; i <= n && i < 9; i++) k[KO_IN + i] = 1, ko_set(KO_PLACE + 4 * i, i);
    int16_t ids[8] = {0};
    pick_personalities(n, difficulty(), ids);
    for (int i = 1; i <= n && i < 9; i++) ko_set(KO_PERS + 4 * i, ids[i - 1]);
}

int FEData_KnockoutRace(char *track, int cap, int *laps, int pers[8], int start[8])
{
    char nm[32];
    int race = ko_get(KO_RACE);
    snprintf(nm, sizeof nm, "Track%d.Track", race + 1);
    FEAccessor *t = FEAccess_Find(nm);
    if (t) refresh(t);
    snprintf(track, (size_t)cap, "%s", t && t->count ? trk_name_of(t, t->pos) : "coastal");
    snprintf(nm, sizeof nm, "Track%d.Laps", race + 1);
    FEAccessor *l = FEAccess_Find(nm);
    *laps = l ? l->pos + 1 : 1;
    int n = 0, nk = ko_get(KO_N);
    const uint8_t *k = ko();
    start[0] = ko_get(KO_PLACE) - 1;   /* (STARTINGPOS 0x247: the places, 0-based) */
    for (int i = 1; i <= nk && i < 8; i++)
        if (k[KO_IN + i]) {
            pers[n] = i;   /* (racer i) */
            start[++n] = ko_get(KO_PLACE + 4 * i) - 1;
        }
    return n;
}

int FEData_KnockoutTraffic(void)
{
    /* (0x4fb630 for the knockout: its race's Track<n>.Traffic, none before it is on, 0x4dd9b0) */
    if (!FEData_KnockoutOn()) return 0;
    char nm[32];
    snprintf(nm, sizeof nm, "Track%d.Traffic", ko_get(KO_RACE) + 1);
    FEAccessor *a = FEAccess_Find(nm);
    return a ? FEAccess_Pos(a) : 0;
}

void FEData_KnockoutOpponent(int racer, RaceOpponent *o)
{
    /* 0x4ddc00: racer i's personality (its id, 0x4e2900) in OpponentCar.Car's car i - 1 */
    memset(o, 0, sizeof *o);
    if (racer < 1 || racer > 8) return;
    o->pers = FEData_PersonalityIndex(ko_get(KO_PERS + 4 * racer));
    slot_car(racer - 1, o);
}

void FEData_KnockoutResult(const int *places, int np)   /* 0x4ddb10 */
{
    uint8_t *k = ko();
    int keep = ko_get(KO_N) - ko_get(KO_RACE);
    k[KO_ON] = 1;
    for (int i = 0; i < 9; i++) ko_set(KO_PLACE + 4 * i, -1 - i);
    /* (the racers walked to Game.NumKOOpponents' value now, not the knockout's +8) */
    int last = FEData_KOOpponents();
    if (np > 0 && places[0] <= keep) {
        for (int i = 0, j = 0; i <= last && i < 9; i++)
            if (k[KO_IN + i]) ko_set(KO_LAST + 4 * i, j < np ? places[j] : 99), j++;
        for (int i = 0; i <= last && i < 9; i++) {
            if (!k[KO_IN + i] || keep < ko_get(KO_LAST + 4 * i)) k[KO_IN + i] = 0;
            else ko_set(KO_PLACE + 4 * i, ko_get(KO_LAST + 4 * i));
        }
        ko_set(KO_RACE, ko_get(KO_RACE) + 1);
        if (ko_get(KO_RACE) < ko_get(KO_N)) return;
    }
    k[KO_ON] = 0;   /* (out, or the last race run) */
}

const char *FEData_LoopedTrack(void)
{
    FEAccessor *a = FEAccess_Find("LoopedTrack.Track");
    if (a) refresh(a);
    return a && a->count ? trk_name_of(a, a->pos) : "monaco3";
}

int FEData_KOOpponents(void)
{
    FEAccessor *a = FEAccess_Find("Game.NumKOOpponents");
    return a ? a->min + a->pos : 4;
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

/* ---------------------------------------------------------------- shared lookups */

const uint8_t *FEData_PartRecord(int32_t id) { return part_rec(id); }

const uint8_t *FEData_CarById(int id)   /* 0x4f39e0 over the whole list (0x6919e8) */
{
    if (!s_car) return NULL;
    for (size_t i = 0; (i + 1) * CAR_SIZE <= s_car_size; i++)
        if (rec16(s_car + CAR_SIZE * i, 0) == id) return s_car + CAR_SIZE * i;
    return s_car_size >= CAR_SIZE ? s_car : NULL;
}

const uint8_t *FEData_TrackById(int id) { return track_by_id(id); }

void FEData_NewCar(SaveCar *c, const uint8_t *r, const int32_t col[4], int32_t stripe, int32_t racenum)
{
    Save none = {0}, *sv = FESave_Current();
    car_object(sv ? sv : &none, c, r, col, stripe, racenum);
}

bool FEData_AddOwned(int id, const int32_t col[4], int32_t stripe, int32_t racenum)
{
    Save *sv = FESave_Current();
    const uint8_t *r = FEData_CarById(id);
    if (!sv || !r) return false;
    SaveCar *nc = realloc(sv->cars, sizeof *nc * (size_t)(sv->ncars + 1));
    if (!nc) return false;
    sv->cars = nc;
    SaveCar *c = &sv->cars[sv->ncars];
    car_object(sv, c, r, col, stripe, racenum);
    memset(c->rec + 0x6a4, 0, 4);
    sv->ncars++;
    for (int i = 0; i < sv->ncars; i++)   /* 0x501080 */
        for (int j = i + 1; j < sv->ncars; j++)
            if (car_before(sv->cars[j].rec + 4, sv->cars[i].rec + 4)) {
                SaveCar t = sv->cars[i];
                sv->cars[i] = sv->cars[j];
                sv->cars[j] = t;
            }
    return true;
}

int FEData_PersonalityIndex(int id)   /* 0x4e2900: nfs5.per's record of an id, else the first */
{
    static uint8_t *per;
    static size_t nper;
    if (!per) per = vfs_read_all("FEData\\Data\\nfs5.per", &nper);
    for (int i = 0; per && i < (int)(nper / 0x2c); i++)
        if (rec16(per, 0x2c * i) == id) return i;
    return 0;
}

bool FEData_OpponentCar(int id, char spec[0x40], uint32_t col[4], int32_t *stripe, int32_t *racenum)
{
    /* 0x4eb830's car for an opponent: its record by id (0x4f39e0), its colours picked, its car object
       (0x4eda40) as the description gets it (0x4ee430) */
    const uint8_t *r = FEData_CarById(id);
    if (!r) return false;
    opp_colours(r, col, stripe, racenum);
    snprintf(spec, 0x40, "%s", (const char *)r + 0xb1);
    return true;
}

/* ---------------------------------------------------------------- the string accessor */

char *FEData_NameInput(void) { return s_nameinput; }

/* FEStringAccessor's typing: a character added at the end (_v12 0x4bc530 -> _v11 0x4bc670: a file name's
   accessor takes no control characters but backspace, nor \ / : * ? " < > |, and no accessor takes %;
   nothing past its length), the last one taken off (_v14 0x4bc550 -> _v13). */
bool FEAccess_TypeChar(FEAccessor *a, int ch)
{
    if (!a || a->kind != ACC_STRING) return false;
    size_t n = strlen(s_nameinput);
    if (ch == 8) {
        if (n) s_nameinput[n - 1] = 0;
        return true;
    }
    if (ch == '%' || (ch < 0x20 && ch != 8) || strchr("\\/:*?\"<>|", ch)) return true;
    if (n == sizeof s_nameinput - 1) return false;
    s_nameinput[n] = (char)ch;
    s_nameinput[n + 1] = 0;
    return true;
}

void FEData_KnockoutStop(void)   /* 0x4ddaf0: the knockout's on byte off */
{
    if (ko()[KO_ON]) ko()[KO_ON] = 0;
}

/* ---------------------------------------------------------------- the cheat codes */

uint32_t g_fe_cheats;   /* 0x65b940 */

bool FE_Cheat(int bit) { return (g_fe_cheats >> (bit & 31) & 1) != 0; }   /* 0x4d8240 */

/* 0x4d8130: a name of up to eight characters (zero filled) turned on its side, the k-th byte of the code
   the k-th bits of the eight characters, against the seven codes at 0x5d9618 (8 bytes and the cheat's
   number, 0xc each). A match: its sound (0xc), then the cheat's bit set; number 4 instead takes one off
   the money (0x500890 of -1). */
bool FEData_CheatCode(const char *name)
{
    uint8_t in[9] = {0}, code[8] = {0};
    size_t n = strlen(name);
    if (n < 9) memcpy(in, name, n);
    else memcpy(in, name, 8);   /* (a longer name runs over the original's buffer) */
    for (int j = 0; j < 8; j++)
        for (int i = 0; i < 8; i++) {
            code[j] |= (uint8_t)((in[i] & 1) << i);
            in[i] >>= 1;
        }
    for (int k = 0; k < 7; k++) {
        const uint8_t *e = exe_data(0x5d9618 + 0xc * (uint32_t)k, 0xc);
        if (!e || memcmp(e, code, 8)) continue;
        Audio_UI(0xc);   /* 0x4aab80 */
        int32_t id;
        memcpy(&id, e + 8, 4);
        if (id == 4) {
            Save *sv = FESave_Current();
            if (sv) sv->money += -1;
        } else {
            g_fe_cheats |= 1u << (id & 31);
        }
        return true;
    }
    return false;
}

/* GenericCar.Car's record (0x6919ec's car at the merged list's place past the owned ones), NULL while
   the merged list is on an owned car. */
static const uint8_t *generic_rec(void)
{
    FEAccessor *m = FEAccess_Find("zMergedList.Car");
    if (!m) return NULL;
    refresh(m);
    if (!m->count || m->pos < m->owned) return NULL;
    return s_car + CAR_SIZE * s_cars[m->pos - m->owned];
}
