/* The game's options (the options module: Options_Read 0x4b6450 reads fe.txt's "name = value" lines,
   names case-insensitive, '#' comments; Options_Parse 0x4b0db0 binds each name to its global by the
   (id, target, key) triples it fills). The port keeps the file's lines as they are: a value is read by
   its key, the key's text from the exe (its address in the triples). The front end changes them and
   they're written back before a race (the original writes fe.txt back the same way), into the user's
   files; a saved fe.txt there is read before the game's own. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void Options_Load(void);
/* The value of a key (its text), def without one. */
const char *Options_Str(const char *key);
int Options_Get(const char *key, int def);
/* By the key's text at its address in the exe (the triples' operands). */
int Options_Int(uint32_t key, int def);
void Options_SetInt(uint32_t key, int v);
/* A key's value text set (the key's line, or a new one). */
void Options_SetStr(const char *name, const char *v);
/* The lines back to the user's fe.txt (only after a change). */
void Options_Save(void);
/* The globals the race reads, from the options (Options_Parse's targets the port has): the HUD's split
   times, opponent list and speed units. */
void Options_Apply(void);
