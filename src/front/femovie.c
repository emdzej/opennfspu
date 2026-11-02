/* The front end's movies in its screens and the movie controls (femovie.h). */
#include "front/femovie.h"
#include "front/fechronicle.h"
#include "front/fe2d.h"
#include "front/fedrift.h"
#include "front/fe_parts.h"
#include "front/feapp.h"
#include "front/feaccess.h"
#include "front/fesave.h"
#include "game/options.h"
#include "real/mad.h"
#include "real/texture.h"
#include "real/window.h"
#include "render/render.h"
#include "snd/snd.h"
#include "app.h"
#include "exe.h"
#include "pending.h"
#include "platform.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The front end's texture depth (0x6907e4: FE_SetScreen 0x4e2d40's last argument, 16). */
#define FE_TEX_DEPTH 16
/* TIMER_ticks' rate (0x5deb48). */
#define TICKS_PER_S 128

struct FEMovie {
    int box_w, box_h;          /* +0x04, +0x08 */
    int w, h;                  /* +0x0c, +0x10: the movie's */
    int x, y;                  /* +0x14, +0x18 */
    char *path;                /* +0x1c */
    bool ended;                /* +0x20 */
    FeImage *image;            /* +0x24 */
    uint8_t *data;             /* the file: the stream of +0x2c (STREAM_create 2, 0x20000 bytes) */
    size_t size, pos;
    const uint8_t *chunk;      /* +0x34: the video chunk got */
    uint32_t csize;
    uint8_t *cur, *ref;        /* +0x38, +0x3c: the frame shown and the one before */
    Window *win;               /* +0x40 */
    bool paused;               /* +0x44 */
    float frametime;           /* +0x48: ms */
    float time;                /* +0x4c: ms */
    int frame;                 /* +0x50 */
    uint32_t last;             /* +0x54: the ticks at the last update */
    bool shown;                /* +0x58 */
    bool failed;               /* +0x59: no file */
    int player;                /* +0x60: the sound's player (0x56c7d0) */
    int32_t entry;             /* +0x64 */
    int vol;                   /* +0x6c */
    bool started;              /* +0x74 */
    bool audio;                /* +0x75 */
    bool counted;              /* (in g_fe_movies) */
};

int g_fe_movies;   /* 0x6907a0: the movies open (the first sets up the display's overlay, 0x5683b0) */

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static uint32_t timer_ticks(void) { return (uint32_t)((uint64_t)app_ticks_ms() * TICKS_PER_S / 1000u); }

/* The stream's next video chunk ('MADm', 'MADe', 'MADk'; the others let go), NULL at its end. */
static const uint8_t *next_video(FEMovie *m)
{
    while (m->pos + 8 <= m->size) {
        const uint8_t *c = m->data + m->pos;
        uint32_t n = rd32(c + 4);
        if (n < 8 || n > m->size - m->pos) break;
        m->pos += n;
        if (!memcmp(c, "MADm", 4) || !memcmp(c, "MADe", 4) || !memcmp(c, "MADk", 4)) {
            m->csize = n;
            return c;
        }
    }
    m->pos = m->size;
    return NULL;
}

static bool has_sound(const FEMovie *m)
{
    for (size_t p = 0; p + 8 <= m->size;) {
        uint32_t n = rd32(m->data + p + 4);
        if (!memcmp(m->data + p, "SC", 2)) return true;   /* (the tap's filter: 'SC..' chunks, 0x4353) */
        if (n < 8) break;
        p += n;
    }
    return false;
}

static void sound_open(FEMovie *m)
{
    SndParams p;
    SND_ParamsDefault(&p);   /* (0x561710) */
    m->player = SND_StreamOpen(&p, 1, 0x1e, SND_StreamBufferSize(1, 0x1e));   /* 0x56c7d0, 0x566d20 */
    m->started = false;
    m->vol = 0;
}

/* The chunk got decoded into the frame shown from the one before (0x4e1d00: the macroblocks in rows of
   16; the quality the chunk's +0x15). */
static void decode(FEMovie *m)
{
    if (!m->chunk) return;
    MAD_decode(m->chunk, m->csize, m->ref, m->cur);
}

/* From the start again (0x4e1b60): the stream (and the sound's player) anew, its first frame got; the
   clock at 0, playing. */
static void rewind_movie(FEMovie *m)
{
    if (m->failed) return;
    if (m->audio) {
        if (0 <= m->player) SND_StreamClose(m->player);   /* 0x566b10 */
        sound_open(m);
    }
    m->pos = 0;
    m->chunk = next_video(m);
    m->time = 0;
    m->frame = 0;
    m->paused = false;
}

FEMovie *FEMovie_Load(const char *path, int x, int y, int w, int h, bool audio, bool notex)
{
    FEMovie *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->box_w = w;
    m->box_h = h;
    m->x = x;
    m->y = y;
    m->player = -1;
    m->entry = -1;
    m->audio = audio;
    if (!FEApp_Sound()) m->audio = false;   /* (0x657c94: no sound) */
    m->path = strdup(path);
    if (!m->path || !vfs_exists(path)) {   /* 0x59d9e0 */
        m->failed = true;
        return m;
    }
    m->data = vfs_read_all(path, &m->size);
    if (!m->data) {
        m->failed = true;
        return m;
    }
    m->chunk = next_video(m);
    if (!m->chunk) {   /* (the original reads the missing chunk's size: a movie has one) */
        m->failed = true;
        return m;
    }
    m->w = (int16_t)(m->chunk[0x10] | m->chunk[0x11] << 8);
    m->h = (int16_t)(m->chunk[0x12] | m->chunk[0x13] << 8);
    m->frametime = (float)((double)(int32_t)rd32(m->chunk + 0xc) * (double)(1.0f / 65536.0f));   /* 0x5b6bd8 */
    m->cur = calloc((size_t)m->w * (size_t)m->h, 8);   /* (h * w * 8 bytes each, as the original) */
    m->ref = calloc((size_t)m->w * (size_t)m->h, 8);
    /* The first one open sets up the display's YUY2 overlay (0x5683b0); the port's display has none
       (the software path, as feintro.c's). */
    g_fe_movies++;
    m->counted = true;
    if (!notex) {
        m->win = WINDOW_new(256, 256, FE_TEX_DEPTH, 0);   /* 0x5356c0 */
        if (m->win) {
            WINDOW_select(m->win);   /* 0x5341f0 */
            WINDOW_clear(0);         /* (0x537c40(0): black) */
            WINDOW_deselect();       /* 0x5342c0 */
        }
    }
    m->time = 0;
    m->frame = 0;
    m->last = 0;
    if (m->cur && m->ref) decode(m);
    m->shown = false;
    if (m->audio) sound_open(m);
    TexManager *tm = g_render ? g_render->textures : NULL;
    if (!notex && m->win && tm) {
        /* 0x4d64e0: the window's shape as the texture "MOVIE" in an image of all of it (the front end's
           states, 0x65b934), then its top-left w x h; the states with bilinear filtering (0x5377b0(1)). */
        FeImage *im = calloc(1, sizeof *im);
        Texture *t = calloc(1, sizeof *t);
        if (im && t && Texture_fromshape(t, m->win->shape, tm, 0, exe_str(0x5d9cf8))) {
            TextureStates st;
            TextureStates_init(&st);
            TextureStates_set(&st, 1, 1);
            TextureStates_settile(&st, 0);
            Texture_setstates(t, &st);
            im->tex = t;
            im->u0 = 0;
            im->v0 = 0;
            im->w = im->h = 256;
            snprintf(im->name, sizeof im->name, "%s", exe_str(0x5d9cf8));
            im->u1 = (float)m->w * 0.00390625f;   /* 0x5b2640 */
            im->v1 = (float)m->h * 0.00390625f;
            st = t->states;
            TextureStates_set(&st, 1, 1);
            Texture_setstates(t, &st);
            m->image = im;
        } else {
            free(im);
            free(t);
        }
    }
    return m;
}

void FEMovie_Free(FEMovie *m)
{
    if (!m) return;
    if (0 <= m->player) SND_StreamClose(m->player);
    free(m->data);
    free(m->cur);
    free(m->ref);
    if (m->counted) --g_fe_movies;   /* (at 0: 0x568860, the overlay let go) */
    free(m->path);
    if (m->image) {
        if (m->image->tex) Texture_dtor(m->image->tex, 1);   /* 0x535f90 */
        free(m->image);
    }
    if (m->win) WINDOW_destroy(m->win);   /* 0x533cf0 */
    free(m);
}

bool FEMovie_Ended(const FEMovie *m) { return m && m->ended; }
bool FEMovie_Playing(const FEMovie *m) { return m && !m->paused; }
void FEMovie_Pause(FEMovie *m) { if (m) m->paused = true; }
void FEMovie_Resume(FEMovie *m) { if (m) m->paused = false; }

/* The movie's update into its window (slot 0, 0x4e1d80): the clock on by the ticks since the last one
   (only after a frame was shown and while playing); the frames due got, the 'MADe' ones among them
   skipped, at most one ahead without sound; at the end from the start again (and ended set); then the
   sound started once, its volume the options' music level, and the frame onto the window given (its
   own, or the victory room's screen page). */
static void update(FEMovie *m, Window *win)
{
    if (m->failed || !m->cur) return;
    /* (0x5649e0: the game's window there and active: always the port's) */
    int32_t dt = m->last ? (int32_t)(timer_ticks() - m->last) : 0;
    m->last = timer_ticks();
    int frame = m->frame, due = frame;
    if (!m->paused && m->shown) {
        double t = (double)dt * 1000.0 / TICKS_PER_S + (double)m->time;
        m->time = (float)t;
        due = (int)(t / (double)m->frametime);   /* (ftol of the sum before its rounding to a float) */
        if (!m->audio && frame + 1 < due) {
            due = frame + 1;
            m->time = (float)due * m->frametime;
        }
    }
    m->shown = false;
    if (frame < due) {
        m->frame = frame + 1;
        m->chunk = next_video(m);
        while (m->frame < due && m->chunk && !memcmp(m->chunk, "MADe", 4)) {
            m->chunk = next_video(m);
            m->frame++;
        }
        if (!m->chunk) {
            m->ended = true;
            rewind_movie(m);
        } else if (memcmp(m->chunk, "MADe", 4) != 0) {
            uint8_t *t = m->ref;   /* (a kept frame: the two swapped) */
            m->ref = m->cur;
            m->cur = t;
        }
        decode(m);
        /* (with sound but no entry the tap's chunks are let go: the port's sound reads the file itself) */
    }
    if (m->failed) return;
    if (m->audio && !m->started && !m->paused) {
        /* 0x56c7b0 when the tap has a table, else no entry */
        m->entry = 0 <= m->player && has_sound(m) ? SND_StreamQueueFile(m->player, 0, m->path, 0) : -1;
        m->started = true;
    }
    int vol = Options_Int(0x5d618c, (int)exe_u32(0x5d1740));   /* MUSICLEVEL (0x657ca0) */
    if (m->vol != vol) {
        m->vol = vol;
        if (0 <= m->player) SND_StreamSetVolume(m->player, vol);   /* 0x566c90 */
    }
    m->shown = true;
    if (win) {
        WINDOW_select(win);
        MAD_blit(0, 0, m->cur, m->w, m->h, 0);   /* 0x56a6d0 (then THRASH_flushwindow: a memory window's none) */
        WINDOW_deselect();
    }
}

void FEMovie_Update(FEMovie *m, Window *win)
{
    if (m && !m->failed) update(m, win);
}

void FEMovie_Draw(FEMovie *m, int32_t ox, int32_t oy, uint32_t colour)
{
    /* (0x4e2de0: FEMOVIESENABLED, 0x657e40, is 0 unless the keys set it) */
    if (!m || m->failed) return;
    update(m, m->win);
    if (!m->image || !m->image->tex || !m->win) return;
    Texture_reload(m->image->tex, m->win->shape);   /* 0x536280 */
    float x = (float)(m->x + ox), y = (float)(m->y + oy);
    if (m->box_h < m->h && m->w < m->box_w) {
        /* (0x4d9920: the image cut to the box's rectangle; no screen's box is that shape) */
        PENDING_NOTE(0x4d9920, "FEMovie's draw cut to its box");
    }
    FE2D_Image(m->image, x, y, (float)m->box_w, (float)m->box_h, colour, 0, false);   /* 0x4d95e0 */
}

/* ---------------------------------------------------------------- the controls */

typedef struct {
    FertControl *c;
    FEMovie *movie;      /* +0x1bc */
    int index;           /* +0x1b8: the demo loop's movie */
    char path[100];      /* +0x154 */
    char other[32];      /* +0x1c0: data.othersource (a copy: the attribute's string goes with an update) */
    bool audio;          /* +0x140 */
    int32_t pos[2];      /* +0x144 */
    int32_t size[2];     /* +0x14c */
    struct Drift *drift; /* the circle's decorations (+0x1f8 ..) */
} Box;

/* The movie circle's decorations (+0x1f8 .. +0x228): three lines across and three down over its box (its
   pos and size), three crossings, four words in two fonts. */
typedef struct Drift {
    DriftLine across[3], down[3];   /* +0x1fc, +0x204 */
    DriftCross cross[3];            /* +0x20c */
    DriftWord words[4];             /* +0x224 */
    HudFont *fonts[2];              /* +0x214: Univers7b, Univers11b */
} Drift;

static void point(const FertControl *c, const char *name, int32_t p[2])
{
    p[0] = p[1] = 0;
    FertAttr *a = FertAttrList_find(&c->attrs, name);
    if (a) FertAttr_point(a, p);
}

/* 0x4cd6c0 (from the circle's v20 and v21), once. */
static Drift *drift_new(const FertControl *c)
{
    Drift *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    int32_t pos[2], size[2];
    point(c, "pos", pos);    /* (0x5d8b2c) */
    point(c, "size", size);  /* (0x5d8b30) */
    int x0 = pos[0], y0 = pos[1], x1 = pos[0] + size[0], y1 = pos[1] + size[1];
    DriftLine_Box(x0, y0, x1, y1, true);
    for (int i = 0; i < 3; i++) DriftLine_Init(&d->across[i]);
    DriftLine_Box(x0, y0, x1, y1, false);
    for (int i = 0; i < 3; i++) DriftLine_Init(&d->down[i]);
    for (int i = 0; i < 3; i++) DriftCross_Init(&d->cross[i]);
    DriftWord_Box(x0, y0, x1, y1);
    for (int i = 0; i < 4; i++) DriftWord_Init(&d->words[i]);
    static const char *const fonts[2] = {"Univers7b.ffn", "Univers11b.ffn"};   /* ("%sUnivers7b.ffn" 0x5d7f90, 0x5d8cec) */
    for (int i = 0; i < 2; i++) {
        char path[300];
        FE_ResolvePath(fonts[i], path, sizeof path);
        d->fonts[i] = FEFont_Get(path);   /* 0x4e2fb0 */
    }
    /* (+0x21c, +0x220: Univers11B and Univers8B by name, 0x4db960, for the cheat's text, 0x4cdb30) */
    return d;
}

static void drift_free(Drift *d)   /* (FEMovieCircle_v22 0x4cd600) */
{
    if (!d) return;
    for (int i = 0; i < 3; i++) DriftCross_Free(&d->cross[i]);   /* 0x51ff90 */
    for (int i = 0; i < 2; i++) FEFont_Release(d->fonts[i]);    /* 0x4e31d0 */
    free(d);
}

/* Over the movie (0x4cd980): the lines, the crossings and the words "NEED FOR SPEED" in white, at the
   control's fade (0x4c8410: 0xff). With a cheat on (the application's +0xe0 v0xb0) they're orange and the
   cheat's own text follows (0x4cdb30): pending. */
static void drift_draw(Drift *d)
{
    int fade = 0xff;
    uint32_t line = (uint32_t)((0x4cu * fade) / 0xff) << 24 | 0xffffffu;   /* 0x4d82a0 */
    uint32_t word = (uint32_t)((0xa0u * fade) / 0xff) << 24 | 0xffffffu;
    uint32_t ring = (uint32_t)((0x30u * fade) / 0xff) << 24 | 0xffffffu;
    for (int i = 0; i < 3; i++) DriftLine_Draw(&d->across[i], line);   /* 0x51f320 */
    for (int i = 0; i < 3; i++) DriftLine_Draw(&d->down[i], line);
    for (int i = 0; i < 3; i++) DriftCross_Draw(&d->cross[i], ring, d->across, 3, d->down, 3);   /* 0x51ffc0 */
    for (int i = 0; i < 4; i++) DriftWord_Draw(&d->words[i], exe_str(0x5d8d00), word, d->fonts, 2);   /* "NEED FOR SPEED", 0x520160 */
}

static Box s_boxes[4];

static const char *data_str(const FertControl *c, const char *name)
{
    for (int k = 0; k < c->nparts; k++) {
        FertAttr *a = FertAttrList_find(&c->parts[k]->attrs, name);
        if (a) return FertAttr_str(a);
    }
    return NULL;
}

static int data_int(const FertControl *c, const char *name)
{
    for (int k = 0; k < c->nparts; k++) {
        FertAttr *a = FertAttrList_find(&c->parts[k]->attrs, name);
        if (a) return a->type == FATTR_INT ? FertAttr_int(a) : atoi(FertAttr_str(a));
    }
    return 0;
}

static int era_year(int32_t era) { return era == 0x209 ? 1970 : era == 0x20a ? 1989 : era == 0x20b ? 2000 : 0; }   /* 0x501f40 */

/* The demo loop's movie by its index (0x4ccad0): the index back to 0 past the eras reached (1 once the
   career was started, PlayerInfo's 0x691dd5; 2, 3, 4 from the classic, golden and modern eras' years);
   0 is ch6, 1 .. 4 the eras' er1pc-av .. er4pc-av. */
static void demo_name(Box *b)
{
    Save *sv = FESave_Current();
    int top = sv && sv->flags[0] ? 1 : 0;   /* 0x500b60 */
    int year = FEData_CareerYear();         /* 0x502870 */
    if (era_year(0x209) <= year) top = 2;
    if (era_year(0x20a) <= year) top = 3;
    if (era_year(0x20b) <= year) top = 4;
    if (top < b->index) b->index = 0;
    static const uint32_t names[5] = {0x5d8b74, 0x5d8b9c, 0x5d8b90, 0x5d8b84, 0x5d8b78};
    snprintf(b->path, sizeof b->path, "%s%s.mad", FE_DIR_MOVIES, exe_str(names[b->index]));   /* "%s%s.mad" */
}

/* Its attributes (FEMovieBox_v20 0x4cca20): data.audio, size, pos. */
static void box_init(Box *b, FertControl *c)
{
    memset(b, 0, sizeof *b);
    b->c = c;
    b->audio = data_int(c, "audio") == 1;
    FertAttr *a = FertAttrList_find(&c->attrs, "size");
    if (a) FertAttr_point(a, b->size);
    a = FertAttrList_find(&c->attrs, "pos");
    if (a) FertAttr_point(a, b->pos);
}

static void box_load(Box *b)
{
    FEMovie_Free(b->movie);
    b->movie = FEMovie_Load(b->path, b->pos[0], b->pos[1], b->size[0], b->size[1], b->audio, false);
}

/* On screen (FEMovieBox_v21 0x4ccbd0): the movie by data.othersource ("demoloop" the demo loop from its
   first, "careerfirst" er1pc-av, "chronicle" the chronicle's chosen file, "chroniclemain" the chronicle's
   era's movie, others er1pc-av), loaded. */
static void box_start(Box *b)
{
    const char *src = data_str(b->c, "othersource");   /* 0x4c8700 */
    snprintf(b->other, sizeof b->other, "%s", src ? src : "");
    const char *o = b->other;
    char name[100];
    snprintf(name, sizeof name, "%s", exe_str(0x5d8b9c));
    if (!strcmp(o, exe_str(0x5d8bd0))) {
        b->index = 0;
        demo_name(b);
    } else {
        if (!strcmp(o, exe_str(0x5d8bb8))) {
            /* (0x699c98, the chronicle's chosen movie, as it is: no path of its own) */
            snprintf(b->path, sizeof b->path, "%s", FEChronicle_Movie());
            box_load(b);
            return;
        }
        if (!strcmp(o, exe_str(0x5d8ba8))) snprintf(name, sizeof name, "%s", FEChronicle_EraMovie());
        snprintf(b->path, sizeof b->path, "%s%s.mad", FE_DIR_MOVIES, name);
    }
    box_load(b);
}

/* Each frame (FEMovieBox_v1 0x4cce50): the demo loop's next movie at its end; careerfirst's tournaments
   at its end or Escape (the key's state, 0x55fc60(6)). */
static void box_update(Box *b)
{
    if (!b->other[0]) return;
    if (!strcmp(b->other, exe_str(0x5d8bd0)) && FEMovie_Ended(b->movie)) {
        b->index++;
        demo_name(b);
        box_load(b);
    }
    if (!strcmp(b->other, exe_str(0x5d8ba8))) {
        /* chroniclemain's: the era's movie changed, the box made again (v21) */
        char path[100];
        snprintf(path, sizeof path, "%s%s.mad", FE_DIR_MOVIES, FEChronicle_EraMovie());
        if (strcmp(path, b->path)) box_start(b);
    } else if (!strcmp(b->other, exe_str(0x5d8bc4))) {
        uint8_t keys[KEY_COUNT] = {0};
        bool esc = plat_keys(keys) && keys[0x01];
        if (FEMovie_Playing(b->movie) && (FEMovie_Ended(b->movie) || esc)) {
            FEMovie_Pause(b->movie);
            FEApp_GotoScreen(exe_str(0x5d8bdc), false);
        }
    }
}

static Box *box_of(FertControl *c)
{
    for (int i = 0; i < 4; i++)
        if (s_boxes[i].c == c) return &s_boxes[i];
    for (int i = 0; i < 4; i++)
        if (!s_boxes[i].c) {
            box_init(&s_boxes[i], c);
            box_start(&s_boxes[i]);
            return &s_boxes[i];
        }
    return NULL;
}

void FEMovieBox_Update(void)
{
    for (int i = 0; i < 4; i++)
        if (s_boxes[i].c) box_update(&s_boxes[i]);
}

void FEMovieBox_Drop(bool (*keep)(const FertControl *c))
{
    for (int i = 0; i < 4; i++)
        if (s_boxes[i].c && !keep(s_boxes[i].c)) {
            FEMovie_Free(s_boxes[i].movie);   /* FEMovieBox_v22 0x4cce20 */
            drift_free(s_boxes[i].drift);
            memset(&s_boxes[i], 0, sizeof s_boxes[i]);
        }
}

/* The movie in the control's colour (+0x120: its color), its alpha by the control's fade (0x4c8410,
   0xff). FEMovieBox_v15 0x4cd080 */
static void box_draw(Box *b, int32_t ox, int32_t oy)
{
    FertAttr *ca = FertAttrList_find(&b->c->attrs, "color");
    uint32_t col = ca ? FertAttr_color(ca) : 0xffffffffu;
    int fade = 0xff;
    col = (col & 0xffffffu) | (uint32_t)((int)(fade * (col >> 24)) / 0xff) << 24;
    FEMovie_Draw(b->movie, ox, oy, col);
}

bool FEMovieBox_Draw(FertControl *c, int32_t ox, int32_t oy)
{
    bool circle = !strcmp(c->id, "NFS5.MOVIECIRCLE");
    if (!circle && strcmp(c->id, "NFS5.MOVIEBOX")) return false;
    if (c->feflags & 0x20) return true;
    Box *b = box_of(c);
    if (!b) return true;
    if (!circle) {
        box_draw(b, ox, oy);
        return true;
    }
    /* FEMovieCircle_v15 0x4cd520: its stencil (the black rectangle stencilshape1 and the oval's eight
       pieces stencilshape2 .. 9) at its pos two depth steps nearer, then the movie drawn over what is
       there (the z test), the queue flushed; then its lines and "NEED FOR SPEED" (0x4cd980).
       (The screen's transition offset, 0x4c8330, isn't modelled: none.) */
    int32_t px = ox + b->pos[0], py = oy + b->pos[1];
    FE2D_DepthPushStep();   /* 0x4da4c0 */
    char name[16];
    for (int k = 1; k <= 9; k++) {
        snprintf(name, sizeof name, "stencilshape%d", k);
        int i = FertControl_findPart(c, name);
        if (0 <= i) FEPart_Draw(c->parts[i], px, py);
    }
    FE2D_OverlayBegin();    /* 0x4da570 */
    box_draw(b, ox, oy);
    FE2D_Flush();           /* 0x4da0a0 */
    if (!b->drift) b->drift = drift_new(c);
    if (b->drift) drift_draw(b->drift);
    FE2D_OverlayEnd();      /* 0x4da5b0 */
    return true;
}
