/* The option records ([options] 0x4b4950 .. 0x4b4b70, the front end's writers 0x4ed1b0 .. 0x4ed310): the
   race's set-up as a list of words, each record an option's id then its value, ended by a zero id. The
   ids index the options' table (0x5d1e40: id, address, name; Options_Parse 0x4b0db0 fills most of it):
   below 0x10e a word, 0x10e .. 0x139 a text, 0x13a .. 0x141 a car's text (its index first), 0x142 ..
   0x146 a count of words, 0x147 .. 0x14d a car's (its index first), 0x14e .. 0x21d a control binding,
   from 0x21e a car description's word (index, value). A network game's players send theirs (0xbc). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The record's size in words. 0x4b4aa0 */
int Option_Size(const int32_t *rec);
/* One record into the port's fields (the race block's, the car descriptions'); the others, which the
   port keeps elsewhere, skipped. 0x4b4950 */
void Option_Apply(const int32_t *rec);

/* A field of the original's address the port keeps elsewhere (the front end's), for Option_Apply. */
void Option_Bind(uint32_t va, void *p, int size);

/* The writers, each moving *p past its record. */
void Option_PutInt(int32_t **p, int id, int32_t v);                          /* 0x4ed1b0 */
void Option_PutCar(int32_t **p, int id, int car, int32_t v);                 /* 0x4ed290 */
void Option_PutCarText(int32_t **p, int id, int car, const char *text);      /* 0x4ed220 */
void Option_PutCarWords(int32_t **p, int id, int car, int n, const int32_t *v);   /* 0x4ed310 */

/* A network race's set-up (Race_Configure 0x4b4b70, its FEGAME_TYPE 3 block): the players' seats, the
   local car's options (and the host's race options) sent, everyone's received into the descriptions;
   after it the local player's seat is g_race_local and the cars are the seats. */
void RaceSetup_Network(void);
