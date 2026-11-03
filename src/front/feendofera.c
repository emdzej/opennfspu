/* The era's end: see feendofera.h. */
#include "front/feendofera.h"
#include "app.h"
#include "exe.h"
#include "front/fe2d.h"
#include "front/fe_parts.h"
#include "front/feaccess.h"
#include "front/feapp.h"
#include "front/femovie.h"
#include "game/music.h"
#include "platform.h"
#include "real/random.h"
#include "render/render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double exe_f64(uint32_t va)   /* (a qword constant) */
{
    uint64_t u = (uint64_t)exe_u32(va) | (uint64_t)exe_u32(va + 4) << 32;
    double d;
    memcpy(&d, &u, 8);
    return d;
}

void Audio_Say(int idx);                     /* game/audio.h (whose car.h clashes with the front end's) */
bool Audio_SpeechBank(const char *entry);
void Audio_SpeechFree(void);

static float K(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

enum { PICS = 64 };

/* A line of the roll (0x2c bytes). */
typedef struct {
    bool number;      /* +0x00: a year in numbers.fsh's digits (+0x04) */
    int value;        /* +0x04 */
    char text[96];    /* +0x08 */
    int alpha;        /* +0x14 */
    int x, y;         /* +0x18 */
    float scale;      /* +0x20 */
    uint32_t colour;  /* +0x24 */
    bool shadow;      /* +0x28: a black copy 4 pixels down and right first */
} Line;

typedef struct {
    FeImage *im[PICS];
    int n;
} Pics;

static struct {
    bool on;
    HudFont *font;                  /* +0x284: Univers20b */
    uint32_t t_line, t_blink, t_start;   /* +0x288, +0x28c, +0x290 */
    int cur, blink;                 /* +0x294, +0x298 */
    float hw, hh;                   /* +0x29c, +0x2a0: half the screen */
    Line *lines;                    /* +0x2a4 */
    int n, cap;                     /* +0x2a8 */
    int next;                       /* +0x2ac: the line the new era's name begins at */
    Pics pics[4];                   /* +0x2b0 .. +0x2bc: the old era's second and first sets, the new era's */
    Pics digits;                    /* +0x2c4: numbers.fsh */
    FeImage *curves[3];             /* +0x2d4 .. +0x2dc: curves 0, curves2 0 and 1 */
    FeImage *movieline;             /* +0x2e0 */
    bool spoke, done;               /* +0x2e4, +0x2e5 */
    FEMovie *movie;                 /* +0x2e8 */
    FeImage *oval[3];               /* +0x2ec, +0x2f0, +0x2f4: MovieOval 0, 1 and 4 */
    uint32_t t_movie, t_last;       /* +0x2f8, +0x2fc */
    int old_i, new_i;               /* +0x300, +0x304: the eras 0 .. 2 */
    bool left;                      /* +0x308 */
    int film;                       /* +0x30c */
    bool escape;                    /* +0x314 */
    int word;                       /* 0x691d6c: a line's time (a tenth of 128 ticks, 0x4fc960) */
} s;

static uint32_t ticks(void) { return (uint32_t)((uint64_t)app_ticks_ms() * 128u / 1000u); }   /* TIMER_ticks */

static int era_index(int era) { return era == 0x20a ? 1 : era == 0x20b ? 2 : 0; }

static void push(const Line *l)
{
    if (s.n == s.cap) {
        int cap = s.cap ? s.cap * 2 : 256;
        Line *nl = realloc(s.lines, sizeof *nl * (size_t)cap);
        if (!nl) return;
        s.lines = nl;
        s.cap = cap;
    }
    s.lines[s.n++] = *l;
}

static Line line_of(const char *text, int x, int y, float scale, int alpha, bool shadow)
{
    Line l;
    memset(&l, 0, sizeof l);
    snprintf(l.text, sizeof l.text, "%s", text ? text : "");
    l.x = x;
    l.y = y;
    l.scale = scale;
    l.alpha = alpha;
    l.colour = 0xffffffu;
    l.shadow = shadow;
    return l;
}

/* A heading (0x4ff7b0 -> 0x4ff530): in the middle, swelling 1 .. 2.5 and back, then fading out. */
static void heading(const char *text)
{
    static const float scale[7] = {1.0f, 1.5f, 2.0f, 2.5f, 2.0f, 1.5f, 1.0f};
    static const int fade[4] = {0xcd, 0xaf, 100, 0x19};
    for (int i = 0; i < 7; i++) {
        Line l = line_of(text, 0x140, 0xf0, scale[i], 0xff, false);
        push(&l);
    }
    for (int i = 0; i < 4; i++) {
        Line l = line_of(text, 0x140, 0xf0, 1.0f, fade[i], false);
        push(&l);
    }
}

/* An item at the screen's middle (0x4ff9c0). */
static void item(const char *text)
{
    Line l = line_of(text, (int)s.hw, (int)s.hh, 1.0f, 0xff, false);
    push(&l);
}

/* A year in digits at a random place (0x4ff360: x 0xb2 .., y 0x80 ..). */
static void number(int value)
{
    char t[16];
    snprintf(t, sizeof t, "%d", value);
    Line l = line_of(t, 0, 0, 1.0f, 0xff, false);
    l.number = true;
    l.value = value;
    l.x = (int)(REAL_rand() % 0x11cu) + 0xb2;
    l.y = (int)(REAL_rand() % 0xe0u) + 0x80;
    push(&l);
}

static void pics_load(Pics *p, const char *name)
{
    char file[64], path[300];
    snprintf(file, sizeof file, "%s.fsh", name);
    FE_ResolvePath(file, path, sizeof path);
    FEShapes_Load(path);   /* FE_ValidateShapes 0x4d67f0 */
    p->n = 0;
    while (p->n < PICS && (p->im[p->n] = FEShapes_Image(path, p->n))) p->n++;   /* 0x4d73d0, 0x4d74e0 */
}

static FeImage *pic(const char *name, int i)
{
    char file[64], path[300];
    snprintf(file, sizeof file, "%s.fsh", name);
    FE_ResolvePath(file, path, sizeof path);
    FEShapes_Load(path);
    return FEShapes_Image(path, i);
}

void FEEndOfEra_Free(void)
{
    if (!s.on) return;
    if (s.movie) FEMovie_Free(s.movie);
    if (s.font) FEFont_Release(s.font);
    Audio_SpeechFree();
    free(s.lines);
    memset(&s, 0, sizeof s);
}

bool FEEndOfEra_On(void) { return s.on; }

void FEEndOfEra_Open(void)
{
    FEEndOfEra_Free();
    s.on = true;
    s.word = (int)(K(0x5b8240) * K(0x5b3880));
    Music_Play(exe_str(0x5db734), 2);   /* "vic*" */
    {
        char path[300];
        FE_ResolvePath("Univers20b.ffn", path, sizeof path);   /* "%sUnivers20b.ffn" 0x5db720 */
        s.font = FEFont_Get(path);
    }
    s.hw = (float)g_display.width * K(0x5b23f0);
    s.hh = (float)g_display.height * K(0x5b23f0);
    int era = FEData_Era();
    int old = era == 0x20b ? 0x20a : 0x209;   /* 0x502890 */
    bool all = FEData_EraDone(era);           /* (the evolution done) */
    s.old_i = era_index(old);
    s.new_i = era_index(era);
    /* the narrator: by the last era done (0x503db0) */
    int last = FEData_RaceEra();
    static const uint32_t banks[3] = {0x5db6d8, 0x5db6f0, 0x5db708};
    Audio_SpeechBank(exe_str(banks[last]));   /* 0x4fc980 */
    if (!all) {
        /* the years from the old era's first to the new era's last (0x503f90, 0x503fc0) */
        static const int first[3] = {0x79e, 0x7b2, 0x7c5}, end[3] = {0x7b2, 0x7c5, 2000};
        int y0 = first[s.old_i], y1 = end[s.new_i], mid = first[s.new_i];
        for (int y = y0; y <= y1; y++) {
            if (y == y0) heading(FEStrings_Get((int)exe_u32(0x5b8244 + 4u * (uint32_t)s.old_i)));
            if (y == mid) {
                s.next = s.n;
                heading(FEStrings_Get((int)exe_u32(0x5b8244 + 4u * (uint32_t)s.new_i)));
            }
            number(y);
            const uint8_t *c;
            for (int i = 0; (c = FEData_CarAt(i)); i++) {
                int16_t year;
                memcpy(&year, c + 0x1f8, 2);
                if (year == y) item((const char *)c + 2);
            }
        }
    } else {
        /* each era's name, then its tournaments (but those of kind +0xe4) with the years of the table
           0x5b8670 counted on across them */
        int k = 0;
        for (int e = 0; e < 3; e++) {
            heading(FEStrings_Get((int)exe_u32(0x5b8244 + 4u * (uint32_t)e)));
            const uint8_t *t;
            for (int i = 0; (t = FEData_EraTournament(0x209 + e, i)); i++) {
                int16_t kind;
                memcpy(&kind, t + 0xe4, 2);
                if (kind != 0) continue;
                int32_t name;
                memcpy(&name, t + 0x24, 4);
                item(FEStrings_Get(name));
                number((int32_t)exe_u32(0x5b8670 + 4u * (uint32_t)k++));   /* 0x5040a0 */
            }
        }
    }
    /* the closing word letter by letter, then swelling and shrinking, fading, flashing out (0x4ffb70) */
    const char *w = FEStrings_Get(0x46f4);
    if (!w) w = "";
    char pre[96];
    int len = (int)strlen(w);
    for (int i = 0; i < len && i < (int)sizeof pre - 1; i++) {
        memcpy(pre, w, (size_t)i + 1);
        pre[i + 1] = 0;
        Line l = line_of(pre, 0x140, 0xf0, 2.0f, 0xff, true);
        push(&l);
    }
    static const uint32_t scb[10] = {0x3fe66666, 0x3fcccccd, 0x3fb33333, 0x3f99999a, 0x3f800000,
                                     0x3f99999a, 0x3fb33333, 0x3fcccccd, 0x3fe66666, 0x40000000};
    for (int i = 0; i < 10; i++) {
        float f;
        memcpy(&f, &scb[i], 4);
        Line l = line_of(w, 0x140, 0xf0, f, 0xff, true);
        push(&l);
    }
    static const int fades[6] = {200, 0x96, 100, 0x32, 0x19, 10};
    for (int i = 0; i < 6; i++) {
        Line l = line_of(w, 0x140, 0xf0, 2.0f, fades[i], true);
        push(&l);
    }
    int a = 0xff;
    for (int i = 0; i < 10; i++) {
        a -= 0x17;
        Line l = line_of(w, 0x140, 0xf0, 2.0f, a, true);
        push(&l);
        l.alpha = 0;
        push(&l);
    }
    /* the pictures: the movie oval's pieces, the eras' sets, the digits, the curves, the film, its line */
    s.oval[0] = pic(exe_str(0x5db6cc), 0);   /* "MovieOval" */
    s.oval[1] = pic(exe_str(0x5db6cc), 1);
    s.oval[2] = pic(exe_str(0x5db6cc), 4);
    const char *set1[3], *set2[3];
    for (int i = 0; i < 3; i++) {
        set1[i] = exe_str(exe_u32(0x5db60c + 4u * (uint32_t)i));   /* C1, G1, M1 */
        set2[i] = exe_str(exe_u32(0x5db618 + 4u * (uint32_t)i));   /* C2, G2, M2 */
    }
    pics_load(&s.pics[1], set1[s.old_i]);
    pics_load(&s.pics[0], set2[s.old_i]);
    pics_load(&s.pics[3], set1[s.new_i]);
    pics_load(&s.pics[2], set2[s.new_i]);
    pics_load(&s.digits, exe_str(0x5db6c4));   /* "numbers" */
    s.curves[0] = pic(exe_str(0x5db6bc), 0);   /* "curves" */
    s.curves[1] = pic(exe_str(0x5db6b4), 0);   /* "curves2" */
    s.curves[2] = pic(exe_str(0x5db6b4), 1);
    s.film = last;   /* (0x503db0 again) */
    char mad[160];
    snprintf(mad, sizeof mad, "%s%s.mad", FE_DIR_MOVIES, exe_str(exe_u32(0x5db624 + 4u * (uint32_t)s.film)));
    s.movie = FEMovie_Load(mad, 0, 0, (int)exe_u32(0x5b8238), (int)exe_u32(0x5b823c), true, false);
    FEMovie_Pause(s.movie);   /* (started at the roll's end, 0x4e1b20) */
    s.movieline = pic(exe_str(0x5db6a8), 0);   /* "movieline" */
}

void FEEndOfEra_Update(void)
{
    if (!s.on) return;
    /* _v4: Escape (its key 0x1b) */
    uint8_t keys[KEY_COUNT] = {0};
    if (plat_keys(keys) && keys[0x01]) s.escape = true;
    /* _v1: with the roll done (or Escape), the film's time over goes back, once */
    if ((s.done || s.escape) && !s.left) {
        int32_t span = (int32_t)exe_u32(0x5b8228 + 4u * (uint32_t)s.film);
        if (s.escape || span < (int32_t)(ticks() - s.t_movie)) {
            s.left = true;
            FEApp_Back();   /* 0x4d1e40(0) */
        }
    }
}

static void image(const FeImage *im, float x, float y, uint32_t colour, uint32_t flags)   /* 0x4d95a0 */
{
    if (im) FE2D_Image(im, x, y, (float)im->w, (float)im->h, colour, flags, false);
}

static void pics_draw(const Pics *p, uint32_t colour)
{
    for (int i = 0; i < p->n; i++) image(p->im[i], (float)p->im[i]->cx, (float)p->im[i]->cy, colour, 0);
}

static int ramp(int v, int lo, int hi)
{
    if (v < lo) return 0;
    if (hi < v || hi == lo) return 0xff;
    return ((v - lo) * 0xff) / (hi - lo);
}

/* A year in the digits (0x4ff240): 19 and its two, or 2000; centred on x. */
static void digits(int value, int alpha, int x, int y)
{
    int d[4] = {1, 9, 0, 0};
    if (value == 2000) {
        d[0] = 2;
        d[1] = 0;
    } else {
        d[2] = (value - 0x76c) / 10;
        d[3] = (value - 0x76c) % 10;
    }
    int w = 0;
    for (int i = 0; i < 4; i++)
        if (0 <= d[i] && d[i] < s.digits.n) w += s.digits.im[d[i]]->w;
    for (int i = 0; i < 4; i++) {
        if (d[i] < 0 || s.digits.n <= d[i]) continue;
        FeImage *im = s.digits.im[d[i]];
        image(im, (float)(x - w / 2), (float)y, (uint32_t)alpha << 24 | 0xffffffu, 0);
        x += im->w;
    }
}

/* The movie oval's stencil (0x4ff050) and its frame lines (0x4ff1a0): four quarters each, flipped. */
static void oval_stencil(void)
{
    static const int16_t a[4][2] = {{0xc0, 0x88}, {0x140, 0x88}, {0xc0, 0xfc}, {0x140, 0xfc}};
    static const int16_t b[4][2] = {{0xa0, 0x48}, {0x140, 0x48}, {0xa0, 0x170}, {0x140, 0x170}};
    static const int16_t c[4][2] = {{0x71, 0x88}, {0x1c0, 0x88}, {0x71, 0xfc}, {0x1c0, 0xfc}};
    for (int i = 0; i < 4; i++) {
        image(s.oval[2], a[i][0], a[i][1], 0xffffffffu, 0);
        image(s.oval[0], b[i][0], b[i][1], 0xffffffffu, (uint32_t)i);
        image(s.oval[1], c[i][0], c[i][1], 0xffffffffu, (uint32_t)i);
    }
}

static void oval_lines(void)
{
    static const int16_t p[4][2] = {{0x70, 0x4c}, {0x140, 0x4c}, {0x70, 0xfc}, {0x140, 0xfc}};
    for (int i = 0; i < 4; i++) image(s.movieline, p[i][0], p[i][1], 0xffffffffu, (uint32_t)i);
}

void FEEndOfEra_Draw(void)
{
    if (!s.on) return;
    uint32_t now = ticks();
    if (!s.done) {
        if (!s.t_start) s.t_start = now;
        if (!s.t_line) s.t_line = now;
        if (!s.t_blink) s.t_blink = now;
        for (int i = 0; i < s.cur && i < s.n; i++) s.lines[i].alpha = 0;
        if ((int32_t)(now - s.t_line) >= s.word) {
            s.t_last = now;
            if (s.n <= ++s.cur) {
                s.cur = 0;
                s.done = true;
            }
            s.t_line = now;
        }
        if (0xb < (int32_t)(now - s.t_blink)) {
            if (1 < ++s.blink) s.blink = 0;
            s.t_blink = now;
        }
        /* the pictures: the old era's first set under its second fading in (10 .. 30 % of the way to the
           new era), then the new era's first set under the old second fading out (90 .. 110 %), then
           under the new era's second fading in */
        float cur = (float)s.cur, nx = (float)s.next;
        if (cur < nx * K(0x5b8258)) {
            int t = ramp(s.cur, (int)(nx * K(0x5b8250)), (int)(nx * K(0x5b8254)));
            pics_draw(&s.pics[1], 0xffffffffu);
            FE2D_DepthStep();   /* 0x4da400 */
            pics_draw(&s.pics[0], (uint32_t)t << 24 | 0xffffffu);
        } else if (nx * K(0x5b8258) < cur && cur < nx * K(0x5b825c)) {
            int lo = (int)(nx * K(0x5b8258)), hi = (int)(nx * K(0x5b825c));
            pics_draw(&s.pics[3], 0xffffffffu);
            FE2D_DepthStep();
            pics_draw(&s.pics[0], (uint32_t)(0xff - ((s.cur - lo) * 0xff) / (hi - lo)) << 24 | 0xffffffu);
        } else {
            int lo = (int)(nx * K(0x5b825c)), hi = (int)((float)(s.n - s.next) * K(0x5b8264) + nx);
            int t = ramp(s.cur, lo, hi);
            pics_draw(&s.pics[3], 0xffffffffu);
            FE2D_DepthStep();
            pics_draw(&s.pics[2], (uint32_t)t << 24 | 0xffffffu);
        }
        /* the curves and the bars, gold, by the blink's half of their tables (0x5b8268: x, y, w, h, the
           curve, its flip; 0x5b8418: left, top, right, bottom) */
        for (int i = 0; i < 12; i++) {
            if (!exe_u32(0x5b8388 + 4u * (uint32_t)(i + s.blink * 12))) continue;
            uint32_t r = 0x5b8268 + 24u * (uint32_t)i;
            int k = (int32_t)exe_u32(r + 16);
            if (0 <= k && k < 3) image(s.curves[k], (float)(int32_t)exe_u32(r), (float)(int32_t)exe_u32(r + 4), 0xffffb400u, exe_u32(r + 20));
        }
        for (int i = 0; i < 8; i++) {
            if (!exe_u32(0x5b8498 + 4u * (uint32_t)(i + s.blink * 8))) continue;
            uint32_t r = 0x5b8418 + 16u * (uint32_t)i;
            int x0 = (int32_t)exe_u32(r), y0 = (int32_t)exe_u32(r + 4);
            int w = (int32_t)exe_u32(r + 8) - x0, h = (int32_t)exe_u32(r + 12) - y0;
            FE2D_Rect((float)x0, (float)y0, (float)(w < 1 ? 1 : w), (float)(h < 1 ? 1 : h), 0xffffb400u);
        }
    } else {
        pics_draw(&s.pics[2], 0xffffffffu);
    }
    /* the last ten lines on: the film in the oval, faded in over 0x280 ticks and out at its end */
    if (s.n - 10 <= s.cur || s.done) {
        if (!s.t_movie) {
            if (s.movie) FEMovie_Resume(s.movie);   /* 0x4e1b20 */
            s.t_movie = ticks();
        }
        FE2D_DepthPushStep();   /* 0x4da4c0 */
        oval_stencil();
        FE2D_OverlayBegin();    /* 0x4da570 */
        int32_t t = (int32_t)(ticks() - s.t_movie), a;
        int32_t span = (int32_t)exe_u32(0x5b8228 + 4u * (uint32_t)s.film);
        if (t < 0x281) a = (t * 0xff) / 0x280;
        else if (span - 0x280 < t) a = span < t ? 0 : (((span - t) - 0x280) * 0xff) / 0x280 + 0xff;
        else a = 0xff;
        int mx = 0x140 - (int32_t)exe_u32(0x5b8238) / 2, my = 0xfc - (int32_t)exe_u32(0x5b823c) / 2;
        if (s.movie) FEMovie_Draw(s.movie, mx, my, (uint32_t)a << 24 | 0xffffffu);
        FE2D_OverlayEnd();      /* 0x4da5b0 */
        oval_lines();
    }
    /* the current line: a year's digits, or its text (its shadow first), the scaled ones flushed alone */
    if (0 <= s.cur && s.cur < s.n) {
        Line *l = &s.lines[s.cur];
        int a = l->alpha;
        if (0 < a) {
            if (l->number && l->value) {
                digits(l->value, a, l->x, l->y);
            } else if (s.font) {
                bool scaled = exe_f64(0x5b2750) < l->scale;
                if (!l->number && l->shadow) {
                    if (scaled) FE2D_Flush();
                    FE2D_Text(s.font, (float)(l->x + 4), (float)(l->y + 4), 1, (uint32_t)a << 24, l->text, l->scale, 1.0f);
                    if (scaled) FE2D_Flush();
                }
                if (scaled) FE2D_Flush();
                FE2D_Text(s.font, (float)l->x, (float)l->y, 1, l->colour | (uint32_t)a << 24, l->text, l->scale, 1.0f);
                if (scaled) FE2D_Flush();
            }
        }
    }
    /* the announcer a second in (0x4a6040: line 4 on, by the era) */
    if (!s.spoke && 0x80 < (int32_t)(now - s.t_start)) {
        int era = FEData_Era(), n = 0;
        if (era == 0x20b) n = 2 - (era != 0x20a);   /* (0x502890 of 0x20b is 0x20a: always 1) */
        Audio_Say(n + 4);
        s.spoke = true;
    }
}
