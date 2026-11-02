/* The custom colour picker (fecolour.h). */
#include "front/fecolour.h"
#include "game/audio.h"
#include "exe.h"
#include "front/fe2d.h"
#include "front/fe_parts.h"
#include "front/feaccess.h"
#include "front/feapp.h"
#include "front/fepointer.h"
#include "real/mouse.h"
#include <stdio.h>
#include <string.h>

static FertControl *s_box;     /* the box it is open on (0x65b710 on) */
static bool s_saved;           /* +0x2c0: the place and the slots kept */
static int s_was_pos;          /* +0x29c */
static uint32_t s_was[8];      /* +0x2a0 */
static int s_area;             /* +0x218: 0 the field, 1 the strip, 2 the slots, 3 OK, 4 cancel, 5 none */
static int s_slot;             /* +0x284 */
static float s_h = 0.5f, s_s = 0.5f, s_l = 0.5f;   /* +0x288 .. +0x290 */

/* The picker's places from the box's rect (_v20 0x4c35e0): x0, y0, x1, y1 each. */
typedef struct { int field[4], strip[4], swatch[4], slots[4], ok[4], cancel[4]; } Layout;

static void layout(FertControl *c, Layout *L)
{
    int32_t r[4];
    FertControl_rect(c, r);
    int x0 = r[0], x1 = r[2], b = r[3], half = (x1 - x0) / 2;
    const int f[4] = {x0 + 6, b + 0x15, x1 - 6, b + 0x74}, st[4] = {x0 + 6, b + 6, x0 + 0x79, b + 0xe};
    const int sw[4] = {x0 + 0x7d, b + 5, x1 - 5, b + 0xf}, sl[4] = {x0 + 5, b + 0x7a, x1 - 5, b + 0x84};
    const int ok[4] = {x0 + 5, b + 0x89, half - 3 + x0, b + 0x9b}, cn[4] = {x1 - half + 3, b + 0x89, x1 - 5, b + 0x9b};
    memcpy(L->field, f, sizeof f);
    memcpy(L->strip, st, sizeof st);
    memcpy(L->swatch, sw, sizeof sw);
    memcpy(L->slots, sl, sizeof sl);
    memcpy(L->ok, ok, sizeof ok);
    memcpy(L->cancel, cn, sizeof cn);
}

static bool inside(const int b[4], int x, int y) { return b[0] <= x && x <= b[2] && b[1] <= y && y <= b[3]; }

/* The area under the mouse (0x4c46a0). */
static void hit(const Layout *L, int x, int y)
{
    s_area = 5;
    if (inside(L->field, x, y)) s_area = 0;
    else if (inside(L->strip, x, y)) s_area = 1;
    else if (inside(L->ok, x, y)) s_area = 3;
    else if (inside(L->cancel, x, y)) s_area = 4;
    else if (inside(L->slots, x, y)) s_area = 2;
}

/* The box's data attribute (its parts' attributes). */
static const char *data(const FertControl *c, const char *name)
{
    for (int k = 0; c && k < c->nparts; k++) {
        FertAttr *a = FertAttrList_find(&c->parts[k]->attrs, name);
        if (a) return FertAttr_str(a);
    }
    return NULL;
}

static FEAccessor *box_acc(void) { return s_box ? FEAccess_Find(data(s_box, "accessor")) : NULL; }

/* 0x4d83b0: the hue's thirds 120 degrees apart, each wrapped into 0 .. 360. */
static float hue(float m1, float m2, float h)
{
    while (360.0f < h) h -= 360.0f;
    while (h < 0.0f) h += 360.0f;
    if (h < 60.0f) return m1 + (m2 - m1) * h / 60.0f;
    if (h < 180.0f) return m2;
    if (h < 240.0f) return m1 + (m2 - m1) * (240.0f - h) / 60.0f;
    return m1;
}

uint32_t FEColour_Hsl(float h, float s, float l)
{
    float r = l, g = l, b = l;
    if (s != 0.0f) {
        float m2 = l <= 0.5f ? l * (1.0f + s) : l + s - l * s, m1 = 2.0f * l - m2, d = h * 360.0f;
        r = hue(m1, m2, d + 120.0f), g = hue(m1, m2, d), b = hue(m1, m2, d - 120.0f);
    }
    uint32_t R = (uint32_t)(r * 255.0f), G = (uint32_t)(g * 255.0f), B = (uint32_t)(b * 255.0f);
    return 0xff000000u | (R & 0xff) << 16 | (G & 0xff) << 8 | (B & 0xff);
}

void FEColour_ToHsl(uint32_t c, float *h, float *s, float *l)   /* 0x4d8680 */
{
    float r = (float)(c >> 16 & 0xff) / 255.0f, g = (float)(c >> 8 & 0xff) / 255.0f, b = (float)(c & 0xff) / 255.0f;
    float mx = r > g ? r : g, mn = r < g ? r : g;
    mx = b > mx ? b : mx, mn = b < mn ? b : mn;
    *l = (mx + mn) * 0.5f;
    if (mx == mn) {
        *s = *h = 0.0f;
        return;
    }
    float d = mx - mn;
    *s = *l <= 0.5f ? d / (mx + mn) : d / (2.0f - mx - mn);
    float H;
    if (r == mx) H = (g - b) / d;
    else if (g == mx) H = 2.0f + (b - r) / d;
    else H = 4.0f + (r - g) / d;
    H *= 60.0f;
    if (H < 0.0f) H += 360.0f;
    *h = H / 360.0f;
}

void FEColour_Open(FertControl *c)   /* 0x4c35c0 */
{
    s_box = c;
    s_saved = false;
    c->feflags |= 0x40;   /* (the box keeps the focus, 0x4c79a0) */
    FEApp_RefreshState(c);
}

bool FEColour_Open_On(const FertControl *c) { return s_box && s_box == c; }
FertControl *FEColour_Box(void) { return s_box; }

/* The colour made into the slot and the accessor at it (0x500f70, then slot 27 0x4b8f00). */
static void put(void)
{
    FEData_SetCustomSlot(s_slot, FEColour_Hsl(s_h, s_s, s_l));
    FEAccess_ChooseCustom(box_acc(), s_slot);
}

static void close_picker(void)
{
    if (s_box) {
        s_box->feflags &= ~0x40u;   /* 0x4c7970 */
        FEApp_RefreshState(s_box);
    }
    s_box = NULL;
    Audio_UI(5);   /* 0x4aab80 */
}

static void pick(const Layout *L, int x, int y)   /* (the field sets the hue and saturation, the strip the lightness) */
{
    if (s_area == 0) {
        s_h = (float)(x - L->field[0]) / (float)(L->field[2] - L->field[0]);
        s_s = (float)(L->field[3] - y) / (float)(L->field[3] - L->field[1]);
    }
    if (s_area == 1) s_l = (float)(x - L->strip[0]) / (float)(L->strip[2] - L->strip[0]);
    put();
}

bool FEColour_Mouse(int kind, int32_t mx, int32_t my, bool down)
{
    if (!s_box) return false;
    Layout L;
    layout(s_box, &L);
    hit(&L, mx, my);
    if (kind == 0 || kind == 1) {
        if (kind == 0 || down) pick(&L, mx, my);
        else put();
    } else {
        FEAccessor *a = box_acc();
        if (s_area == 3) {   /* OK: the place set again (its colour taken) */
            if (a) FEAccess_SetPos(a, FEAccess_Pos(a));
            close_picker();
        } else if (s_area == 4) {   /* cancel: the slots and the place back */
            for (int i = 0; i < 8; i++) FEData_SetCustomSlot(i, s_was[i]);
            if (a) FEAccess_SetPos(a, s_was_pos);
            close_picker();
        } else if (s_area == 2) {   /* a slot: its colour into the picker */
            int k = (mx - L.slots[0]) * 8 / (L.slots[2] - L.slots[0]);
            s_slot = k < 0 ? 0 : 7 < k ? 7 : k;
            uint32_t c = FEData_CustomSlot(s_slot);
            if (c) FEColour_ToHsl(c, &s_h, &s_s, &s_l);
        }
    }
    return true;
}

static void text(const char *font, float x, float y, uint32_t colour, int id)
{
    char p[300];
    FE_ResolvePath(font, p, sizeof p);
    HudFont *f = FEFont_Get(p);
    const char *t = FEStrings_Get(id);
    if (!f || !t) return;
    char *copy = FE2D_Alloc((int)strlen(t) + 1);
    if (!copy) return;
    strcpy(copy, t);
    FE2D_Text(f, x, y, 1, colour, copy, 1.0f, 1.0f);
}

/* A button (OK 0x2be, cancel 0x2bf): red, gold under the mouse; its text centred in white; its shine,
   pressed in while held there; framed. */
static void button(const int b[4], bool over, bool held, int id, int ox, int oy)
{
    float x = (float)(b[0] + ox), y = (float)(b[1] + oy), w = (float)(b[2] - b[0]), h = (float)(b[3] - b[1]);
    FE2D_DepthStep();
    FE2D_Rect(x, y, w, h, over ? 0xffffbb00u : 0xffbc0000u);
    text(exe_str(0x5d7fa0) + 2, (float)((b[2] - b[0]) / 2 + b[0] + ox), y + 2.0f, 0xffffffffu, id);   /* "Universc11b.ffn" */
    if (over && held) FEStyle_Inset(x, y, w, h, 0xff, 3);
    else FEStyle_Shine(x, y, w, h, 0xff);
    FE2D_Frame(x, y, w, h, 0xff000000u);
}

bool FEColour_Draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (!s_box || s_box != c) return false;
    FEAccessor *a = box_acc();
    if (!s_saved) {   /* (its first draw: the place and the slots kept for cancel) */
        s_was_pos = a ? FEAccess_Pos(a) : 0;
        for (int i = 0; i < 8; i++) s_was[i] = FEData_CustomSlot(i);
        s_saved = true;
    }
    Layout L;
    layout(c, &L);
    int32_t r[4];
    FertControl_rect(c, r);
    float x0 = (float)(r[0] + ox), y0 = (float)(r[1] + oy), w = (float)(r[2] - r[0]), hb = (float)(r[3] - r[1]);
    float yb = (float)(r[3] + oy);
    /* the box gold with its title (0x262), the panel under it from gold to 0xb282a8b0, 160 high */
    FE2D_DepthStep();
    FE2D_Rect(x0, y0, w, hb, 0xffffbb00u);
    FE2D_Frame(x0, y0, w, hb, 0xff000000u);
    text(exe_str(0x5d7f90) + 2, (float)((int)w / 2) + x0, y0 + 2.0f, 0xff000000u, 0x262);   /* "Univers7b.ffn" */
    FE2D_Gouraud(x0, yb, w, 160.0f, 0xffffbb00u, 0xffffbb00u, 0xb282a8b0u, 0xb282a8b0u);
    FE2D_Frame(x0, yb, w, 160.0f, 0xff000000u);
    /* the lightness strip: a column a pixel, the hue and saturation at its lightness; framed, its marker */
    FE2D_DepthStep();
    for (int i = L.strip[0]; i <= L.strip[2]; i++) {
        float t = (float)(i - L.strip[0]) / (float)(L.strip[2] - L.strip[0]);
        FE2D_Rect((float)(i + ox), (float)(L.strip[1] + oy), 1.0f, (float)(L.strip[3] - L.strip[1]), FEColour_Hsl(s_h, s_s, t));
    }
    FE2D_Frame((float)(L.strip[0] - 1 + ox), (float)(L.strip[1] - 1 + oy), (float)(L.strip[2] - L.strip[0] + 2),
               (float)(L.strip[3] - L.strip[1] + 2), 0xff000000u);
    float mx = (float)(int)(s_l * (float)(L.strip[2] - L.strip[0])) + (float)(L.strip[0] - 1 + ox);
    FE2D_Frame(mx, (float)(L.strip[1] - 2 + oy), 3.0f, (float)(L.strip[3] - L.strip[1] + 4), 0xff000000u);
    /* the colour */
    FE2D_Rect((float)(L.swatch[0] + ox), (float)(L.swatch[1] + oy), (float)(L.swatch[2] - L.swatch[0]),
              (float)(L.swatch[3] - L.swatch[1]), FEColour_Hsl(s_h, s_s, s_l));
    FE2D_Frame((float)(L.swatch[0] + ox), (float)(L.swatch[1] + oy), (float)(L.swatch[2] - L.swatch[0]),
               (float)(L.swatch[3] - L.swatch[1]), 0xff000000u);
    /* the hue and saturation field (COLB), framed */
    char icons[300];
    FE_ResolvePath("weeicons.fsh", icons, sizeof icons);   /* ("%sweeicons.fsh" 0x5d7bf8) */
    FEShapes_Load(icons);
    FeImage *im = FEShapes_Named(icons, exe_str(0x5d7f88));   /* "COLB" */
    FE2D_DepthStep();
    if (im) FE2D_Image(im, (float)(L.field[0] + ox), (float)(L.field[1] + oy), (float)im->w, (float)im->h, 0xffffffffu, 0, false);
    FE2D_Frame((float)(L.field[0] - 1 + ox), (float)(L.field[1] - 1 + oy), (float)(L.field[2] - L.field[0] + 2),
               (float)(L.field[3] - L.field[1] + 2), 0xff000000u);
    /* the eight slots, the chosen one framed in white */
    int sw = (L.slots[2] - L.slots[0]) / 8;
    float sy = (float)(L.slots[1] + oy), sh = (float)(L.slots[3] - L.slots[1]);
    FE2D_DepthStep();
    for (int i = 0; i < 8; i++) {
        float sx = (float)(L.slots[0] + i * sw + ox);
        FE2D_Rect(sx, sy, (float)sw, sh, FEData_CustomSlot(i));
        FE2D_Frame(sx, sy, (float)sw, sh, 0xff000000u);
    }
    FE2D_DepthStep();
    FE2D_Frame((float)(L.slots[0] + s_slot * sw + ox), sy, (float)sw, sh, 0xffffffffu);
    int32_t px, py;
    uint32_t mb = 0;
    MOUSE_state(&px, &py, &mb);
    hit(&L, px, py);
    if (s_area == 0 && !(c->feflags & 0x10)) FEPointer_Want(2);   /* 0x4e1630 */
    button(L.ok, s_area == 3, (mb & 1) != 0, 0x2be, ox, oy);
    button(L.cancel, s_area == 4, (mb & 1) != 0, 0x2bf, ox, oy);
    return true;
}
