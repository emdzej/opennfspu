/* The options screen's CD player ([frontend] 0x4bdd00 .. 0x4bee40): the game songs there are ("game*",
   0x65b628: Music_LoadSongList's) and the ones chosen for the races (0x65b62c, a vector of entry
   numbers: by default every song whose name starts with "game", at most the first 40), kept in the
   player's save (the CDPlayer section, 0x4bdd50 / 0x4bdf20) and passed to the race as its playlist
   (options 0x3c NUMPLAYLISTSONGS and 0x143, 0x4be610). The song shown (CDPlayer.Title, 0x65b630)
   plays when chosen. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void FECD_Init(void);                       /* 0x4bdfb0 (FEApplication_Run's start) */
void FECD_Reset(void);                      /* 0x4be3c0: the defaults again (RESETLIST) */
void FECD_Clear(void);                      /* 0x4be5c0 (CLEARALL) */
void FECD_Add(int32_t id);                  /* 0x4be250 */
void FECD_Remove(int i);                    /* 0x4be370: the i-th chosen */
void FECD_Select(int32_t id);               /* 0x4be6a0: that song played if it isn't the one */
const char *FECD_CurrentName(void);         /* 0x4be720: the chosen song's entry name, NULL none */

/* The available list: count, a row's title (mode 1 the artist), its entry number (0x4be7c0) and
   length in ms. */
int FECD_Count(void);
const char *FECD_Text(int i, int mode);
int32_t FECD_Id(int i);
int32_t FECD_Length(int i);
/* The chosen list: count, the i-th's row in the available list (-1 none). */
int FECD_SelCount(void);
const int32_t *FECD_Sel(void);
int FECD_SelRow(int i);
/* The chosen list as the save gives it (0x4bdd50) and the race's playlist (0x4be610): n written. */
void FECD_SetSelected(const int32_t *ids, int n);
/* The song shown (0x65b630) and its choice (FECurrentSongSelection_v11 0x4be9a0). */
int FECD_Pos(void);
void FECD_SetPos(int pos);
