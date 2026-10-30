/* INI text files (see car_ini.h). Parse 0x5234c0, sections 0x523830/0x5236d0, keys 0x5238b0/0x523950,
   the file object 0x523200..0x523440, and the buffered reader the load goes through (0x56d510..0x56d890). */
#include "render/car_ini.h"
#include "exe.h"
#include "real/file.h"
#include "real/mem.h"
#include "support/ufile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The real library's MEM_alloc with the game's default flags (0x5deb60), as its buffered files and the
   INI data allocate (0x56d870); freed with MEM_purge (0x56d890). */
static void *ini_alloc(uint32_t size) { return MEM_alloc(NULL, (int)size, 0); }
static void ini_free(void *p) { MEM_purge(p); }

/* Past the end of the line and the blank space after it: the next line's first character, or NULL at
   the end of the text. 0x5235f0 */
static char *next_line(char *p, const char *end)
{
    if (p >= end) return NULL;
    while (*p != '\n' && *p != '\r')
        if (++p >= end) return NULL;
    while (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t')
        if (++p >= end) return NULL;
    return p;
}

/* Leading blanks skipped, trailing ones cut (in place; the end is taken before the leading blanks are
   skipped). 0x523640 */
static char *trim(char *s)
{
    char *e = s + strlen(s) - 1;
    while (*s == ' ' || *s == '\t') s++;
    while (s < e && (*e == ' ' || *e == '\t')) *e-- = 0;
    return s;
}

IniData *IniData_Parse(const char *text, uint32_t len)
{
    IniData *ini = ini_alloc(sizeof *ini);
    if (!ini) return NULL;
    memset(ini, 0, sizeof *ini);
    ini->buf = ini_alloc(len + 1);
    if (!ini->buf) { ini_free(ini); return NULL; }
    memcpy(ini->buf, text, len);
    ini->buf[len] = 0;
    char *p = ini->buf;
    ini->nsections = 0;
    ini->end = ini->buf + len;
    while (p && ini->nsections < INI_MAXSECTIONS) {
        if (*p == '[') {
            p++;
            ini->secname[ini->nsections] = p;
            while (*p != ']' && *p != '\n' && *p != '\r' && *p) p++;
            if (*p == ']') {
                *p = 0;
                p = next_line(p, ini->end);
                ini->secbody[ini->nsections] = p;
                ini->secname[ini->nsections] = trim(ini->secname[ini->nsections]);
                ini->nsections++;
            } else {
                /* An unclosed "[": the slot is cleared and the scan goes on from where it stopped. */
                ini->secname[ini->nsections] = NULL;
            }
        } else {
            p = next_line(p, ini->end);
        }
    }
    ini->cursec = -1;
    return ini;
}

void IniData_Free(IniData *ini)
{
    ini_free(ini->buf);
    ini_free(ini);
}

int IniData_FindSection(const IniData *ini, const char *name)
{
    if (!name) return ini->cursec;
    for (int i = 0; i < ini->nsections; i++)
        if (strcmp(ini->secname[i], name) == 0) return i;
    return -1;
}

void IniData_LoadSection(IniData *ini, int index)
{
    ini->cursec = index;
    char *p = ini->secbody[index];
    ini->nkeys = 0;
    for (;;) {
        if (!p || *p == '[' || ini->nkeys > INI_MAXKEYS - 1 || p >= ini->end) return;
        ini->key[ini->nkeys] = p;
        while (*p != '=' && *p != '\n' && *p != '\r' && *p && p < ini->end) p++;
        if (*p != '=' && *p != 0) {
            /* No '=' on the line: the original clears the slot and starts over at the same place, which
               never ends (no file of the game has such a line in a section it reads). The port moves on
               to the next line instead. */
            ini->key[ini->nkeys] = NULL;
            p = next_line(p, ini->end);
            continue;
        }
        *p = 0;
        ini->key[ini->nkeys] = trim(ini->key[ini->nkeys]);
        p++;
        if (*p == 0)
            while (p < ini->end && *++p == 0) {}
        ini->value[ini->nkeys] = p;
        while (*p != '\n' && *p != '\r' && *p != ';' && *p && p < ini->end) p++;
        *p = 0;
        ini->value[ini->nkeys] = trim(ini->value[ini->nkeys]);
        p = next_line(p, ini->end);
        ini->nkeys++;
    }
}

int IniData_SelectSection(IniData *ini, const char *name)
{
    int i = IniData_FindSection(ini, name);
    if (i >= 0) IniData_LoadSection(ini, i);
    return i;
}

int IniData_FindKey(const IniData *ini, const char *key)
{
    for (int i = 0; i < ini->nkeys; i++)
        if (strcmp(ini->key[i], key) == 0) return i;
    return -1;
}

const char *IniData_Value(const IniData *ini, int index) { return ini->value[index]; }

const char *IniData_Get(const IniData *ini, const char *key)
{
    int i = IniData_FindKey(ini, key);
    return i >= 0 ? IniData_Value(ini, i) : NULL;
}

/* ---- the file object ---- */

bool IniFile_Parse(IniFile *f, const char *text, uint32_t len)
{
    f->ini = IniData_Parse(text, len);
    return f->ini != NULL;
}

/* The original reads through its buffered stdio file (0x56d510 open: fopen, the size from a seek to the
   end; 0x56d670 read in 4 KB pieces; 0x56d640 close) after resolving the name under the search path
   (0x59e1f0, the std::string form of UTIL_searchpath). The port reads the same bytes through the file
   layer. */
bool IniFile_Load(IniFile *f, const char *name)
{
    char path[260];
    if (!UTIL_searchpath(name, path)) return false;
    FileHandle h;
    if (!FILE_openwait(path, 1, 0, &h)) return false;
    int size = FILE_sizewait(h, 0);
    bool ok = false;
    if (size >= 0) {
        f->text = ini_alloc((uint32_t)size);
        if (f->text) {
            int got = FILE_readwait(h, 0, f->text, size, 0);
            ok = IniFile_Parse(f, f->text, (uint32_t)(got > 0 ? got : 0));
        }
    }
    FILE_closewait(h, 0);
    return ok;
}

void IniFile_Free(IniFile *f)
{
    if (f->ini) {
        IniData_Free(f->ini);
        f->ini = NULL;
    }
    if (f->text) {
        ini_free(f->text);
        f->text = NULL;
    }
}

int IniFile_GetInt(const IniFile *f, const char *key, int def)
{
    const char *v = IniData_Get(f->ini, key);
    return v ? atoi(v) : def;
}

bool IniFile_GetBool(const IniFile *f, const char *key, bool def) { return IniFile_GetInt(f, key, def) != 0; }

float IniFile_GetFloat(const IniFile *f, const char *key, float def)
{
    const char *v = IniData_Get(f->ini, key);
    return v ? (float)atof(v) : def;
}

/* The values are read with sscanf and the exe's formats ("%f,%f,%f" 0x5dd988, four 0x5dd994): what isn't
   read keeps the default. */
void IniFile_GetVec3(const IniFile *f, float out[3], const char *key, const float def[3])
{
    float v[3] = {def[0], def[1], def[2]};
    const char *s = IniData_Get(f->ini, key);
    if (s) sscanf(s, exe_str(0x5dd988), &v[0], &v[1], &v[2]);
    memcpy(out, v, sizeof v);
}

void IniFile_GetVec4(const IniFile *f, float out[4], const char *key, const float def[4])
{
    float v[4] = {def[0], def[1], def[2], def[3]};
    const char *s = IniData_Get(f->ini, key);
    if (s) sscanf(s, exe_str(0x5dd994), &v[0], &v[1], &v[2], &v[3]);
    memcpy(out, v, sizeof v);
}
