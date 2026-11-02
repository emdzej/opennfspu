/* The game's music (0x4ad2a0 .. 0x4ae780): zzzymus.viv's songs through an SND stream player, by a
   playlist (Music_Init 0x4aded0, the 25 Hz task 0x4ad790). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The song playing (+0x170 of the globals): its entry's name, the title and artist parts (0x4ad720),
   its ms left when it started and its length. */
typedef struct {
    int32_t left;             /* +0x00 */
    const char *name;         /* +0x04 */
    const char *artist;       /* +0x08 */
    char title[0x20];         /* +0x20 */
    int32_t length;           /* +0x1c */
} MusicSong;

extern int32_t g_music_shuffle;   /* 0x657a5c: RANDOMPLAYLIST (option 0x43) */
/* The race's playlist (options 0x3c NUMPLAYLISTSONGS and 0x143: the front end's CD player's songs,
   0x4be610), entry numbers; the port's -1 for a race without the front end. */
extern int32_t g_music_nplaylist;     /* 0x657cb8 */
extern int32_t g_music_playlist[0x80];

/* The archive GameData\Music\zzz<name>.viv and a stream player with a buffer of bytes (0x4aded0). */
void Music_Init(int bytes, int unused, const char *name);
void Music_Stop(void);                               /* 0x4ae080: the player closed */
void Music_Free(void);                               /* 0x4ae0e0 */
void Music_FadeOut(int ms);                          /* 0x4ae190 (0: at once) */
void Music_SetPlaylist(int n, const int32_t *songs); /* 0x4ae240: entry numbers */
int Music_Play(const char *pattern, int mode);       /* 0x4ae2a0 */
void Music_SetVolume(int vol);                       /* 0x4ae690 */
void Music_Next(void);                               /* 0x4ad560: the music key */
int Music_SongList(const char *pattern, int32_t *out, int cap);   /* 0x4adbf0's entries */
int Music_PlayedMs(void);
/* A song of a list (Music_LoadSongList 0x4adbf0's entries, 0x40 bytes from +8): the entry's name, its
   title and artist (0x4ad720), the ms its bytes last (bytes * 10 / 348) and its entry number. */
typedef struct {
    const char *name;         /* +0x00 */
    const char *title;        /* +0x04: tbuf */
    const char *artist;       /* +0x08 */
    int32_t f0c, f10, f14;
    int32_t length;           /* +0x18 */
    int32_t id;               /* +0x1c */
    char tbuf[0x20];          /* +0x20 */
} MusicListSong;
int Music_LoadSongList(const char *pattern, MusicListSong *out, int cap);
/* 0x4ad4c0: {a song started since the last call, playing (non-zero), its ms left}; false no music. */
bool Music_Status(int32_t out[3]);
const MusicSong *Music_Current(void);
