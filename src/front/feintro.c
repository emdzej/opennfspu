/* The intro movies and the movie player (see feintro.h). */
#include "front/feintro.h"
#include "render/render.h"
#include "front/fe_parts.h"
#include "front/feapp.h"
#include "real/mad.h"
#include "real/file.h"
#include "real/mem.h"
#include "real/mouse.h"
#include "real/set3d.h"
#include "real/shape.h"
#include "real/window.h"
#include "real/texture.h"
#include "support/ufile.h"
#include "game/music.h"
#include "game/options.h"
#include "snd/snd.h"
#include "app.h"
#include "exe.h"
#include "platform.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- the player */

enum { BUFFERS = 6 };   /* initrender's frame buffers (showmad asks for 6) */
/* (MC may reach past a frame's edges: the port pads each buffer; the original reads its neighbours.) */
enum { PAD_ROWS = 40 };

typedef enum { M_PREFILL, M_PLAY, M_WAIT_BUFFER, M_DRAIN, M_AUDIO, M_DONE } MovieState;

typedef struct {
    MovieState state;
    char path[128];
    uint8_t *data;              /* the file (the original's STREAM reads it in blocks) */
    size_t size, pos;
    const uint8_t *chunk;       /* local_88: the video chunk at hand, NULL at the end */
    uint32_t csize;
    int w, h, x, y;             /* 0x65e280, 0x65e3e8; 0x65e270, 0x65e274 */
    uint32_t dur;               /* 0x65e268: a frame's time, 16.16 ms */
    uint8_t *pool;
    uint8_t *time_buf[BUFFERS]; /* 0x65b9d4: the queue's buffers, then the free ones */
    int32_t time[BUFFERS];      /* 0x65b9d0: the queue's display times */
    int count;                  /* 0x65ba64: frames queued */
    const uint8_t *ref;         /* local_7c: the reference frame */
    int32_t t;                  /* iStack_8c: the next frame's time (ms) */
    uint32_t frac;              /* uStack_84 */
    int drop_e;                 /* *param_2: the 'MADe' frames skipped (set with the software path) */
    int quit;                   /* 0x65e3d4: Escape or a mouse button */
    int player, entry;          /* the sound's player; its entry (iStack_70), -1 without */
    int audio;                  /* 0x65b9cc */
    uint32_t start;             /* 0x65b9c8: the clock's start (ms) */
    int32_t sync_at, sync_err;  /* 0x65e3d8, 0x65e3ec */
} Movie;

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static bool is_video(const uint8_t *c)
{
    return !memcmp(c, "MADm", 4) || !memcmp(c, "MADe", 4) || !memcmp(c, "MADk", 4);
}

/* The next video chunk from the stream (STREAM_get, the 'SC' audio ones going to the sound's tap); NULL
   at the end. */
static const uint8_t *next_chunk(Movie *m)
{
    while (m->pos + 8 <= m->size) {
        const uint8_t *c = m->data + m->pos;
        uint32_t n = rd32(c + 4);
        if (n < 8 || n > m->size - m->pos) break;
        m->pos += n;
        if (is_video(c)) {
            m->csize = n;
            return c;
        }
    }
    m->csize = 0;
    return NULL;
}

static void advance(Movie *m)
{
    uint32_t v = m->frac + m->dur;
    m->frac = v & 0xffff;
    m->t += (int32_t)v >> 16;
}

/* The clock (GetTickCount from the start), pulled toward the sound's position when it plays: the error
   is smoothed over eight readings and a jump of more than 0x108 ms moves the start. The device's latency
   (0x56ad50: DirectSound's play-to-write distance) is 0 here, the platform pulling the mix as it plays. */
static int32_t movie_clock(Movie *m)
{
    uint32_t ticks = app_ticks_ms();
    int32_t now = (int32_t)(ticks - m->start);
    if (m->audio) {
        int32_t e[4];
        SND_StreamEntry(m->entry, e);
        int32_t played = e[1] - 0;
        if (m->sync_at < played) {
            int32_t err = m->sync_err + ((played - m->sync_err / 8) - now);
            m->sync_at = played;
            m->sync_err = err;
            if (0x108 < (err < 1 ? -err : err)) {
                m->sync_err = 0;
                m->start -= (uint32_t)(err / 8);
                now = (int32_t)(ticks - m->start);
            }
        }
    }
    return now;
}

/* Shows the last frame due (0x568530: the software path's lock, 2x blit and unlock of the display's
   window) and frees the ones before it. 1 if one was shown. 0x4dc0c0 */
static int show(Movie *m, int32_t now)
{
    if (m->count == 0 || now < m->time[0]) return 0;
    int k = 1;
    while (k < m->count && !(now < m->time[k])) k++;
    WINDOW_selectdisplay();
    MAD_blit(m->x, m->y, m->time_buf[k - 1], m->w, m->h, 3);
    WINDOW_deselect();
    for (int i = 0; i < BUFFERS - k; i++) {
        uint8_t *b = m->time_buf[i];
        m->time[i] = m->time[i + k];
        m->time_buf[i] = m->time_buf[i + k];
        m->time_buf[i + k] = b;
    }
    m->count -= k;
    return 1;
}

/* The chunk decoded into the first free buffer and queued at its time. */
static void decode(Movie *m, int32_t now, bool check)
{
    uint8_t *dest = m->time_buf[m->count];
    MAD_decode(m->chunk, m->csize, m->ref, dest);
    /* (The original looks at the clock after each row of macroblocks until one shows; the port's clock
       stands still within a frame, so once is the same.) */
    if (check) show(m, now);
    if (memcmp(m->chunk, "MADe", 4) != 0) m->ref = dest;
    int i = m->count;
    while (0 < i && !(m->time[i - 1] < m->t)) {
        m->time[i] = m->time[i - 1];
        m->time_buf[i] = m->time_buf[i - 1];
        i--;
    }
    m->count++;
    m->time[i] = m->t;
    m->time_buf[i] = dest;
}

static void keys(Movie *m)
{
    uint8_t k[16];
    int n = plat_key_presses(k, 16);
    for (int i = 0; i < n; i++)
        if (k[i] == 0x01) m->quit = 1;   /* (DIK_ESCAPE: 0x55fd10's 0x1b) */
    int32_t x, y;
    uint32_t b;
    if (MOUSE_state(&x, &y, &b) && (b & 0xf)) m->quit = 1;
}

static bool movie_open(Movie *m, const char *path)
{
    memset(m, 0, sizeof *m);
    m->player = m->entry = -1;
    snprintf(m->path, sizeof m->path, "%s", path);
    m->data = vfs_read_all(path, &m->size);
    if (!m->data) return false;
    m->chunk = next_chunk(m);
    if (!m->chunk) {
        plat_log("showmad - no MAD chunks found");
        free(m->data);
        m->data = NULL;
        return false;
    }
    m->dur = rd32(m->chunk + 0xc);
    m->w = (int16_t)(m->chunk[0x10] | m->chunk[0x11] << 8);
    m->h = (int16_t)(m->chunk[0x12] | m->chunk[0x13] << 8);
    /* (scale 3: centred at twice the size on the display, 0x5deac8 x 0x5deacc) */
    m->x = 640 / 2 - m->w;
    m->y = 480 / 2 - m->h;
    size_t frame = (size_t)m->w * m->h * 2, pad = (size_t)m->w * 2 * PAD_ROWS;
    m->pool = calloc(BUFFERS, frame + 2 * pad);
    if (!m->pool) {
        free(m->data);
        m->data = NULL;
        return false;
    }
    for (int i = 0; i < BUFFERS; i++) m->time_buf[i] = m->pool + i * (frame + 2 * pad) + pad;
    m->state = M_PREFILL;
    return true;
}

static void movie_close(Movie *m)
{
    if (0 <= m->player) SND_StreamClose(m->player);   /* 0x566b10 */
    free(m->pool);
    free(m->data);
    m->pool = m->data = NULL;
    m->state = M_DONE;
}

/* The sound from the same file (0x56c7d0: a player of one entry and 0x1e blocks fed by the tap; 0x56c7b0
   the file queued), when the file has any. */
static void movie_audio(Movie *m)
{
    bool has = false;
    for (size_t p = 0; p + 8 <= m->size;) {
        uint32_t n = rd32(m->data + p + 4);
        if (!memcmp(m->data + p, "SCHl", 4)) {
            has = true;
            break;
        }
        if (n < 8) break;
        p += n;
    }
    if (FEApp_Sound()) {
        SndParams p;
        SND_ParamsDefault(&p);
        m->player = SND_StreamOpen(&p, 1, 0x1e, SND_StreamBufferSize(1, 0x1e));
        if (has && 0 <= m->player) m->entry = SND_StreamQueueFile(m->player, 0, m->path, 0);
    }
    m->audio = -1 < m->entry;
    m->start = app_ticks_ms();
    m->sync_at = m->sync_err = 0;
    m->quit = 0;
}

/* One app frame of showmad: false once the movie is over. */
static bool movie_frame(Movie *m)
{
    if (m->state == M_PREFILL) {
        /* The first frames into all the buffers before the clock starts. */
        while (m->chunk && m->count < BUFFERS) {
            decode(m, 0, false);
            advance(m);
            m->chunk = next_chunk(m);
        }
        movie_audio(m);
        m->state = M_PLAY;
    }
    for (;;) {
        if (m->state == M_WAIT_BUFFER) {
            /* (0x4dc979: the queue full, frames shown as they fall due) */
            if (m->count == BUFFERS) show(m, movie_clock(m));
            if (m->count == BUFFERS) return true;
            int32_t now = movie_clock(m);
            decode(m, now, true);
            advance(m);
            m->chunk = next_chunk(m);
            keys(m);
            m->state = M_PLAY;
        }
        if (m->state == M_PLAY) {
            if (!m->chunk || m->quit) {
                m->state = M_DRAIN;
                continue;
            }
            /* (0x4b0d70: the app's activation and the message pump) The software path drops the frames
               that aren't references (0x5688a0 = 2). */
            m->drop_e = 1;
            int32_t now = movie_clock(m);
            if (!memcmp(m->chunk, "MADe", 4) && (m->drop_e || m->t - now < 0x85)) {
                advance(m);
                m->chunk = next_chunk(m);
                if (!m->chunk) {
                    m->state = M_DRAIN;
                    continue;
                }
                /* (the next one goes straight to the lateness test, a 'MADe' too) */
            }
            if (m->t - now < 0x43) {
                /* Too late to decode in time: frames dropped up to the next key frame. */
                do {
                    advance(m);
                    m->chunk = next_chunk(m);
                } while (m->chunk && memcmp(m->chunk, "MADk", 4) != 0);
                if (!m->chunk) {
                    m->state = M_DRAIN;
                    continue;
                }
            }
            m->state = M_WAIT_BUFFER;
            continue;
        }
        if (m->state == M_DRAIN) {
            int32_t now = movie_clock(m);
            if (now < m->t) {
                show(m, now);
                return true;
            }
            m->state = M_AUDIO;
        }
        if (m->state == M_AUDIO) {
            if (-1 < m->entry && !m->quit) {
                int32_t e[4];
                SND_StreamEntry(m->entry, e);
                keys(m);
                if (e[0] != 3 && !m->quit) return true;
            }
            movie_close(m);
            return false;
        }
        return false;
    }
}

/* ---------------------------------------------------------------- the intro */

typedef enum { I_CLEAR, I_MOVIE, I_LOADSCREEN, I_DONE } IntroStep;

static struct {
    IntroStep step;
    int movie;           /* 0 EArts-av.mad, 1 inPC-av.mad */
    int n;               /* the clears (3) and the loading screen's flips (6) left */
    Movie m;
    uint8_t *loadscrn;
    void *logo;          /* porsche.fsh, as FE_IntroLogo loads it */
} s;

static void movie_path(int i, char *out, size_t cap)
{
    snprintf(out, cap, exe_str(i == 0 ? 0x5d99bc : 0x5d998c), FE_DIR_MOVIES);
}

/* The next movie that exists, after its three cleared frames; else the loading screen. */
static void next_movie(void)
{
    char path[128];
    while (s.movie < 2) {
        if (s.movie == 1 && !s.logo) {
            char logo[128];
            snprintf(logo, sizeof logo, exe_str(0x5d999c), FE_DIR_ART);
            s.logo = FILE_loadfile(logo, 0);   /* (0x59d760: loaded and never used or freed) */
        }
        movie_path(s.movie, path, sizeof path);
        if (vfs_exists(path)) {
            s.step = I_CLEAR;
            s.n = 3;
            return;
        }
        s.movie++;
    }
    Render_SetVideoMode(g_render, 640, 480, 16, 2);   /* (again before the loading screen) */
    char ls[128];
    snprintf(ls, sizeof ls, exe_str(0x5d99ac), FE_DIR_ART);
    s.loadscrn = vfs_exists(ls) ? SHAPE_loadfile(ls, 0, 0) : NULL;   /* (0x59d9e0, 0x59d690) */
    s.step = s.loadscrn ? I_LOADSCREEN : I_DONE;
    s.n = 6;
}

void FEIntro_Start(void)
{
    Render_SetVideoMode(g_render, 640, 480, 16, 2);   /* 0x467f00 (then 0x55fde0: the keys flushed) */
    uint8_t k[16];
    while (plat_key_presses(k, 16) > 0) {}
    FEApp_Sound();
    memset(&s, 0, sizeof s);
    next_movie();
}

/* A loading screen's tiles: rows of 32, 64 and three of 128 pixels squares (the shapes in order). */
static void tiles(const uint8_t *fsh)
{
    static const int rows[5][3] = {{0, 32, 0}, {32, 64, 20}, {96, 128, 30}, {224, 128, 35}, {352, 128, 40}};
    for (int r = 0; r < 5; r++) {
        int size = rows[r][1], index = rows[r][2];
        for (int x = 0; x < 640; x += size, index++) {
            const uint8_t *shape = SHAPE_entry(fsh, index);
            if (shape) WINDOW_drawscaled(shape, x, rows[r][0], size, size);
        }
    }
}

static void loadscreen_draw(void)
{
    WINDOW_selectback();
    tiles(s.loadscrn);
    WINDOW_deselect();
    THRASH.fn.pageflip();
}

/* FE_LoadingScreen 0x4dd4b0 */
void FE_LoadingScreen(int step)
{
    static uint8_t *text, *art;   /* 0x6903f4, 0x6903f8 */
    if (g_texgraphics && g_texgraphics->textures) TexManager_end(g_texgraphics->textures);   /* 0x535df0 */
    Music_FadeOut(1);   /* 0x4ae190 */
    /* (the music stream's prebuffering waited for, 0x4ad440 against 0x4ad490: the port's stream is fed by
       the frames) */
    char path[300], found[300];
    int language = Options_Int(0x5d61cc, 0);   /* LANGUAGE (0x657c84) */
    if (!text && language != 0) {
        snprintf(path, sizeof path, exe_str(0x5d1090), FE_DIR_ART);   /* "%sloadtext.fsh" */
        if (UTIL_searchpath(path, found)) text = UTIL_loadfileabort(path, 0);
    }
    snprintf(path, sizeof path, exe_str(0x5d10c0), FE_DIR_ART, 0);   /* "%sera%d.fsh" */
    if (!art) art = UTIL_loadfileabort(path, 0);
    /* (0x4b0b40: the application's activation, race.c) */
    WINDOW_selectback();
    if (art) tiles(art);
    if (language != 0 && text) {
        const uint8_t *t = SHAPE_entry(text, language - 1);
        if (t) {
            int16_t w = (int16_t)(t[4] | t[5] << 8), h = (int16_t)(t[6] | t[7] << 8);
            int16_t cx = (int16_t)(t[8] | t[9] << 8), cy = (int16_t)(t[10] | t[11] << 8);
            WINDOW_drawscaledkey(t, -cx, -cy, w, h);
        }
    }
    if (0 <= step) {
        union { uint32_t u; float f; } k = {exe_u32(0x5b4ac0)};   /* 12.4 pixels a step */
        WINDOW_fillrect(0x12e, 0x1d3, (int)((double)step * (double)k.f), 6, 0xffffbb00);
    }
    WINDOW_deselect();
    THRASH.fn.pageflip();
    if (8 < step) {
        MEM_purge(art);
        art = NULL;
        if (text) MEM_purge(text);
        text = NULL;
    }
}

bool FEIntro_Frame(void)
{
    switch (s.step) {
    case I_CLEAR:
        THRASH.fn.window(2);
        THRASH.fn.clearwindow();
        THRASH.fn.flushwindow();
        THRASH.fn.pageflip();
        THRASH.fn.sync(0);
        if (--s.n == 0) {
            char path[128];
            movie_path(s.movie, path, sizeof path);
            s.step = movie_open(&s.m, path) ? I_MOVIE : I_CLEAR;
            if (s.step == I_CLEAR) {
                s.movie++;
                next_movie();
            }
        }
        return true;
    case I_MOVIE:
        if (!movie_frame(&s.m)) {
            s.movie++;
            next_movie();
        }
        return true;
    case I_LOADSCREEN:
        loadscreen_draw();
        if (--s.n == 0) {
            MEM_purge(s.loadscrn);
            s.loadscrn = NULL;
            uint8_t k[16];
            while (plat_key_presses(k, 16) > 0) {}
            s.step = I_DONE;
        }
        return true;
    case I_DONE:
        return false;
    }
    return false;
}
