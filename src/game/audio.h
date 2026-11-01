/* The game's audio ([audio], 0x4a4000-0x4b0000) on the SND library (src/snd/): its start by the sound
   quality (Audio_Init 0x4a6610, 0x4a6480), the banks it keeps (AudioCmn_AddBank 0x4a6990), the engines
   (EngineAudio_Load 0x4ac9e0: a car's <sound>.viv, its bank and four CRDl layer tables; each odd tick a
   snapshot of the engine's volume, pitch, rpm, throttle and view (0x4ac550) that every third one plays
   (0x4ac060)), and the odd ticks' update (Audio_Tick 0x4a7ef0: the listener at the camera, the player's
   car's sounds 0x4a7430, 0x4a9e20). The opponents' (0x4a6e50) and the track's sound elements (0x4aed00) follow; music and most speech are pending. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

extern int32_t g_sound_quality;
extern int32_t g_audio_fx;         /* 0x5d1744: the effects' volume, 0 .. 0x7f (the options') */    /* 0x657c98: 0 high .. 2 low, 3 none */

bool Audio_Init(void);
void Audio_Shutdown(void);
/* Engine slot 0 or 1 from the car's sound name (its description's SOUNDNAME). 0x4ac9e0 */
bool Audio_LoadEngine(int slot, const char *sound);
/* The player's engine and the collisions' sounds (0x4a8aa0). */
void Audio_LoadCollisionSounds(const char *engine);
/* The streamed one-shots' archives (Audio_LoadTrackSounds 0x4afda0). */
void Audio_LoadTrackSounds(void);
/* Before the race (0x4a8670): the track's sound elements' voices (0x4ae850), the opponents' engine
   banks and the track's sounds near the start read (Audio_LoadTrackBanks 0x4af390). */
void Audio_StartRace(void);
/* The start's countdown, step 0 .. 3 ("3", "2", "1", "Go"). 0x4a6010 */
void Audio_Countdown(int step);
/* The announcer (0x4a5f70 .. 0x4a6450): a lap's number, a best lap, the final lap, a place (0 first),
   seconds behind the one ahead, the lead over the other of two (seconds, negative behind). */
/* One of the announcer's lines by its number (0x4af6a0 + 0x4b0370 with type 2). */
void Audio_Say(int idx);
void Audio_SayLap(int lap);
void Audio_SayBestLap(void);
void Audio_SayFinalLap(void);
void Audio_SayPlace(int place);
void Audio_SayBehind(int s);
void Audio_SayLead(int s);
/* A lightning strike's thunder (0x4a8110): one of track.viv's 991 .. 999 after 0x87 - delay odd ticks,
   from the angle. */
void Audio_Thunder(int delay, int angle);
/* The odd ticks' audio (0x4a7ef0), the listener at the race camera. */
void Audio_Tick(void);
/* A knocked object's sound at p: the nearest one-shot sound element within 5 triggered. 0x4af210 */
void Audio_HitSound(const float p[3]);
