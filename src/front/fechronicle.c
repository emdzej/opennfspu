/* The Porsche Chronicle (see fechronicle.h). */
#include "front/fechronicle.h"
#include "front/fepointer.h"
#include "exe.h"
#include "front/fe2d.h"
#include "front/fe_parts.h"
#include "front/feaccess.h"
#include "front/feapp.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ERAS = 5, YEARS = 0x35, KINDS = 3, SLOTS = 10, MEDIA = 4 };

static bool s_loaded;                                 /* 0x699d04 */
static FeImage *s_icons[ERAS * YEARS * KINDS * SLOTS]; /* 0x692038: by era, year 48 .. 100, kind, five pairs */
static int s_first[ERAS];                             /* 0x699c70: an era's first year */
static int s_last[ERAS];                              /* 0x692024: the year's top for the era */
static int s_era = -1;                                /* 0x5dba3c (2 in the exe's data) */
static char s_media[MEDIA][100];                      /* 0x699c84: the files loaded */
static int s_nmedia;                                  /* 0x699d00 */
static int s_shown;                                   /* 0x699cfc: the one the picture shows */
static char s_movie[100];                             /* 0x699c98 */
static char s_eramovie[16];                           /* 0x691f5c */

static int era(void)
{
    if (s_era < 0) s_era = (int)exe_u32(0x5dba3c);
    return s_era;
}

int FEChronicle_Era(void) { return era(); }

/* An icon bank's path ("%s%s.fsh" 0x5cccc8: the chronicle's folder, the era's name, 0x14 apart from
   "iconsrace" 0x5b8ab4 to 0x5b8b18). */
static void bank_path(int e, char *out, size_t cap)
{
    snprintf(out, cap, "%s%s.fsh", FE_DIR_CHRON, exe_str(0x5b8ab4 + 0x14u * (unsigned)e));
}

/* The icon banks (0x5066a0, once): each shape filed by its name's year (its first two digits, 00 being
   2000) and kind (its third: p 0, m 1, v 2, others 0), ten at most to a year and kind; the era's first
   year the least seen, its top 1996 and one more for each third, fifth, ... picture in a year from it. */
static void load_icons(void)
{
    if (s_loaded) return;
    s_loaded = true;
    memset(s_icons, 0, sizeof s_icons);
    for (int e = 0; e < ERAS; e++) {
        char path[200];
        bank_path(e, path, sizeof path);
        FEShapes_Load(path);   /* FE_ValidateShapes 0x4d67f0 (and the file read for its names, 0x59d690) */
        s_first[e] = 0;
        s_last[e] = 0x7cc;
        for (int i = 0;; i++) {
            FeImage *im = FEShapes_Image(path, i);
            if (!im) break;
            const signed char *n = (const signed char *)im->tag;   /* (0x537cd0) */
            int kind = 0;
            switch (n[2]) {
            case 'M': case 'm': kind = 1; break;
            case 'P': case 'p': kind = 0; break;
            case 'V': case 'v': kind = 2; break;
            }
            int yy = n[1] - 0x210 + n[0] * 10;
            if (yy == 0) yy = 100;
            int row = e * YEARS - 0x30 + yy;
            if (row < 0 || row >= ERAS * YEARS) continue;   /* (the port's: a name of no year) */
            FeImage **slot = &s_icons[(kind + row * KINDS) * SLOTS];
            int k = 0;
            while (k < SLOTS && slot[k]) k++;
            int year = yy == 100 ? 2000 : yy + 0x76c;
            if (s_first[e] == 0 || year < s_first[e]) s_first[e] = year;
            if (k < SLOTS) {
                if (s_last[e] <= yy + 0x76c && 1 < k && k % 2 == 0 && kind == 0) s_last[e]++;
                slot[k] = im;
            }
        }
    }
}

/* An icon (0x506a00): the era's year yy (48 .. 100), kind, set k, plain or hovered; none before the banks.
   (A year past the era's reads the next era's, as the original's table runs on; past the table none.) */
static FeImage *icon(int yy, int kind, int k, bool hover)
{
    if (!s_loaded) return NULL;
    int i = (hover ? 1 : 0) + (k + (kind + (era() * YEARS - 0x30 + yy) * KINDS) * 5) * 2;
    return 0 <= i && i < (int)(sizeof s_icons / sizeof s_icons[0]) ? s_icons[i] : NULL;
}

static void free_media(void)
{
    for (int i = 0; i < s_nmedia; i++) FEShapes_Release(s_media[i]);   /* 0x4d7270 */
    s_nmedia = 0;
    s_shown = 0;
}

void FEChronicle_Free(void)   /* 0x506920 */
{
    if (!s_loaded) return;
    s_loaded = false;
    for (int e = 0; e < ERAS; e++) {
        char path[200];
        bank_path(e, path, sizeof path);
        FEShapes_Release(path);
    }
    free_media();
}

/* The media of a kind for the era's year and set (FEChronicle_LoadMedia 0x506b60): the files
   "%s%c%c%02d%d%c.fsh" (0x5dbad0: p, m or v, the era's letter of "CBADE" 0x5b8b18, yy % 100, 1 .. 4,
   'a' + the set) while they are there; a movie's kind its .mad (0x5dbabc, 1) as the chosen movie. */
static bool load_media(int kind, int yy, int k)
{
    free_media();
    char letter = kind == 0 ? 'p' : kind == 2 ? 'v' : 'm';
    char path[100];
    for (;;) {
        snprintf(path, sizeof path, "%s%c%c%02d%d%c.fsh", FE_DIR_CHRON, letter, exe_str(0x5b8b18)[era()], yy % 100,
                 s_nmedia + 1, 'a' + k);
        if (vfs_exists(path)) {   /* 0x59d9e0 */
            snprintf(s_media[s_nmedia], sizeof s_media[0], "%s", path);
            FEShapes_Load(path);
            s_nmedia++;
        }
        /* (the same name tried again: the next only after one that was there) */
        if (!vfs_exists(path) || s_nmedia > 3) {
            if (kind == 2)
                snprintf(s_movie, sizeof s_movie, "%s%c%c%02d%d%c.mad", FE_DIR_CHRON, letter, exe_str(0x5b8b18)[era()],
                         yy % 100, 1, 'a' + k);
            return s_nmedia != 0;
        }
    }
}

static const char *media(int i) { return 0 <= i && i < s_nmedia ? s_media[i] : NULL; }   /* 0x506b40 */

const char *FEChronicle_Shown(void) { return media(s_shown); }

/* The era's year range and its first year (the eras' callbacks 0x506d50 .. 0x506e50). */
static void era_year(void)
{
    FEAccessor *y = FEAccess_Find(exe_str(0x5dba80));   /* "Chronicle.Year" */
    FEAccess_SetRange(y, 0x79c, s_last[era()]);
    if (y) FEAccess_SetPos(y, s_first[era()] - 0x79c);
}

void FEChronicle_Open(void)   /* 0x506450 */
{
    load_icons();
    FEAccessor *y = FEAccess_Find(exe_str(0x5dba80));
    if (y) FEAccess_SetPos(y, 0);
    era_year();
}

bool FEChronicle_Callback(const char *cb)
{
    static const uint32_t names[ERAS] = {0x5dba98, 0x5dba94, 0x5d0fd4, 0x5dba90, 0x5d0fb4};   /* RACEMODEL, 911, 356, 924, 914 */
    for (int e = 0; e < ERAS; e++)
        if (!strcmp(cb, exe_str(names[e]))) {
            s_era = e;
            era_year();
            return true;
        }
    return false;
}

const char *FEChronicle_EraMovie(void)   /* 0x506a50 */
{
    static const uint32_t names[ERAS] = {0x5dbab8, 0x5dbab4, 0x5dbab0, 0x5dbaac, 0x5dbaa8};   /* ch5 Ch3 Ch1 Ch2 Ch4 */
    int e = era();
    snprintf(s_eramovie, sizeof s_eramovie, "%s", exe_str(0 <= e && e < ERAS ? names[e] : 0x5dbaa4));
    return s_eramovie;
}

const char *FEChronicle_Movie(void) { return s_movie; }

/* ---------------------------------------------------------------- the controls' helpers */

static const char *data_str(const FertControl *c, const char *name)
{
    for (int k = 0; k < c->nparts; k++) {
        FertAttr *a = FertAttrList_find(&c->parts[k]->attrs, name);
        if (a) return FertAttr_str(a);
    }
    return NULL;
}

static void ctl_pos(const FertControl *c, int32_t p[2])   /* 0x524b90 */
{
    p[0] = p[1] = 0;
    FertAttr *a = FertAttrList_find(&c->attrs, "pos");
    if (a) FertAttr_point(a, p);
}

/* The control's colour (+0x120, 0x5288c0: its color, white without one). */
static uint32_t ctl_colour(const FertControl *c)
{
    FertAttr *a = FertAttrList_find(&c->attrs, "color");
    return a ? FertAttr_color(a) : 0xffffffffu;
}

static uint32_t fade_a(uint32_t c, int a) { return a == 0xff ? c : (uint32_t)(((c >> 24) * (uint32_t)a) / 0xff) << 24 | (c & 0xffffffu); }   /* 0x4d82a0 */
static uint32_t white(int a) { return (uint32_t)a << 24 | 0xffffffu; }   /* 0x4d8280 */
static uint32_t black(int a) { return (uint32_t)a << 24; }               /* 0x4d8290 */
static uint32_t red(int a) { return (uint32_t)a << 24 | 0xbc0000u; }     /* 0x4d8260 */

static HudFont *font(const char *name)   /* (0x4db960: the front end's fonts by name) */
{
    char file[64], path[300];
    snprintf(file, sizeof file, "%s.ffn", name);
    FE_ResolvePath(file, path, sizeof path);
    return FEFont_Get(path);
}

static void image(const FeImage *im, float x, float y, uint32_t colour)   /* 0x4d95a0 */
{
    FE2D_Image(im, x, y, (float)im->w, (float)im->h, colour, 0, false);
}

/* ---------------------------------------------------------------- the year box */

static struct {
    FertControl *c;
    bool hover;           /* +0x14c */
    int row, col;         /* +0x150, +0x154 */
    int year[5], set[5];  /* +0x160, +0x174: the columns' years (yy) and icon sets */
    HudFont *f8, *f7;     /* +0x158, +0x15c */
    bool logo_loaded;     /* +0x188 */
    FeImage *logo;        /* +0x18c: weeicons' "356R" */
} s_box;

/* On screen (v21 0x5075d0, v20 0x5074e0): its fonts, Univers8b and Univers7b, and weeicons' 356R. (Its
   area.pos, +0x140, isn't read again.) */
static void box_start(FertControl *c)
{
    memset(&s_box, 0, sizeof s_box);
    s_box.c = c;
    s_box.f8 = font(exe_str(0x5d7e18));
    s_box.f7 = font(exe_str(0x5d77a8));
    char path[300];
    FE_ResolvePath("weeicons.fsh", path, sizeof path);   /* ("%sweeicons.fsh" 0x5d7bf8 of 0x65b334) */
    FEShapes_Load(path);
    s_box.logo_loaded = true;
    s_box.logo = FEShapes_Named(path, exe_str(0x5dbba0));   /* 0x4d75f0 */
}

/* Its draw (v15 0x507050): the bands (the years' white, the names' and the two rows' red) and five
   columns 91 apart: the year (black, centred), a frame for the name, the name of the set's picture (the
   356 Roadster No1's the weeicons logo), and the two rows' icons (the picture, the media) in their
   frames, the one under the mouse hovered. A column goes on to the year's next set; with no icon of the
   set in any kind, to the next year's first. (The transition's offset 0x4c8330 and fade 0x4c8410
   come from draw_at: ox, oy and g_fe_fade.) */
static void box_draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (s_box.c != c) box_start(c);
    FEAccessor *ya = FEAccess_Find(exe_str(0x5dba80));
    int yy = (FEAccess_Int(ya) - 0x76c) % 100;
    int32_t p[2];
    ctl_pos(c, p);
    float x = (float)(p[0] + ox), y = (float)(p[1] + oy);
    int a = 0xff;
    FE2D_Rect(x, y, 454.0f, 16.0f, fade_a(white(a), 0x80));
    FE2D_Frame(x, y, 455.0f, 16.0f, black(a));
    FE2D_Rect(x, y + 16.0f, 454.0f, 20.0f, fade_a(red(a), 0x80));
    FE2D_Rect(x, y + 38.0f, 454.0f, 60.0f, fade_a(red(a), 0x80));
    FE2D_Rect(x, y + 100.0f, 454.0f, 60.0f, fade_a(red(a), 0x80));
    int k = 0;
    for (int col = 0, cx = 0; cx < 0x1c7; col++, cx += 0x5b, k++) {
        int none = 0;
        for (int kind = 0; kind < 3; kind++)
            if (!icon(yy, kind, k, false) && 0 < k) none++;
        if (none == 3) {
            yy++;
            k = 0;
        }
        s_box.year[col] = yy;
        s_box.set[col] = k;
        char *t = FE2D_Alloc(16);
        if (t) {
            snprintf(t, 16, "%d", yy + 0x76c);   /* ("%d" 0x5cd1b0; 0x4d9fe0) */
            FE2D_Text(s_box.f8, x + (float)(cx + 0x2d), y + 2.0f, 1, black(a), t, 1.0f, 1.0f);
        }
        FE2D_Frame(x + (float)cx, y + 16.0f, 90.0f, 20.0f, black(a));
        const FeImage *pic = icon(yy, 0, k, false);
        if (pic) {
            if (!strcasecmp(pic->name, exe_str(0x5dbb80))) {   /* "356 Roadster No1" */
                if (s_box.logo) image(s_box.logo, x + (float)(cx - s_box.logo->w / 2 + 0x2d), y + 22.0f, white(a))   /* (a whole-pixel half) */;
            } else if (pic->name[0]) {
                FE2D_Text(s_box.f7, x + (float)(cx + 0x2e), y + 21.0f, 1, white(a), pic->name, 1.0f, 1.0f);
            }
        }
        float ry = y + 38.0f;
        for (int row = 0; row < 2; row++, ry += 62.0f) {
            bool hl = s_box.hover && col == s_box.col && row == s_box.row;
            const FeImage *im = icon(yy, row, k, hl);
            if (im) image(im, x + (float)(cx + 1), ry + 1.0f, white(a));
            FE2D_Frame(x + (float)cx, ry, 90.0f, 60.0f, black(a));
        }
    }
}

/* ---------------------------------------------------------------- the picture and its icons */

static struct {
    FertControl *c;
    bool drag;            /* +0x140 */
    int32_t down[2];      /* +0x144 */
    int32_t scroll[2];    /* +0x14c */
    int32_t from[2];      /* +0x154 */
} s_pic;

/* Its draw (v15 0x5079d0): the shown media's shapes from the third, a first row of those tagged "1111"
   (0x5dbc10), then rows as long, laid out from the scroll and cut to the control. The scroll kept within
   the picture's size past the control's. */
static void pic_draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (s_pic.c != c) {   /* (v20 0x5078e0) */
        memset(&s_pic, 0, sizeof s_pic);
        s_pic.c = c;
    }
    int32_t p[2];
    ctl_pos(c, p);
    int32_t px = p[0] + ox, py = p[1] + oy;
    uint32_t colour = fade_a(ctl_colour(c), 0xff);
    const char *m = media(s_shown);
    if (!m) return;
    int count = 0;
    while (FEShapes_Image(m, count)) count++;   /* (0x4d73d0) */
    int cols = 0, rows = 0, col = 0, first_done = 0, w = 0, h = 0;
    for (int i = 2; i < count; i++) {
        FeImage *im = FEShapes_Image(m, i);
        if (col == 0) h += im->h;
        if (first_done == 0) {
            if (!strcmp(im->tag, exe_str(0x5dbc10))) {
                cols++;
                col++;
                w += im->w;
            } else {
                first_done = 1;
                rows++;
                col = 0;
            }
        } else if (cols <= ++col) {
            first_done++;
            rows++;
            col = 0;
        }
    }
    int32_t r[4];
    FertControl_rect(c, r);   /* 0x4c8240 */
    int ex = r[0] - r[2] + w, ey = r[1] - r[3] + h;
    if (ex < s_pic.scroll[0]) s_pic.scroll[0] = ex;
    if (ey < s_pic.scroll[1]) s_pic.scroll[1] = ey;
    if (s_pic.scroll[0] < 0) s_pic.scroll[0] = 0;
    if (s_pic.scroll[1] < 0) s_pic.scroll[1] = 0;
    if ((0 < ex || 0 < ey) && (c->feflags & 4)) FEPointer_Want(4);   /* (0x4e1630: the pointer to drag) */
    int ty = 0, at = 2;
    for (int row = 0; row < rows + 1; row++) {
        int tx = 0;
        FeImage *im = NULL;
        for (int k = 0; k < cols; k++) {
            im = FEShapes_Image(m, at + k);
            if (!im) break;
            FE2D_ImageClip(im, (float)(tx - s_pic.scroll[0] + px), (float)(ty - s_pic.scroll[1] + py),
                           (float)im->w, (float)im->h, r, colour, 0, false);   /* 0x4d98c0 */
            tx += im->w;
        }
        if (im) ty += im->h;
        at += cols;
    }
}

/* An icon (v21 0x507e60, v15 0x507ed0): media data.number's first two shapes, the second while the
   mouse is over it; drawn over its parts in the control's colour, a drop shadow round it, or an inset
   while hovered; the first shape's name under it (Univers7b, black). */
static void icon_draw(FertControl *c, int32_t ox, int32_t oy)
{
    FertAttr *na = NULL;
    for (int k = 0; k < c->nparts && !na; k++) na = FertAttrList_find(&c->parts[k]->attrs, "number");
    int n = na ? FertAttr_int(na) : 0;   /* (0x4c86d0 "data.number") */
    const char *m = n < s_nmedia ? media(n) : NULL;
    FeImage *plain = m ? FEShapes_Image(m, 0) : NULL, *hov = m ? FEShapes_Image(m, 1) : NULL;
    FeImage *im = (c->feflags & 4) ? hov : plain;
    if (!im) return;
    FEControl_Draw(c, ox, oy);   /* 0x4c7e30 */
    int32_t p[2];
    ctl_pos(c, p);
    float x = (float)(p[0] + ox), y = (float)(p[1] + oy);
    int a = 0xff;
    uint32_t col = ctl_colour(c);
    image(im, x, y, (col & 0xffffffu) | (uint32_t)(((int)(a * (col >> 24))) / 0xff) << 24);
    bool hover = (c->feflags & 4) && !(c->feflags & 0x30);   /* 0x4c7880 */
    if (!hover || (c->feflags & 0x10)) FEStyle_DropShadow(x, y, (float)im->w, (float)im->h, a, 4, 4);   /* 0x520780 */
    else FEStyle_Inset(x, y, (float)im->w, (float)im->h, a, 3);   /* 0x5208d0 */
    if (plain && plain->name[0]) {
        static HudFont *f7;   /* +0x14c ("%sUnivers7b.ffn" 0x5d7f90, at its making) */
        if (!f7) f7 = font(exe_str(0x5d77a8));
        FE2D_Text(f7, x, y + (float)(plain->h + 2), 0, black(a), plain->name, 1.0f, 1.0f);
    }
}

/* ---------------------------------------------------------------- the year slider */

static struct {
    FertControl *c;
    int hit, held;        /* +0x110, +0x10c (0 none, 3 the knob, 4/5 the track) */
    int32_t from[2];      /* +0x100 */
    int start;            /* +0x108: the accessor's place at the press */
} s_sl;

static FertPart *part(FertControl *c, const char *name)
{
    int i = FertControl_findPart(c, name);
    return i < 0 ? NULL : c->parts[i];
}

static int part_w(FertControl *c, const char *name)   /* (0x4e2370: its size's width) */
{
    FertPart *p = part(c, name);
    FertAttr *a = p ? FertAttrList_find(&p->attrs, "size") : NULL;
    int32_t s[2] = {0, 0};
    if (a) FertAttr_point(a, s);
    return s[0];
}

/* The knob along the track at the accessor's place (FESlider_MainFunctionality_v11 0x4d05c0: its first
   place plus place * (the track's width - the knob's) / (count - 1); horizontal). */
static void slider_place(FertControl *c)
{
    FEAccessor *a = FEAccess_Find(data_str(c, "accessor"));
    FertPart *k = part(c, "knob");
    if (!a || !k) return;
    int n = FEAccess_Count(a), d = 0;
    if (1 < n) d = FEAccess_Pos(a) * (part_w(c, "track") - part_w(c, "knob")) / (n - 1);
    FertAttr *kp = FertAttrList_find(&k->attrs, "pos");
    int32_t at[2] = {0, 0};
    if (kp) FertAttr_point(kp, at);
    if (kp) FertAttr_setPoint(kp, d, at[1]);   /* 0x527310 (the knob's first place 0, 0) */
}

/* What is under (x, y) (0x4d06f0): the knob 3, the track 4 (within its width) or 5, else 0. */
static int slider_hit(FertControl *c, int32_t x, int32_t y)
{
    int32_t r[4], cp[2];
    FertControl_rect(c, r);
    ctl_pos(c, cp);
    FertPart *k = part(c, "knob"), *t = part(c, "track");
    int32_t pr[4];
    if (k) {
        FertPart_rect(k, pr);
        if (cp[0] + pr[0] <= x && x <= cp[0] + pr[2] && cp[1] + pr[1] <= y && y <= cp[1] + pr[3]) return 3;
    }
    if (!t) return 0;
    FertPart_rect(t, pr);
    if (!(cp[0] + pr[0] <= x && x <= cp[0] + pr[2] && cp[1] + pr[1] <= y && y <= cp[1] + pr[3])) return 0;
    return cp[0] + pr[0] < x && x < cp[0] + pr[2] ? 4 : 5;
}

/* ---------------------------------------------------------------- the dispatch */

static bool is(const FertControl *c, const char *id) { return c && !strcmp(c->id, id); }

bool FEChronicle_Draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (is(c, "NFS5.ChronicleMainBoxControl")) {
        if (!(c->feflags & 0x20)) box_draw(c, ox, oy);
        return true;
    }
    if (is(c, "NFS5.ChroniclePicture")) {
        if (!(c->feflags & 0x20)) pic_draw(c, ox, oy);
        return true;
    }
    if (is(c, "NFS5.ChroniclePictureIcon")) {
        if (!(c->feflags & 0x20)) icon_draw(c, ox, oy);
        return true;
    }
    if (is(c, "NFS5.CHRONICLESLIDER")) {
        /* FEHorizontalSlider_v1 0x4cadd0 -> v11 each frame, then its parts (0x4c7e30) */
        if (s_sl.c != c) {
            memset(&s_sl, 0, sizeof s_sl);
            s_sl.c = c;
        }
        slider_place(c);
        FEControl_Draw(c, ox, oy);
        return true;
    }
    return false;
}

bool FEChronicle_Down(FertControl *c, int32_t x, int32_t y)
{
    if (is(c, "NFS5.ChroniclePicture")) {   /* v5 0x507910 */
        s_pic.from[0] = s_pic.scroll[0];
        s_pic.from[1] = s_pic.scroll[1];
        s_pic.drag = true;
        s_pic.down[0] = x;
        s_pic.down[1] = y;
        return true;
    }
    if (is(c, "NFS5.CHRONICLESLIDER") && s_sl.c == c) {
        /* FESlider_MainFunctionality_v5 0x4d0330: on the knob a drag from the mouse; on the track the knob
           to the mouse first and the drag from where it was (the place moves with the mouse's next move) */
        FEAccessor *a = FEAccess_Find(data_str(c, "accessor"));
        s_sl.hit = slider_hit(c, x, y);
        if (s_sl.hit == 3) {
            s_sl.from[0] = x;
            s_sl.from[1] = y;
            s_sl.start = FEAccess_Pos(a);
        } else if (s_sl.hit == 4 || s_sl.hit == 5) {
            FertPart *k = part(c, "knob");
            FertAttr *kp = k ? FertAttrList_find(&k->attrs, "pos") : NULL;
            int32_t at[2] = {0, 0}, r[4];
            if (kp) FertAttr_point(kp, at);
            FertControl_rect(c, r);
            s_sl.from[0] = r[0] + at[0];
            s_sl.from[1] = r[1] + at[1];
            s_sl.start = FEAccess_Pos(a);
            if (kp) FertAttr_setPoint(kp, x - r[0], at[1]);
            s_sl.hit = slider_hit(c, x, y);
        }
        s_sl.held = s_sl.hit;
        return s_sl.hit != 0;
    }
    return false;
}

void FEChronicle_Move(FertControl *c, int32_t x, int32_t y)
{
    if (is(c, "NFS5.ChronicleMainBoxControl")) {   /* v8 0x5076e0 */
        int32_t p[2];
        ctl_pos(c, p);
        s_box.col = (x - p[0]) / 0x5b;
        s_box.row = (y - p[1] - 0x26) / 0x3d;
        s_box.hover = 0 <= s_box.col && s_box.col < 5 && 0 <= s_box.row && s_box.row < 3;
    }
    if (s_pic.drag) {   /* v8 0x507960 */
        s_pic.scroll[0] = s_pic.from[0] - x + s_pic.down[0];
        s_pic.scroll[1] = s_pic.from[1] - y + s_pic.down[1];
    }
    if (s_sl.c && s_sl.held == 3) {   /* FESlider_MainFunctionality_v8 0x4d0950 */
        FEAccessor *a = FEAccess_Find(data_str(s_sl.c, "accessor"));
        int span = part_w(s_sl.c, "track") - part_w(s_sl.c, "knob");
        if (a && span) {
            int v = (x - s_sl.from[0]) * (FEAccess_Count(a) - 1) / span + s_sl.start;
            if (v > FEAccess_Count(a) - 1) v = FEAccess_Count(a) - 1;   /* (0x4bbe20 keeps it in range) */
            if (v < 0) v = 0;
            FEAccess_SetPos(a, v);
        }
    }
}

void FEChronicle_Leave(FertControl *c)
{
    if (is(c, "NFS5.ChronicleMainBoxControl")) s_box.hover = false;   /* v7 0x5076d0 */
    if (is(c, "NFS5.ChroniclePicture")) s_pic.drag = false;           /* v7 0x5079b0 */
    if (is(c, "NFS5.CHRONICLESLIDER")) s_sl.hit = 0;                  /* v7 0x4d0940 */
}

bool FEChronicle_Up(FertControl *c)
{
    s_pic.drag = false;   /* (v9 0x507950) */
    s_sl.held = 0;        /* (v9 0x4d0520) */
    if (is(c, "NFS5.ChronicleMainBoxControl")) {   /* v9 0x507780 */
        if (!s_box.hover) return false;
        int yy = s_box.year[s_box.col], k = s_box.set[s_box.col];
        /* (GotoScreen(.., 0, 1): in the screen's group, its history kept so that GOBACK returns; the port's
           stack pushes it) */
        if (s_box.row == 0 && load_media(0, yy, k)) FEApp_GotoScreen(exe_str(0x5dbbb8), true);
        else if (s_box.row == 1 && load_media(1, yy, k)) FEApp_GotoScreen(exe_str(0x5dbba8), true);
        return true;
    }
    if (is(c, "NFS5.ChroniclePictureIcon")) {   /* v9 0x508080 */
        FertAttr *na = NULL;
        for (int k = 0; k < c->nparts && !na; k++) na = FertAttrList_find(&c->parts[k]->attrs, "number");
        int n = na ? FertAttr_int(na) : 0;
        const char *m = n < s_nmedia ? media(n) : NULL;
        if (!m || !FEShapes_Image(m, 1)) return false;
        s_shown = n;
        return true;
    }
    return is(c, "NFS5.ChroniclePicture") || is(c, "NFS5.CHRONICLESLIDER");
}
