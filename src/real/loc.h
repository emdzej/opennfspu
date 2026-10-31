/* EA real library: localised strings (the LOCH container of hud.loc and the front end's .loc files;
   0x536af0, 0x536b10, 0x536b60). +0x04 the id index's offset (a 'LOCI' block: at +0x8 its count, from
   +0x10 sorted pairs {u16 id, u16 string}), +0x08 flags (1: ids go through the index), +0x0c the language
   count, +0x0e the language chosen, from +0x10 each language's 'LOCL' table (+0xc its count, from +0x10
   the strings' offsets). */
#pragma once
#include <stdint.h>

/* The language (index into the tables), only for set 0 (0x536af0). */
void LOC_setlanguage(uint8_t *loc, int set, int language);
/* String id of the chosen language, NULL past its table. 0x536b10 */
const char *LOC_string(const uint8_t *loc, int id);
