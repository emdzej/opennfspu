/* The game's audio ([audio], 0x4a4000-0x4b0000) on the SND library (src/snd/): its start by the sound
   quality (Audio_Init 0x4a6610, 0x4a6480), the banks it keeps (AudioCmn_AddBank 0x4a6990), the engines
   (EngineAudio_Load 0x4ac9e0: a car's <sound>.viv, its bank and four CRDl layer tables; each odd tick a
   snapshot of the engine's volume, pitch, rpm, throttle and view (0x4ac550) that every third one plays
   (0x4ac060)), and the odd ticks' update (Audio_Tick 0x4a7ef0: the listener at the camera, the player's
   car's sounds 0x4a7430, 0x4a9e20). The opponents' (0x4a6e50) and the track's sound elements (0x4aed00) follow; music and most speech are pending. */
#pragma once
#include "game/car.h"
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
/* The front end's and the HUD's sounds (0x4aab80): fesfx's patch (5 a click, 8 a slider, 9 a key or
   button, 4 a billboard) at the mix's volume. */
void Audio_UI(int patch);
/* A speech bank of the speech archive (zzzxeng.viv's entry) played at once (0x4fc980), and its stop and
   release. */
bool Audio_SpeechBank(const char *entry);
void Audio_SpeechFree(void);
/* The sounds stopped with a pause (0x4aaf20) and started again (0x4aafd0). */
void Audio_Pause(void);
/* The effects off or on again (I_SFX). 0x4ab2b0 */
void Audio_ToggleSfx(void);
void Audio_Resume(void);
/* The race's sounds stopped at its end (0x4ab040), and before the replay plays again (0x4ab130). */
void Audio_Stop(void);
void Audio_Flush(void);
/* The race's bank-3 one-shots: a gate passed (0x4aac00, patch 2), a cone hit (0x4aac20, 0), the wrong
   way's warning (0x4aac40, 1, quieter). */
void Audio_Gate(void);
void Audio_Cone(void);
void Audio_WrongWay(void);
/* A lightning strike's thunder (0x4a8110): one of track.viv's 991 .. 999 after 0x87 - delay odd ticks,
   from the angle. */
void Audio_Thunder(int delay, int angle);
/* The odd ticks' audio (0x4a7ef0), the listener at the race camera. */
void Audio_Tick(void);
/* A knocked object's sound at p: the nearest one-shot sound element within 5 triggered. 0x4af210 */
void Audio_HitSound(const float p[3]);

/* A near car's horn (0x4a6bb0): sounded `times` times for len ticks, gap between (when silent). */
void Audio_Horn(Car *c, int times, int len, int gap);
/* Held on (0x4a6b60), or let go after this tick. */
void Audio_HornHold(Car *c, bool on);
