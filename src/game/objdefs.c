/* The track's object definitions (see objdefs.h). */
#include "game/objdefs.h"
#include "exe.h"
#include "pending.h"
#include "vfs.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static WObjectDef **defs;   /* 0x628ba0 +0: the definitions */
static int ndefs, capdefs;
static WAudioDef **auds;   /* the holder's +4 */
static int nauds, capauds;

static bool kept(char c)
{
    return (c > '`' && c < '{') || (c > '@' && c < '[') || (c > '/' && c < ':') || c == ';' || c == '{' || c == '}' ||
           c == '=' || c == ':';
}

/* The text starts with the key (compared lowercased). 0x47d080 */
static bool match(const char *p, const char *key)
{
    for (; *key; key++, p++)
        if (*p != (char)tolower((unsigned char)*key)) return false;
    return true;
}

/* The value after "key" (and its '='), NULL unless the key is there and a ';' is still ahead. */
static const char *value(const char *p, const char *key)
{
    if (!match(p, key)) return NULL;
    const char *v = p + strlen(key);
    if (*v == '=') v++;
    return strchr(p, ';') ? v : NULL;
}

static const char *past_semicolon(const char *p) { return strchr(p, ';') + 1; }

/* key=<text>; into buf (0x47d410's copy); true when read. */
static bool str_value(const char **pp, const char *key, char *buf, size_t size)
{
    const char *v = value(*pp, key);
    if (!v) return false;
    size_t n = 0;
    while (*v != ';' && n + 1 < size) buf[n++] = *v++;
    buf[n] = 0;
    *pp = past_semicolon(*pp);
    return true;
}

static bool float_value(const char **pp, const char *key, float *out)   /* 0x47d360 */
{
    const char *v = value(*pp, key);
    if (!v) return false;
    *out = (float)atof(v);
    *pp = past_semicolon(*pp);
    return true;
}

static bool int_value(const char **pp, const char *key, int32_t *out)   /* 0x47d200 */
{
    const char *v = value(*pp, key);
    if (!v) return false;
    *out = atoi(v);
    *pp = past_semicolon(*pp);
    return true;
}

static bool bool_value(const char **pp, const char *key, uint8_t *out)   /* 0x47d140 */
{
    const char *v = value(*pp, key);
    if (!v) return false;
    *out = atoi(v) != 0;
    *pp = past_semicolon(*pp);
    return true;
}

/* A tag value: its first four characters in upper case. */
static bool tag_value(const char **pp, const char *key, uint32_t *out)
{
    char buf[256] = {0};
    if (!str_value(pp, key, buf, sizeof buf)) return false;
    uint8_t t[4];
    for (int i = 0; i < 4; i++) t[i] = (uint8_t)toupper((unsigned char)buf[i]);
    memcpy(out, t, 4);
    return true;
}

/* Past a section: to its '{', then to the character after its matching '}'. 0x47d4d0 */
static const char *skip_section(const char *p)
{
    while (kept(*p) && *p != '{') p++;
    int depth = 1;
    for (;;) {
        char c = *++p;
        if (!kept(c) || depth == 0) break;
        if (c == '{') depth++;
        if (c == '}') depth--;
    }
    return p;
}

/* 0x47e680 */
static const char *parse_piece(const char *p, WObjectPiece *pc)
{
    const char *head = exe_str(0x5d0434);   /* "section:piece{" */
    if (!match(p, head)) return NULL;
    p += strlen(head);
    while (*p != '}') {
        char buf[256];
        if (tag_value(&p, exe_str(0x5d0428), &pc->basegroup)) continue;    /* basegroup */
        if (tag_value(&p, exe_str(0x5d041c), &pc->quatgroup)) continue;    /* quatgroup */
        if (tag_value(&p, exe_str(0x5d0410), &pc->vertgroup)) continue;    /* vertgroup */
        if (float_value(&p, exe_str(0x5d0400), &pc->playbackrate)) continue;   /* playbackrate */
        if (str_value(&p, exe_str(0x5d03f4), buf, sizeof buf)) {           /* properties */
            if (match(buf, exe_str(0x5d03ec))) pc->flags |= exe_u32(0x5b45a4);   /* LOOP */
            if (match(buf, exe_str(0x5d03e0))) pc->flags |= exe_u32(0x5b45b0);   /* LANDFLAT */
            if (match(buf, exe_str(0x5d03d4))) pc->flags |= exe_u32(0x5b45ac);   /* DIRECTED */
            continue;
        }
        /* An unknown key: the original stays on it for ever; the port steps over it. */
        const char *q = strchr(p, ';');
        if (!q) return NULL;
        p = q + 1;
    }
    return p + 1;
}

static void push_def(WObjectDef *d)   /* 0x47eec0 */
{
    if (ndefs == capdefs) {
        int n = capdefs ? capdefs * 2 : 1;
        WObjectDef **nd = realloc(defs, (size_t)n * sizeof *nd);
        if (!nd) return;
        defs = nd;
        capdefs = n;
    }
    defs[ndefs++] = d;
}

/* The kind named by a value among the strings at vas (their index, or `keep` for none). */
static int32_t named(const char *v, const uint32_t *vas, const int32_t *vals, int n, int32_t keep)
{
    for (int i = 0; i < n; i++)
        if (match(v, exe_str(vas[i]))) return vals[i];
    return keep;
}

/* 0x47dbb0 */
static const char *parse_anim(const char *p)
{
    const char *head = exe_str(0x5d03c4);   /* "section:anim{" */
    if (!match(p, head)) return NULL;
    p += strlen(head);
    WObjectDef *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->radius = 0.5f;
    d->tickspervertframe = 8;
    for (;;) {
        if (*p == '}') break;
        char buf[256];
        if (match(p, exe_str(0x5d03b4))) {   /* section:piece */
            WObjectPiece *pc = calloc(1, sizeof *pc);
            if (!pc) break;
            pc->playbackrate = 1.0f;
            if (d->npieces < WOBJ_MAXPIECES) d->piece[d->npieces++] = pc;   /* (the original has no bound) */
            p = parse_piece(p, pc);
            if (d->nspeeds < 1) d->speedpieces[0]++;
            else d->speedpieces[d->nspeeds - 1]++;
            if (!p) {
                push_def(d);
                return NULL;
            }
            continue;
        }
        if (tag_value(&p, exe_str(0x5d01dc), &d->tag)) continue;   /* tag */
        float f;
        if (float_value(&p, exe_str(0x5d03ac), &f)) {               /* speed */
            if (d->nspeeds < WOBJ_MAXSPEEDS) d->speed[d->nspeeds] = f;
            d->nspeeds++;
            continue;
        }
        if (str_value(&p, exe_str(0x5d03a0), buf, sizeof buf)) {   /* collidetype */
            static const uint32_t names[] = {0x5d0394, 0x5d0388, 0x5d0380};   /* kSmackable, kCylinder, kBox */
            static const int32_t kinds[] = {3, 1, 2};
            d->collidetype = named(buf, names, kinds, 3, d->collidetype);
            continue;
        }
        if (str_value(&p, exe_str(0x5d0374), buf, sizeof buf)) {   /* audiotype */
            /* kAudSign .. kAudRock; Woodbox is looked for before Wood. */
            static const uint32_t names[] = {0x5d0368, 0x5d0358, 0x5d034c, 0x5d0340, 0x5d0334, 0x5d0328, 0x5d031c,
                                             0x5d0310, 0x5d0304, 0x5d02f8, 0x5d02f0, 0x5d02e4, 0x5d02d8};
            static const int32_t kinds[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
            d->audiotype = named(buf, names, kinds, 13, d->audiotype);
            continue;
        }
        if (float_value(&p, exe_str(0x5d02d0), &d->mass)) continue;   /* mass */
        if (float_value(&p, exe_str(0x5d02c0), &d->velocityfactor)) {   /* velocityfactor */
            d->velocityfactor = d->velocityfactor * exe_f32(0x5b3060);
            continue;
        }
        if (int_value(&p, exe_str(0x5d02b0), &d->frametickoffset) || int_value(&p, exe_str(0x5d029c), &d->tickspervertframe) ||
            float_value(&p, exe_str(0x5d0294), &d->radius) || bool_value(&p, exe_str(0x5d0288), &d->triggerable) ||
            bool_value(&p, exe_str(0x5d027c), &d->drivethru) || bool_value(&p, exe_str(0x5d026c), &d->drivethru) ||
            bool_value(&p, exe_str(0x5d025c), &d->dynamiclight))
            continue;
        /* An unknown key: past the next ';', or with none left past the next '}'. */
        const char *q = strchr(p, ';');
        if (!q) {
            q = strchr(p, '}');
            if (!q) break;
        }
        p = q + 1;
    }
    push_def(d);
    return p + 1;
}

void WObjectDefs_free(void)
{
    for (int i = 0; i < nauds; i++) free(auds[i]);
    free(auds);
    auds = NULL;
    nauds = capauds = 0;
    for (int i = 0; i < ndefs; i++) {
        for (int k = 0; k < defs[i]->npieces; k++) free(defs[i]->piece[k]);
        free(defs[i]);
    }
    free(defs);
    defs = NULL;
    ndefs = capdefs = 0;
}

/* ---- the audio sections (the holder's +4) ---- */

static bool short_value(const char **pp, const char *key, int16_t *out)   /* 0x47d2b0 */
{
    int32_t v;
    if (!int_value(pp, key, &v)) return false;
    *out = (int16_t)v;
    return true;
}

/* A section:sound (0x18 bytes, WMidGround_ParseSound 0x47d840). */
static const char *parse_sound(const char *p, WSoundDef *d)
{
    const char *head = exe_str(0x5d024c);   /* "section:sound{" */
    if (!match(p, head)) return NULL;
    p += strlen(head);
    while (*p != '}') {
        if (short_value(&p, exe_str(0x5d0244), &d->patch) || float_value(&p, exe_str(0x5d023c), &d->range) ||
            float_value(&p, exe_str(0x5d0234), &d->fadein) || short_value(&p, exe_str(0x5d022c), &d->trigger) ||
            short_value(&p, exe_str(0x5d0224), &d->fadeout) || short_value(&p, exe_str(0x5d0218), &d->minrepeat) ||
            short_value(&p, exe_str(0x5d0208), &d->randomrepeat) || short_value(&p, exe_str(0x5d0200), &d->volume))
            continue;
        const char *q = strchr(p, ';');
        if (!q) {
            q = strchr(p, '}');
            if (!q) return NULL;
            return q + 1;
        }
        p = q + 1;
    }
    return p + 1;
}

/* A section:audio (0x28 bytes, WMidGround_ParseAudio 0x47d570): its tag and up to eight sounds. */
static const char *parse_audio(const char *p)
{
    const char *head = exe_str(0x5d01f0);   /* "section:audio{" */
    if (!match(p, head)) return NULL;
    p += strlen(head);
    WAudioDef *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    while (*p && *p != '}') {
        if (match(p, exe_str(0x5d01e0))) {   /* section:sound */
            WSoundDef *d = a->n < WAUD_MAXSOUNDS ? &a->sound[a->n] : NULL;
            WSoundDef spare;
            memset(&spare, 0, sizeof spare);
            const char *q = parse_sound(p, d ? d : &spare);
            if (!q) break;
            if (d) a->n++;
            p = q;
            continue;
        }
        if (tag_value(&p, exe_str(0x5d01dc), &a->tag)) continue;   /* tag */
        const char *q = strchr(p, ';');
        if (!q) break;
        p = q + 1;
    }
    if (nauds == capauds) {
        int n = capauds ? capauds * 2 : 4;
        WAudioDef **na = realloc(auds, (size_t)n * sizeof *na);
        if (!na) {
            free(a);
            return *p ? p + 1 : p;
        }
        auds = na;
        capauds = n;
    }
    auds[nauds++] = a;
    return *p ? p + 1 : p;
}

const WAudioDef *WObjectDefs_findaudio(uint32_t type)   /* 0x47ecb0 */
{
    for (int i = 0; i < nauds; i++)
        if (auds[i]->tag == type) return auds[i];
    return NULL;
}

void WMidGround_LoadObjectDefs(const char *path)
{
    WObjectDefs_free();   /* (the original makes a new holder each time, 0x47ed00) */
    size_t len = 0;
    char *text = (char *)vfs_read_all(path, &len);
    if (!text) return;
    char *buf = calloc(1, len + 1);   /* "objectDefs" */
    if (!buf) {
        free(text);
        return;
    }
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        if (kept(c)) buf[n++] = (char)tolower((unsigned char)c);
        if (c == '/' && i + 1 < len && text[i + 1] == '/')
            while (i + 1 < len && text[i] != '\r' && text[i + 1] != '\n') i++;   /* to the line's end */
    }
    const char *p = buf;
    while (p && p < buf + n) {
        if (match(p, exe_str(0x5d01cc))) {   /* section:anim */
            p = parse_anim(p);
        } else if (match(p, exe_str(0x5d01bc))) {   /* section:audio */
            const char *q = parse_audio(p);
            p = q ? q : skip_section(p);
        } else {
            p = skip_section(p);
        }
    }
    free(buf);
    free(text);
}

const WObjectDef *WObjectDefs_find(uint32_t type)
{
    if (!defs || !type) return NULL;
    for (int i = 0; i < ndefs; i++)
        if (defs[i]->tag == type) return defs[i];
    return ndefs ? defs[0] : NULL;
}

int WObjectDefs_count(void) { return ndefs; }

const WObjectDef *WObjectDefs_at(int i) { return i >= 0 && i < ndefs ? defs[i] : NULL; }
