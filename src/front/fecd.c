/* The options screen's CD player (fecd.h). */
#include "front/fecd.h"
#include "exe.h"
#include "game/music.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

enum { LIST_MAX = 0x80, SEL_MAX = 0x400 };

static MusicListSong s_list[LIST_MAX];   /* 0x65b628 */
static int s_n;
static bool s_have;                      /* (the list made: 0x65b628 non-NULL) */
static int32_t s_sel[SEL_MAX];           /* 0x65b62c */
static int s_nsel;
static int32_t s_pos;                    /* 0x65b630 */
static int32_t s_cur;                    /* 0x5d7938: the song chosen's entry number, -1 none */

/* The defaults (0x4bdfb0's and 0x4be3c0's walk): every song whose name's first four characters,
   lowercased, are "game" (0x5d1a0c), from the first 40 rows only (the walk stops at 0xa00 bytes). */
void FECD_Reset(void)
{
    s_nsel = 0;
    for (int i = 0; i < s_n && i < 0x28; i++) {
        char k[5] = {0};
        for (int j = 0; j < 4 && s_list[i].name[j]; j++) k[j] = (char)tolower((unsigned char)s_list[i].name[j]);
        if (!strcmp(k, exe_str(0x5d1a0c)) && s_nsel < SEL_MAX) s_sel[s_nsel++] = s_list[i].id;
    }
}

void FECD_Init(void)
{
    s_cur = (int32_t)exe_u32(0x5d7938);
    s_n = Music_LoadSongList(exe_str(0x5d7948), s_list, LIST_MAX);   /* "game*" */
    s_have = true;
    FECD_Reset();
}

void FECD_Clear(void) { s_nsel = 0; }

void FECD_Add(int32_t id)
{
    if (s_nsel < SEL_MAX) s_sel[s_nsel++] = id;
}

void FECD_Remove(int i)
{
    if (i < 0 || i >= s_nsel) return;
    memmove(s_sel + i, s_sel + i + 1, sizeof *s_sel * (size_t)(s_nsel - i - 1));
    s_nsel--;
}

/* (The play needs the screen on top to allow it, its slot 0xa8: the options screen's 0x50e340 says yes,
   and only it chooses songs.) */
void FECD_Select(int32_t id)
{
    if (id == s_cur) return;
    s_cur = id;
    if (id == -1 || !s_have) return;
    for (int i = 0; i < s_n; i++) {
        if (s_list[i].id != id) continue;
        if (s_list[i].name) {
            Music_FadeOut(0);
            Music_Play(s_list[i].name, 0);
        }
        return;
    }
}

const char *FECD_CurrentName(void)
{
    for (int i = 0; s_have && i < s_n; i++)
        if (s_list[i].id == s_cur) return s_list[i].name;
    return NULL;
}

int FECD_Count(void) { return s_n; }

const char *FECD_Text(int i, int mode)
{
    if (!s_n || i < 0 || i >= s_n) return "";
    const char *t = mode == 0 ? s_list[i].title : mode == 1 ? s_list[i].artist : "";
    return t ? t : "";
}

int32_t FECD_Id(int i) { return i >= 0 && i < s_n ? s_list[i].id : 0; }
int32_t FECD_Length(int i) { return i >= 0 && i < s_n ? s_list[i].length : 0; }

int FECD_SelCount(void) { return s_nsel; }
const int32_t *FECD_Sel(void) { return s_sel; }

/* (The chosen list's rows are its entry numbers taken as rows of the available list, as the original's
   FESelectedSongsAccessor_v12 0x4be810 and TOTALPLAY do: the same while the game songs lead the
   archive.) */
int FECD_SelRow(int i)
{
    if (i < 0 || i >= s_nsel || s_sel[i] < 0 || s_sel[i] >= s_n) return -1;
    return s_sel[i];
}

void FECD_SetSelected(const int32_t *ids, int n)
{
    s_nsel = 0;
    for (int i = 0; i < n && s_nsel < SEL_MAX; i++) s_sel[s_nsel++] = ids[i];
}

int FECD_Pos(void) { return s_pos; }

void FECD_SetPos(int pos)
{
    s_pos = pos;
    FECD_Select(s_have && pos >= 0 && pos < s_n ? s_list[pos].id : 0);
}
