/* The police and the cars they pursue (ai_police.c). */
#pragma once
#include "game/ai_internal.h"

/* The track's police script (AI_LoadCopScript 0x403040: with COPS), its reset for a restart (0x4030b0)
   and its end (0x4030d0). */
void Police_LoadScript(void);
void Police_ResetScript(void);
void Police_FreeScript(void);
/* A human's or racer's controller as a pursued car (0x403d10, 0x405c90) and its heat (0x405dd0). */
void Police_PursuedInit(AiHigh *p);
void Police_PursuedTick(AiHigh *p);
/* A police car's controller (0x403e90; number: its place among the police) and its tick (0x403ed0). */
void Police_Init(AiHigh *h, int number);
void Police_Tick(AiHigh *h);
