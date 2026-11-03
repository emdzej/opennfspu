/* The front end's side of FERT's parts and controls (fe_parts.h). */
#include "front/fe_parts.h"
#include "pending.h"
#include "front/fe2d.h"
#include "front/fetrans.h"
#include "real/font.h"
#include "real/loc.h"
#include "real/shape.h"
#include "real/window.h"
#include "render/render.h"
#include "app.h"
#include "exe.h"
#include "game/options.h"
#include "support/crt.h"
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

/* ---------------------------------------------------------------- shape banks (0x4d67f0 .. 0x4d74e0) */

typedef struct {
    char path[260];       /* lowercased */
    int refs;             /* +0x04 */
    FeImage **images;     /* +0x08 */
    int count;            /* +0x0c */
    Texture *pages[64];   /* the map of page textures (+0x00) */
    uint8_t *file;
} Bank;

static Bank *s_banks;     /* 0x65b938 (a map by path) */
static int s_nbanks;

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static int16_t rd16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }
/* a shape's place in the file's canvas: the low twelve bits of +0xc, and bits 16..27 (signed) */
static int shape_x(const uint8_t *sh) { return (int32_t)((uint32_t)rd32(sh + 0xc) << 20) >> 20; }
static int shape_y(const uint8_t *sh) { return (int32_t)((uint32_t)rd32(sh + 0xc) << 4) >> 20; }

void FEGoTo_Draw(FertControl *c, int32_t ox, int32_t oy);
bool (*FE_ControlDrawHook)(FertControl *c, int32_t ox, int32_t oy);
static void FEGouraudBox_Draw(FertControl *c, int32_t ox, int32_t oy);

static void lower(char *dst, size_t cap, const char *src)
{
    snprintf(dst, cap, "%s", src ? src : "");
    for (char *p = dst; *p; p++) *p = (char)tolower((unsigned char)*p);
}

static Bank *find_bank(const char *key)
{
    for (int i = 0; i < s_nbanks; i++)
        if (!strcmp(s_banks[i].path, key)) return &s_banks[i];
    return NULL;
}

/* The window depth of a shape's type (unknown types are reported and leave the last one). */
static int shape_depth(const uint8_t *sh, int last)
{
    switch (sh[0] & 0x7f) {
    case 0x6d: return 0x115c;
    case 0x78: return 0x10;
    case 0x7b: return SHAPE_typevalid(SHAPE_clut(sh));   /* 0x537d00 */
    case 0x7d: return 0x20;
    case 0x7e: return 0xf;
    default: return last;
    }
}

void FEShapes_Load(const char *path)
{
    char key[260];
    lower(key, sizeof key, path);
    Bank *b = find_bank(key);
    if (b) {
        b->refs++;
        return;
    }
    size_t n = 0;
    uint8_t *file = vfs_read_all(key, &n);   /* 0x59f630 .. 0x59f940 */
    if (!file) return;
    Bank *a = realloc(s_banks, (size_t)(s_nbanks + 1) * sizeof *a);
    if (!a) {
        free(file);
        return;
    }
    s_banks = a;
    b = &a[s_nbanks++];
    memset(b, 0, sizeof *b);
    snprintf(b->path, sizeof b->path, "%s", key);
    b->file = file;
    b->refs = 1;
    int count = (int)SHAPE_count(file);   /* 0x537ca0 */
    b->count = count;
    b->images = calloc((size_t)(count > 0 ? count : 1), sizeof *b->images);
    uint8_t *placed = calloc((size_t)(count > 0 ? count : 1), 1);
    if (!b->images || !placed) {
        free(placed);
        return;
    }
    TexManager *tm = g_render ? g_render->textures : NULL;
    /* Pages of 256 x 256 over the file's 2048 x 2048 canvas, by the shapes' places (across, then down
       each column of pages), until every shape is in one. */
    bool all = false;
    for (int px = 0; px < 8 && !all; px++)
        for (int py = 0; py < 8 && !all; py++) {
            Window *w = NULL;
            int depth = 0;
            bool any = false;
            for (int i = 0; i < count; i++) {
                const uint8_t *sh = file + rd32(file + 0x14 + 8 * i);
                int x = shape_x(sh), y = shape_y(sh);
                if (!(px * 256 <= x && x < px * 256 + 256 && py * 256 <= y && y < py * 256 + 256)) continue;
                int d = shape_depth(sh, depth);
                if (!any) {
                    any = true;
                    depth = d;
                }
                placed[i] = 1;
                if (!w) {
                    /* 0x5356c0. (An 8-bit page would get a copy of the first shape's clut: the depth is a
                       clut's, 15, 24 or 32 by 0x5de890, never 8, so the original never takes that way.) */
                    w = WINDOW_new(256, 256, depth, 0);
                    if (!w) break;
                    WINDOW_select(w);
                    WINDOW_clear(0);   /* (0x537c40(0): black) */
                }
                WINDOW_drawshape(sh, x - px * 256, y - py * 256);   /* 0x53d2e0 */
                FeImage *im = calloc(1, sizeof *im);
                if (!im) continue;
                int sw = rd16(sh + 4), shh = rd16(sh + 6);
                im->u0 = (float)(x - px * 256) * 0.00390625f;   /* 0x5b2640 */
                im->u1 = (float)(sw - px * 256 + x) * 0.00390625f;
                im->v0 = (float)(y - py * 256) * 0.00390625f;
                im->v1 = (float)((shh - py * 256) + y) * 0.00390625f;
                im->w = sw;
                im->h = shh;
                im->cx = -rd16(sh + 8);
                im->cy = -rd16(sh + 10);
                const char *nm = SHAPE_name(sh);   /* 0x56a6a0 ("" without one) */
                snprintf(im->name, sizeof im->name, "%s", nm ? nm : "");
                uint32_t tag = SHAPE_entryname(file, i);   /* 0x537cd0 */
                memcpy(im->tag, &tag, 4);
                b->images[i] = im;
            }
            if (w) {
                const char *base = strrchr(key, '\\');
                char tname[300];
                snprintf(tname, sizeof tname, "%s%02d", base ? base + 4 : key, px + 8 * py);   /* "%s%02d" (base+4, as is) */
                Texture *t = calloc(1, sizeof *t);
                if (t && tm) {
                    Texture_fromshape(t, w->shape, tm, 0, tname);   /* TEXTURE_name */
                    TextureStates st;
                    TextureStates_init(&st);
                    TextureStates_set(&st, 1, 1);
                    TextureStates_settile(&st, 0);
                    Texture_setstates(t, &st);   /* 0x5361e0 (0x65b934) */
                    b->pages[px + 8 * py] = t;
                    for (int i = 0; i < count; i++)
                        if (b->images[i] && !b->images[i]->tex) b->images[i]->tex = t;
                } else {
                    free(t);
                }
                WINDOW_deselect();
                WINDOW_destroy(w);   /* 0x533cf0 */
            }
            all = true;
            for (int i = 0; i < count; i++)
                if (!placed[i]) all = false;
        }
    free(placed);
}

FeImage *FEShapes_Image(const char *path, int index)
{
    char key[260];
    lower(key, sizeof key, path);
    Bank *b = find_bank(key);
    return b && 0 <= index && index < b->count ? b->images[index] : NULL;
}

FeImage *FEShapes_Named(const char *path, const char *name)
{
    char key[260];
    lower(key, sizeof key, path);
    Bank *b = find_bank(key);
    if (!b || !b->file) return NULL;
    /* the directory's entries after the 16-byte header: a 4-character name and an offset */
    size_t n = strlen(name);
    for (int i = 0; i < b->count; i++) {
        const char *e = (const char *)b->file + 0x10 + 8 * i;
        if (n <= 4 && !strncmp(e, name, n) && (n == 4 || e[n] == 0)) return b->images[i];
    }
    return NULL;
}

void FEShapes_Release(const char *path)
{
    char key[260];
    lower(key, sizeof key, path);
    Bank *b = find_bank(key);
    if (!b || --b->refs > 0) return;
    for (int i = 0; i < b->count; i++) free(b->images[i]);
    free(b->images);
    free(b->file);
    /* (the page textures stay with the texture manager) */
    *b = s_banks[--s_nbanks];
}

/* ---------------------------------------------------------------- shape parts (0x52aa90, 0x4e6140) */

static void shape_eval(FertPart *p)
{
    if (p->kind != FPART_SHAPE) return;
    FertAttr *pa = FertAttrList_find(&p->attrs, "shapePathName"), *ia = FertAttrList_find(&p->attrs, "shapeIndex");
    char path[300];
    FE_ResolvePath(pa ? FertAttr_str(pa) : "", path, sizeof path);
    int32_t index = ia ? FertAttr_int(ia) : 0;
    if (p->image && !strcmp(path, p->bank) && index == p->bankindex) return;   /* unchanged */
    if (index < 0) return;
    /* FEShapePart_v8 0x4e6140: with a low-end card (FELOWENDCARD, 0x657e60), the rooms' detail off or
       under 33 MB (0x4e2da0, 0x4e2db0: the port has neither), the white backgrounds and big pieces
       named as written (optwht2.fsh, careerwhtbg.fsh, .., whtrkrec.fsh: 0x5da2e4 .. 0x5da230) are
       hidden, not loaded */
    p->hidden = false;
    if (Options_Int(0x5d5d8c, 0) != 0) {
        static const uint32_t names[12] = {0x5da2e4, 0x5da2d4, 0x5da2c0, 0x5da2b0, 0x5da2a0, 0x5da28c,
                                           0x5da27c, 0x5da26c, 0x5da25c, 0x5da24c, 0x5da240, 0x5da230};
        const char *raw = pa ? FertAttr_str(pa) : "";
        for (int i = 0; i < 12; i++)
            if (!strcmp(raw, exe_str(names[i]))) {
                p->hidden = true;
                return;
            }
    }
    if (p->bank[0]) FEShapes_Release(p->bank);   /* 0x4d7270 */
    snprintf(p->bank, sizeof p->bank, "%s", path);
    p->bankindex = index;
    FEShapes_Load(path);                          /* 0x4d67f0 */
    FeImage *im = FEShapes_Image(path, index);    /* 0x4d74e0 */
    p->image = im;
    FertAttr *sz = FertAttrList_find(&p->attrs, "size");
    if (im && sz) FertAttr_setPoint(sz, im->w, im->h);   /* 0x527310 */
}

static void part_free(FertPart *p)
{
    if (p->bank[0]) FEShapes_Release(p->bank);
}

/* ---------------------------------------------------------------- text parts */

static const char *attr_str(const FertPart *p, const char *name)
{
    FertAttr *a = FertAttrList_find(&p->attrs, name);
    return a ? FertAttr_str(a) : "";
}

static const char *part_text(const FertPart *p);

/* The text a part shows: a localized part's string of its stringID (0x4e5d80), else its text. */
const char *FEPart_Text(const FertPart *p) { return part_text(p); }
HudFont *FEPart_Font(const FertPart *p);

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

HudFont *FEPart_Font(const FertPart *p) { return part_font(p); }

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

void FEParts_Init(void)
{
    Fert_MeasureText = measure;
    Fert_PartEvalHook = shape_eval;
    Fert_PartFreeHook = part_free;
}

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
    case FPART_RECT:      /* 0x4e5f60 (the fade, 0x4e2e40 / 0x4c8410: g_fe_fade's) */
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
    case FPART_SHAPE: {   /* 0x4e6600 */
        FertAttr *fl = FertAttrList_find(&p->attrs, "flip");
        if (!p->hidden && p->image)
            FE2D_Image(p->image, (float)(pos[0] + ox), (float)(pos[1] + oy), (float)((FeImage *)p->image)->w,
                       (float)((FeImage *)p->image)->h, 0xffffffffu /* (the control's colour, +0x120) */,
                       fl ? (uint32_t)FertAttr_int(fl) : 0, false);
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

/* A window at its origin: its own position (a panel's screen's is the panel's) on its parent's. */
static void draw_control(FertControl *c, int32_t ox, int32_t oy);
static void FERadioItem_Draw(FertControl *c, int32_t ox, int32_t oy);

static void draw_at(FertControl *w, int32_t ox, int32_t oy)
{
    int32_t wp[2] = {0, 0};
    FertAttr *pa = FertAttrList_find(&w->attrs, "pos");
    if (pa) FertAttr_point(pa, wp);
    ox += wp[0], oy += wp[1];
    /* (its controls by its transition; a window never put on or off (a panel's screen) by the one it is
       in, else none) */
    const FertTrans *outer = FETrans_Window();
    FETrans_SetWindow(w->trans.live ? &w->trans : outer);
    /* (the screen object, the layout's first section, holds the screen's settings: room, callback list,
       music; the window's draw doesn't draw it) */
    for (int i = w->nchildren - 1; 0 <= i; i--) {   /* 0x528390: last first */
        FertControl *c = w->children[i];
        if (!strcmp(c->id, "NFS5.3DCAR")) continue;
        if (!strcmp(c->id, "NFS5.PANEL") || !strcmp(c->id, "NFS5.RADIOGROUP")) {
            /* FEPanel 0x4ce720: its screen when open (its area is the layout editor's) */
            if (c->sub && !(c->feflags & 0x20)) draw_at(c->sub, ox, oy);
            continue;
        }
        /* (FECar3DControl_v15 0x4c0570 draws none of its parts: its car is in the room) */
        /* each at its transition's fade and offset (0x4c8410, 0x4c8330), which its draw takes */
        int32_t dx = 0, dy = 0;
        FETrans_Offset(c, &dx, &dy);
        g_fe_fade = FETrans_Fade(c);
        draw_control(c, ox + dx, oy + dy);
        g_fe_fade = 0xff;
    }
    FETrans_SetWindow(outer);
}

static void draw_control(FertControl *c, int32_t ox, int32_t oy)
{
    if (FE_ControlDrawHook && FE_ControlDrawHook(c, ox, oy)) return;
    if (!strcmp(c->id, "NFS5.MOVIECIRCLE")) return;   /* (none of its parts is drawn plainly: femovie.c) */
    if (!strcmp(c->id, "NFS5.GOURAUDBOX")) FEGouraudBox_Draw(c, ox, oy);
    else if (!strcmp(c->id, "NFS5.GlintyRectangle")) FEGlintyBox_Draw(c, ox, oy);
    else if (!strcmp(c->id, "NFS5.GOTOBUTTON")) FEGoTo_Draw(c, ox, oy);
    else if (!strcmp(c->id, "NFS5.TABBUTTON") || !strcmp(c->id, "NFS5.GRAPHICTABBUTTON")) FERadioItem_Draw(c, ox, oy);
    else FEControl_Draw(c, ox, oy);
}

void FEWindow_Draw(FertControl *w) { draw_at(w, 0, 0); }

/* ---------------------------------------------------------------- the front end's styles (0x520780 ..) */

/* A colour's alpha scaled by a (0x4d82a0; 0xff keeps it). */
static uint32_t fade(uint32_t c, int a) { return a == 0xff ? c : (uint32_t)(((c >> 24) * (uint32_t)a) / 0xff) << 24 | (c & 0xffffffu); }

/* Whether the plain look is drawn (0x4e2dc0: the display can't modulate alpha, 0x6197aa). */
static bool plain(void) { return g_render_modalpha == 0; }

static void quad4(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1, float x2, float y2, uint32_t c2,
                  float x3, float y3, uint32_t c3)   /* 0x4d9380's order: two along the top, two along the bottom */
{
    const float p[8] = {x0, y0, x1, y1, x2, y2, x3, y3};
    const uint32_t c[4] = {c0, c1, c2, c3};
    FE2D_Quad(p, c);
}

/* The red gradient behind a bar (0x521180). */
void FEStyle_Shadow(float x, float y, float w, float h, int a)
{
    uint32_t l = fade(0xffe00c0cu, a), r = fade(0xff820000u, a);
    FE2D_Gouraud(x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f, l, r, l, r);
}

/* A shine over a bar (0x521090): light fading down over its top half, dark over the bottom one. */
void FEStyle_Shine(float x, float y, float w, float h, int a)
{
    if (plain()) {
        FE2D_Frame(x, y, w, h, (uint32_t)a << 24);
        return;
    }
    uint32_t c0 = fade(0xffffffu, a), c1 = fade(0x80ffffffu, a), c2 = fade(0x91000000u, a), c3 = fade(0, a);
    float hh = (h - 2.0f) * 0.5f;
    w -= 2.0f;
    x += 1.0f;
    FE2D_Gouraud(x, y + 1.0f, w, hh, c1, c1, c0, c0);
    FE2D_Gouraud(x, hh + y + 1.0f, w, hh, c3, c3, c2, c2);
}

/* A bevel of b pixels inside a bar (0x5211f0): light top and left, dark right and bottom. */
void FEStyle_Bevel(float x, float y, float w, float h, int a, int b)
{
    if (plain()) return;
    uint32_t hi = fade(0xccffffffu, a), hi2 = fade(0x33ffffffu, a), lo2 = fade(0x33000000u, a), lo = fade(0xcc000000u, a);
    float yb = y + h, fb = (float)b;
    float y1 = (yb + 1.0f) - 2.0f, xi = fb + x + 1.0f, x0 = x + 1.0f, yi = fb + y + 1.0f, y0 = y + 1.0f;
    quad4(x0, y0, hi, xi, yi, hi2, x0, y1, hi, xi, y1 - fb, hi2);   /* left */
    float xr = (x + w + 1.0f) - 1.0f;
    quad4(x0, y0, hi, xr, y0, hi2, xi, yi, hi, xr - fb, yi, hi2);   /* top */
    float xr2 = (x + w) - 1.0f;
    quad4(xr2 - fb, yi, lo2, xr2, y0, lo, xr2 - fb, y1 - fb, lo2, xr2, y1, lo);   /* right */
    float yb2 = (yb - fb) - 1.0f;
    quad4(xi, yb2, lo2, xr - fb, yb2, lo2, x0, yb - 1.0f, lo, xr, yb - 1.0f, lo);   /* bottom */
}

/* A drop shadow (dx, dy) out from a bar's right and bottom (0x520780). */
void FEStyle_DropShadow(float x, float y, float w, float h, int a, int dx, int dy)
{
    if (plain()) return;
    uint32_t c8 = fade(0x19000000u, a), c9 = fade(0xcc000000u, a), c10 = fade(0x0c000000u, a);
    float fy = (float)dy, y2 = fy + y, y3 = y2 + h, fx = (float)dx, x5 = fx + x, x6 = x5 + w;
    float yb = y + h, xr = x + w;
    quad4(xr, y2, c9, x6, y2, c8, xr, yb, c9, x6, y3, c8);
    float x1 = fx + fx + x;
    quad4(x1, yb, c9, xr, yb, c9, x5, y3, c8, x6, y3, c8);
    quad4(x5, yb, c10, x1, yb, c9, x5, y3, c10, x5, y3, c10);
    float y4 = fy + fy + y;
    quad4(xr, y2, c10, x6, y2, c10, xr, y4, c9, xr, y4, c9);
}

/* A hovered bar's inset (0x5208d0): dark top and left, light right and bottom. */
void FEStyle_Inset(float x, float y, float w, float h, int a, int b)
{
    if (plain()) return;
    uint32_t c11 = fade(0xcc000000u, a), c12 = fade(0x19000000u, a), c13 = fade(0x33ffffffu, a), c14 = fade(0xccffffffu, a);
    float fb = (float)b, y2 = fb + y + 1.0f, xr = x + w, x3 = (xr - fb) - 1.0f, x4 = fb + x, x5 = x4 + 1.0f;
    float y6 = y + 1.0f, x7 = xr - 1.0f, x8 = x + 1.0f, y9 = (y + h) - 1.0f;
    quad4(x8, y6, c11, x7, y6, c11, x5, y2, c12, x3, y2, c12);
    quad4(x8, y6, c11, x4, y2, c12, x8, y9, c11, x4, y9 - fb, c12);
    float y4 = ((y + h) - fb) - 1.0f;
    quad4(x3, y2, c13, x7, y6, c14, x3, y4, c13, x7, y9, c14);
    quad4(x5, y4, c13, (xr - fb) + 1.0f, y4, c13, x - 2.0f, y9, c14, xr - 2.0f, y9, c14);
}

/* A raised bevel of b pixels (0x520ad0): light top and left, dark right and bottom, each fading inwards. */
void FEStyle_Raised(float x, float y, float w, float h, int a, int b)
{
    if (plain()) return;
    uint32_t c6 = fade(0xe5ffffffu, a), c7 = fade(0x80ffffffu, a), c8 = fade(0x80000000u, a), c9 = fade(0xffffffu, a);
    uint32_t c10 = fade(0, a), c11 = fade(0xb2ffffffu, a), c12 = fade(0xccffffffu, a), c13 = fade(0xcc000000u, a);
    float fb = (float)b, y2 = fb + y, xr = x + w, x3 = xr - fb, x4 = fb + x, yb = y + h, y1 = yb - fb;
    quad4(x, y, c6, xr, y, c7, x4, y2, c9, x3, y2, c10);       /* top */
    quad4(x, y, c12, x4, y2, c9, x, yb, c11, x4, y1, c9);      /* left */
    quad4(x3, y2, c10, xr, y, c8, x3, y1, c10, xr, yb, c13);   /* right */
    quad4(x4, y1, c9, x3, y1, c10, x, yb, c8, xr, yb, c13);    /* bottom */
}

/* A bar of the share p0 .. p1 per cent of (x0, y0) .. (x1, y1) (0x521530): its part under 50 from c0 to
   c50, over 50 from c50 to c100, each half at most half the width; a drop shadow (3, 4) under it, or
   from its right end leftwards (-4, 4) when reversed. (Port: the ends' colours along the ramp by their
   share; the original's own blend steps are close to it.) */
void FEStyle_Range(int p0, int p1, int x0, int y0, int x1, int y1, uint32_t c0, uint32_t c50, uint32_t c100, int a,
                   bool shadow, bool reverse)
{
    if (p1 < p0) return;
    float W = (float)(x1 - x0), half = W * 0.5f, h = (float)(y1 - y0);
    c0 = fade(c0, a), c50 = fade(c50, a), c100 = fade(c100, a);
    float start = (float)p0 * W * 0.01f + (float)x0;
    float l1 = ((float)(p1 < 50 ? p1 : 50) - (float)p0) * W * 0.01f;
    l1 = l1 < 0 ? 0 : l1 > half ? half : l1;
    float l2 = ((float)p1 - (float)(p0 > 50 ? p0 : 50)) * W * 0.01f;
    l2 = l2 < 0 ? 0 : l2 > half ? half : l2;
    int q0 = p0 < 0 ? 0 : p0 > 50 ? 50 : p0, q1 = p1 < 50 ? p1 : 50;
    uint32_t s0 = FE2D_Mix(c0, c50, q0 * 255 / 50), s1 = FE2D_Mix(c0, c50, (q1 < 0 ? 0 : q1) * 255 / 50);
    int r0 = p0 > 50 ? p0 - 50 : 0, r1 = p1 > 100 ? 50 : p1 - 50;
    uint32_t t0 = FE2D_Mix(c50, c100, r0 * 255 / 50), t1 = FE2D_Mix(c50, c100, (r1 < 0 ? 0 : r1) * 255 / 50);
    if (0 < l1) FE2D_Gouraud(start, (float)y0, l1, h, s0, s1, s0, s1);
    if (0 < l2) FE2D_Gouraud(l1 + start, (float)y0, l2, h, t0, t1, t0, t1);
    float len = l1 + l2;
    if (4.0f < len && shadow) {
        if (reverse) FEStyle_DropShadow(len + start, (float)y0, -len, h, a, -4, 4);
        else FEStyle_DropShadow(start, (float)y0, len, h, a, 3, 4);
    }
}

/* An embossed knob (0x520e40): a blue to sand gradient, light top and left edges b pixels wide, dark
   bottom and right ones. */
void FEStyle_Emboss(float x, float y, float w, float h, int a, int b)
{
    if (plain()) return;
    uint32_t c6 = fade(0xe5ffffffu, a), c7 = fade(0x80ffffffu, a), c8 = fade(0xffffffu, a), c9 = fade(0, a);
    uint32_t c10 = fade(0x66000000u, a), c11 = fade(0x80808080u, a), c12 = fade(0x50000000u, a), c13 = fade(0x808080u, a);
    uint32_t c14 = fade(0x0c000000u, a), c15 = fade(0x800c0c0cu, a), c16 = fade(0xe5000000u, a);
    uint32_t top = fade(0xff9cd1ffu, a), bot = fade(0xfffedd81u, a);
    FE2D_Gouraud(x, y, w, h, top, top, bot, bot);
    float fb = (float)b, y2 = fb + y, xr = x + w, x3 = xr - fb, x4 = fb + x, yb = y + h, y1 = yb - fb;
    quad4(x, y, c6, xr, y, c11, x4, y2, c8, x3, y2, c8);       /* top */
    quad4(x4, y1, c13, x3, y1, c14, x, yb, c15, xr, yb, c16);  /* bottom */
    quad4(x, y, c6, x4, y2, c8, x, yb, c7, x4, y1, c8);        /* left */
    quad4(x3, y2, c9, xr, y, c10, x3, y1, c9, xr, yb, c12);    /* right */
}

void FEStyle_Glow(float x, float y, float w, float h, int a, int b)
{
    uint32_t in = fade(0xffffffffu, a), out = fade(0x80ffffffu, a);
    float fb = (float)b, xr = fb + x + w, x0 = x - fb, y0 = y - fb, yb = fb + y + h;
    quad4(x0, y0, out, xr, y0, out, x0 + fb, y, in, xr - fb, y, in);               /* top */
    quad4(x0 + fb, y + h, in, xr - fb, y + h, in, x0, yb, out, xr, yb, out);       /* bottom */
    quad4(x0, y0, out, x, y0 + fb, in, x0, yb, out, x, yb - fb, in);               /* left */
    quad4(x + w, y0 + fb, in, xr, y0, out, x + w, yb - fb, in, xr, yb, out);       /* right */
}

/* TIMER_ticks (0x53c050), 128 a second, on the port's clock. */
static uint32_t timer_ticks(void) { return (uint32_t)((uint64_t)app_ticks_ms() * 128u / 1000u); }

void FEGlint_Init(FEGlint *g, uint32_t colour, int w, int h, int mode)
{
    g->w = w, g->h = h, g->mode = mode, g->colour = colour;
    g->active = false;
    g->start = timer_ticks();
    g->dur = 0;
    g->cur = 0;
}

/* The glint (0x51f660): a run lasts 64 + rand() % 32 ticks once allowed after the last wait, then a wait
   of 256 + rand() % 256. During a run a streak len long (the least of (w + h) / 5, w - 1 and h - 1) runs
   round a corner: along a vertical edge for h, then along the bottom or top, while its twin runs along
   a horizontal edge for w, then down or up the other side; mode 1 from the top left, 2 the top right,
   3 the bottom left, 4 the bottom right. Each streak fades from transparent at its tail to the colour
   0xddffffff at its head (0x4d8300 by its length out of len), split where it turns the corner. (Where the
   tail is still on the edge the original mixes from the length itself taken as a colour: next to
   transparent black; kept.) */
void FEGlint_Draw(FEGlint *g, int px, int py, int a, bool allow)
{
    if (plain()) return;
    FE2D_Frame((float)px, (float)py, (float)g->w, (float)g->h, fade(g->colour, a));   /* 0x4d8cb0 */
    if (!g->mode) return;
    uint32_t t = timer_ticks();
    if (!g->active) {
        if ((int)(t - g->start) <= g->dur || !allow) return;
        g->active = true;
        g->start = t;
        g->dur = CRT_rand() % 32 + 0x40;
        g->cur = g->mode == 5 ? CRT_rand() % 4 + 1 : g->mode;
        return;
    }
    if (g->dur < (int)(t - g->start)) {
        g->active = false;
        g->start = t;
        g->dur = CRT_rand() % 256 + 0x100;
    }
    if (!g->active) return;
    int W = g->w, H = g->h, L = W + H;
    int len = L / 5;
    if (W - 1 <= len) len = W - 1;
    if (H - 1 < len) len = H - 1;
    if (len <= 0) return;   /* (the original divides by it) */
    int head = ((len + L) * (int)(t - g->start)) / g->dur - len;
    uint32_t C = fade(0xddffffffu, a);   /* 0x4d82a0 */

    /* the vertical streak and its turn along the far horizontal edge */
    int v = head, seg, turn;
    uint32_t top, bottom, tc0, tc1;
    if (H < v) {
        seg = 0;
        turn = L - v < len ? L - v : len;
        goto mix_v;
    } else if (len + v < H) {
        if (v < 0) {
            seg = len + v;
            v = 0;
        } else {
            seg = len;
        }
        turn = 0;
    mix_v:
        top = FE2D_Mix((uint32_t)seg, C, 0xff - seg * 0xff / len);
        bottom = C;
        tc0 = 0xffffff;
        tc1 = FE2D_Mix(0xffffffu, C, turn * 0xff / len);
    } else {
        seg = H - v;
        turn = len - seg;
        top = 0xffffff;
        bottom = FE2D_Mix(0xffffffu, C, seg * 0xff / len);
        tc0 = bottom;
        tc1 = C;
    }
    if (v < H) {
        float x = (float)(g->cur == 2 || g->cur == 4 ? px - 1 + W : px);
        float y = (float)(g->cur <= 2 ? py + v : py - v + H), hh = (float)(g->cur <= 2 ? seg : -seg);
        if (1 <= g->cur && g->cur <= 4) FE2D_Gouraud(x, y, 1.0f, hh, top, top, bottom, bottom);   /* 0x4d9160 */
    }
    if (H <= len + v && 1 <= g->cur && g->cur <= 4) {
        int off = v - H < 0 ? 0 : v - H;
        bool right = g->cur == 2 || g->cur == 4;
        float x = (float)(right ? px - off + W : px + off), w = (float)(right ? -turn : turn);
        float y = (float)(g->cur <= 2 ? py - 1 + H : py);
        FE2D_Gouraud(x, y, w, 1.0f, tc0, tc1, tc0, tc1);
    }

    /* the horizontal streak and its turn down (or up) the far vertical edge */
    int u = head, hseg;
    uint32_t left, right_c, ec0, ec1 = C;
    if (W < u) {
        hseg = 0;
        turn = L - u < len ? L - u : len;
        goto mix_h;
    } else if (u + len < W) {
        if (u < 0) {
            hseg = u + len;
            u = 0;
        } else {
            hseg = len;
        }
        turn = 0;
    mix_h:
        left = FE2D_Mix((uint32_t)hseg, C, 0xff - hseg * 0xff / len);
        right_c = C;
        ec0 = 0xffffff;
        ec1 = FE2D_Mix(0xffffffu, C, turn * 0xff / len);
    } else {
        hseg = W - u;
        turn = len - hseg;
        left = 0xffffff;
        right_c = FE2D_Mix(0xffffffu, C, hseg * 0xff / len);
        ec0 = right_c;
    }
    if (u < W && 1 <= g->cur && g->cur <= 4) {
        bool right = g->cur == 2 || g->cur == 4;
        float x = (float)(right ? W - u + px : px + u), w = (float)(right ? -hseg : hseg);
        float y = (float)(g->cur <= 2 ? py : py - 1 + H);
        FE2D_Gouraud(x, y, w, 1.0f, left, right_c, left, right_c);
    }
    if (W <= len + u && 1 <= g->cur && g->cur <= 4) {
        int off = u - W < 0 ? 0 : u - W;
        bool right = g->cur == 2 || g->cur == 4;   /* (its far edge: the left one) */
        float x = (float)(right ? px : W - 1 + px);
        float y = (float)(g->cur <= 2 ? py + off : py - off + H), hh = (float)(g->cur <= 2 ? turn : -turn);
        FE2D_Gouraud(x, y, 1.0f, hh, ec0, ec0, ec1, ec1);
    }
}

static int data_int(const FertControl *c, const char *name, int def)
{
    for (int k = 0; k < c->nparts; k++) {
        FertAttr *a = FertAttrList_find(&c->parts[k]->attrs, name);
        if (a) return a->type == FATTR_INT ? FertAttr_int(a) : atoi(FertAttr_str(a));
    }
    return def;
}

/* Whether the drawn window's transition is over (0x4c83f0: its counter, 0x4e0930 with 1, at 0). */
static bool trans_done(void)
{
    const FertTrans *t = FETrans_Window();
    return !t || FETrans_Counter(t) == 0;
}

/* The controls' glinty boxes (FEGoToButton_v21 0x4c8b10: data.width by data.height in black, its mode
   data.glintybox, +0x160 the button seen; FEGlintyBoxControl_v21 0x521db0: data.width by data.height in
   data.col, its mode data.animate), one a control, the least lately drawn given up for a new one. */
typedef struct {
    const FertControl *c;
    FEGlint g;
    bool seen;
    uint32_t used;
} GoToGlint;
static GoToGlint s_goto_glint[64];
static uint32_t s_goto_frame;

static GoToGlint *ctl_glint(const FertControl *c, uint32_t colour, int w, int h, int mode)
{
    s_goto_frame++;
    GoToGlint *e = NULL, *old = &s_goto_glint[0];
    for (int i = 0; i < 64 && !e; i++) {
        if (s_goto_glint[i].c == c && s_goto_glint[i].g.mode == mode) e = &s_goto_glint[i];
        if (s_goto_glint[i].used < old->used) old = &s_goto_glint[i];
    }
    if (!e) {
        e = old;
        e->c = c;
        e->seen = false;
        FEGlint_Init(&e->g, colour, w, h, mode);
        e->g.start--;   /* (made with the layout, before its first draw) */
    }
    e->used = s_goto_frame;
    return e;
}

/* A GoTo button (FEGoToButton_v15 0x4c8ba0): the gradient, its parts, the drop shadow or the hovered
   inset, the shine or the bevel, the focus frame; its stems down to the oval. */
void FEGoTo_Draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (c->feflags & 0x20) return;   /* 0x4c7aa0: hidden */
    int32_t r[4];
    FertControl_rect(c, r);
    float x = (float)(r[0] + ox), y = (float)(r[1] + oy), w = (float)(r[2] - r[0]), h = (float)(r[3] - r[1]);
    int a = 0xff;   /* (0x4c8410: the control's fade, g_fe_fade's in the 2D) */
    int ds = data_int(c, "dropshadow", 0);
    bool shadow = ds == 1, gradient = ds == 2;   /* +0x14c, +0x14d (0x4c89e0) */
    bool hover = (c->feflags & 4) != 0, disabled = (c->feflags & 0x10) != 0, focused = (c->feflags & 0x40) != 0;
    if (gradient) FEStyle_Shadow(x, y, w, h, a);
    FEControl_Draw(c, ox, oy);   /* 0x4c7e30 */
    if (!hover || disabled) {
        if (shadow) FEStyle_DropShadow(x, y, w, h, a, 4, 4);
    } else {
        FEStyle_Inset(x, y, w, h, a, 3);
    }
    if (!hover) {
        if (!gradient) FEStyle_Shine(x, y, w, h, a);
        else FEStyle_Bevel(x, y, w, h, a, 3);
    }
    if (focused && gradient) {
        FE2D_Frame(x, y, w, h, (uint32_t)a << 24 | 0xffffffu);
        return;
    }
    int mode = data_int(c, "glintybox", -1);
    if (mode == -1) return;   /* (+0x150: the glint, with its stems) */
    uint32_t k = (uint32_t)a << 24;
    FE2D_Rect(x, y, 1.0f, (float)(data_int(c, "stemleftheight", 0) + (r[3] - r[1])), k);
    FE2D_Rect(x + w, y, 1.0f, (float)(data_int(c, "stemrightheight", 0) + (r[3] - r[1])), k);
    GoToGlint *gg = ctl_glint(c, 0xff000000u, data_int(c, "width", 0), data_int(c, "height", 0), mode);
    if (!gg) return;
    /* +0x160, seen: set when disabled, or once its transition is over (0x4c83f0: the window's counter
       at 0); a run may start then, and later only while hovered, pressed or focused (0x4c7860,
       0x4c7920, 0x4c79a0); never while disabled (0x4c7a10) */
    bool disabled_now = (c->feflags & 0x10) != 0, allow;
    if (disabled_now) gg->seen = true;
    if (!gg->seen) {
        allow = trans_done();
        if (allow) gg->seen = true;
    } else {
        allow = (c->feflags & (4 | 8 | 0x40)) != 0;
    }
    FEGlint_Draw(&gg->g, (int)x, (int)y, a, allow && !disabled_now);
}

/* A tab button (FERadioItem 0x4cfd80, FEGraphicalTabButton's too): its parts; with data.dropshadow
   (+0x144) the drop shadow and the shine, or hovered the inset; with data.glintybox (+0x154) its glinty
   box, data.width by 18 in black (FERadioItem_v20 0x4cfaf0, 0x4cfb50), run as a GoTo button's (+0x158
   seen), the selected one's also. */
static void FERadioItem_Draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (c->feflags & 0x20) return;   /* 0x4c7aa0: hidden */
    FEControl_Draw(c, ox, oy);   /* 0x4c7e30 */
    int32_t r[4];
    FertControl_rect(c, r);   /* 0x4c8240 */
    float x = (float)(r[0] + ox), y = (float)(r[1] + oy), w = (float)(r[2] - r[0]), h = (float)(r[3] - r[1]);
    int a = 0xff;   /* (0x4c8410: the control's fade, g_fe_fade's in the 2D) */
    bool hover = (c->feflags & 4) != 0, disabled = (c->feflags & 0x10) != 0;
    if (data_int(c, "dropshadow", 0)) {
        if (!hover || disabled) {
            FEStyle_DropShadow(x, y, w, h, a, 4, 4);
            FEStyle_Shine(x, y, w, h, a);
        } else {
            FEStyle_Inset(x, y, w, h, a, 3);
        }
    }
    int mode = data_int(c, "glintybox", -1);
    if (mode == -1) return;
    GoToGlint *gg = ctl_glint(c, 0xff000000u, data_int(c, "width", 0), 0x12, mode);
    if (!gg) return;
    bool allow;
    if (disabled) gg->seen = true;
    if (!gg->seen) {
        allow = trans_done();   /* 0x4c83f0 */
        if (allow) gg->seen = true;
    } else {
        allow = (c->feflags & (4 | 8 | 0x40)) != 0;   /* 0x4c7860, 0x4c7920, 0x4c79a0 */
    }
    FEGlint_Draw(&gg->g, (int)x, (int)y, a, allow && !disabled);
}

/* The combo box's fonts (FE_LoadFonts 0x4c4a00: univers8b, universC11b from the art folder; the label
   part's own), cached as the parts' are. */
static HudFont *combo_font(const char *name)
{
    static struct { char name[64]; HudFont *f; } cache[8];
    static int n;
    for (int i = 0; i < n; i++)
        if (!strcmp(cache[i].name, name)) return cache[i].f;
    char path[300];
    FE_ResolvePath(name, path, sizeof path);
    HudFont *f = FEFont_Get(path);
    if (n < 8) {
        snprintf(cache[n].name, sizeof cache[n].name, "%s", name);
        cache[n++].f = f;
    }
    return f;
}

static void combo_text(HudFont *f, float x, float y, int align, uint32_t colour, const char *t)
{
    int n = (int)strlen(t) + 1;
    char *copy = f ? FE2D_Alloc(n) : NULL;   /* (the queue keeps the pointer until its flush) */
    if (!copy) return;
    memcpy(copy, t, (size_t)n);
    FE2D_Text(f, x, y, align, colour, copy, 1.0f, 1.0f);   /* 0x4d9e00 */
}

/* FEComboBox's bar (0x4c53e0; slot 15 0x4c6630 / slot 17 0x4c6650 call it), at the part backrect (its
   data.width by data.height, FE_LoadPos 0x4e21c0). The bar's colour 0xffbc0000, hovered or focused
   0xffffbb00, disabled 0x30ff0000; the texts at 0x30 when disabled, and "N/A" (299) for the value.
   data.nolabel 4, the graphics options': the value centred in a grey box data.GraphicsSpecialCaseWidth
   wide right of the bar (0xa0444444, framed), black where it runs over the bar (drawn over it, the
   depth test), the arrow centred in the bar. Else: the bar (data.dropshadow 2: the gradient), its stems
   (data.stemleftheight, stemrightheight), the line after the left label (+0x1f0, nolabel 0: its width
   in the label font + 6, 0x4c5030); nolabel 3 no texts, else the left label (labelleft) in white at
   (5, 2), and the value: nolabel 1 in its place in white, 0 right-aligned 12 in from the right in
   black, 2 none; a colour box (+0x1de) instead its colour 20 x 10, 40 in from the right, framed. The
   arrow 3 in from the right; the shine (the gradient's bevel) unless hovered; the frame; its glinty
   box (data.glintybox, +0x20c, run as a GoTo button's); hovered the inset, else data.dropshadow 1's
   drop shadow, down over the open selection box. The fonts: data.usesmallfont univers8b for both,
   else universC11b and the label part's. (0x5297f0, the application's focus, is taken as had.) */
void FEComboBox_Draw(FertControl *c, int32_t ox, int32_t oy, const char *value, bool swatch, uint32_t colour,
                     int open_h, bool scroll)
{
    if (c->feflags & 0x20) return;   /* 0x4c7aa0: hidden */
    int32_t r[4];
    FertControl_rect(c, r);
    int cx = r[0] + ox, cy = r[1] + oy;   /* (0x524b90 + 0x4c8330 + the parent's) */
    int kb = FertControl_findPart(c, "backrect"), kl = FertControl_findPart(c, "labelleft");
    int kv = FertControl_findPart(c, "label"), ka = FertControl_findPart(c, "arrowbutton");
    int32_t bp[2] = {0, 0}, bs[2] = {data_int(c, "width", 0), data_int(c, "height", 0)};
    if (kb >= 0) {
        FertAttr *pa = FertAttrList_find(&c->parts[kb]->attrs, "pos"), *sa = FertAttrList_find(&c->parts[kb]->attrs, "size");
        if (pa) FertAttr_point(pa, bp);
        if (sa) FertAttr_point(sa, bs);
    }
    int x = cx + bp[0], y = cy + bp[1], bw = bs[0], bh = bs[1];
    float fx = (float)x, fy = (float)y;
    int a = 0xff;   /* (0x4c8410: the control's fade, g_fe_fade's in the 2D) */
    int width = data_int(c, "width", 0), nolabel = data_int(c, "nolabel", 0), ds = data_int(c, "dropshadow", 1);
    int gw = data_int(c, "GraphicsSpecialCaseWidth", 0), mode = data_int(c, "glintybox", -1);
    bool shadow = ds == 1, gradient = ds == 2, small = data_int(c, "usesmallfont", 0) != 0;
    bool hover = (c->feflags & 4) != 0, disabled = (c->feflags & 0x10) != 0, focused = (c->feflags & 0x40) != 0;
    uint32_t bar = disabled ? fade(0x30ff0000u, a) : hover || focused ? (uint32_t)a << 24 | 0xffbb00u : fade(0xffbc0000u, a);
    int ta = disabled ? 0x30 : 0xff;
    uint32_t black = fade((uint32_t)a << 24, ta), white = fade((uint32_t)a << 24 | 0xffffffu, ta), k = (uint32_t)a << 24;
    HudFont *f1 = combo_font(small ? "univers8b.ffn" : "universC11b.ffn");
    HudFont *f2 = small ? f1 : NULL;
    if (!f2 && kv >= 0) {
        FertAttr *fa = FertAttrList_find(&c->parts[kv]->attrs, "fontPathName");
        f2 = combo_font(fa ? FertAttr_str(fa) : "");
    }
    const char *shown = disabled ? FEStrings_Get(299) : value ? value : "";
    const FeImage *arrow = ka >= 0 && !c->parts[ka]->hidden ? c->parts[ka]->image : NULL;
    if (nolabel == 4) {
        float gx = (float)(x + width);
        FE2D_Rect(gx, fy, (float)gw, (float)bh, fade(0xa0444444u, a));   /* 0x4d8d30 */
        FE2D_Frame(gx, fy, (float)gw, (float)bh, k);                       /* 0x4d8cb0 */
        float tx = (float)(gw / 2 + width + x);
        combo_text(f2, tx, (float)(y + 2), 1, fade(fade(0xaaffffffu, a), ta), shown);
        FE2D_Frame(fx, fy, (float)(bw + scroll), (float)bh, k);
        FE2D_DepthPushStep();   /* 0x4da4c0 */
        FE2D_Rect(fx + 1.0f, fy + 1.0f, (float)(bw - 2), (float)(bh - 2), bar);
        FE2D_OverlayBegin();    /* 0x4da570 */
        combo_text(f2, tx, (float)(y + 2), 1, black, shown);
        FE2D_OverlayEnd();      /* 0x4da5b0 */
        if (arrow)
            FE2D_Image(arrow, (float)((width - arrow->w) / 2 + x), (float)((bh - arrow->h) / 2 + y), (float)arrow->w,
                       (float)arrow->h, (uint32_t)a << 24 | 0xffffffu, 0, false);   /* 0x4d95a0 */
        if (!hover || disabled) {
            if (gradient) FEStyle_Bevel(fx, fy, (float)bw, (float)bh, a, 3);   /* 0x5211f0 */
            else FEStyle_Shine(fx, fy, (float)bw, (float)bh, a);              /* 0x521090 */
        }
    } else {
        if (gradient) FEStyle_Shadow(fx, fy, (float)bw, (float)bh, a);   /* 0x521180 */
        else FE2D_Rect(fx, fy, (float)bw, (float)bh, bar);
        FE2D_Rect(fx, fy, 1.0f, (float)(bh + data_int(c, "stemleftheight", 0)), k);
        FE2D_Rect((float)(x + bw + scroll), fy, 1.0f, (float)(bh + data_int(c, "stemrightheight", 0)), k);
        int sep = 0;   /* +0x1f0 */
        if (nolabel == 0 && kl >= 0 && f1 && f1->font) {
            float ex = 0, ey = 0, ew = 0, eh = 0;
            FONT_extent(f1->font, FEPart_Text(c->parts[kl]), &ex, &ey, &ew, &eh);   /* 0x5366e0 */
            sep = (int)(ew + 6.0f);
        }
        FE2D_Rect((float)(x + sep), fy, 1.0f, (float)bh, k);
        if (nolabel != 3) {
            if (nolabel != 1 && kl >= 0) combo_text(f1, (float)(x + 5), (float)(y + 2), 0, white, FEPart_Text(c->parts[kl]));
            if (nolabel == 1) {
                combo_text(f1, (float)(x + 5), (float)(y + 2), 0, white, shown);
            } else if (swatch) {
                float sx = (float)(x + bw - 0x28), sy = (float)(y + 4);
                FE2D_Rect(sx, sy, 20.0f, 10.0f, fade(colour, a));
                FE2D_Frame(sx, sy, 20.0f, 10.0f, k);
            } else if (nolabel != 2) {
                combo_text(f2, (float)(x + bw - 0xc), (float)(y + 2), 2, black, shown);
            }
        }
        if (arrow)
            FE2D_Image(arrow, (float)(x - arrow->w + bw - 3), (float)((bh - arrow->h) / 2 + 1 + y), (float)arrow->w,
                       (float)arrow->h, (uint32_t)a << 24 | 0xffffffu, 0, false);   /* 0x4d95a0 */
        if (!hover || disabled) {
            if (gradient) FEStyle_Bevel(fx, fy, (float)bw, (float)bh, a, 3);
            else FEStyle_Shine(fx, fy, (float)bw, (float)bh, a);
        }
        if (!gradient || !hover || !disabled) FE2D_Frame(fx, fy, (float)(bw + scroll), (float)bh, k);   /* (0x4c7880) */
        GoToGlint *gg = bw == width && mode != -1 ? ctl_glint(c, 0xff000000u, width, bh, mode) : NULL;
        if (gg) {
            /* +0x214, seen: as a GoTo button's (0x4c83f0, 0x4c7860, 0x4c7920, 0x4c79a0, 0x4c7a10) */
            bool allow;
            if (disabled) gg->seen = true;
            if (!gg->seen) {
                allow = trans_done();
                if (allow) gg->seen = true;
            } else {
                allow = (c->feflags & (4 | 8 | 0x40)) != 0;
            }
            FEGlint_Draw(&gg->g, cx, cy, a, allow && !disabled);
        }
        if (gradient && hover && disabled) FE2D_Frame(fx, fy, (float)bw, (float)bh, (uint32_t)a << 24 | 0xffffffu);
    }
    /* (the selection box between, 0x4c60ce: the application's, drawn after the window) */
    if (hover && !disabled) FEStyle_Inset(fx, (float)cy, (float)bw, (float)(bh + open_h), a, 3);   /* 0x5208d0 */
    else if (shadow) FEStyle_DropShadow(fx, (float)cy, (float)bw, (float)(bh + open_h), a, 4, 4);  /* 0x520780 */
}

static uint32_t data_colour(const FertControl *c, const char *name);

/* A glinty rectangle (FEGlintyBoxControl_v15 0x521e60): only its glinty box, at its place, a run free to
   start whenever its transition is over (0x4c83f0 at 0). */
void FEGlintyBox_Draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (c->feflags & 0x20) return;
    int32_t pos[2] = {0, 0};
    FertAttr *pa = FertAttrList_find(&c->attrs, "pos");
    if (pa) FertAttr_point(pa, pos);
    GoToGlint *gg = ctl_glint(c, data_colour(c, "col"), data_int(c, "width", 0), data_int(c, "height", 0), data_int(c, "animate", 0));
    if (gg) FEGlint_Draw(&gg->g, pos[0] + ox, pos[1] + oy, 0xff, trans_done());   /* (the fade: g_fe_fade's) */
}

static uint32_t data_colour(const FertControl *c, const char *name)
{
    for (int k = 0; k < c->nparts; k++) {
        FertAttr *a = FertAttrList_find(&c->parts[k]->attrs, name);
        if (a) return a->type == FATTR_COLOR || a->type == FATTR_INT ? FertAttr_color(a) : (uint32_t)strtoul(FertAttr_str(a), NULL, 0);
    }
    return 0;
}

/* A gouraud box (FEGouraudBoxControl_v15 0x4c9a10, its values 0x4c9920): its four corners' colours, each
   corner moved by its offsets; its area rect is the layout editor's. */
static void FEGouraudBox_Draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (c->feflags & 0x20) return;
    int32_t pos[2] = {0, 0}, size[2] = {0, 0};
    FertAttr *a = FertAttrList_find(&c->attrs, "pos");
    if (a) FertAttr_point(a, pos);
    a = FertAttrList_find(&c->attrs, "size");
    if (a) FertAttr_point(a, size);
    float x = (float)(pos[0] + ox), y = (float)(pos[1] + oy), w = (float)size[0], h = (float)size[1];
    int al = 0xff;   /* (0x4c8410: the fade, g_fe_fade's in the 2D) */
    FE2D_DepthStep();
    quad4(x + (float)data_int(c, "ULoffX", 0), y + (float)data_int(c, "ULoffY", 0), fade(data_colour(c, "cUL"), al),
          x + w + (float)data_int(c, "URoffX", 0), y + (float)data_int(c, "URoffY", 0), fade(data_colour(c, "cUR"), al),
          x + (float)data_int(c, "BLoffX", 0), y + h + (float)data_int(c, "BLoffy", 0), fade(data_colour(c, "cBL"), al),
          x + w + (float)data_int(c, "BRoffX", 0), y + h + (float)data_int(c, "BRoffY", 0), fade(data_colour(c, "cBR"), al));
}
