/* The used car market (see femarket.h). */
#include "game/audio.h"
#include "front/femarket.h"
#include "app.h"
#include "exe.h"
#include "front/feaccess.h"
#include "front/fesave.h"
#include "pending.h"
#include "real/random.h"
#include <stdlib.h>
#include <string.h>

/* The market's list in its order: the market's own cars (the save's used cars, in their order) and the
   player's put up since the session began, each where it joined. */
typedef struct {
    bool owned;
    int idx;   /* the player's: its car id (+0); the market's: its index in the save's used cars */
} MarketEnt;

static MarketEnt s_ent[256];
static int s_n;
static unsigned s_serial;

static int32_t get32(const uint8_t *r, int off);

static Save *market(void)
{
    Save *sv = FESave_Current();
    if (FESave_Serial() != s_serial) {   /* (0x4f6d70: the list the save's section gives, each load) */
        s_serial = FESave_Serial();
        s_n = 0;
        for (int i = 0; sv && i < sv->nused && s_n < 256; i++) s_ent[s_n++] = (MarketEnt){false, i};
        /* 0x501020 after the load: the owned cars up for sale (+0x691) listed again at their price
           (+0x68c), their offers cleared (0x4f8760). (Its 0x501080 sorts the owned cars, which a save
           keeps in that order.) */
        for (int i = 0; sv && i < sv->ncars; i++)
            if (sv->cars[i].rec[0x691]) Market_List(i, get32(sv->cars[i].rec, 0x68c));
    }
    return sv;
}

static int owned_index(Save *sv, int32_t id)   /* (the garage's order changes as cars come and go) */
{
    for (int i = 0; i < sv->ncars; i++) {
        int32_t v;
        memcpy(&v, sv->cars[i].rec, 4);
        if (v == id) return i;
    }
    return -1;
}

static SaveCar *car_of(Save *sv, MarketEnt e)
{
    if (!e.owned) return &sv->used[e.idx];
    int i = owned_index(sv, e.idx);
    return i >= 0 ? &sv->cars[i] : NULL;
}
static uint8_t *rec_of(Save *sv, MarketEnt e) { SaveCar *c = car_of(sv, e); return c ? c->rec : NULL; }
static int32_t get32(const uint8_t *r, int off) { int32_t v; memcpy(&v, r + off, 4); return v; }
static void put32(uint8_t *r, int off, int32_t v) { memcpy(r + off, &v, 4); }
static int16_t get16(const uint8_t *r, int off) { int16_t v; memcpy(&v, r + off, 2); return v; }
static float kf(uint32_t va) { uint32_t b = exe_u32(va); float f; memcpy(&f, &b, 4); return f; }
static double kd(uint32_t va)
{
    uint64_t b = (uint64_t)exe_u32(va) | (uint64_t)exe_u32(va + 4) << 32;
    double d;
    memcpy(&d, &b, 8);
    return d;
}

/* __ftol (0x5a0d48): truncation; out of range the x87's integer indefinite. */
static int32_t ftol(double v) { return v > -2147483649.0 && v < 2147483648.0 ? (int32_t)v : INT32_MIN; }

static void drop(int k)   /* the entry out of the list */
{
    memmove(&s_ent[k], &s_ent[k + 1], (size_t)(s_n - k - 1) * sizeof *s_ent);
    s_n--;
}

static int find_owned(int owned)
{
    Save *sv = FESave_Current();
    if (!sv || owned < 0 || owned >= sv->ncars) return -1;
    int32_t id = get32(sv->cars[owned].rec, 0);
    for (int k = 0; k < s_n; k++)
        if (s_ent[k].owned && s_ent[k].idx == id) return k;
    return -1;
}

void Market_List(int owned, int32_t price)   /* 0x4f8760 */
{
    Save *sv = market();
    if (!sv || owned < 0 || owned >= sv->ncars || s_n >= 256) return;
    uint8_t *r = sv->cars[owned].rec;
    put32(r, 0x68c, price);
    r[0x691] = 1;
    r[0x690] = 0;
    r[0x692] = 0;
    put32(r, 0x694, 0);
    r[0x6a0] = 1;
    s_ent[s_n++] = (MarketEnt){true, get32(r, 0)};
}

void Market_Unlist(int owned)   /* 0x4f88e0 */
{
    Save *sv = market();
    int k = sv ? find_owned(owned) : -1;
    if (k < 0) return;
    uint8_t *r = sv->cars[owned].rec;
    put32(r, 0x68c, 0);
    r[0x691] = 0;
    r[0x690] = 0;
    r[0x692] = 0;
    put32(r, 0x694, 0);
    drop(k);
}

/* One of the market's own cars bought (FEUsedCarCallbackList_v55 0x4f68f0 -> 0x4f88e0): out of the
   market, its market fields cleared, into *out. */
bool Market_TakeUsed(int used, SaveCar *out)
{
    Save *sv = market();
    if (!sv || used < 0 || used >= sv->nused) return false;
    int k = -1;
    for (int j = 0; j < s_n && k < 0; j++)
        if (!s_ent[j].owned && s_ent[j].idx == used) k = j;
    if (k < 0) return false;
    *out = sv->used[used];
    put32(out->rec, 0x68c, 0);
    out->rec[0x691] = 0;
    out->rec[0x690] = 0;
    out->rec[0x692] = 0;
    put32(out->rec, 0x694, 0);
    memmove(&sv->used[used], &sv->used[used + 1], (size_t)(sv->nused - used - 1) * sizeof *sv->used);
    sv->nused--;
    drop(k);
    for (int j = 0; j < s_n; j++)
        if (!s_ent[j].owned && s_ent[j].idx > used) s_ent[j].idx--;
    return true;
}

int Market_Listed(void)
{
    market();
    int n = 0;
    for (int k = 0; k < s_n; k++) n += s_ent[k].owned;
    return n;
}

/* The buyers (0x4f7430), each car of the market in turn, in the market's year (0x691b58):
     s = (value - price) * 100 / value (the double 0x5b4bb0), truncated, - its damage (0x4eefd0) + 100
       + its mileage's points (+0x698 in the table of its age, the year less +0x1fc: up to 10 0x5b7c88,
       20 0x5b7cb8, 30 0x5b7ce8, else 0x5b7d18; four (min, max, points))
       + a fifth of +0x69c + its damage's points (0x5b7d48: five (min, max, points));
   the table 0x5b7d88 (seven (min, max, per cent, flag byte); none: no sale) by s: without the flag, that
   per cent of the time a sale at the price (the market's own car sold, +0x690; the player's an offer of
   the price); else an offer (+0x692) of its value + (rand % 601 - 300) * 0.0001 of it, at most the price
   and at least 0 (the market's own cars get theirs too). */
static void buyers(Save *sv)
{
    int year = sv->usedinfo;
    for (int k = 0; k < s_n; k++) {
        const SaveCar *c = car_of(sv, s_ent[k]);
        uint8_t *r = rec_of(sv, s_ent[k]);
        if (!c) continue;
        int32_t value = FEData_CarValue(c, year);
        double t = ((double)value - (double)get32(r, 0x68c)) * kd(0x5b4bb0) / (double)value;
        int32_t dmg = ftol(FEData_CarDamage(c));
        int32_t s = (int32_t)((uint32_t)ftol(t) - (uint32_t)dmg + 100u);   /* (wrapping as the x87's) */
        int age = year - get16(r, 0x1fc);
        uint32_t tab = age <= 10 ? 0x5b7c88 : age <= 20 ? 0x5b7cb8 : age <= 30 ? 0x5b7ce8 : 0x5b7d18;
        int32_t miles = get32(r, 0x698), mp = 0, dp = 0;
        for (int i = 0; i < 4; i++, tab += 12)
            if ((int32_t)exe_u32(tab) <= miles && miles <= (int32_t)exe_u32(tab + 4)) mp = (int32_t)exe_u32(tab + 8);
        s += get32(r, 0x69c) / 5 + mp;
        for (uint32_t e = 0x5b7d48; e + 4 < 0x5b7d88; e += 12)
            if ((int32_t)exe_u32(e) <= dmg && dmg <= (int32_t)exe_u32(e + 4)) dp = (int32_t)exe_u32(e + 8);
        s += dp;
        uint32_t pct = 0;
        bool flag = true;
        for (uint32_t e = 0x5b7d88; e + 4 < 0x5b7dfc; e += 16)
            if ((int32_t)exe_u32(e) <= s && s <= (int32_t)exe_u32(e + 4)) {
                pct = exe_u32(e + 8);
                flag = (exe_u32(e + 12) & 0xff) != 0;
            }
        if (!flag && REAL_rand() % 100 < pct) {
            if (!r[0x6a0]) r[0x690] = 1;
            else {
                r[0x692] = 1;
                put32(r, 0x694, get32(r, 0x68c));
            }
            continue;
        }
        r[0x692] = 1;
        uint32_t d = REAL_rand() % 0x259;
        int32_t offer = ftol(((double)d - (double)kf(0x5b2524)) * (double)kf(0x5b38f8) * (double)value) + value;
        if (offer > get32(r, 0x68c)) offer = get32(r, 0x68c);
        if (offer < 0) offer = 0;
        put32(r, 0x694, offer);
    }
}

static void drop_used(Save *sv, int k)   /* a market car of its own deleted (0x4edae0) */
{
    int i = s_ent[k].idx;
    free(sv->used[i].parts);
    memmove(&sv->used[i], &sv->used[i + 1], (size_t)(sv->nused - i - 1) * sizeof *sv->used);
    sv->nused--;
    drop(k);
    for (int j = 0; j < s_n; j++)
        if (!s_ent[j].owned && s_ent[j].idx > i) s_ent[j].idx--;
}

/* The year's new cars (0x4f7660): of the racers with a model (0x6919f0), each out two years before the
   market's year and for sale (+0x204 not 2 or 3) by the chance of its age (0x5b7a10: its entry at most
   REAL_rand() % 60), a used car of it (0x4f7860); with more than 24 in the market, the first of the
   market's own leaves first. */
static void new_cars(Save *sv, int year)
{
    const uint8_t *pool[160];
    int n = FEData_UsedPool(pool, 160);
    for (int i = 0; i < n; i++) {
        int first = get16(pool[i], 0x1f8), kind = get16(pool[i], 0x204);
        if (!(first < year - 1 && kind != 3 && kind != 2)) continue;
        if (exe_u32(0x5b7a10 + 4u * (uint32_t)(year - first)) > REAL_rand() % 60) continue;
        SaveCar nc;
        if (!FEData_UsedCar(pool[i], year, &nc)) continue;
        if (s_n > 0x18)
            for (int k = 0; k < s_n; k++)
                if (!s_ent[k].owned && !rec_of(sv, s_ent[k])[0x6a0]) {
                    drop_used(sv, k);
                    break;
                }
        SaveCar *nu = realloc(sv->used, (size_t)(sv->nused + 1) * sizeof *nu);
        if (!nu || s_n >= 256) {
            free(nc.parts);
            continue;
        }
        sv->used = nu;
        sv->used[sv->nused] = nc;
        s_ent[s_n++] = (MarketEnt){false, sv->nused};
        sv->nused++;
    }
}

void Market_Update(int year)   /* 0x4f7350 */
{
    Save *sv = market();
    if (!sv) return;
    /* srand(time(0)) (0x537e80): the port's clock in seconds, so runs repeat. */
    REAL_srand(app_ticks_ms() / 1000u);
    sv->usedinfo = year;
    buyers(sv);
    for (int k = 0; k < s_n;) {
        const uint8_t *r = rec_of(sv, s_ent[k]);
        if (r && r[0x690] && !r[0x6a0]) drop_used(sv, k);
        else k++;
    }
    for (int k = 0; k < s_n; k++) {
        uint8_t *r = rec_of(sv, s_ent[k]);
        if (r && !r[0x6a0]) put32(r, 0x68c, FEData_CarValue(car_of(sv, s_ent[k]), year));
    }
    new_cars(sv, year);
}

int Market_FirstOffer(void)   /* 0x4f8b10 */
{
    Save *sv = market();
    for (int k = 0; sv && k < s_n; k++) {
        const uint8_t *r = rec_of(sv, s_ent[k]);
        if (r && r[0x6a0] && r[0x692] && s_ent[k].owned) return owned_index(sv, s_ent[k].idx);
    }
    return -1;
}

void Market_Decline(int owned)   /* 0x4f8cc0 */
{
    Save *sv = market();
    if (!sv || owned < 0 || owned >= sv->ncars) return;
    sv->cars[owned].rec[0x692] = 0;
    put32(sv->cars[owned].rec, 0x694, 0);
}

void Market_Accept(int owned)   /* 0x501740 */
{
    Save *sv = market();
    if (!sv || owned < 0 || owned >= sv->ncars) return;
    int32_t offer = get32(sv->cars[owned].rec, 0x694);
    if (offer > 999999999) offer = 999999999;
    if (offer < -999999999) offer = -999999999;
    sv->money += offer;
    Audio_UI(10);   /* 0x4aab80 */
    Market_Unlist(owned);
    free(sv->cars[owned].parts);
    memmove(&sv->cars[owned], &sv->cars[owned + 1], (size_t)(sv->ncars - owned - 1) * sizeof *sv->cars);
    sv->ncars--;
    FESave_Write();
}
