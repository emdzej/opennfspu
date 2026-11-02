/* The game's music ([audio] 0x4ad2a0 .. 0x4ae780): GameData\Music\zzzymus.viv's songs (EA streams named
   "<kind><n>-<title>-<artist>": game, menu, win, lose, vic) played one after another through an SND
   stream player, by a playlist of entry numbers, in order or shuffled; a song changed by fading the one
   playing out first. A task at 25 Hz (0x4ad790) moves it on. The globals are 0x1b4 bytes at 0x656ab4
   (fields by offset below). */
#include "game/music.h"
#include "exe.h"
#include "real/locatbig.h"
#include "real/random.h"
#include "snd/snd.h"
#include "vfs.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int32_t volume;           /* +0x00: the music's volume, 0 off */
    int32_t nsongs;           /* +0x04: the archive's entries */
    int32_t nlist;            /* +0x08: the playlist's length */
    int32_t cur;              /* +0x0c: the playlist's song playing, -1 none */
    int32_t f10;              /* +0x10: a song was asked for */
    int32_t state;            /* +0x14: 0 playing (on to the next at its end), 1 a change waits for the
                                 fade, 2 just queued (its volume to set), -1 stopped */
    int32_t f18;              /* +0x18 */
    int32_t fade;             /* +0x1c: the fade-out's code (-5, -4, -3), 0 none */
    int32_t fadetime;         /* +0x20: ms */
    int32_t shuffle;          /* +0x24 */
    const char *song;         /* +0x28: the entry's name */
    uint8_t *buffer;          /* +0x6c */
    int32_t player;           /* +0x70 */
    int32_t queued;           /* +0x74: the stream's id */
    int32_t task;             /* +0x78 */
    int32_t open;             /* +0x7c */
    int32_t f84;              /* +0x84: the buffer's bytes / 40 */
    uint8_t *dir;             /* +0x88: the archive's directory */
    int32_t status[3];        /* +0x8c: SND_StreamStatus's */
    int32_t entry[4];         /* +0x98: SND_StreamEntry's (state, played, left, read ahead) */
    char path[0x40];          /* +0xa8 */
    int8_t list[0x80];        /* +0xe8 */
    MusicSong info;           /* +0x170: the song playing's name parts and times */
} Music;

static Music *s_music;   /* 0x656ab4 */
int32_t g_music_shuffle; /* 0x657a5c: the options' shuffle */
int32_t g_music_nplaylist = -1;      /* 0x657cb8 */
int32_t g_music_playlist[0x80];      /* 0x657cbc */

/* (GetTickCount in the shuffles: the SND library's 100 Hz ticks as milliseconds, so runs repeat) */
static uint32_t tick_count(void) { return SND_Ticks() * 10u; }

/* The song's name parts (0x4ad720): the title between the first and second '-', the artist after the
   second (up to 31 characters each kept as the original's: the title copied, the artist pointed at). */
static void name_parts(const char *name, char title[0x20], const char **artist)
{
    memset(title, 0, 0x20);
    *artist = NULL;
    int n = 0;
    bool dash = false;
    for (const char *p = name; p && *p; p++) {
        if (*p == '-') {
            if (dash) {
                *artist = p + 1;
                break;
            }
            dash = true;
            n = 0;
        } else if (n < 0x1f) {
            title[n++] = *p;
        }
        title[n] = 0;
    }
}

static void song_info(MusicSong *s, const char *name)
{
    s->name = name;
    name_parts(name, s->title, &s->artist);
}

/* The name against a pattern (0x4a5d60): the rightmost of the operators & | ! ~ ^ splits it (and, or,
   or, xor); else '*' any run, '?' any character, '#' a digit, '~' not the rest, others by case. */
static bool match(const char *s, char *pat)
{
    char *op = NULL;
    for (const char *o = exe_str(0x5d13bc); *o; o++) {
        char *p = strrchr(pat, *o);
        if (p > op) op = p;
    }
    if (op) {
        char c = *op;
        *op = 0;
        bool r;
        if (c == '&') r = match(s, pat) && match(s, op + 1);
        else if (c == '|') r = match(s, pat) || match(s, op + 1);
        else if (c == '!' || c == '~') {
            r = match(s, pat) || match(s, op + 1);
            c = '!';
        } else r = match(s, op + 1) ^ match(s, pat);
        *op = c;
        return r;
    }
    for (;; pat++, s++) {
        char c = *pat;
        if (c == '*') {
            for (;; s++) {
                if (match(s, pat + 1)) return true;
                if (!*s) return false;
            }
        }
        if (c == '?') {
            if (!*s) return false;
        } else if (c == '#') {
            if (!isdigit((unsigned char)*s)) return false;
        } else if (c == '~') {
            return !match(s, pat + 1);
        } else {
            if (tolower((unsigned char)*s) != tolower((unsigned char)c)) return false;
            if (!c) return true;
        }
    }
}

static const char *entry(int i, uint32_t *off)
{
    uint32_t o = 0, len = 0;
    const char *n = BIG_locateentry(s_music->dir, NULL, i, &o, &len);
    if (off) *off = o;
    return n;
}

static void song_queue(void)   /* the queueing part of 0x4ad680 / 0x4ad790 / 0x4ae2a0 */
{
    Music *m = s_music;
    uint32_t off = 0;
    m->song = entry(m->list[m->cur], &off);
    if (-1 < m->player) m->queued = SND_StreamQueueFile(m->player, 1000, m->path, off);
    m->state = 2;
    memset(&m->info, 0, sizeof m->info);
}

static void music_task(void);

/* The stream player for the buffer (0x4adde0) and the 25 Hz task. */
static void music_open(int bytes)
{
    Music *m = s_music;
    if (!m) return;
    if (!m->open) m->open = 1;
    m->f84 = bytes / 0x28;
    if (m->player < 0 && m->buffer) {
        int kb = (bytes + ((bytes >> 31) & 0x3ff)) >> 10;
        SndParams p;
        SND_ParamsDefault(&p);
        p.vol = 0;
        p.f0a = 0;
        m->player = SND_StreamOpen(&p, 1, kb, SND_StreamBufferSize(1, kb) + bytes);
    }
    if (!m->task) {
        SND_AddTick(music_task);   /* (SYNCTASK_add at 25 Hz: every fourth of the library's ticks) */
        m->task = 1;
    }
}

void Music_Init(int bytes, int unused, const char *name)   /* 0x4aded0 */
{
    (void)unused;
    if (s_music) return;
    s_music = calloc(1, sizeof *s_music);
    Music *m = s_music;
    if (!m) return;
    m->cur = -1;   /* 0x4add20 */
    m->player = -1;
    m->buffer = calloc(1, 1);   /* (the stream's buffer: the port's player keeps its own) */
    music_open(bytes);
    snprintf(m->path, sizeof m->path, exe_str(0x5d1bf0), "GameData\\Music\\", name);   /* 0x65b314 (the install's) */
    VfsFile *f = vfs_open(m->path);
    if (f) {   /* 0x59dd20: the directory */
        uint8_t h[16];
        if (vfs_read_at(f, 0, h, 16) == 16) {
            uint32_t hs = BIG_headersize(h);
            if (16 <= hs && hs < 0x100000) {
                m->dir = malloc(hs);
                if (m->dir && vfs_read_at(f, 0, m->dir, hs) != (int64_t)hs) {
                    free(m->dir);
                    m->dir = NULL;
                }
            }
        }
        vfs_close(f);
    }
    if (m->dir) m->nsongs = (int32_t)BIG_count(m->dir);
}

void Music_Stop(void)   /* 0x4ae080 */
{
    Music *m = s_music;
    if (!m) return;
    if (m->task) {
        SND_RemoveTick(music_task);
        m->task = 0;
    }
    if (-1 < m->player) {
        SND_StreamClose(m->player);
        m->player = -1;
    }
    m->open = 0;
}

void Music_Free(void)   /* 0x4ae0e0 */
{
    Music *m = s_music;
    if (!m) return;
    Music_Stop();
    free(m->buffer);
    free(m->dir);
    free(m);
    s_music = NULL;
}

/* The song playing faded out over ms (0x4ae190); 0 stops it at once. */
void Music_FadeOut(int ms)
{
    Music *m = s_music;
    if (!m || m->cur < 0) return;
    if (ms == 0) {
        if (-1 < m->player) SND_StreamStop(m->player);
        m->fadetime = 0;
        m->song = NULL;
        m->state = 0;
        m->cur = -1;
        return;
    }
    if (m->state == 0) {
        if (m->player < 0) m->fadetime = 0;
        else {
            SND_StreamFade(m->player, ms, 0);
            m->fadetime = ms;
        }
        m->song = NULL;
    }
    m->state = -1;
    m->cur = -1;
}

/* The playlist from entry numbers (0x4ae240). */
void Music_SetPlaylist(int n, const int32_t *songs)
{
    Music *m = s_music;
    if (!m) return;
    m->nlist = 0;
    for (int i = 0; i < n; i++)
        if (-1 < songs[i] && songs[i] < m->nsongs && m->nlist < 0x80) m->list[m->nlist++] = (int8_t)songs[i];   /* (the port bounds it to the list's 0x80) */
}

static int rnd_pick(int n) { return (int)((uint32_t)abs((int)(tick_count() + REAL_rand())) % (uint32_t)n); }

/* A song to play (0x4ae2a0): with a pattern, the playlist made of the entries it matches (else of those
   matching "*-<pattern>"); mode 1 adds one of them at random to the old playlist's end (the playlist
   kept), 2 picks one at random (the playlist emptied after); otherwise the first, a random one, or
   (no pattern) the next at random. The one playing fades out first (half a second). Returns the
   playlist's length. */
int Music_Play(const char *pattern, int mode)
{
    Music *m = s_music;
    if (!m) return 0;
    int oldn = m->nlist;
    int8_t old[0x80];
    memcpy(old, m->list, sizeof old);
    if (pattern) {
        char pat[0x80];
        snprintf(pat, sizeof pat, "%s", pattern);
        m->nlist = 0;
        for (int i = 0; i < m->nsongs && m->nlist < 0x80; i++)
            if (match(entry(i, NULL), pat)) m->list[m->nlist++] = (int8_t)i;
        if (m->nlist == 0 && strlen(pattern) < 0x3d) {
            snprintf(pat, sizeof pat, exe_str(0x5d1c1c), pattern);
            for (int i = 0; i < m->nsongs && m->nlist < 0x80; i++)
                if (match(entry(i, NULL), pat)) m->list[m->nlist++] = (int8_t)i;
        }
    }
    if (m->volume == 0) return 0;
    if (m->nlist == 0) return m->nlist;
    m->shuffle = g_music_shuffle;   /* (1 when 0x65b928 is set: not in a single race) */
    int pick;
    if (mode == 1 && 0 < m->nlist) {
        int8_t s = m->list[rnd_pick(m->nlist)];
        memcpy(m->list, old, sizeof old);
        m->list[oldn] = s;   /* just past the playlist's end: played once, then back to the list */
        m->nlist = oldn;
        pick = oldn;
    } else if (mode == 2 && 0 < m->nlist) {
        m->list[0] = m->list[rnd_pick(m->nlist)];
        m->nlist = 0;
        pick = 0;
    } else if (m->nlist == 1) {
        pick = 0;
    } else if (!pattern) {
        pick = ((int)((uint32_t)abs((int)(tick_count() + REAL_rand())) % (uint32_t)(m->nlist - 1)) + 1 + m->cur) % m->nlist;
    } else {
        pick = rnd_pick(m->nlist);
    }
    if (m->state == 0) {
        if (m->cur < 0) {
            m->fadetime = 0;
            SND_StreamSetVolume(m->player, 0);
            m->cur = pick;
            song_queue();
        } else {
            SND_StreamFade(m->player, 500, 0);
            m->fadetime = 500;
            m->state = 1;
            m->cur = pick;
            m->song = NULL;
        }
    } else {
        m->state = 1;
        m->cur = pick;
    }
    m->f10 = 1;
    m->fade = 0;
    if (pattern) m->f18 = 1;
    return m->nlist;
}

/* The music's key (I_MUSIC, 0x4ad560): the song playing faded out and the song cleared; with a song asked
   for (+0x10) the music stops, else the next is one of the playlist at random. */
void Music_Next(void)
{
    Music *m = s_music;
    if (!m || m->nlist == 0 || m->volume <= 0) return;
    m->song = NULL;
    m->f18 = 1;
    m->fade = 0;
    m->info.left = m->info.length = 0;
    m->info.name = m->info.artist = NULL;
    m->info.title[0] = 0;
    if (m->state == 0 && -1 < m->cur) {
        SND_StreamFade(m->player, 500, 0);
        m->fadetime = 1000;
    }
    m->cur++;
    if (m->f10) {
        m->f10 = 0;
        m->state = -1;
        m->cur = -1;
        return;
    }
    m->f10 = 1;
    m->state = 1;
    if (m->nlist == 1) m->cur = 0;
    else if (0 < m->nlist) m->cur = rnd_pick(m->nlist);
}

/* The music's volume (0x4ae690): 0 stops it; from 0 a song starts; a fade in progress is rescaled. */
void Music_SetVolume(int vol)
{
    Music *m = s_music;
    if (!m || m->volume == vol) return;
    if (vol == 0) {
        m->volume = 0;
        Music_FadeOut(0);
        return;
    }
    bool done = false;
    if (m->fadetime && -1 < m->player && 0 < m->volume) {
        int v = SND_StreamVolume(m->player);
        if (0 < v) {
            int ms = (m->fadetime * v) / m->volume;
            if (ms) {
                SND_StreamSetVolume(m->player, (ms * vol) / m->fadetime);
                SND_StreamFade(m->player, ms, 0);
                done = true;
            }
        }
    }
    if (!done && -1 < m->player && m->state != 2) SND_StreamSetVolume(m->player, vol);
    int was = m->volume;
    m->volume = vol;
    if (was == 0) Music_Play(NULL, 0);
}

/* The entries matching the pattern (Music_LoadSongList 0x4adbf0's numbers): their count. */
int Music_SongList(const char *pattern, int32_t *out, int cap)
{
    Music *m = s_music;
    if (!m || !m->dir) return 0;
    char pat[0x80];
    int n = 0;
    for (int i = 0; i < m->nsongs && n < cap; i++) {
        snprintf(pat, sizeof pat, "%s", pattern);
        if (match(entry(i, NULL), pat)) out[n++] = i;
    }
    return n;
}

/* The ms of the song playing played so far (its stream entry's, 0x565d90), 0 for none. */
int Music_PlayedMs(void)
{
    Music *m = s_music;
    if (!m || m->player < 0) return 0;
    int32_t st[3], e[4];
    SND_StreamStatus(m->player, st);
    if (st[0] <= 0) return 0;
    SND_StreamEntry(st[1], e);
    return e[0] == 2 ? e[1] : 0;
}

int Music_LoadSongList(const char *pattern, MusicListSong *out, int cap)
{
    Music *m = s_music;
    if (!m || !m->dir) return 0;
    char pat[0x80];
    int n = 0;
    for (int i = 0; i < m->nsongs && n < cap; i++) {
        uint32_t o = 0, len = 0;
        const char *name = BIG_locateentry(m->dir, NULL, i, &o, &len);
        snprintf(pat, sizeof pat, "%s", pattern);
        if (!match(name, pat)) continue;
        MusicListSong *e = &out[n++];
        memset(e, 0, sizeof *e);
        e->name = name;
        name_parts(name, e->tbuf, &e->artist);
        e->title = e->tbuf;
        e->id = i;
        e->length = (int32_t)(len * 10 / 0x15c);
    }
    return n;
}

/* 0x4ad4c0: the song's start flag taken (+0x18), playing (the fade's code, else the playlist's place + 1)
   and the stream entry's ms left (+0xa0) into the song's times (+0x170); a fade's error code names the
   failure in the artist's place (+0x178, as the original's). */
bool Music_Status(int32_t out[3])
{
    Music *m = s_music;
    if (!m) return false;
    m->info.left = m->entry[2];
    int32_t playing = m->fade == 0 ? m->cur + 1 : m->fade;
    if (m->fade == -4) m->info.artist = exe_str(0x5d1ba8);   /* "CBUFFER NOT ALLOCATED" */
    else if (m->fade == -3) m->info.artist = exe_str(0x5d1bc0);   /* "STREAM NOT CREATED" */
    out[0] = m->f18 != 0;
    m->f18 = 0;
    out[1] = playing;
    out[2] = m->info.left;
    return true;
}

const MusicSong *Music_Current(void) { return s_music && s_music->song ? &s_music->info : NULL; }

/* 25 times a second (0x4ad790). */
static void music_step(void)
{
    Music *m = s_music;
    if (!m || !m->dir) return;
    if (m->player < 0) m->status[0] = 0;
    else {
        SND_StreamStatus(m->player, m->status);
        if (0 < m->status[0]) SND_StreamEntry(m->status[1], m->entry);
    }
    /* near a song's end (under 0.55 s read ahead and less than is left): its fade out over that */
    if (m->fade == 0 && m->state != 2 && m->f84 && m->status[0] != 0 && m->entry[3] < m->entry[2] && m->entry[3] < 0x226) {
        m->fade = -5;
        SND_StreamFade(m->player, m->entry[3], 0);
        m->fadetime = m->entry[3];
        m->state = 2;
    }
    if (m->state == 0) {
        if (m->status[0] == 0 && -1 < m->cur) {
            if (m->nlist == 1) m->cur = 0;
            else if (0 < m->nlist) {
                if (m->shuffle == 0) m->cur = (m->cur + 1) % m->nlist;
                else m->cur = ((int)((uint32_t)abs((int)(tick_count() + REAL_rand())) % (uint32_t)(m->nlist - 1)) + 1 + m->cur) % m->nlist;
            }
            SND_StreamSetVolume(m->player, 0);
            if (0 < m->nlist) {
                song_queue();   /* 0x4ad680 */
                m->f18 = 1;
                m->f10 = 1;
            }
        }
        return;
    }
    if (m->state != 2) {
        if (m->status[0] != 0 && SND_StreamVolume(m->player) != 0) return;   /* the fade still going */
        if (-1 < m->player) SND_StreamStop(m->player);
        m->song = NULL;
        if (m->state == 1) {
            m->fadetime = 0;
            song_queue();
            return;
        }
        m->state = 0;
        return;
    }
    if (m->buffer && -1 < m->player) {
        if (m->status[0] == 0) return;
        /* the song started: its times and name, its volume (the options' music volume for the end
           of race's lose, win and vic ones) */
        m->info.left = m->entry[2];
        m->info.length = m->entry[1] + m->entry[2];
        song_info(&m->info, m->song);
        bool end = m->song && (!strncmp(m->song, exe_str(0x5d1bdc), 4) || !strncmp(m->song, exe_str(0x5d1bd8), 3) ||
                               !strncmp(m->song, exe_str(0x5d1bd4), 3));
        SND_StreamFade(m->player, 5, end ? (int32_t)exe_u32(0x5d1740) : m->volume);
        m->state = 0;
        return;
    }
    m->fade = m->buffer ? -3 : -4;
    if (m->status[0] != 0) {
        SND_StreamFade(m->player, m->entry[3], 0);
        m->fadetime = m->entry[3];
    }
    m->f18 = 1;
    m->f10 = 0;
    m->song = NULL;
    m->state = -1;
    m->cur = -1;
}

static void music_task(void)
{
    static int div;
    if (++div < 4) return;
    div = 0;
    music_step();
}
