/* The front end's data accessors (feaccess.h). */
#include "front/feaccess.h"
#include "front/fe_parts.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { TRK_SIZE = 0x4a8, CAR_SIZE = 0x670 };

typedef enum { ACC_INT, ACC_TRACK, ACC_CAR } AccKind;

struct FEAccessor {
    char name[64];
    AccKind kind;
    int pos, count, min;
};

static FEAccessor s_acc[8];   /* 0x65b3a8 */
static int s_nacc;
static uint8_t *s_trk, *s_car;
static int s_tracks[32], s_ntracks;   /* the records with a model (0x691d00) */
static int s_cars[160], s_ncars;      /* (0x6919f0) */

static FEAccessor *add(const char *name, AccKind kind, int count, int min, int pos)
{
    if (s_nacc == 8) return NULL;
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
        if (!strncmp(l, key, kl) && (l[kl] == ' ' || l[kl] == '=')) {
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
    }
    return "";
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
