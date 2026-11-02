/* The car logo: see fecarlogo.h. */
#include "front/fecarlogo.h"
#include "exe.h"
#include "app.h"
#include "front/fe2d.h"
#include "front/fe_parts.h"
#include "front/feaccess.h"
#include "front/fechronicle.h"
#include "support/crt.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FertControl *c;
    FeImage *logo, *name;   /* +0x148, +0x14c */
    int index;              /* +0x150: the logo's first shape */
    char file[32];          /* +0x154: the loaded logo file ("" none) */
    char icons[300];        /* +0x17c: WeeIcons.fsh, its GLNT +0x178 */
    FeImage *glint;
    bool sweeping;          /* +0x212 */
    uint32_t start;         /* +0x214 */
    int wait;               /* +0x218 */
} Logo;

static Logo s_logos[4];

/* TIMER_ticks (0x53c050), 128 a second, on the port's clock. */
static uint32_t timer_ticks(void) { return (uint32_t)((uint64_t)app_ticks_ms() * 128u / 1000u); }

/* rand() % 128 the MSVC way (a negative remainder can't come of rand's 15 bits). */
static int rand128(void) { return CRT_rand() % 128; }

static const char *data_str(const FertControl *c, const char *name)
{
    for (int k = 0; k < c->nparts; k++) {
        FertAttr *a = FertAttrList_find(&c->parts[k]->attrs, name);
        if (a) return FertAttr_str(a);
    }
    return NULL;
}

static int data_int(const FertControl *c, const char *name)   /* 0x4c86d0 */
{
    for (int k = 0; k < c->nparts; k++) {
        FertAttr *a = FertAttrList_find(&c->parts[k]->attrs, name);
        if (a) return FertAttr_int(a);
    }
    return 0;
}

static void art_path(const char *file, char *out, int cap)   /* "%s%s.fsh" 0x5cccc8 of 0x65b334 */
{
    char n[64];
    snprintf(n, sizeof n, "%s.fsh", file);
    FE_ResolvePath(n, out, cap);
}

static void release_file(Logo *l)
{
    if (!l->file[0]) return;
    char p[300];
    art_path(l->file, p, sizeof p);
    FEShapes_Release(p);   /* 0x4d7270 */
    l->file[0] = 0;
}

/* On screen (v21 0x4c0f10): no logo yet, WeeIcons loaded for the glint, the first glint after up to 127
   ticks. */
static Logo *logo_of(FertControl *c)
{
    Logo *free_slot = NULL;
    for (int i = 0; i < 4; i++) {
        if (s_logos[i].c == c) return &s_logos[i];
        if (!s_logos[i].c && !free_slot) free_slot = &s_logos[i];
    }
    if (!free_slot) return NULL;
    Logo *l = free_slot;
    memset(l, 0, sizeof *l);
    l->c = c;
    l->index = -1;
    FE_ResolvePath("weeicons.fsh", l->icons, sizeof l->icons);   /* ("%sweeicons.fsh" 0x5d7bf8) */
    FEShapes_Load(l->icons);
    l->glint = FEShapes_Named(l->icons, "GLNT");   /* (0x4d75f0, 0x5d7bf0) */
    l->start = timer_ticks();
    l->wait = rand128();
    return l;
}

void FECarLogo_Drop(bool (*keep)(const FertControl *c))
{
    for (int i = 0; i < 4; i++) {
        Logo *l = &s_logos[i];
        if (!l->c || keep(l->c)) continue;
        release_file(l);   /* v22 0x4c1010 */
        FEShapes_Release(l->icons);
        memset(l, 0, sizeof *l);
    }
}

static void clear(Logo *l) { l->logo = l->name = NULL; }

/* The car by data.othersource or data.accessor (0x4c10c0): its nfs5.car record, NULL without one. */
static const uint8_t *logo_car(const FertControl *c)
{
    const char *src = data_str(c, "othersource");
    if (!src) src = "";
    if (!strcmp(src, "FactoryDriver")) return FEAccess_CarRecord(FEAccess_Find("FactoryDriver.Car"));
    if (!strcmp(src, "EndOfEra")) return FEAccess_CarRecord(FEAccess_Find("PrizeCars.Car"));   /* (not ported: none) */
    /* (0x4f39e0 of the mission's prize id, 0x51a8d0: the prize accessor's) */
    if (!strcmp(src, "FactoryDriverPrize")) return FEAccess_CarRecord(FEAccess_Find("FDPrizeCars.Car"));
    const char *acc = data_str(c, "accessor");
    FEAccessor *a = acc && *acc ? FEAccess_Find(acc) : NULL;
    if (a) return FEAccess_CarRecord(a);
    /* CarCompare1: the compared car (0x50df80's accessor's) */
    if (!strcmp(src, exe_str(0x5d7c28))) return FEAccess_CarRecord(FEAccess_Find("CarCompare.Data"));
    return NULL;
}

/* The chronicle's (0x4c1240 with othersource Chron): the shown media's second shape names the logo; a
   different logo file or shape lets the old file go and loads the new, when it exists. */
static void update_chron(Logo *l)
{
    const char *m = FEChronicle_Shown();   /* 0x506b40 of 0x699cfc */
    const FeImage *im = m ? FEShapes_Image(m, 1) : NULL;
    if (!im || strlen(im->name) < 3) {
        clear(l);
        release_file(l);
        return;
    }
    const char *file = im->name + 2;
    int index = ((im->name[0] - '0') * 10 + (im->name[1] - '0')) * 2;
    if (!strcmp(l->file, file) && index == l->index) return;
    release_file(l);
    char p[300];
    art_path(file, p, sizeof p);
    if (!vfs_exists(p)) {   /* 0x59d9e0 */
        clear(l);
        return;
    }
    l->index = index;
    snprintf(l->file, sizeof l->file, "%s", file);
    FEShapes_Load(p);
    l->logo = FEShapes_Image(p, index);   /* 0x4d74e0 */
    l->name = FEShapes_Image(p, index + 1);
}

/* Its update (v1 0x4c1240): a car's logo file (+0x147) and logo (+0x1f6, twice it the shape), the shapes
   anew when either changed. Without a car the last stay. */
static void update(Logo *l)
{
    const char *src = data_str(l->c, "othersource");
    if (src && !strcmp(src, "Chron")) {
        update_chron(l);
        return;
    }
    const uint8_t *r = logo_car(l->c);
    if (!r) return;
    const char *file = (const char *)r + 0x147;
    int16_t logo;
    memcpy(&logo, r + 0x1f6, 2);
    char p[300];
    if (!strcmp(l->file, file)) {
        if (l->index == logo * 2) return;
    } else {
        if (file[0]) {
            art_path(file, p, sizeof p);
            FEShapes_Load(p);   /* FE_ValidateShapes */
        }
        release_file(l);
        snprintf(l->file, sizeof l->file, "%s", file);
    }
    l->index = logo * 2;
    if (!l->file[0]) {
        clear(l);
        return;
    }
    art_path(l->file, p, sizeof p);
    int count = 0;
    while (FEShapes_Image(p, count)) count++;   /* 0x4d73d0 */
    if (count <= l->index) {
        clear(l);
        return;
    }
    l->logo = FEShapes_Image(p, l->index);
    l->name = FEShapes_Image(p, l->index + 1);
}

/* Its draw (v15 0x4c1750): the logo and its name (none when it is a 1-pixel-high shape) side by side from
   the control's place, left, centred (align 1) or right (2) of it, centred down its box, in white at the
   control's fade (0x4c8410: 0xff; the transition offset 0x4c8330: none). Over them (drawn over what is
   there, so only on them) the glint every so often: across the screen's width (0x4e2d30: 640) in as many
   ticks, at the control's top, white at a quarter of the fade; then a wait of up to 127 ticks. (The port
   runs the update with the draw: it changes nothing but the logo.) */
bool FECarLogo_Draw(FertControl *c, int32_t ox, int32_t oy)
{
    if (strcmp(c->id, "NFS5.CarLogo")) return false;
    Logo *l = logo_of(c);
    if (!l) return true;
    update(l);
    if (!l->logo || !l->name) return true;
    int32_t p[2] = {0, 0};
    FertAttr *pa = FertAttrList_find(&c->attrs, "pos");   /* 0x524b90 */
    if (pa) FertAttr_point(pa, p);
    int x = p[0] + ox, y = p[1] + oy;
    FE2D_DepthPushStep();   /* 0x4da4c0 */
    int w2 = l->name->h == 1 ? 0 : l->name->w;
    int align = data_int(c, "align");
    if (align == 1) x -= (l->logo->w + w2) / 2;
    else if (align == 2) x -= l->logo->w + w2;
    int32_t r[4];
    FertControl_rect(c, r);   /* 0x4c8240 */
    float ly = (float)((r[3] - r[1]) / 2 - l->logo->h / 2 + y);
    int fade = 0xff;
    uint32_t white = (uint32_t)fade << 24 | 0xffffffu;   /* 0x4d8280 */
    FE2D_Image(l->logo, (float)x, ly, (float)l->logo->w, (float)l->logo->h, white, 0, false);   /* 0x4d95a0 */
    if (w2 > 0) FE2D_Image(l->name, (float)(x + l->logo->w), ly, (float)l->name->w, (float)l->name->h, white, 0, false);
    FE2D_OverlayBegin();   /* 0x4da570 */
    uint32_t now = timer_ticks();
    if (!l->sweeping && (int)(l->start + (uint32_t)l->wait) < (int)now) {
        l->sweeping = true;
        l->start = now;
    }
    if (l->sweeping) {
        int t = (int)(timer_ticks() - l->start);
        if (640 <= t) {
            l->sweeping = false;
            l->start = timer_ticks();
            l->wait = rand128();
        } else if (l->glint) {
            uint32_t g = (uint32_t)((0x40u * fade) / 0xff) << 24 | 0xffffffu;   /* 0x4d82a0 of white */
            FE2D_Image(l->glint, (float)t, (float)y, (float)l->glint->w, (float)l->glint->h, g, 0, false);
        }
    }
    FE2D_OverlayEnd();   /* 0x4da5b0 */
    return true;
}
