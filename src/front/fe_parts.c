/* The front end's side of FERT's parts and controls (fe_parts.h). */
#include "front/fe_parts.h"
#include "front/fe2d.h"
#include "real/font.h"
#include "real/loc.h"
#include "vfs.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- fonts (0x4e2fb0) */

typedef struct {
    char path[260];
    uint8_t *file;     /* the record's +0 */
    HudFont *drawer;   /* +4 (0x536540) */
    int refs;          /* +0x20 */
} FontEntry;

static FontEntry *s_fonts;   /* 0x6907f8 (a map by path) */
static int s_nfonts;

HudFont *FEFont_Get(const char *path)
{
    char key[260];
    snprintf(key, sizeof key, "%s", path ? path : "");
    for (char *p = key; *p; p++) *p = (char)tolower((unsigned char)*p);   /* (crt 0x5af324) */
    for (int i = 0; i < s_nfonts; i++)
        if (!strcmp(s_fonts[i].path, key)) {
            s_fonts[i].refs++;
            return s_fonts[i].drawer;
        }
    size_t n = 0;
    uint8_t *file = vfs_read_all(key, &n);   /* 0x4e3300 -> 0x59d690 */
    if (!file) return NULL;
    Font *f = FONT_open(file);
    HudFont *d = f ? HudFont_new(f) : NULL;
    if (!d) {
        if (f) FONT_close(f);
        free(file);
        return NULL;
    }
    TextureStates_set(&d->states, 1, 0);   /* 0x5377b0(0) */
    FontEntry *a = realloc(s_fonts, (size_t)(s_nfonts + 1) * sizeof *a);
    if (!a) return d;
    s_fonts = a;
    FontEntry *e = &a[s_nfonts++];
    snprintf(e->path, sizeof e->path, "%s", key);
    e->file = file;
    e->drawer = d;
    e->refs = 1;
    return d;
}

void FEFont_Release(HudFont *f)
{
    for (int i = 0; i < s_nfonts; i++)
        if (s_fonts[i].drawer == f) {
            if (--s_fonts[i].refs > 0) return;
            FONT_close(f->font);   /* 0x4e3390 */
            free(f);
            free(s_fonts[i].file);
            s_fonts[i] = s_fonts[--s_nfonts];
            return;
        }
}

/* ---------------------------------------------------------------- strings (0x4e0090, 0x4e0270) */

static uint8_t *s_strings;   /* 0x690714: the list has one table */

void FEStrings_Load(int language)
{
    free(s_strings);
    size_t n = 0;
    s_strings = vfs_read_all(FE_DIR_LOCALE "FEStrings.loc", &n);
    if (language >= 7) language = 0;
    else if (language < 0) language = 6;
    if (s_strings) LOC_setlanguage(s_strings, 0, language);   /* 0x536af0 */
}

const char *FEStrings_Get(int id)
{
    if (id == 0) return "";
    const char *s = s_strings ? LOC_string(s_strings, id) : NULL;
    return s ? s : "Not Found";
}

/* ---------------------------------------------------------------- paths (0x52caa0) */

void FE_ResolvePath(const char *name, char *out, int cap)
{
    if (!name) name = "";
    bool absolute = !*name || (isalpha((unsigned char)name[0]) && name[1] == ':') || (name[0] == '\\' && name[1] == '\\');
    if (absolute) snprintf(out, (size_t)cap, "%s", name);
    else snprintf(out, (size_t)cap, "%s%s", FE_DIR_ART, name);
}

/* ---------------------------------------------------------------- text parts */

static const char *attr_str(const FertPart *p, const char *name)
{
    FertAttr *a = FertAttrList_find(&p->attrs, name);
    return a ? FertAttr_str(a) : "";
}

/* The text a part shows: a localized part's string of its stringID (0x4e5d80), else its text. */
static const char *part_text(const FertPart *p)
{
    if (p->kind == FPART_LOCALIZEDTEXT) return FEStrings_Get(atoi(attr_str(p, "stringID")));
    return attr_str(p, "text");
}

static HudFont *part_font(const FertPart *p)
{
    char path[300];
    FE_ResolvePath(attr_str(p, "fontPathName"), path, sizeof path);
    /* (the original keeps the drawer in the part and releases it when the path changes; the port's
       cache keeps every font) */
    for (int i = 0; i < s_nfonts; i++) {
        char key[260];
        snprintf(key, sizeof key, "%s", path);
        for (char *q = key; *q; q++) *q = (char)tolower((unsigned char)*q);
        if (!strcmp(s_fonts[i].path, key)) return s_fonts[i].drawer;
    }
    return FEFont_Get(path);
}

static bool measure(const FertPart *p, int32_t *w, int32_t *h)
{
    HudFont *f = part_font(p);
    if (!f || !f->font) return false;
    float x = 0, y = 0, fw = 0, fh = 0;
    FONT_extent(f->font, part_text(p), &x, &y, &fw, &fh);   /* 0x5366e0 */
    *w = (int32_t)fw;   /* (__ftol) */
    *h = (int32_t)fh;
    return true;
}

void FEParts_Init(void) { Fert_MeasureText = measure; }

/* ---------------------------------------------------------------- draws */

static void pt(const FertPart *p, const char *name, int32_t out[2])
{
    FertAttr *a = FertAttrList_find(&p->attrs, name);
    out[0] = out[1] = 0;
    if (a) FertAttr_point(a, out);
}

static uint32_t colour(const FertPart *p, const char *name)
{
    FertAttr *a = FertAttrList_find(&p->attrs, name);
    return a ? FertAttr_color(a) : 0;
}

void FEPart_Draw(FertPart *p, int32_t ox, int32_t oy)
{
    int32_t pos[2], size[2];
    pt(p, "pos", pos);
    pt(p, "size", size);
    switch (p->kind) {
    case FPART_RECT:      /* 0x4e5f60 (the fade, 0x4e2e40 / 0x4c8410, pending) */
        FE2D_Rect((float)(pos[0] + ox), (float)(pos[1] + oy), (float)size[0], (float)size[1], colour(p, "color"));
        break;
    case FPART_TEXT:
    case FPART_LOCALIZEDTEXT: {   /* 0x4e69c0 */
        HudFont *f = part_font(p);
        if (!f) break;
        const char *s = part_text(p);
        int n = (int)strlen(s) + 1;
        char *copy = FE2D_Alloc(n);   /* (the queue keeps the pointer until its flush) */
        if (!copy) break;
        memcpy(copy, s, (size_t)n);
        FE2D_Text(f, (float)(pos[0] + ox), (float)(pos[1] + oy), 0, colour(p, "foreColor"), copy, 1.0f, 1.0f);
        break;
    }
    default:
        break;
    }
}

void FEControl_Draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (c->state < 0 || c->nstates <= c->state) return;
    FertAttr *pa = FertAttrList_find(&c->attrs, "pos");
    int32_t pos[2] = {0, 0};
    if (pa) FertAttr_point(pa, pos);
    const FertState *s = &c->states[c->state];
    for (int i = 0; i < s->nparts; i++) FEPart_Draw(c->parts[s->parts[i]], pos[0] + ox, pos[1] + oy);   /* 0x529e70 */
}

void FEWindow_Draw(FertControl *w)
{
    if (w->screen) FEControl_Draw(w->screen, 0, 0);
    for (int i = w->nchildren - 1; 0 <= i; i--) FEControl_Draw(w->children[i], 0, 0);   /* 0x528390: last first */
}
