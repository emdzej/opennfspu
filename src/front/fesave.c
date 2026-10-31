/* The player's save (fesave.h). */
#include "front/fesave.h"
#include "exe.h"
#include "front/fe_parts.h"
#include "platform.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const uint8_t *d; size_t n, p; bool bad; } Rd;

static int32_t rd_i32(Rd *r)
{
    if (r->p + 4 > r->n) { r->bad = true; return 0; }
    int32_t v;
    memcpy(&v, r->d + r->p, 4);
    r->p += 4;
    return v;
}

static void rd_skip(Rd *r, size_t k)
{
    if (r->p + k > r->n) { r->bad = true; r->p = r->n; return; }
    r->p += k;
}

static const char *rd_str(Rd *r)
{
    const char *s = (const char *)r->d + r->p;
    const void *z = memchr(s, 0, r->n - r->p);
    if (!z) { r->bad = true; r->p = r->n; return ""; }
    r->p += (size_t)((const char *)z - s) + 1;
    return s;
}

static bool rd_car(Rd *r, SaveCar *c)   /* 0x4edd30 */
{
    if (r->p + SAVE_CAR > r->n) return r->bad = true, false;
    memcpy(c->rec, r->d + r->p, SAVE_CAR);
    r->p += SAVE_CAR;
    c->ninstalled = rd_i32(r);
    c->nother = rd_i32(r);
    int k = c->ninstalled + c->nother;
    if (r->bad || k < 0 || (size_t)k * 8 > r->n - r->p) return r->bad = true, false;
    c->parts = k ? malloc((size_t)k * 8) : NULL;
    if (k && !c->parts) return r->bad = true, false;
    if (k) memcpy(c->parts, r->d + r->p, (size_t)k * 8);
    r->p += (size_t)k * 8;
    return true;
}

static bool add_section(Save *s, const char *name, const uint8_t *d, size_t n)
{
    SaveSection *ns = realloc(s->sec, sizeof *ns * (size_t)(s->nsec + 1));
    if (!ns) return false;
    s->sec = ns;
    SaveSection *x = &s->sec[s->nsec++];
    memset(x, 0, sizeof *x);
    snprintf(x->name, sizeof x->name, "%s", name);
    if (n) {
        x->data = malloc(n);
        if (!x->data) return false;
        memcpy(x->data, d, n);
    }
    x->size = n;
    return true;
}

bool Save_Parse(Save *s, const uint8_t *d, size_t n)
{
    memset(s, 0, sizeof *s);
    Rd r = {d, n, 0, false};
    while (r.p < n && !r.bad) {
        const char *name = rd_str(&r);
        size_t start = r.p;
        if (!strcmp(name, "Missions")) {
            int32_t k = rd_i32(&r);
            rd_skip(&r, (size_t)(k < 0 ? 0 : k));
        } else if (!strcmp(name, "PartsMarket")) {
            int32_t k = rd_i32(&r);
            rd_skip(&r, (size_t)(k < 0 ? 0 : k) * 0x168);
        } else if (!strcmp(name, "Tournament")) {
            for (int i = 0; i < 7; i++) rd_i32(&r);
            int32_t k = rd_i32(&r);
            rd_skip(&r, (size_t)(k < 0 ? 0 : k));
        } else if (!strcmp(name, "PlayerInfo")) {   /* 0x500bc0 */
            int32_t k = rd_i32(&r);
            if (k < 0 || k > 256) return false;
            s->cars = k ? calloc((size_t)k, sizeof *s->cars) : NULL;
            for (int i = 0; i < k && rd_car(&r, &s->cars[i]); i++) s->ncars++;
            s->money = rd_i32(&r);
            s->info = rd_i32(&r);
            snprintf(s->player, sizeof s->player, "%s", rd_str(&r));
            snprintf(s->player2, sizeof s->player2, "%s", rd_str(&r));
            if (r.p + 35 > n) return false;
            memcpy(s->flags, d + r.p, 3);
            memcpy(s->extra, d + r.p + 3, 32);
            r.p += 35;
            if (!add_section(s, name, NULL, 0)) return false;
            continue;
        } else if (!strcmp(name, "TrackRecords")) {
        } else if (!strcmp(name, "UsedCarMarket")) {   /* 0x4f6d70 */
            int32_t k = rd_i32(&r);
            s->usedinfo = rd_i32(&r);
            if (k < 0 || k > 256) return false;
            s->used = k ? calloc((size_t)k, sizeof *s->used) : NULL;
            for (int i = 0; i < k && rd_car(&r, &s->used[i]); i++) s->nused++;
            if (!add_section(s, name, NULL, 0)) return false;
            continue;
        } else if (!strcmp(name, "Personalities")) {
            int32_t k = rd_i32(&r);
            rd_skip(&r, (size_t)(k < 0 ? 0 : k) * 2);
        } else if (!strcmp(name, "Knockout")) {
            rd_skip(&r, 0x84);
        } else if (!strcmp(name, "CDPlayer")) {
            int32_t k = rd_i32(&r);
            rd_skip(&r, (size_t)(k < 0 ? 0 : k) * 4);
        } else if (!strcmp(name, "accessor")) {
            int32_t k = rd_i32(&r);
            rd_skip(&r, (size_t)(k < 0 ? 0 : k));
        } else {
            return false;   /* (the reader stops there too: no loader of that name, 0x4de460) */
        }
        if (r.bad || !add_section(s, name, d + start, r.p - start)) return false;
    }
    return !r.bad;
}

typedef struct { uint8_t *b; size_t n, cap; bool bad; } Wr;

static void wr(Wr *w, const void *p, size_t k)
{
    if (w->bad) return;
    if (w->n + k > w->cap) {
        size_t c = w->cap ? w->cap * 2 : 0x10000;
        while (c < w->n + k) c *= 2;
        uint8_t *nb = realloc(w->b, c);
        if (!nb) { w->bad = true; return; }
        w->b = nb, w->cap = c;
    }
    memcpy(w->b + w->n, p, k);
    w->n += k;
}

static void wr_i32(Wr *w, int32_t v) { wr(w, &v, 4); }
static void wr_str(Wr *w, const char *s) { wr(w, s, strlen(s) + 1); }

static void wr_car(Wr *w, const SaveCar *c)
{
    wr(w, c->rec, SAVE_CAR);
    wr_i32(w, c->ninstalled);
    wr_i32(w, c->nother);
    if (c->ninstalled + c->nother) wr(w, c->parts, (size_t)(c->ninstalled + c->nother) * 8);
}

uint8_t *Save_Serialize(const Save *s, size_t *n)
{
    Wr w = {0};
    for (int i = 0; i < s->nsec; i++) {
        const SaveSection *x = &s->sec[i];
        wr_str(&w, x->name);
        if (!strcmp(x->name, "PlayerInfo")) {
            wr_i32(&w, s->ncars);
            for (int k = 0; k < s->ncars; k++) wr_car(&w, &s->cars[k]);
            wr_i32(&w, s->money);
            wr_i32(&w, s->info);
            wr_str(&w, s->player);
            wr_str(&w, s->player2);
            wr(&w, s->flags, 3);
            wr(&w, s->extra, 32);
        } else if (!strcmp(x->name, "UsedCarMarket")) {
            wr_i32(&w, s->nused);
            wr_i32(&w, s->usedinfo);
            for (int k = 0; k < s->nused; k++) wr_car(&w, &s->used[k]);
        } else if (x->size) {
            wr(&w, x->data, x->size);
        }
    }
    if (w.bad) { free(w.b); return NULL; }
    *n = w.n;
    return w.b;
}

void Save_Free(Save *s)
{
    for (int i = 0; i < s->nsec; i++) free(s->sec[i].data);
    free(s->sec);
    for (int i = 0; i < s->ncars; i++) free(s->cars[i].parts);
    free(s->cars);
    for (int i = 0; i < s->nused; i++) free(s->used[i].parts);
    free(s->used);
    memset(s, 0, sizeof *s);
}

/* ---------------------------------------------------------------- the front end's player */

static Save s_save;
static bool s_tried, s_ok;
static char s_name[64];

Save *FESave_Current(void)
{
    if (s_tried) return s_ok ? &s_save : NULL;
    s_tried = true;
    snprintf(s_name, sizeof s_name, "%s", exe_str(0x5d9a44));   /* "XXDefXX": the default player */
    char path[128];
    snprintf(path, sizeof path, "SaveData/%s.sav", s_name);   /* ("%s%s.sav" 0x5d9a4c in SaveData\, 0x65b360) */
    size_t n = 0;
    uint8_t *d = plat_load_user_file(path, &n);
    if (!d) {
        snprintf(path, sizeof path, "SaveData\\%s.sav", s_name);
        d = vfs_read_all(path, &n);
    }
    if (d) {
        s_ok = Save_Parse(&s_save, d, n);
        if (!s_ok) Save_Free(&s_save);
        free(d);
    }
    if (!s_ok) plat_log("front end: no player save (SaveData\\XXDefXX.sav)");
    return s_ok ? &s_save : NULL;
}

bool FESave_Write(void)
{
    if (!s_ok) return false;
    size_t n = 0;
    uint8_t *b = Save_Serialize(&s_save, &n);
    if (!b) return false;
    char path[128];
    snprintf(path, sizeof path, "SaveData/%s.sav", s_name);
    bool ok = plat_save_user_file(path, b, n);
    free(b);
    return ok;
}

const char *FE_MoneyText(int32_t v)
{
    static char b[64];
    const char *f;
    snprintf(b, sizeof b, "%d", v);   /* (0x5cd1b0) */
    if (v < -1000000) {
        v = -v;
        if ((f = FEStrings_Get(0x6a5))) snprintf(b, sizeof b, f, '-', v / 1000000, v % 1000000 / 1000, v % 1000);
    } else if (v < -1000) {
        v = -v;
        if ((f = FEStrings_Get(0x6a7))) snprintf(b, sizeof b, f, '-', v / 1000, v % 1000);
    } else if (v < 0) {
        snprintf(b, sizeof b, "%d", -v);   /* (the original drops the sign here) */
    } else if (999 < v && v < 1000000) {
        if ((f = FEStrings_Get(0x6a6))) snprintf(b, sizeof b, f, v / 1000, v % 1000);
    } else if (1000000 <= v) {
        if ((f = FEStrings_Get(0x6a4))) snprintf(b, sizeof b, f, v / 1000000, v % 1000000 / 1000, v % 1000);
    }
    return b;
}
