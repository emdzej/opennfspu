/* The showcase: see feshowcase.h. */
#include "front/feshowcase.h"
#include "app.h"
#include "exe.h"
#include "front/fe2d.h"
#include "front/fe_parts.h"
#include "front/feaccess.h"
#include "front/feapp.h"
#include "front/fedrift.h"
#include "pending.h"
#include "real/loc.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FE_DIR_SHOWCASE "FEData\\Showcase\\"   /* 0x65b338 */

static char s_pics[200];   /* 0x699d54: the car's pictures' file ("" none) */
static uint8_t *s_texts;   /* 0x699d58: its texts */

static uint32_t timer_ticks(void) { return (uint32_t)((uint64_t)app_ticks_ms() * 128u / 1000u); }   /* TIMER_ticks */

void FEShowcase_Load(const uint8_t *rec)
{
    free(s_texts);   /* 0x5081e0 */
    s_texts = NULL;
    if (s_pics[0]) FEShapes_Release(s_pics);   /* 0x4d7270 */
    s_pics[0] = 0;
    if (!rec) return;
    snprintf(s_pics, sizeof s_pics, "%s%s.fsh", FE_DIR_SHOWCASE, (const char *)rec + 0xe3);   /* "%s%s.fsh" 0x5cccc8 */
    if (!vfs_exists(s_pics)) snprintf(s_pics, sizeof s_pics, exe_str(0x5dbcd0), FE_DIR_SHOWCASE);   /* "%s993tCo96.fsh" */
    FEShapes_Load(s_pics);   /* FE_ValidateShapes */
    char loc[100];
    snprintf(loc, sizeof loc, "%s%s.loc", FE_DIR_LOCALE, (const char *)rec + 0x4d);   /* "%s%s.loc" 0x5dbcc4 */
    size_t n = 0;
    s_texts = vfs_read_all(loc, &n);   /* 0x59d760 */
    if (s_texts) LOC_setlanguage(s_texts, 0, FEApp_Language());   /* 0x536af0 */
}

const char *FEShowcase_Text(int n)
{
    if (!s_texts) return exe_str(0x5d9b38);   /* "Not Found" (0x508370) */
    const char *s = LOC_string(s_texts, n);
    return s ? s : exe_str(0x5dbd2c);         /* "Not found" (v5) */
}

bool FEShowcase_Callback(const char *cb)
{
    static const struct { const char *cb, *acc; } k[5] = {
        {"SHOWCASE.DEALERCAR", "DealerCar.Car"}, {"SHOWCASE.GENERICCAR", NULL}, {"SHOWCASE.OWNEDCAR", "OwnedCars.Car"},
        {"SHOWCASE.USEDCAR", "UsedCar.Car"},     {"SHOWCASE.MERGEDCAR", "zMergedList.Car"}};
    for (int i = 0; i < 5; i++) {
        if (strcmp(cb, k[i].cb)) continue;
        /* the accessor's car (its slot 0x90); GenericCar.Car's the merged list's past the owned ones */
        FEShowcase_Load(k[i].acc ? FEAccess_CarRecord(FEAccess_Find(k[i].acc)) : FEData_GenericCar());
        /* (0x50df70: the accessor the car compare compares, 0x699fe0: the car compare screen pends) */
        return true;
    }
    return false;
}

static const char *data_str(const FertControl *c, const char *name)
{
    for (int k = 0; k < c->nparts; k++) {
        FertAttr *a = FertAttrList_find(&c->parts[k]->attrs, name);
        if (a) return FertAttr_str(a);
    }
    return NULL;
}

static void ctl_pos(const FertControl *c, int32_t p[2])   /* 0x524b90 */
{
    p[0] = p[1] = 0;
    FertAttr *a = FertAttrList_find(&c->attrs, "pos");
    if (a) FertAttr_point(a, p);
}

static void image(const FeImage *im, float x, float y, uint32_t colour)   /* 0x4d95a0 */
{
    if (im) FE2D_Image(im, x, y, (float)im->w, (float)im->h, colour, 0, false);
}

/* NFS5.ShowcasePicture (FEShowcasePictureControl, 0x144 bytes, vtable 0x5b8d8c; v20 0x508450, v15
   0x508470): the car's picture data.number at its place in its colour (0x5288c0; its alpha by the fade,
   0x4c8410: 0xff). */
static void picture_draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (!s_pics[0]) return;
    const char *num = data_str(c, "number");
    int32_t p[2];
    ctl_pos(c, p);
    FertAttr *ca = FertAttrList_find(&c->attrs, "color");
    uint32_t col = ca ? FertAttr_color(ca) : 0xffffffffu;
    image(FEShapes_Image(s_pics, num ? Fert_ParseInt(num) : 0), (float)(p[0] + ox), (float)(p[1] + oy), col);   /* 0x508350 */
}

/* NFS5.ANIMSHOWCASEPICTURE (FEAnimatingShowcaseControl, 0x1d0 bytes, vtable 0x5b55d8): show.fsh's four
   pictures, each a left half (shapes 0 .. 3) and a right one (4 .. 7), one shown 128 ticks, then fading
   into the next over 64. Over them five copies of its label's text drift (0x520110 / 0x520160) in
   Univers7b and Univers11b, across 200, 0 .. 400, 200 of the screen. */
typedef struct {
    FertControl *c;
    char path[300];
    int cur, next;        /* +0x148, +0x14c */
    bool fading;          /* +0x150 */
    uint32_t since;       /* +0x154 */
    int span;             /* +0x158 */
    char label[100];      /* +0x164 */
    DriftWord words[5];   /* +0x160 (+0x15c the count) */
    HudFont *fonts[2];    /* +0x1c8 (+0x1cc the count) */
} Anim;
static Anim s_anim;

static HudFont *font(const char *name)   /* 0x4db960: a front end font by name */
{
    char file[64], path[300];
    snprintf(file, sizeof file, "%s.ffn", name);
    FE_ResolvePath(file, path, sizeof path);
    return FEFont_Get(path);
}

static void anim_start(FertControl *c)   /* v20 0x4bd770 */
{
    if (s_anim.c) {
        FEShapes_Release(s_anim.path);
        for (int i = 0; i < 2; i++) FEFont_Release(s_anim.fonts[i]);
    }
    memset(&s_anim, 0, sizeof s_anim);
    s_anim.c = c;
    FE_ResolvePath("show.fsh", s_anim.path, sizeof s_anim.path);   /* ("%sshow.fsh" 0x5d78d4) */
    FEShapes_Load(s_anim.path);
    int k = FertControl_findPart(c, "label");   /* 0x4c8730 ("label" 0x5d78ec) */
    snprintf(s_anim.label, sizeof s_anim.label, "%s", 0 <= k ? FEPart_Text(c->parts[k]) : "");
    DriftWord_Box(200, 0, 400, 200);   /* 0x5200e0 */
    for (int i = 0; i < 5; i++) DriftWord_Init(&s_anim.words[i]);
    s_anim.fonts[0] = font(exe_str(0x5d77a8));   /* "Univers7b" */
    s_anim.fonts[1] = font(exe_str(0x5d78e0));   /* "Univers11b" */
    s_anim.next = 1;
    s_anim.since = timer_ticks();
    s_anim.span = 0x80;
}

static void anim_draw(FertControl *c, int32_t ox, int32_t oy)   /* v15 0x4bd9a0 */
{
    if (s_anim.c != c) anim_start(c);
    Anim *a = &s_anim;
    int32_t p[2];
    ctl_pos(c, p);
    float x = (float)(p[0] + ox), y = (float)(p[1] + oy);
    FE2D_DepthPushStep();   /* 0x4da4c0 */
    const FeImage *lc = FEShapes_Image(a->path, a->cur), *rc = FEShapes_Image(a->path, 4 + a->cur);
    float xr = x + (float)(lc ? lc->w : 0);   /* (the right halves beside the current's left one) */
    uint32_t now = timer_ticks();
    if (!a->fading) {
        if (a->span < (int)(now - a->since)) {
            a->fading = true;
            a->next = a->cur + 1 > 3 ? 0 : a->cur + 1;
            a->since = timer_ticks();
            a->span = 0x40;
        }
        image(lc, x, y, 0xffffffffu);
        image(rc, xr, y, 0xffffffffu);
    } else {
        int t = (int)(timer_ticks() - a->since) * 0xff / a->span;
        if (t > 0xff) t = 0xff;
        uint32_t in = (uint32_t)t << 24 | 0xffffffu, out = (uint32_t)(0xff - t) << 24 | 0xffffffu;
        image(FEShapes_Image(a->path, a->next), x, y, in);
        image(FEShapes_Image(a->path, 4 + a->next), xr, y, in);
        image(lc, x, y, out);
        image(rc, xr, y, out);
        if (a->span < (int)(timer_ticks() - a->since)) {
            a->fading = false;
            a->cur = a->next;
            a->since = timer_ticks();
            a->span = 0x80;
        }
    }
    FE2D_OverlayBegin();   /* 0x4da570 */
    uint32_t wc = 0xa0ffffffu;   /* (0x4d82a0 by the control's fade, 0x4c8410: 0xff) */
    for (int i = 0; i < 5; i++) DriftWord_Draw(&a->words[i], a->label, wc, a->fonts, 2);   /* 0x520160 */
    FE2D_OverlayEnd();     /* 0x4da5b0 */
}

bool FEShowcase_Draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (!strcmp(c->id, "NFS5.ShowcasePicture")) {
        picture_draw(c, ox, oy);
        return true;
    }
    if (!strcmp(c->id, "NFS5.ANIMSHOWCASEPICTURE")) {
        anim_draw(c, ox, oy);
        return true;
    }
    return false;
}
