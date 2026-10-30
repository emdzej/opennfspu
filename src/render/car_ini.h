/* The game's INI text files (0x5234c0..0x523ae0, just below FERT): the car's .tpg and .clr, and other
   "[section] key=value" files. A file is read whole into one buffer and split in place: sections are
   found when it is parsed, a section's keys when it is selected (IniData_SelectSection), so one section
   is "current" at a time and the getters look keys up in it.

   Limits as the original's: 700 sections, 100 keys per section. Section and key names compare
   case-sensitively (strcmp); values run to the end of the line or a ';' and are trimmed of blanks and
   tabs. A key line without '=' is skipped.

   (This module isn't car code; it lives here until the rest of the front end's support code is ported.) */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { INI_MAXSECTIONS = 700, INI_MAXKEYS = 100 };

typedef struct {
    int cursec;                          /* +0x0000: the selected section, -1 none */
    int nsections;                       /* +0x0004 */
    char *secname[INI_MAXSECTIONS];      /* +0x0008 */
    char *secbody[INI_MAXSECTIONS];      /* +0x0af8: the first line after "[name]" */
    int nkeys;                           /* +0x15e8: in the selected section */
    char *key[INI_MAXKEYS];              /* +0x15ec */
    char *value[INI_MAXKEYS];            /* +0x177c */
    char *buf;                           /* +0x190c: the text (a copy, NUL-terminated) */
    char *end;                           /* +0x1910 */
} IniData;
#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(IniData) == 0x1914, "the original's allocation size");
#endif

IniData *IniData_Parse(const char *text, uint32_t len);       /* 0x5234c0 (NULL without memory) */
void IniData_Free(IniData *ini);                              /* 0x523680 */
int IniData_FindSection(const IniData *ini, const char *name);  /* 0x523830: index or -1; NULL = the current */
void IniData_LoadSection(IniData *ini, int index);           /* 0x5236d0: splits its keys in place */
int IniData_SelectSection(IniData *ini, const char *name);   /* 0x5236a0: index or -1 (then nothing changes) */
int IniData_FindKey(const IniData *ini, const char *key);    /* 0x5238b0 */
const char *IniData_Value(const IniData *ini, int index);    /* 0x523950 */
const char *IniData_Get(const IniData *ini, const char *key);  /* 0x523920: the value or NULL */

/* A file's INI data: the object the game embeds (R3DCar keeps its .tpg one at +4). */
typedef struct {
    IniData *ini;   /* +0x0 */
    char *text;     /* +0x4: the file as read (the parse keeps its own copy) */
} IniFile;

bool IniFile_Parse(IniFile *f, const char *text, uint32_t len);  /* 0x523200 */
/* Reads the file (name under the search path) and parses it; false if it can't be read. 0x523230 */
bool IniFile_Load(IniFile *f, const char *name);
void IniFile_Free(IniFile *f);                                   /* 0x523320 */
/* Typed getters on the current section: def if the key is missing. */
int IniFile_GetInt(const IniFile *f, const char *key, int def);            /* 0x523350 (atoi) */
bool IniFile_GetBool(const IniFile *f, const char *key, bool def);         /* 0x523380 */
float IniFile_GetFloat(const IniFile *f, const char *key, float def);      /* 0x5233a0 (atof) */
void IniFile_GetVec3(const IniFile *f, float out[3], const char *key, const float def[3]);   /* 0x5233d0 */
void IniFile_GetVec4(const IniFile *f, float out[4], const char *key, const float def[4]);   /* 0x523440 */
