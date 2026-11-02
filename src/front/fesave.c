/* The player's save (fesave.h). */
#include "front/fesave.h"
#include "front/fecd.h"
#include "front/fehudpreview.h"
#include "exe.h"
#include "front/fe_parts.h"
#include "pending.h"
#include "platform.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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
        } else if (!strcmp(name, "Knockout")) {   /* 0x4dd810: the knockout's state raw (front/feaccess.c) */
            rd_skip(&r, 0x84);
        } else if (!strcmp(name, "CDPlayer")) {   /* 0x4bdd50 */
            int32_t k = rd_i32(&r);
            if (k < 0 || k > 0x400) return false;
            free(s->cd);
            s->cd = k ? calloc((size_t)k, sizeof *s->cd) : NULL;
            s->ncd = 0;
            for (int i = 0; i < k && !r.bad; i++) s->cd[s->ncd++] = rd_i32(&r);
            if (r.bad || !add_section(s, name, NULL, 0)) return false;
            continue;
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
        } else if (!strcmp(x->name, "CDPlayer")) {   /* 0x4bdf20 */
            wr_i32(&w, s->ncd);
            for (int k = 0; k < s->ncd; k++) wr_i32(&w, s->cd[k]);
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
    free(s->cd);
    memset(s, 0, sizeof *s);
}

/* ---------------------------------------------------------------- the front end's player */

static Save s_save;
static bool s_tried, s_ok;
static char s_stem[104];     /* the file the save came from ("<picture><name>", or the template's) */
static unsigned s_serial;    /* (the port's: a new load for the screens' own copies) */
static char s_players[64][104];
static int s_nplayers, s_chosen = -1;

static const char *template_stem(void) { return exe_str(0x5d9a44); }   /* (0x5d9a40: "XXDefXX") */

/* A SaveData file: the user's own first, then the game's ("%s%s.sav" 0x5d9a4c in 0x65b360; the user's
   files are flat). */
static uint8_t *read_savedata(const char *file, size_t *n)
{
    uint8_t *d = plat_load_user_file(file, n);
    if (d) return d;
    char path[160];
    snprintf(path, sizeof path, "SaveData\\%s", file);
    return vfs_read_all(path, n);
}

static bool savedata_exists(const char *file)
{
    size_t n = 0;
    uint8_t *d = read_savedata(file, &n);
    free(d);
    return d != NULL;
}

/* 0x4ddfc0: a player of that name, under any picture ("%d%s" 0x5d9a58, 0 .. 9); the empty name counts. */
bool FESave_NameTaken(const char *name)
{
    for (int i = 0; i < 10; i++) {
        char f[128];
        snprintf(f, sizeof f, "%d%s.sav", i, name);
        if (savedata_exists(f)) return true;
        if (!name[0]) return true;
    }
    return false;
}

static void write_list_file(const char *stem)
{
    plat_save_user_file("player.lst", stem, strlen(stem));   /* ("%splayer.lst" 0x5d9a94) */
}

/* Save_Write 0x4de850: the data areas' sections to "<picture><name>.sav" (the name itself for picture -1);
   nothing for no name. */
static bool has_section(const Save *sv, const char *name)
{
    for (int i = 0; i < sv->nsec; i++)
        if (!strcmp(sv->sec[i].name, name)) return true;
    return false;
}

/* The CD player's chosen songs: from a save read (its loader fills the list, 0x4bdd50) and into the
   one written (the writer's from the list, 0x4bdf20). */
void FESave_CDApply(void)
{
    if (s_ok && has_section(&s_save, "CDPlayer")) FECD_SetSelected(s_save.cd, s_save.ncd);
}

static void cd_take(void)
{
    int n = FECD_SelCount();
    int32_t *c = n ? malloc(sizeof *c * (size_t)n) : NULL;
    if (n && !c) return;
    if (n) memcpy(c, FECD_Sel(), sizeof *c * (size_t)n);
    free(s_save.cd);
    s_save.cd = c;
    s_save.ncd = n;
}

static bool save_write(const char *name, int32_t pic)
{
    if (!name[0] || !s_ok) return false;
    cd_take();
    char f[128];
    if (pic == -1) snprintf(f, sizeof f, "%s.sav", name);
    else snprintf(f, sizeof f, "%d%s.sav", (int)pic, name);
    size_t n = 0;
    uint8_t *b = Save_Serialize(&s_save, &n);
    if (!b) return false;
    bool ok = plat_save_user_file(f, b, n);
    free(b);
    return ok;
}

static void add_player(const char *name, bool is_dir, void *user)
{
    (void)user;
    char f[160];
    size_t l = strlen(name);
    if (is_dir || l < 5 || strcasecmp(name + l - 4, ".sav") || l - 4 >= 100 || s_nplayers == 64) return;
    snprintf(f, sizeof f, "%.*s", (int)(l - 4), name);
    if (!strcasecmp(f, template_stem())) return;
    for (int i = 0; i < s_nplayers; i++)
        if (!strcmp(s_players[i], f)) return;
    snprintf(s_players[s_nplayers++], sizeof s_players[0], "%s", f);
}

static int stem_cmp(const void *a, const void *b) { return strcmp((const char *)a + 1, (const char *)b + 1); }

/* Save_ListPlayers 0x4dea10: UserList.UserList anew from the saves there are (their stems under 100
   characters, not the template's) in the order found, the one player.lst names chosen, then sorted by
   name past the picture's digit. */
void FESave_ListPlayers(void)
{
    char last[104] = "";
    size_t n = 0;
    uint8_t *d = read_savedata("player.lst", &n);
    if (d && n <= 100) {
        memcpy(last, d, n);
        last[n] = 0;
    }
    free(d);
    s_nplayers = 0;
    s_chosen = -1;
    /* (the user's files, then the game's SaveData\*.sav not already among them) */
    int k = plat_user_file_count();
    for (int i = 0; i < k; i++) {
        char f[160];
        if (plat_user_file_name(i, f, sizeof f)) add_player(f, NULL, NULL);
    }
    vfs_list("SaveData", add_player, NULL);
    /* (the position is the match's in the order found, then the list sorted under it: it may name
       another player after the sort) */
    for (int i = 0; i < s_nplayers; i++)
        if (!strcmp(s_players[i], last)) s_chosen = i;
    qsort(s_players, (size_t)s_nplayers, sizeof s_players[0], stem_cmp);
}

/* The replays (Replay.List, FEdaUserList_v0 0x4e5400 and FE_ListReplays 0x50ca80): the save directory's
   "*.rpl" in the order found (the user's, then the game's not among them), each to its first '.'. */
static char s_replays[64][100];
static int s_nreplays;

static void add_replay(const char *name, bool is_dir, void *user)
{
    (void)user;
    size_t l = strlen(name);
    if (is_dir || l < 5 || strcasecmp(name + l - 4, ".rpl") || s_nreplays == 64) return;
    char f[100];
    snprintf(f, sizeof f, "%s", name);
    char *dot = strchr(f, '.');   /* (0x5a2620) */
    if (dot) *dot = 0;
    for (int i = 0; i < s_nreplays; i++)
        if (!strcmp(s_replays[i], f)) return;
    snprintf(s_replays[s_nreplays++], sizeof s_replays[0], "%s", f);
}

void FESave_ListReplays(void)
{
    s_nreplays = 0;
    int k = plat_user_file_count();
    for (int i = 0; i < k; i++) {
        char f[160];
        if (plat_user_file_name(i, f, sizeof f)) add_replay(f, false, NULL);
    }
    vfs_list("SaveData", add_replay, NULL);
}

int FESave_ReplayCount(void) { return s_nreplays; }
const char *FESave_ReplayStem(int i) { return i >= 0 && i < s_nreplays ? s_replays[i] : ""; }

void FESave_DeleteReplay(int i)
{
    if (i < 0 || i >= s_nreplays) return;
    char f[112];
    snprintf(f, sizeof f, "%s.rpl", s_replays[i]);   /* ("%s%s.rpl" 0x5ccec4; 0x59dfb0: the game's own stay) */
    plat_delete_user_file(f);
    FESave_ListReplays();
}

int FESave_PlayerCount(void) { return s_nplayers; }
const char *FESave_PlayerStem(int i) { return i >= 0 && i < s_nplayers ? s_players[i] : ""; }
int FESave_PlayerChosen(void) { return s_chosen; }
void FESave_ChoosePlayer(int i) { s_chosen = i >= 0 && i < s_nplayers ? i : -1; }

/* 0x4de460: the save of a stem read into the data areas (their loaders by section, LoadInfo), the player
   then the stem's name past its picture (the template's its own), player.lst the stem. With write_first
   the current player's save is written first, when the check the original makes holds: some
   "<0..9><stem>.sav" there, or the stem empty (a check of the wrong name: switching players rarely
   writes), and the player isn't the template. */
bool FESave_LoadStem(const char *stem, bool write_first)
{
    if (write_first) {
        bool any = false;
        for (int i = 0; i < 10 && !any; i++) {
            char f[128];
            snprintf(f, sizeof f, "%d%s.sav", i, stem);
            any = savedata_exists(f) || !stem[0];
        }
        if (any && s_ok && strcmp(s_save.player, template_stem())) save_write(s_save.player, s_save.info);
    }
    char f[128];
    snprintf(f, sizeof f, "%s.sav", stem);
    size_t n = 0;
    uint8_t *d = read_savedata(f, &n);
    if (!d) return false;
    Save next;
    bool ok = Save_Parse(&next, d, n);
    free(d);
    if (!ok) {
        Save_Free(&next);
        return false;
    }
    if (s_ok) Save_Free(&s_save);
    s_save = next;
    s_ok = s_tried = true;
    s_serial++;
    snprintf(s_stem, sizeof s_stem, "%s", stem);
    snprintf(s_save.player, sizeof s_save.player, "%s", strcmp(stem, template_stem()) ? stem + 1 : stem);
    FESave_CDApply();
    write_list_file(stem);
    FESave_ListPlayers();
    /* (0x501020's re-listing of the owned cars up for sale: the market's, front/femarket.c, at its list's
       rebuild for the new serial) */
    return true;
}

/* Save_Load 0x4de060: a new player of a name and a picture: none of that name already, the template
   read (player2 the name), the default HUD layouts copied for it (FEData\Data\nfs5-<n>.hud to
   "<name><n>.hud" and XXhud<n>XX.hud), the picture set, its save written and read back as the player. */
bool FESave_Create(const char *name, int pic)
{
    if (FESave_NameTaken(name)) return false;
    bool ok = FESave_LoadStem(template_stem(), true);
    if (ok) snprintf(s_save.player2, sizeof s_save.player2, "%s", name);
    for (int k = 1; ok && k <= 2; k++) {
        char src[64], dst[128];
        snprintf(src, sizeof src, "FEData\\Data\\nfs5-%d.hud", k);
        size_t n = 0;
        uint8_t *d = vfs_read_all(src, &n);
        if (!d) ok = false;
        snprintf(dst, sizeof dst, "%s%d.hud", name, k);   /* ("%s%s%s" 0x5ccb98) */
        if (ok) ok = plat_save_user_file(dst, d, n);
        snprintf(dst, sizeof dst, "XXhud%dXX.hud", k);
        if (ok) ok = plat_save_user_file(dst, d, n);
        free(d);
    }
    /* 0x4e53c0, copies made or not: the data areas' slot 1, FEdaDisplay's alone doing anything (the
       choices from the template player's layout, so the defaults just copied) */
    FEHudPreview_ResetChoices();
    if (!ok) return false;
    s_save.info = pic;
    if (!save_write(name, s_save.info)) return false;
    char stem[104];
    snprintf(stem, sizeof stem, "%d%s", pic, name);
    return FESave_LoadStem(stem, true);
}

/* 0x4de350: a player's save and HUD layouts deleted ("<stem>.sav", "<name>1.hud", "<name>2.hud"). */
bool FESave_Delete(const char *stem)
{
    char f[128];
    snprintf(f, sizeof f, "%s.sav", stem);
    if (!plat_delete_user_file(f)) return false;
    for (int k = 1; stem[0] && k <= 2; k++) {
        snprintf(f, sizeof f, "%s%d.hud", stem + 1, k);
        plat_delete_user_file(f);
    }
    return true;
}

/* The front end's start (FEApplication_Run 0x4d2e70): the players listed and player.lst's loaded
   (0x4dee70). Without one the port keeps the template's data with no player's name, which writes
   nothing, while the player is chosen or made. */
static void start_player(void)
{
    s_tried = true;
    FESave_ListPlayers();
    char last[104] = "";
    size_t n = 0;
    uint8_t *d = read_savedata("player.lst", &n);
    if (d && n <= 100) {
        memcpy(last, d, n);
        last[n] = 0;
    }
    free(d);
    if (last[0] && FESave_LoadStem(last, false)) return;
    char f[128];
    snprintf(f, sizeof f, "%s.sav", template_stem());
    d = read_savedata(f, &n);
    if (d) {
        s_ok = Save_Parse(&s_save, d, n);
        if (!s_ok) Save_Free(&s_save);
        free(d);
    }
    if (s_ok) {
        s_serial++;
        FESave_CDApply();
        s_save.player[0] = 0;
        s_stem[0] = 0;
    } else {
        plat_log("front end: no player save (SaveData\\XXDefXX.sav)");
    }
}

Save *FESave_Current(void)
{
    if (!s_tried) start_player();
    return s_ok ? &s_save : NULL;
}

bool FESave_HavePlayer(void) { FESave_Current(); return s_ok && s_stem[0]; }
const char *FESave_Stem(void) { return s_stem; }
unsigned FESave_Serial(void) { FESave_Current(); return s_serial; }

bool FESave_Write(void)
{
    if (!s_ok) return false;
    return save_write(s_save.player, s_save.info);
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
