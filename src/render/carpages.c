/* The car's texture pages: see carpages.h. */
#include "render/carpages.h"
#include "render/car_pending.h"
#include "exe.h"
#include "pending.h"
#include "real/file.h"
#include "real/locatbig.h"
#include "real/mem.h"
#include "real/shape.h"
#include "render/rmaterial.h"
#include "render/rtex.h"
#include "support/ufile.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

uint8_t *g_carschemes;              /* 0x6115f4 */
uint32_t g_carnschemes;             /* 0x6115f8 */
CarPage g_carpages[2];              /* 0x611610 (an array new'd with its count in front) */
void *g_carfiles[5];                /* 0x61160c */
static R3DMaterialSet *g_carset;    /* 0x6115ec: the global pages' material set */
static bool common_loaded;
/* The global pages' texture states (LoadCommon's stack in the original, which the pages point at). */
static TextureStates common_states;

/* The car directory (0x65b300: an entry of the path table the game fills at startup, not ported). */
const char *g_r3d_cardir = "GameData\\CarModel\\";
/* The track's night flag (0x628a28, the track module's): 0 by day. */
int16_t g_r3d_night;
/* The front end's game type ("Game.GameType" of its data, 0x6573e8) and the name of the race's car
   (0x657444): read through the front end's data areas (0x4b78a0), not ported. */
static int g_gametype;
static char g_carname[64];

static uint32_t rd32le(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int16_t rd16s(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

/* A shape's position on its page: the low 12 bits and bits 16..27 of the record's +0xc, signed. */
static int pos_x(const uint8_t *shape) { return (int32_t)(rd32le(shape + 0xc) << 20) >> 20; }
static int pos_y(const uint8_t *shape) { return (int32_t)(rd32le(shape + 0xc) << 4) >> 20; }

const uint8_t *SHAPE_find(const uint8_t *fsh, uint32_t code)
{
    int i = (int)rd32le(fsh + 8);
    if (i == 0) return NULL;
    /* From the last entry down; entry 0 is looked at only after the loop. */
    for (;;) {
        i--;
        if (i == 0) break;
        if (rd32le(fsh + 0x10 + 8 * i) == code) break;
    }
    if (rd32le(fsh + 0x10 + 8 * i) != code) return NULL;
    return fsh + rd32le(fsh + 0x14 + 8 * i);
}

/* ---- pages ---- */

void CarPage_init(CarPage *p)
{
    memset(p, 0, sizeof *p);
    p->bpp = 0x613;
    p->texclass = 2;
}

void CarPage_free(CarPage *p)
{
    if (p->shared) return;
    if (p->damage) WINDOW_destroy(p->damage);
    if (p->window) WINDOW_destroy(p->window);
    p->damage = p->window = NULL;
}

void CarPage_fromwindow(CarPage *p, Window *w, bool damage)
{
    CarPage_free(p);
    p->hasdamage = damage;
    p->width = w->width;
    p->height = w->height;
    Texture *t = RTexBank_get(&p->set->bank, w->shape, 0, p->name, p->states, 0, true, 0);
    p->texture = t;
    /* Without a texture manager (no render system yet) the bank makes no texture; the original always
       has one. */
    p->texwidth = t ? t->width : 0;
    p->texheight = t ? t->height : 0;
    p->shared = 0;
    if (damage && t) {
        uint32_t format;
        uint8_t *shape;
        if (Texture_info(t, &format, &shape)) {
            p->damage = WINDOW_new(rd16s(shape + 4), rd16s(shape + 6), w->depth, 0x10);
            if (p->damage) {
                WINDOW_select(p->damage);
                WINDOW_drawshape(shape, 0, 0);
                WINDOW_deselect();
            }
            Texture_invalidate(t);
        }
    }
}

Window *CarPage_Lock(CarPage *p)
{
    uint32_t format;
    uint8_t *shape = NULL;
    p->window = NULL;
    if (p->texture && Texture_info(p->texture, &format, &shape) && shape) p->window = WINDOW_fromshape(shape, 0x10);
    if (p->window && p->damage) {
        WINDOW_select(p->window);
        WINDOW_drawshape(p->damage->shape, 0, 0);   /* 0x53d2e0 */
        WINDOW_deselect();
    }
    return p->window;
}

void CarPage_Unlock(CarPage *p)
{
    if (p->window) WINDOW_destroy(p->window);   /* 0x533cf0 */
    p->window = NULL;
    if (p->texture) Texture_invalidate(p->texture);   /* 0x535d60 */
}

/* What 0x447a40 and 0x447ac0 share: the source's record, the own set kept, the texture under the
   page's name in the own set's bank if it isn't there yet. */
static void page_share(CarPage *p, const CarPage *src, const uint32_t *name)
{
    CarPage_free(p);
    struct R3DMaterialSet *set = p->set;
    *p = *src;
    p->window = NULL;
    if (name) p->name = *name;
    p->set = set;
    if (!RTexBank_find(&p->set->bank, 0, p->name))
        RTexBank_insert(&p->set->bank, p->texture, 0, p->name, p->states, 0, true);
    p->shared = 1;
}

void CarPage_share(CarPage *p, const CarPage *global) { page_share(p, global, NULL); }

void CarPage_copy(CarPage *p, const CarPage *src, uint32_t name) { page_share(p, src, &name); }

/* ---- the shape blit with recolouring ---- */

/* A colour recoloured by the car's slots, or kept: the alpha byte picks the slot (its top 20 levels;
   levels 0, 10 and 20 keep the colour), the red byte is the intensity. */
static bool tint_slot(uint32_t c, int *slot)
{
    int k = -(int)(((c >> 24) + 1) * 0x14 >> 8);
    if (k == -0x14 || k == -10 || k == 0) return false;
    int i = k + 0x13;
    if (i > 9) i = k + 0x12;
    *slot = i;
    return true;
}

static uint32_t tint(uint32_t c, const uint32_t *colours)
{
    int i;
    if (!tint_slot(c, &i)) return c;
    uint32_t s = colours[i], v = c >> 16 & 0xff;
    return ((c >> 24) ? 0xff000000u : 0) | ((s >> 16 & 0xff) * v >> 8) << 16 | ((s >> 8 & 0xff) * v >> 8) << 8 |
           ((s & 0xff) * v >> 8);
}

static uint16_t to1555(uint32_t c)
{
    return (uint16_t)((((uint32_t)((c >> 24) != 0) << 8 | (c >> 16 & 0xf8)) << 5 | (c >> 8 & 0xf8)) << 2 | (c >> 3 & 0x1f));
}

static uint16_t to4444(uint32_t c)
{
    return (uint16_t)((c >> 24 & 0xf0) << 8 | (c >> 16 & 0xf0) << 4 | (c >> 8 & 0xf0) | (c & 0xff) >> 4);
}

/* The page pixel of (x, y), NULL outside the window: the original's direct paths don't clip (the data
   keeps every shape on its page); the port drops what would land outside. */
static uint8_t *page_px(Window *w, int x, int y, int bytes)
{
    if (x < 0 || y < 0 || x >= w->width || y >= w->height) return NULL;
    return w->shape + 0x10 + (x + y * w->width) * bytes;
}

void R3DCar_BlitShape(Window *w, const uint8_t *shape, bool tinted, int bpp, const uint32_t *colours, int dx, int dy)
{
    int bits = SHAPE_typevalid(shape);
    int sw = rd16s(shape + 4), sh = rd16s(shape + 6), x0 = pos_x(shape) + dx, y0 = pos_y(shape) + dy;
    const uint8_t *src = shape + 0x10;
    if (bits == 8 && tinted) {
        /* The palette recoloured once. 255 entries: the 256th stays as the stack left it in the
           original (here 0). */
        uint32_t pal[256] = {0};
        const uint8_t *clut = SHAPE_clut(shape);
        for (int i = 0; i < 255 && clut; i++) pal[i] = tint(rd32le(clut + 0x10 + 4 * i), colours);
        if (bpp != 0x20 && bpp != 0x613 && bpp != 0x115c) return;   /* nothing for other formats */
        for (int y = 0; y < sh; y++)
            for (int x = 0; x < sw; x++) {
                uint32_t c = pal[*src++];
                uint8_t *d = page_px(w, x0 + x, y0 + y, bpp == 0x20 ? 4 : 2);
                if (!d) continue;
                if (bpp == 0x20) memcpy(d, &c, 4);
                else {
                    uint16_t v = bpp == 0x613 ? to1555(c) : to4444(c);
                    memcpy(d, &v, 2);
                }
            }
        return;
    }
    if (bits == 0x20 && tinted && (bpp == 0x20 || bpp == 0x613)) {
        for (int y = 0; y < sh; y++)
            for (int x = 0; x < sw; x++, src += 4) {
                uint32_t c = rd32le(src);
                uint8_t *d = page_px(w, x0 + x, y0 + y, bpp == 0x20 ? 4 : 2);
                if (!d) continue;
                if (bpp == 0x20) {
                    uint32_t v = tint(c, colours);
                    memcpy(d, &v, 4);
                    continue;
                }
                int i;
                uint16_t v;
                if (!tint_slot(c, &i)) v = to1555(c);
                else {
                    /* Tinted straight to 1555: blue takes the top five bits of the product (>> 11). */
                    uint32_t s = colours[i], l = c >> 16 & 0xff;
                    v = (uint16_t)((((uint32_t)((c >> 24) != 0) << 8 | ((s >> 16 & 0xff) * l >> 8 & 0xf8)) << 5 |
                                    ((s >> 8 & 0xff) * l >> 8 & 0xf8)) << 2 | (uint8_t)((s & 0xff) * l >> 11));
                }
                memcpy(d, &v, 2);
            }
        return;
    }
    if (bpp == 0x20 && bits == 0x20) {
        for (int y = 0; y < sh; y++)
            for (int x = 0; x < sw; x++, src += 4) {
                uint8_t *d = page_px(w, x0 + x, y0 + y, 4);
                if (d) memcpy(d, src, 4);
            }
        return;
    }
    /* Everything else through the 2D library, at the shape's own position (the mirror offset isn't
       applied on this path). */
    WINDOW_select(w);
    WINDOW_clipall();
    WINDOW_drawshape(shape, pos_x(shape), pos_y(shape));
    WINDOW_deselect();
}

/* ---- the car's virtuals ---- */

/* A shape's section: "file%d.%c%c%c%c" with the code's characters (a NUL ends the name; trim: trailing
   blanks become NULs first, as 0x439b90 does and 0x439e10 / 0x4447b0 don't). */
static void shape_section(char *out, size_t cap, int file, uint32_t code, bool trim)
{
    char c[4] = {(char)code, (char)(code >> 8), (char)(code >> 16), (char)(code >> 24)};
    if (trim)
        for (int i = 3; i >= 0 && c[i] == ' '; i--) c[i] = 0;
    /* A NUL character ends the name, as in the original's sprintf. */
    snprintf(out, cap, exe_str(0x5cd704), file + 1, c[0], c[1], c[2], c[3]);   /* "file%d.%c%c%c%c" */
}

bool R3DCar_UseShape(R3DCar *car, int file, uint32_t code)
{
    R3DCarSpec *spec = car->state->spec;
    if (spec && spec->customtpage) return false;
    bool use = true;
    char sec[64];
    shape_section(sec, sizeof sec, file, code, true);
    if (IniData_SelectSection(car->tpg.ini, sec) < 0) return use;
    int count = IniFile_GetInt(&car->tpg, exe_str(0x5cd658), 0);             /* "count" */
    if (count > 0) {
        use = false;
        char key[32];
        for (int i = 1; i <= count; i++) {
            snprintf(key, sizeof key, exe_str(0x5cd64c), i);                 /* "geometry%d" */
            int opt = IniFile_GetInt(&car->tpg, key, -1);
            if (opt < 0) {
                snprintf(key, sizeof key, exe_str(0x5cd640), i);             /* "texture%d" */
                opt = IniFile_GetInt(&car->tpg, key, 0) + 0x3c;
            }
            snprintf(key, sizeof key, exe_str(0x5cd638), i);                 /* "type%d" */
            int type = IniFile_GetInt(&car->tpg, key, 0);
            if (opt >= 0 && opt < CAR_NOPTIONS && car->state->options[opt] == type) {
                use = true;
                break;
            }
        }
    }
    int frontend = IniFile_GetInt(&car->tpg, exe_str(0x5cd6f8), -1);        /* "frontend" */
    int testdrive = IniFile_GetInt(&car->tpg, exe_str(0x5cd6ec), -1);       /* "testdrive" */
    int night = IniFile_GetInt(&car->tpg, exe_str(0x5cd6e4), -1);           /* "night" */
    if (frontend > -1 && frontend != (g_r3d_frontend != 0)) use = false;
    bool gametype = false;
    if (night > -1) {
        if (!g_r3d_frontend) {
            if (night != g_r3d_night) use = false;
        } else
            gametype = true;
    } else if (g_r3d_frontend)
        gametype = true;
    if (gametype) {
        /* The front end's game type (and in mode 9 the chosen car's name into 0x657444). */
        PENDING_NOTE(0x4b78a0, "FE data Game.GameType");
        g_carname[0] = 0;
    }
    /* "skidpad" (0x5cc54c): the test drive shapes follow the game type; elsewhere testdrive=1 hides. */
    bool skidpad = strcasecmp(g_carname, exe_str(0x5cc54c)) == 0;
    if (testdrive <= -1) return use;
    if (skidpad) {
        if (testdrive != (g_gametype == 9)) use = false;
        return use;
    }
    if (testdrive == 1) use = false;
    return use;
}

bool R3DCar_TintShape(R3DCar *car, int file, uint32_t code)
{
    bool t = true;
    char sec[64];
    snprintf(sec, sizeof sec, exe_str(0x5cd71c), file + 1);                  /* "file%d.details" */
    if (IniData_SelectSection(car->tpg.ini, sec) < 0) return t;
    t = IniFile_GetBool(&car->tpg, exe_str(0x5cd714), true);                 /* "tint" */
    shape_section(sec, sizeof sec, file, code, false);
    if (IniData_SelectSection(car->tpg.ini, sec) >= 0) t = IniFile_GetBool(&car->tpg, exe_str(0x5cd714), t);
    return t;
}

void R3DCar_LoadTextures(R3DCar *car, Window *w, const uint8_t *shape, int file, uint32_t code)
{
    char sec[64];
    shape_section(sec, sizeof sec, file, code, false);
    if (IniData_SelectSection(car->tpg.ini, sec) < 0) return;
    bool licence = IniFile_GetBool(&car->tpg, exe_str(0x5cde00), false);     /* "license" */
    bool racedecal = IniFile_GetBool(&car->tpg, exe_str(0x5cddf4), false);   /* "racedecal" */
    bool flyer = IniFile_GetBool(&car->tpg, exe_str(0x5cddec), false);       /* "flyer" */
    if (licence || racedecal || flyer)
        /* The licence text (spec +0x1c0), the race number (+0x510) or the flyer's number, drawn with the
           licence / race-decal fonts (license.ffn, racedecal.ffn) centred on the shape: the fonts
           (0x53c430, 0x53ea50, 0x53d730) aren't ported. */
        PENDING_NOTE(0x53d730, "FONT_drawtext (licence plates, race numbers)");
    (void)w;
    (void)shape;
}

void R3DCar_LoadDetails(R3DCar *car, int file, Window *w, int bpp)
{
    char sec[64];
    snprintf(sec, sizeof sec, exe_str(0x5cd71c), file + 1);                  /* "file%d.details" */
    IniData_SelectSection(car->tpg.ini, sec);
    bool mirror = IniFile_GetBool(&car->tpg, exe_str(0x5cdde0), false);      /* "mirror" */
    int mx = IniFile_GetInt(&car->tpg, exe_str(0x5cddd0), 0);                /* "mirroroffsetx" */
    int my = IniFile_GetInt(&car->tpg, exe_str(0x5cddc0), 0);                /* "mirroroffsety" */
    int global = IniFile_GetInt(&car->tpg, exe_str(0x5cdcfc), 0);            /* "global" */
    const void *fsh;
    if (global == 0) fsh = file >= 0 && (uint32_t)file < car->nsf ? car->sf[file] : NULL;
    else fsh = global >= 1 && global <= 5 ? g_carfiles[global - 1] : NULL;
    if (!fsh) return;
    for (int i = 0; i < (int)SHAPE_count(fsh); i++) {
        const uint8_t *shape = SHAPE_entry(fsh, i);
        uint32_t code = SHAPE_entryname(fsh, i);
        if (!R3DCar_UseShape(car, file, code) || !shape) continue;
        bool t = R3DCar_TintShape(car, file, code);
        R3DCar_BlitShape(w, shape, t, bpp, car->colours, 0, 0);
        R3DCar_LoadTextures(car, w, shape, file, code);
        if (mirror) {
            R3DCar_BlitShape(w, shape, t, bpp, car->colours, mx, my);
            R3DCar_LoadTextures(car, w, shape, file, code);
        }
    }
}

/* ---- the pages of the .tpg ---- */

void R3DCar_FreePages(R3DCar *car)
{
    for (uint32_t i = 0; i < car->npages; i++) CarPage_free(&car->pages[i]);
    free(car->pages);
    car->pages = NULL;
    car->npages = 0;
}

void R3DObject_LoadHeader(R3DCar *car)
{
    R3DCarSpec *spec = car->state->spec;
    bool custom = spec && spec->customtpage > 0;
    R3DCar_FreePages(car);
    if (car->tpg.ini) {
        IniData_SelectSection(car->tpg.ini, exe_str(0x5cdad8));               /* "header" */
        int ntpages = IniFile_GetInt(&car->tpg, exe_str(0x5cdd50), 0);        /* "numtpages" */
        int nfiles = IniFile_GetInt(&car->tpg, exe_str(0x5cdc40), 0);         /* "numfiles" */
        TextureStates *st = &car->pagestates;
        TextureStates_init(st);
        TextureStates_set(st, 1, 1);
        TextureStates_set(st, 0, 1);
        TextureStates_set(st, 2, 0);
        TextureStates_setmipmap(st, 0);
        TextureStates_set(st, 4, (int32_t)THRASH_f2u(0.25f));   /* 0x3e800000 */
        car->pages = calloc(ntpages > 0 ? (size_t)ntpages : 1, sizeof *car->pages);
        /* Which file draws each page ([fileN.details] tpage=); an alloca'd byte array in the original,
           not cleared (a page without a file reads what was on the stack): zero here. */
        uint8_t *map = calloc((size_t)(ntpages > 0 ? ntpages : 0) + 0x20, 1);
        char sec[64];
        if (ntpages > 0)
            for (int f = 1; f <= nfiles; f++) {
                snprintf(sec, sizeof sec, exe_str(0x5cd71c), f);                 /* "file%d.details" */
                IniData_SelectSection(car->tpg.ini, sec);
                int t = IniFile_GetInt(&car->tpg, exe_str(0x5cdd48), 0);        /* "tpage" */
                /* tpage 0 writes the byte before the array in the original. */
                if (t >= 1 && t <= ntpages) map[t - 1] = (uint8_t)(f - 1);
            }
        for (int page = 1; page <= ntpages; page++) {
            snprintf(sec, sizeof sec, exe_str(0x5cdd38), page);                 /* "tpage%d.details" */
            IniData_SelectSection(car->tpg.ini, sec);
            int width = IniFile_GetInt(&car->tpg, exe_str(0x5cdd30), 0);
            int height = IniFile_GetInt(&car->tpg, exe_str(0x5cdd28), 0);
            int bpp = IniFile_GetInt(&car->tpg, exe_str(0x5cdd24), 0x613);    /* "bpp": 1555 = 0x613 */
            IniFile_GetBool(&car->tpg, exe_str(0x5cdd18), false);             /* "persistent" (unused) */
            int source = IniFile_GetInt(&car->tpg, exe_str(0x5cdd0c), page);  /* "sourcetpage" */
            bool damage = IniFile_GetBool(&car->tpg, exe_str(0x5cdd04), true);
            int global = IniFile_GetInt(&car->tpg, exe_str(0x5cdcfc), 0);
            TextureStates_settile(st, IniFile_GetBool(&car->tpg, exe_str(0x5cdcf4), false));   /* "tile" */
            if (source != page && source >= 1 && source <= ntpages) map[page - 1] = map[source - 1];
            if (custom) bpp = 0x115c;
            CarPage *p = &car->pages[car->npages++];
            CarPage_init(p);
            p->set = car->materials;
            p->states = st;
            p->texclass = car->texinfo;
            if (source == page || car->textures) {
                if (global == 0) {
                    Window *w = WINDOW_new(width, height, bpp, 0x10);
                    if (!w) continue;
                    if (custom) {
                        WINDOW_select(w);
                        WINDOW_clear(0x40202020);
                        WINDOW_deselect();
                    }
                    R3DCar_LoadDetails(car, map[page - 1], w, bpp);   /* slot 10 */
                    p->name = (uint32_t)(page - 1);
                    CarPage_fromwindow(p, w, damage);
                    WINDOW_destroy(w);
                } else if (global >= 1 && global <= 2) {
                    p->name = 0;
                    CarPage_share(p, &g_carpages[global - 1]);
                }
            } else if (source >= 1 && source < page)
                CarPage_copy(p, &car->pages[source - 1], (uint32_t)(page - 1));
        }
        free(map);
    }
    /* The decals and liveries of the options (0x447b80, with the light or dark decal shapes). */
    if (car->textures) R3DCar_PendingDecals(car);
    R3DMaterialSet_Build(car->materials, car->root, "");
}

void R3DCar_GlobalTextures(R3DCar *car)
{
    static const uint32_t race[3] = {0x31435053, 0x32435053, 0x33435053};   /* 'SPC1' 'SPC2' 'SPC3' */
    static const uint32_t fe[3] = {0x55444546, 0x20204546, 0x48534546};     /* 'FEDU' 'FE  ' 'FESH' */
    const uint32_t *names = g_r3d_frontend ? fe : race;
    for (int i = 0; i < 3; i++) {
        Texture *t = g_rtex_global ? RTexBank_find(g_rtex_global, 0, names[i]) : NULL;
        /* 0x440870: into the slot, with its states tiled. */
        car->globaltex[i] = t;
        if (t) {
            TextureStates s = t->states;
            TextureStates_settile(&s, 1);
            Texture_setstates(t, &s);
        }
    }
}

/* ---- colours ---- */

void R3DCar_HSV(const uint8_t bgr[3], uint8_t hsv[3])
{
    const uint8_t *mx = bgr[1] <= bgr[2] ? &bgr[2] : &bgr[1];
    if (*mx < bgr[0]) mx = &bgr[0];
    const uint8_t *mn = bgr[2] <= bgr[1] ? &bgr[2] : &bgr[1];
    if (bgr[0] < *mn) mn = &bgr[0];
    unsigned max = *mx, min = *mn;
    hsv[2] = (uint8_t)max;
    uint8_t s = max == 0 ? 0 : (uint8_t)((max * 0xff - min * 0xff) / max);
    hsv[1] = s;
    if (s == 0) {
        hsv[0] = 0xfe;
        return;
    }
    float delta = (float)(max - min), h = 0;
    /* Sectors of 2 (0x5b2580) and 4 (0x5b3878, a double); 42.5 per sector (0x5b3874). */
    if (bgr[2] == max) h = (float)((int)bgr[1] - (int)bgr[0]) / delta;
    else if (bgr[1] == max) h = (float)((int)bgr[0] - (int)bgr[2]) / delta + 2.0f;
    else if (bgr[0] == max) h = (float)((int)bgr[2] - (int)bgr[1]) / delta + (float)4.0;
    hsv[0] = (uint8_t)(int)lrintf(h * 42.5f);
}

void R3DCar_SetColours(R3DCar *car, uint32_t scheme)
{
    R3DCarSpec *spec = car->state->spec;
    if (!spec) return;
    spec->scheme = scheme;
    if (scheme < g_carnschemes) {
        for (int i = 0; i < 18; i++) {
            /* A band names one of the four colours; the original reads further into the spec for
               larger values (packages.ini has none). */
            unsigned b = g_carschemes[scheme * 18 + i];
            car->colours[i] = b < 4 ? spec->colours[b] : 0;
        }
        uint8_t bgr[3] = {(uint8_t)spec->colours[0], (uint8_t)(spec->colours[0] >> 8), (uint8_t)(spec->colours[0] >> 16)};
        R3DCar_HSV(bgr, car->hsv);
        if (car->textures) R3DCar_PendingDecals(car);
    }
    if (car->crp && car->tpg.ini) {
        char sec[32];
        snprintf(sec, sizeof sec, exe_str(0x5cd660), car->state->style + 1);   /* "style%d" */
        IniData_SelectSection(car->tpg.ini, sec);
        if (IniFile_GetBool(&car->tpg, exe_str(0x5cd630), false))               /* "stripe" */
            car->colours[6] = spec->colours[1];
    }
}

/* ---- the shared art ---- */

void R3DCar_LoadCommon(void)
{
    if (common_loaded) return;
    common_loaded = true;
    char path[260];
    TexManager *m = g_texgraphics ? g_texgraphics->textures : NULL;
    /* 0x611614: the .crp cache's map (the port's cache is r3dcar.c's). */
    g_rtex_global = RTexBank_ctor(calloc(1, sizeof *g_rtex_global), m, 3);
    snprintf(path, sizeof path, exe_str(0x5cddb0), g_r3d_cardir);           /* "%scarcmn.fsh" */
    RTexBank_loadfile(g_rtex_global, path, 0x10000);
    /* The environment map (R3DCar_LoadEnvMap into 0x6115d8) and the race-decal and licence fonts
       (racedecal.ffn, license.ffn: 0x6115fc, 0x611604) pend. */
    PENDING_NOTE(0x440fa0, "R3DCar_LoadEnvMap and the car fonts");
    g_carset = R3DMaterialSet_ctor(calloc(1, sizeof *g_carset), m, 3);
    for (int i = 0; i < 5; i++) {
        snprintf(path, sizeof path, exe_str(0x5cccc8), g_r3d_cardir, exe_str(exe_u32(0x5cdafc + 4 * (uint32_t)i)));
        g_carfiles[i] = UTIL_loadfileabort(path, 0);                              /* shadow intglass suit head cabrio */
    }
    for (int i = 0; i < 2; i++) CarPage_init(&g_carpages[i]);
    TextureStates_init(&common_states);
    /* Page 0x8000: shadow.fsh's first shape as it is; page 0x8001: the interior glass ('glat' in the race,
       'glai' in the front end) converted to 4444. */
    for (int i = 0; i < 2; i++) {
        CarPage *p = &g_carpages[i];
        p->set = g_carset;
        p->states = &common_states;
        p->texclass = 3;
        p->name = 0x8000u + (uint32_t)i;
    }
    if (g_carfiles[0] && SHAPE_count(g_carfiles[0])) {
        Window *w = WINDOW_fromshape((uint8_t *)(uintptr_t)SHAPE_entry(g_carfiles[0], 0), 0);
        if (w) {
            CarPage_fromwindow(&g_carpages[0], w, false);
            WINDOW_destroy(w);
        }
    }
    const uint8_t *glass = g_carfiles[1] ? SHAPE_find(g_carfiles[1], rd32le((const uint8_t *)exe_str(g_r3d_frontend ? 0x5cdd80 : 0x5cdd88))) : NULL;
    if (glass) {
        Window *w = WINDOW_new(rd16s(glass + 4), rd16s(glass + 6), 0x115c, 0x10);
        if (w) {
            R3DCar_BlitShape(w, glass, false, 0x115c, NULL, 0, 0);
            CarPage_fromwindow(&g_carpages[1], w, false);
            WINDOW_destroy(w);
        }
    }
    snprintf(path, sizeof path, exe_str(0x5cdd70), g_r3d_cardir);            /* "%spackages.ini" */
    IniFile ini = {0};
    if (IniFile_Load(&ini, path) && IniData_SelectSection(ini.ini, exe_str(0x5cdad8)) >= 0 &&
        (g_carnschemes = (uint32_t)IniFile_GetInt(&ini, exe_str(0x5cd658), 1)) != 0) {
        g_carschemes = calloc(g_carnschemes, 18);
        char key[32];
        for (uint32_t p = 1; p <= g_carnschemes; p++) {
            snprintf(key, sizeof key, exe_str(0x5cdd64), p);                 /* "package%d" */
            IniData_SelectSection(ini.ini, key);
            for (int b = 0; b < 18; b++) {
                snprintf(key, sizeof key, exe_str(0x5cdd5c), b);             /* "band%d" */
                g_carschemes[(p - 1) * 18 + (uint32_t)b] = (uint8_t)IniFile_GetInt(&ini, key, b == 14 || b == 15 ? 3 : 0);
            }
        }
    }
    IniFile_Free(&ini);
}

void R3DCar_FreeCommon(void)
{
    if (!common_loaded) return;
    if (g_rtex_global) {
        RTexBank_clear(g_rtex_global);
        RTexBank_dtor(g_rtex_global);
        free(g_rtex_global);
        g_rtex_global = NULL;
    }
    for (int i = 0; i < 5; i++) {
        MEM_purge(g_carfiles[i]);
        g_carfiles[i] = NULL;
    }
    for (int i = 0; i < 2; i++) CarPage_free(&g_carpages[i]);
    if (g_carset) {
        R3DMaterialSet_dtor(g_carset);
        free(g_carset);
        g_carset = NULL;
    }
    free(g_carschemes);
    g_carschemes = NULL;
    g_carnschemes = 0;
    common_loaded = false;
}
