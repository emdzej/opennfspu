/* The front end's data accessors (feaccess.h). */
#include "front/feaccess.h"
#include "front/fe_parts.h"
#include "exe.h"
#include "game/options.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { TRK_SIZE = 0x4a8, CAR_SIZE = 0x670 };

typedef enum { ACC_INT, ACC_TRACK, ACC_CAR, ACC_LIST } AccKind;

struct FEAccessor {
    char name[64];
    AccKind kind;
    int pos, count, min;
    uint32_t list;   /* ACC_LIST: the exe's zero-ended string ids (FEListAccessor 0x4bb260) */
    uint32_t key;    /* the option it keeps (its key's address), 0 none */
};

enum { NACC = 64 };
static FEAccessor s_acc[NACC];   /* 0x65b3a8 */
static int s_nacc;
static uint8_t *s_trk, *s_car;
static int s_tracks[32], s_ntracks;   /* the records with a model (0x691d00) */
static int s_cars[160], s_ncars;      /* (0x6919f0) */

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
    add("zMergedList.Car", ACC_CAR, s_ncars, 0, cp);
    audio();
    display();
    graphics();
    add("OpponentCar.Car", ACC_CAR, s_ncars, 0, cp);
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

int FEAccess_Count(const FEAccessor *a) { return a->count; }
int FEAccess_Pos(const FEAccessor *a) { return a->pos; }

void FEAccess_SetPos(FEAccessor *a, int pos)
{
    if (!a->count) return;
    a->pos = ((pos % a->count) + a->count) % a->count;
    if (a->key) Options_SetInt(a->key, a->min + a->pos);   /* (the accessor's option, slot 11 0x4bbe20) */
}

const char *FEAccess_Text(const FEAccessor *a)
{
    static char b[64];
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
    }
    return "";
}

const char *FEAccess_TextArg(const FEAccessor *a, const char *arg)
{
    static char b[16];
    if (!arg || !*arg || !strcmp(arg, "SHORT")) return FEAccess_Text(a);
    if (!strcmp(arg, "PERCENT")) {   /* 0x4bc0e0 */
        int p = a->count < 2 ? 100 : a->pos * 100 / (a->count - 1);
        snprintf(b, sizeof b, "%d", p > 99 ? 100 : p);
        return b;
    }
    return arg;
}

const char *FEAccess_CarFile(const FEAccessor *a)
{
    return a && a->kind == ACC_CAR && a->count ? car_file(a->pos) : NULL;
}

const char *FEData_Track(void)
{
    FEAccessor *a = FEAccess_Find("Track1.Track");
    return a && a->count ? trk_name(a->pos) : "coastal";
}

const char *FEData_Car(void)
{
    FEAccessor *a = FEAccess_Find("zMergedList.Car");
    return a && a->count ? car_file(a->pos) : "964";
}

int FEData_Opponents(void)
{
    FEAccessor *a = FEAccess_Find("Game.NumOpponents");
    return a ? a->min + a->pos : 0;
}
