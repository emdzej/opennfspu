/* The tournament's track map: see fetrackmap.h. */
#include "front/fetrackmap.h"
#include "app.h"
#include "front/fe2d.h"
#include "front/fe_parts.h"
#include "front/feaccess.h"
#include <stdio.h>
#include <string.h>

#define FE_DIR_TRACKART "FEData\\TrackArt\\"   /* 0x65b350 */
#define MAXRACES 50

typedef struct {
    FertControl *c;
    char names[MAXRACES][0x32];        /* +0x140: each race's map file loaded ("" none) */
    FeImage *map[MAXRACES];            /* +0xb04: shape 0 */
    FeImage *marks[MAXRACES];          /* +0xbcc: shape 1 */
    FeImage *marks_rev[MAXRACES];      /* +0xc94: shape 2 */
    int shown;                         /* +0xd5c: the race shown */
    const uint8_t *loaded;             /* +0xd60: the tournament whose maps are loaded */
    HudFont *font;                     /* +0xd68: Univers7b */
    uint32_t start;                    /* +0xd6c */
    const uint8_t *chosen;             /* +0xd70: Tournament.Tournament's value (v0x28) */
    int fade;                          /* +0xd74 */
} TrackMap;

static TrackMap s_maps[2];

/* TIMER_ticks (0x53c050), 128 a second, on the port's clock. */
static uint32_t timer_ticks(void) { return (uint32_t)((uint64_t)app_ticks_ms() * 128u / 1000u); }

static int16_t r16(const uint8_t *r, int off) { int16_t v; memcpy(&v, r + off, 2); return v; }
static int32_t r32(const uint8_t *r, int off) { int32_t v; memcpy(&v, r + off, 4); return v; }

static int races(const uint8_t *t) { return t ? r16(t, 0x39c) : 0; }   /* 0x502920 */
static const uint8_t *race_track(const uint8_t *t, int i) { return FEData_TrackById(r16(t, 0x3a0 + 0xa4 * i)); }   /* 0x502980 */

static void map_path(const char *name, char *out, int cap) { snprintf(out, (size_t)cap, "%s%s.fsh", FE_DIR_TRACKART, name); }   /* "%s%s.fsh" 0x5cccc8 */

/* On screen (v21 0x4fbf90): no maps, the font, the clock started. */
static TrackMap *map_of(FertControl *c)
{
    TrackMap *free_slot = NULL;
    for (int i = 0; i < 2; i++) {
        if (s_maps[i].c == c) return &s_maps[i];
        if (!s_maps[i].c && !free_slot) free_slot = &s_maps[i];
    }
    if (!free_slot) return NULL;
    TrackMap *m = free_slot;
    memset(m, 0, sizeof *m);
    m->c = c;
    char path[300];
    FE_ResolvePath("Univers7b.ffn", path, sizeof path);   /* ("%sUnivers7b.ffn" 0x5d7f90) */
    m->font = FEFont_Get(path);
    m->start = timer_ticks();
    m->chosen = FEData_Tournament();   /* (v20 0x4fbf40) */
    return m;
}

void FETrackMap_Drop(bool (*keep)(const FertControl *c))
{
    for (int i = 0; i < 2; i++) {
        TrackMap *m = &s_maps[i];
        if (!m->c || keep(m->c)) continue;
        /* v22 0x4fc040: the maps' files and the font let go */
        for (int k = 0; k < MAXRACES; k++)
            if (m->names[k][0]) {
                char p[300];
                map_path(m->names[k], p, sizeof p);
                FEShapes_Release(p);   /* 0x4d7270 */
            }
        if (m->font) FEFont_Release(m->font);   /* 0x4e31d0 */
        memset(m, 0, sizeof *m);
    }
}

/* Its update (v1 0x4fc0f0): the fade, up over the first 32 ticks of each 256 (8 a tick) and down over
   the last 31 (0x7ff less 8 a tick), the race shown the 256-tick step; past the last race, or with
   another tournament chosen, from the first again. A tournament not loaded yet: each race's map file
   (the track record's +0x58) and its three shapes, the files loaded before let go. */
static void update(TrackMap *m)
{
    int32_t t = (int32_t)(timer_ticks() - m->start);
    int u = t % 256;
    if (u < 0x20) m->fade = u * 8;
    if (0xe0 < u) m->fade = 0x7ff - u * 8;
    m->shown = t / 256;
    const uint8_t *tour = FEData_Tournament();   /* 0x506120 */
    if (races(tour) <= m->shown || FEData_Tournament() != m->chosen) {
        m->chosen = FEData_Tournament();
        m->shown = 0;
        m->start = timer_ticks();
    }
    if (m->loaded == tour) return;
    for (int i = 0; tour && i < races(tour) && i < MAXRACES; i++) {
        const uint8_t *tr = race_track(tour, i);
        const char *name = tr ? (const char *)tr + 0x58 : "";
        char p[300];
        if (!*name) {
            m->map[i] = NULL;
        } else {
            map_path(name, p, sizeof p);
            FEShapes_Load(p);   /* FE_ValidateShapes */
            m->map[i] = FEShapes_Image(p, 0);   /* 0x4d74e0 */
            m->marks[i] = FEShapes_Image(p, 1);
            m->marks_rev[i] = FEShapes_Image(p, 2);
        }
        if (m->names[i][0]) {
            map_path(m->names[i], p, sizeof p);
            FEShapes_Release(p);
        }
        snprintf(m->names[i], sizeof m->names[i], "%s", name);
    }
    m->loaded = tour;
}

static uint32_t gold(int a) { return (uint32_t)a << 24 | 0xffbb00u; }    /* 0x4d8270 */
static uint32_t white(int a) { return (uint32_t)a << 24 | 0xffffffu; }   /* 0x4d8280 */
static uint32_t faded(uint32_t c, int f) { return f == 0xff ? c : (((c >> 24) * (uint32_t)f) / 0xff) << 24 | (c & 0xffffffu); }   /* 0x4d82a0 */

/* Its draw (v15 0x4fc330): with the shown race's map, the map at its shape's own place, gold at the
   control's fade times the map's; the marks (by the race's +0x3a4, reversed) in white, at their shape's
   place, or mirrored across the map's width when the race's +0x3a5 is set (both flipped too); then the
   races down from (352, 280), 15 apart: "-" (0x5cd1fc) and, 4 on, the track's name (string +4 + 0x14).
   (Its position and the transition's offset, 0x524b90 and 0x4c8330, are worked out and not used.) */
bool FETrackMap_Draw(FertControl *c, int32_t ox, int32_t oy)
{
    (void)ox, (void)oy;
    if (strcmp(c->id, "NFS5.TrackSmall2dSpline")) return false;
    TrackMap *m = map_of(c);
    if (!m) return true;
    update(m);
    const uint8_t *tour = FEData_Tournament();
    if (!tour || MAXRACES <= m->shown || !m->map[m->shown]) return true;
    const uint8_t *race = tour + 0x3a0 + 0xa4 * m->shown;
    bool mirror = race[5] != 0;
    const FeImage *map = m->map[m->shown];
    const FeImage *marks = race[4] ? m->marks_rev[m->shown] : m->marks[m->shown];
    int fade = 0xff;   /* (the control's fade, 0x4c8410) */
    FE2D_Image(map, (float)-map->cx, (float)-map->cy, (float)map->w, (float)map->h, faded(gold(fade), m->fade), mirror, false);   /* 0x4d95a0 */
    if (marks) {
        float x = (float)(marks->cx < 0 ? -marks->cx : marks->cx);
        if (mirror) x = (((float)map->w - x) + (float)(map->cx < 0 ? -map->cx : map->cx)) - (float)marks->w;
        FE2D_Image(marks, x, (float)-marks->cy, (float)marks->w, (float)marks->h, white(fade), mirror, false);
    }
    if (!m->font) return true;
    int n = races(tour);
    for (int i = 0; i < n; i++) {
        const uint8_t *tr = race_track(tour, i);
        float y = (float)(0x118 + 15 * i);
        uint32_t col = i == m->shown ? gold(fade) : white(fade);
        FE2D_Text(m->font, 352.0f, y, 0, col, "-", 1.0f, 1.0f);   /* 0x4d9e00 */
        if (tr) FE2D_Text(m->font, 356.0f, y, 0, col, FEStrings_Get(r32(tr, 4) + 0x14), 1.0f, 1.0f);
    }
    return true;
}
