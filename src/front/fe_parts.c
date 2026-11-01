/* The front end's side of FERT's parts and controls (fe_parts.h). */
#include "front/fe_parts.h"
#include "pending.h"
#include "front/fe2d.h"
#include "real/font.h"
#include "real/loc.h"
#include "real/shape.h"
#include "real/window.h"
#include "render/render.h"
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
                    w = WINDOW_new(256, 256, depth, 0);   /* 0x5356c0 (an 8-bit page's palette: pending) */
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
    /* (0x4e6140: some backgrounds aren't shown in some modes, 0x657e60: pending) */
    p->hidden = false;
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

void FEWindow_Draw(FertControl *w)
{
    /* (the screen object, the layout's first section, holds the screen's settings: room, callback list,
       music; the window's draw doesn't draw it) */
    for (int i = w->nchildren - 1; 0 <= i; i--) {   /* 0x528390: last first */
        FertControl *c = w->children[i];
        if (!strcmp(c->id, "NFS5.3DCAR")) continue;
        if (!strcmp(c->id, "NFS5.PANEL") || !strcmp(c->id, "NFS5.RADIOGROUP")) {
            /* FEPanel 0x4ce720: its screen when open (its area is the layout editor's) */
            if (c->sub && !(c->feflags & 0x20)) FEWindow_Draw(c->sub);
            continue;
        }
        if (!strcmp(c->id, "NFS5.TrackSmall2dSpline") || !strcmp(c->id, "NFS5.TrackLarge2dSpline")) {
            PENDING_NOTE(0x4fbf40, "the tracks' 2D splines (FETrack2dSpline: the tournament's locations)");
            continue;
        }
        if (!strcmp(c->id, "NFS5.HUD")) {
            /* FEHudDisplayControl (vtable 0x5b603c): the race HUD's preview with the display settings */
            PENDING_NOTE(0x4caea0, "FEHudDisplayControl: the HUD's preview");
            continue;
        }
        if (!strcmp(c->id, "NFS5.MOVIECIRCLE")) {
            /* FEMovieCircle_v15 0x4cd520: its stencil shapes cut the movie (FEMovieBox_v15) into the oval;
               none of its parts is drawn plainly. The movies aren't ported: nothing until they are. */
            PENDING_NOTE(0x4cd520, "FEMovieCircle: the movie in the oval (the stencil, FEMovieBox)");
            continue;
        }   /* (FECar3DControl_v15 0x4c0570 draws none of its parts: its car is in the room) */
        if (FE_ControlDrawHook && FE_ControlDrawHook(c, 0, 0)) continue;
        if (!strcmp(c->id, "NFS5.GOURAUDBOX")) FEGouraudBox_Draw(c, 0, 0);
        else if (!strcmp(c->id, "NFS5.GOTOBUTTON") || !strcmp(c->id, "NFS5.ComboBoxSlider")) FEGoTo_Draw(c, 0, 0);
        else FEControl_Draw(c, 0, 0);
    }
}

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

static int data_int(const FertControl *c, const char *name, int def)
{
    for (int k = 0; k < c->nparts; k++) {
        FertAttr *a = FertAttrList_find(&c->parts[k]->attrs, name);
        if (a) return a->type == FATTR_INT ? FertAttr_int(a) : atoi(FertAttr_str(a));
    }
    return def;
}

/* A GoTo button (FEGoToButton_v15 0x4c8ba0): the gradient, its parts, the drop shadow or the hovered
   inset, the shine or the bevel, the focus frame; its stems down to the oval. */
void FEGoTo_Draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (c->feflags & 0x20) return;   /* 0x4c7aa0: hidden */
    int32_t r[4];
    FertControl_rect(c, r);
    float x = (float)(r[0] + ox), y = (float)(r[1] + oy), w = (float)(r[2] - r[0]), h = (float)(r[3] - r[1]);
    int a = 0xff;   /* (0x4c8410: the control's fade) */
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
    if (data_int(c, "glintybox", -1) == -1) return;   /* (+0x150: the glint, with its stems) */
    uint32_t k = (uint32_t)a << 24;
    FE2D_Rect(x, y, 1.0f, (float)(data_int(c, "stemleftheight", 0) + (r[3] - r[1])), k);
    FE2D_Rect(x + w, y, 1.0f, (float)(data_int(c, "stemrightheight", 0) + (r[3] - r[1])), k);
    /* (the glint sweeping along the bar, 0x51f660: pending) */
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
    int al = 0xff;   /* (0x4c8410: the fade) */
    FE2D_DepthStep();
    quad4(x + (float)data_int(c, "ULoffX", 0), y + (float)data_int(c, "ULoffY", 0), fade(data_colour(c, "cUL"), al),
          x + w + (float)data_int(c, "URoffX", 0), y + (float)data_int(c, "URoffY", 0), fade(data_colour(c, "cUR"), al),
          x + (float)data_int(c, "BLoffX", 0), y + h + (float)data_int(c, "BLoffy", 0), fade(data_colour(c, "cBL"), al),
          x + w + (float)data_int(c, "BRoffX", 0), y + h + (float)data_int(c, "BRoffY", 0), fade(data_colour(c, "cBR"), al));
}
