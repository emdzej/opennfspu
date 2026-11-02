/* The replay files (<name>.rpl in the save directory, 0x1ce28 bytes; [game] 0x413830 .. 0x414710): the
   race's settings block (0x6573e8, 0x3e24 bytes: the options, the track, the records, the tournament,
   the mission, the counts and nine car descriptions from +0xca4), the tick the recording stopped at
   (0x5ed844) and the replay buffer (0x5ed848, 0x19000 bytes). Written with the block's +0x3c (the view)
   2. A replay started from the front end (the view 2) reads its file and takes the block back, but the
   player's display settings (0x657a48 .., 0x657c84 .., 0x657d5c ..). The port keeps the block's fields
   in their own globals: the file is built from them and given back to them, the fields it doesn't have
   zero. */
#pragma once
#include <stdbool.h>

/* The race as it stands into <file> (Replay_WriteFinalTick 0x414680; Replay_Save 0x413ff0 is the same
   with "replay.rpl"): the final tick the current one when still recording. */
bool Replay_Write(const char *file);
/* Whether the save directory has <file> (0x59d9e0). */
bool Replay_Exists(const char *file);
/* <file>, else the default replay.rpl, into the loaded copy (Replay_Load 0x414710); false (the replay
   off, 0x5e99f4 0) with neither. */
bool Replay_Load(const char *file);
/* The loaded copy's settings, final tick and replay back into the race (0x413830's view 2), the
   player's display settings kept. Run before the track loads and again after the cars' set-up, whose
   stand-ins it overwrites. */
void Replay_Apply(void);
/* Whether a file's replay is loaded (the race started as one); Replay_Unload drops it. */
bool Replay_Loaded(void);
void Replay_Unload(void);
