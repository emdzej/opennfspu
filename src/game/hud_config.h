/* The HUD's layouts (HUD_LoadStrings 0x41e190, 0x41f7c0, the getters 0x420470-0x4211d0): two files of
   records per configuration, SaveData\<player>1.hud (else XXhud1XX.hud; "HudConfigs", 0x1e0 bytes a
   record) and <player>2.hud (else XXhud2XX.hud; 0x68 bytes). The first holds the look: +0x00 the name,
   +0x14 the trays' colour (its alpha 0: no trays), +0x18 the labels', +0x34/+0x38 the text insets, +0x4c,
   +0x64, +0x7c the fonts (0x18 bytes each), from +0x94 the visuals' sizes in pixels, from +0x188 their
   places as fractions of the screen. The second the choices: +0x00 a size, +0x0c and +0x20 two more
   places, from +0x30 which visuals show and how (+0x50 the colour scheme, +0x64 the gear). The places
   become pixels once (0x41fb60) into a table of thirteen (0x60c828), which the visuals read and the HUD
   editor writes. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The places' table (0x60c828 + 8 k): the record's +0x188 + 8 k for k < 11, then the second record's
   +0x0c and +0x20. */
enum {
    HUDPOS_GEAR = 0,     /* 0x420b30 (+0x188) */
    HUDPOS_SPEED = 1,    /* 0x420b40 */
    HUDPOS_TIME = 2,     /* 0x420b50 */
    HUDPOS_TACHDIAL = 6, /* 0x420bb0 */
    HUDPOS_SPEEDO = 7,   /* 0x420bc0 */
    HUDPOS_MULTITIME = 11, /* 0x420b60: the second record's +0x0c */
    HUDPOS_TACH = 12,    /* 0x420b80: the second record's +0x20 */
    HUDPOS_COUNT = 13,
};

/* The dials' options (0x657df4 .. 0x657e1c, the race keys 0x6a .. 0x74: SPEEDOSTYLEKMH .. SPEEDOTOTALUNITS),
   which the front end takes from the car's nfs5.car record (0x4eadb0): +0x300 the km/h speedometer's
   {style, total units, -, start angle, degrees per unit (thousandths), -}, +0x30c the mph one's, +0x318
   the tachometer's {style, total units, needle style, start angle, degrees per unit, needle pivot}. */
typedef struct {
    int32_t speedo_kmh, speedo_mph;  /* 0x657df4, 0x657df8: the face's image */
    int32_t needle, pivot;           /* 0x657dfc, 0x657e00 */
    int32_t tach;                    /* 0x657e04 */
    int32_t tach_start, tach_deg, tach_units;        /* 0x657e08, 0x657e0c, 0x657e10 */
    int32_t speedo_start, speedo_deg, speedo_units;  /* 0x657e14, 0x657e18, 0x657e1c */
} HudDials;
extern HudDials g_hud_dials;
extern int32_t g_hud_splits;     /* 0x657dd8: SPLITTIMES (1 the record, 2 the best lap, 3 the last) */
extern int32_t g_hud_imperial;   /* 0x657dc0: miles (the front end's Display_SpeedUnits) */

/* The localised strings and the layouts for the player (HUD_LoadStrings 0x41e190, counted: only the
   first loads). w, h: the screen. */
void HudConfig_Load(const char *player, int config, int w, int h);
void HudConfig_Free(void);   /* 0x41e2e0 */

const uint8_t *HudConfig_loc(void);    /* 0x41e180: hud.loc */
const uint8_t *HudConfig_look(void);   /* the current first record (0x60c818[0x60c95c]) */
const uint8_t *HudConfig_choice(void); /* the second (0x60c904[0x60c95c]) */
int32_t HudConfig_i32(int off);        /* a 32-bit field of the first record (0x420470 ..) */
int32_t HudConfig_choicei32(int off);  /* of the second (0x421050 ..) */
const char *HudConfig_font(int off);   /* a font's name: +0x4c (0x420600), +0x64, +0x7c */
uint32_t HudConfig_scheme(void);       /* the highlight colour by the scheme (0x420510) */
bool HudConfig_notrays(void);          /* 0x41e5e0 */
/* The trays' alpha: the layout's default (0x5ccb34) or none (0x41e560, 0x41e5b0). */
void HudConfig_trays(bool on);
void HudConfig_pos(int k, int32_t *x, int32_t *y);   /* 0x420b30 .. 0x420bf0 */
void HudConfig_setpos(int k, int32_t x, int32_t y);  /* 0x420680 .. 0x420800 */
