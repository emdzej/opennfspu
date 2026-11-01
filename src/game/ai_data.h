/* The AI's data (the [ai] module's loads, 0x406950): the traffic's configuration (0x406c00), the drivers'
   personalities, scripts and glue (AI_LoadPersonalities 0x407f50: GameData\Simulation\AI\prsonal.bin,
   scripts.bin, glue.bin or hhglue.bin), each car's personality and its per-car tables. */
#pragma once
#include "game/car.h"
#include <stdint.h>

#define AI_NPERS 9   /* the personalities' records (0x5e67c0 .. 0x5e6ab8) */

/* A personality's record (0x54 bytes): its traits, each the exe's value for the file's level (0x407d90). */
typedef struct { uint32_t v[21]; } AiPersonality;
extern AiPersonality g_ai_pers[AI_NPERS];
/* The driving scripts (0x5e6b08: per personality seven entries of four pairs of bytes). */
extern uint8_t g_ai_scripts[AI_NPERS][7][8];
/* The glue (0x5e6ab4: 21 factors, scaled with police about). */
extern float g_ai_glue[21];
/* The traffic's configuration (0x5e6d08 ..): three counts, two factors, then two sets of eleven. */
extern int32_t g_traf_count[3];
extern float g_traf_f[2], g_traf_set[2][11];

/* The race's AI data loads and the cars' set-up from them. 0x406950 */
void AI_LoadData(void);
/* The car's personality record (+0xe38). */
const AiPersonality *AI_Personality(Car *c);
float AI_PersF(Car *c, int off);
/* The car's squares table (+0x1014: 128 bytes, i * i / k / 4, and k at +0x80), NULL unless an opponent. */
const uint8_t *AI_SquaresTable(Car *c);
