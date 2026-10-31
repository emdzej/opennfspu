/* The game's audio ([audio], 0x4a4000-0x4b0000) on the SND library (src/snd/): its start by the sound
   quality (Audio_Init 0x4a6610, 0x4a6480), the banks it keeps (AudioCmn_AddBank 0x4a6990), the engines
   (EngineAudio_Load 0x4ac9e0: a car's <sound>.viv, its bank and four CRDl layer tables; each odd tick a
   snapshot of the engine's volume, pitch, rpm, throttle and view (0x4ac550) that every third one plays
   (0x4ac060)), and the odd ticks' update (Audio_Tick 0x4a7ef0: the listener at the camera, the player's
   car's sounds 0x4a7430, 0x4a9e20). Collisions, the track's sounds, music and speech are pending. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

extern int32_t g_sound_quality;    /* 0x657c98: 0 high .. 2 low, 3 none */

bool Audio_Init(void);
void Audio_Shutdown(void);
/* Engine slot 0 or 1 from the car's sound name (its description's SOUNDNAME). 0x4ac9e0 */
bool Audio_LoadEngine(int slot, const char *sound);
/* The player's engine and the collisions' sounds (0x4a8aa0). */
void Audio_LoadCollisionSounds(const char *engine);
/* The odd ticks' audio (0x4a7ef0), the listener at the race camera. */
void Audio_Tick(void);
