/* The sky's layer description (see sky_text.h). */
#include "render/sky_text.h"
#include "exe.h"
#include "game/track.h"
#include "platform.h"
#include "real/mem.h"
#include "support/ufile.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cuts [p, end) into tokens in place: blanks before a token skipped, a token runs to CR, LF or a tab (not
   a space), each closed with a NUL; the length of what was kept. 0x45f9d0 */
static int tokenize(char *p, char *end)
{
    char *out = p, *in = p;
    for (; in < end; in++) {
        while ((*in == '\n' || *in == '\r' || *in == ' ' || *in == '\t') && in < end) in++;
        while (*in != '\n' && *in != '\r' && *in != '\t') {
            if (in >= end) goto done;
            *out++ = *in++;
        }
        if (in >= end) break;
        *out++ = 0;
    }
done:
    if (out[-1] != 0) *out++ = 0;
    return (int)(out - p);
}

/* The text's cursor to the next token that isn't a comment (inline in 0x45fa40). */
static void next_token(SkyText *t)
{
    do {
        while (*t->cur && t->cur < t->end) t->cur++;
        if (t->cur < t->end) t->cur++;
    } while (*t->cur == '#' && t->cur < t->end);
}

void SkyRecord_next(SkyRecord *r)
{
    do r->cur += strlen(r->cur) + 1;
    while (*r->cur == '#');
}

/* Whether some suffix of tok starts with name (the original's scan with strncmp at every position). */
static int contains(const char *tok, const char *name)
{
    size_t n = strlen(name);
    for (const char *p = tok; *p; p++)
        if (!strncmp(name, p, n)) return 1;
    return 0;
}

static uint32_t type_flags(const char *tok)
{
    /* The names and flags in the order 0x45fa40 compares them (strings 0x5cf40c..0x5cf4fc). */
    static const struct { const char *name; uint32_t flags; } types[] = {
        {"basic", SKYREC_BASIC},
        {"multinormal", SKYREC_MULTINORMAL},
        {"additive", SKYREC_ADDITIVE},
        {"multiadditive", SKYREC_MULTIADDITIVE},
        {"multiattenuate", SKYREC_MULTIATTENUATE},
        {"multimodulate", SKYREC_MULTIMODULATE},
        {"freecloud", SKYREC_FREECLOUD},
        {"config", SKYREC_CONFIG},
        {"basicwater", SKYREC_WATER | SKYREC_BASIC},
        {"multinormalwater", SKYREC_WATER | SKYREC_MULTINORMAL},
        {"multiadditivewater", SKYREC_WATER | SKYREC_MULTIADDITIVE},
        {"multiattenuatewater", SKYREC_WATER | SKYREC_MULTIATTENUATE},
        {"multimodulatewater", SKYREC_WATER | SKYREC_MULTIMODULATE},
    };
    for (size_t i = 0; i < sizeof types / sizeof *types; i++)
        if (!strcmp(tok, types[i].name)) {
            if (types[i].flags == SKYREC_ADDITIVE) plat_log(exe_str(0x5cf4ac));   /* "Additive mode no longer supported" */
            return types[i].flags;
        }
    return SKYREC_UNKNOWN;
}

SkyText *Sky_LoadText(SkyText *t, const char *name, int level)
{
    memset(t, 0, sizeof *t);
    char path[200];
    snprintf(path, sizeof path, "%sSky\\%s.txt", g_track_dir, name);       /* 0x5cf518 */
    t->buf = UTIL_loadfile(path, 0);
    if (!t->buf) {
        snprintf(path, sizeof path, "%sSky\\default.txt", g_track_dir);   /* 0x5cf504 */
        t->buf = UTIL_loadfile(path, 0);
        if (!t->buf) return t;
    }
    int size = MEM_size(t->buf);
    if (!size) return t;
    t->len = tokenize(t->buf, t->buf + size);
    t->end = t->buf + t->len;
    for (char *p = t->buf; p < t->end; p++) *p = (char)tolower((unsigned char)*p);
    t->cur = t->buf;
    const char *capability = exe_str(0x5cf3a0 + 0x14u * (unsigned)level), *all = exe_str(0x5cf3f0);
    while (t->cur < t->end) {
        next_token(t);                        /* past "begin" */
        if (contains(t->cur, capability) || contains(t->cur, all)) {
            next_token(t);
            uint32_t flags = type_flags(t->cur);
            next_token(t);
            SkyRecord *r = malloc(sizeof *r);
            if (r) *r = (SkyRecord){flags, t->cur, t->cur};
            /* The original doesn't bound the 20 slots; the data never has more. */
            if (t->count < SKY_MAXRECORDS) t->rec[t->count++] = r;
            else free(r);
        }
        while (strncmp(t->cur, exe_str(0x5cf404), 5) != 0 && t->cur < t->end) next_token(t);   /* "begin" */
    }
    return t;
}

void SkyText_free(SkyText *t)
{
    for (int i = 0; i < t->count; i++) free(t->rec[i]);
    MEM_purge(t->buf);
    memset(t, 0, sizeof *t);
}
