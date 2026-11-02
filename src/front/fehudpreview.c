/* The display options' HUD preview: see fehudpreview.h. */
#include "front/fehudpreview.h"
#include "front/fe2d.h"
#include "front/fe_parts.h"
#include "front/feaccess.h"
#include "front/fesave.h"
#include "game/hud_config.h"
#include "render/render.h"
#include <stdio.h>
#include <string.h>

/* The choices' accessors (+0x140 ..) and the layout's choice each stands for (0x421050 .., 0x420e30 ..;
   the screen layout's none). */
enum { LAYOUT, SPEEDO, UNITS, TACH, DAMAGE, MAP, MAPSCALE, OPPLIST, SPLITS, DISPCOLS, TRANSLUCENCY, REARVIEW,
       RECORDTIME, GEAR, NCHOICES };
static const struct { const char *name; int off; } k_choice[NCHOICES] = {
    {"Display.ScreenLayout", -1},   {"Display.Speedometer", 0x30}, {"Display.SpeedUnits", 0x34},
    {"Display.Tach", 0x38},         {"Display.Damage", 0x3c},      {"Display.Map", 0x40},
    {"Display.MapScale", 0x44},     {"Display.OppList", 0x48},     {"Display.SplitTimes", 0x4c},
    {"Display.DispCols", 0x50},     {"Display.Translucency", 0x58}, {"Display.RearView", 0x5c},
    {"Display.RecordTime", 0x60},   {"Display.Gear", 0x64}};

static struct {
    FertControl *c;
    FeImage *shape[15];   /* +0x178 */
    char path[300];       /* +0x1b4 */
    bool on;              /* +0x218 */
    int layout;           /* +0x21c: the screen layout's choice the layout was loaded for */
} s;

static FEAccessor *acc(int k) { return FEAccess_Find(k_choice[k].name); }   /* (v20 0x4caea0's) */
static int value(int k) { FEAccessor *a = acc(k); return a ? FEAccess_Pos(a) : 0; }   /* (v0x28) */

static const char *player(void)   /* 0x500820: the player's name (0x691dc8) */
{
    Save *sv = FESave_Current();
    return sv ? sv->player : "";
}

/* The layout for the screen layout chosen (HUD_LoadStrings 0x41e190 with the display mode's size), and
   the choices from it. */
static void load(void)
{
    s.layout = value(LAYOUT);
    HudConfig_Load(player(), s.layout, g_display.width, g_display.height);
    for (int k = SPEEDO; k < NCHOICES; k++) {
        FEAccessor *a = acc(k);
        if (a) FEAccess_SetPos(a, HudConfig_choicei32(k_choice[k].off));   /* (v0x2c) */
    }
}

/* The choices into the layout (0x4cb320), the files written (0x41ea70) and the layout let go (0x41e2e0). */
static void store(void)
{
    for (int k = SPEEDO; k < NCHOICES; k++) HudConfig_setchoice(k_choice[k].off, value(k));
    HudConfig_Save(player());
    HudConfig_Free();
}

/* On screen (v21 0x4cb110): hud.fsh's fifteen shapes, the layout and the choices from it. */
static bool open_for(FertControl *c)
{
    if (s.c == c) return true;
    if (s.c) return false;
    memset(&s, 0, sizeof s);
    s.c = c;
    s.on = true;
    FE_ResolvePath("hud.fsh", s.path, sizeof s.path);   /* ("%shud.fsh" 0x5d8a4c) */
    FEShapes_Load(s.path);
    for (int i = 0; i < 15; i++) s.shape[i] = FEShapes_Image(s.path, i);   /* 0x4d74e0 */
    load();
    return true;
}

void FEHudPreview_Drop(bool (*keep)(const FertControl *c))
{
    if (!s.c || keep(s.c)) return;
    if (s.on) {   /* v22 0x4cb2e0 */
        FEShapes_Release(s.path);   /* 0x4d7270 */
        store();
    }
    memset(&s, 0, sizeof s);
}

/* Its draw (v15 0x4cb440): another screen layout chosen, the choices stored and the new one's loaded;
   then the screen's shape at the control's place, and over it, cut to the 240 x 180 (0x5b6030, 0x5b6034)
   box, each visual's shape at its layout place (a fraction of the box): the damage (1: shape 3, 2: 1, at
   +0x1a0; 3: 2 at +0x1b0), the map (1, 2: 4; 3: 5; +0x1c8), the standings (6, +0x1a8), the mirror (7,
   +0x1d8), the split times (10, the choice's +0x0c), the gear (11, +0x188), the tachometer (1: 12 at the
   choice's +0x20; 2: 14 at +0x1b8), the speedometer (1: 12 at +0x190; 2: 13 at +0x1c0); all in white at
   the control's fade (0x4c8410: 0xff). */
bool FEHudPreview_Draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (strcmp(c->id, "NFS5.HUD")) return false;
    if (!open_for(c)) return true;
    if (value(LAYOUT) != s.layout) {
        store();
        load();
    }
    int32_t p[2] = {0, 0};
    FertAttr *pa = FertAttrList_find(&c->attrs, "pos");   /* 0x524b90 */
    if (pa) FertAttr_point(pa, p);
    int x = p[0] + ox, y = p[1] + oy;   /* (the transition's offset, 0x4c8330: none) */
    uint32_t white = 0xffffffffu;       /* 0x4d8280 */
    if (s.shape[0]) FE2D_Image(s.shape[0], (float)x, (float)y, (float)s.shape[0]->w, (float)s.shape[0]->h, white, 0, false);   /* 0x4d95a0 */
    float bw = 240.0f, bh = 180.0f;   /* (0x5b6030, 0x5b6034) */
    const int32_t clip[4] = {x, y, (int32_t)((float)x + bw), (int32_t)((float)y + bh)};
    const uint8_t *look = HudConfig_look(), *ch = HudConfig_choice();
    if (!look || !ch) return true;
#define AT(rec, off, sh)                                                                                       \
    do {                                                                                                       \
        const FeImage *im_ = s.shape[sh];                                                                      \
        float f_[2];                                                                                           \
        memcpy(f_, (rec) + (off), 8);                                                                          \
        if (im_)                                                                                               \
            FE2D_ImageClip(im_, (float)x + f_[0] * bw, (float)y + bh * f_[1], (float)im_->w, (float)im_->h, clip, \
                           white, 0, false); /* 0x4d98c0 */                                                    \
    } while (0)
    switch (value(DAMAGE)) {
    case 1: AT(look, 0x1a0, 3); break;
    case 2: AT(look, 0x1a0, 1); break;
    case 3: AT(look, 0x1b0, 2); break;
    }
    int map = value(MAP);
    if (0 < map && map < 3) AT(look, 0x1c8, 4);
    else if (map == 3) AT(look, 0x1c8, 5);
    if (value(OPPLIST)) AT(look, 0x1a8, 6);
    if (value(REARVIEW)) AT(look, 0x1d8, 7);
    if (value(SPLITS)) AT(ch, 0x0c, 10);
    if (value(GEAR)) AT(look, 0x188, 11);
    int tach = value(TACH);
    if (tach == 1) AT(ch, 0x20, 12);
    else if (tach == 2) AT(look, 0x1b8, 14);
    int sp = value(SPEEDO);
    if (sp == 1) AT(look, 0x190, 12);
    else if (sp == 2) AT(look, 0x1c0, 13);
#undef AT
    return true;
}
