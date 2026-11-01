/* The front end's side of FERT's parts and controls: its font cache (0x4e2fb0 .. 0x4e3420), its strings
   (0x4e0090 .. 0x4e0270), the text parts' measuring (FETextPart_v4 0x4e6800, FELocalizedTextPart_v4
   0x4e5d80) and the parts' and controls' draws into the 2D queue (FERectPart_v5 0x4e5f60, the text
   part's 0x4e69c0; FEControl slot 15 0x4c7e30 -> FERT_Control_v15 0x5250b0). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "front/fert.h"
#include "render/hud2d.h"
#include "front/fe2d.h"

/* The folders the original's path table holds (0x65b330 controls, 0x65b334 art, 0x65b340 locale,
   0x65b344 layouts; set at start). */
#define FE_DIR_CONTROLS "FEData\\Controls"
#define FE_DIR_ART "FEData\\Art\\"
#define FE_DIR_LOCALE "FEData\\Locale\\"
#define FE_DIR_LAYOUTS "FEData\\Layouts\\"

/* A font by its path, loaded once and counted (0x4e2fb0: the path lowercased; the file whole and a
   drawer made from it, its texture state 1 off); its release (0x4e31d0). NULL if it can't be read. */
HudFont *FEFont_Get(const char *path);
void FEFont_Release(HudFont *f);

/* The string table (0x4e0090: <locale>FEStrings.loc, the options' language: 7 and up 0, below 0 6);
   a string by id (0x4e0270): "" for 0, "Not Found" for none. */
void FEStrings_Load(int language);
const char *FEStrings_Get(int id);

/* An FSH's shapes as images (FE_ValidateShapes 0x4d67f0): loaded once and counted, packed into 256 x 256
   texture pages by the places they store; an image by index (0x4d74e0); a release (0x4d7270). */
void FEShapes_Load(const char *path);
FeImage *FEShapes_Image(const char *path, int index);
void FEShapes_Release(const char *path);

/* A path name attribute as a file path (0x52caa0): as it is when absolute ("x:", "\\\\"), else under
   the registry's root (the art folder). */
void FE_ResolvePath(const char *name, char *out, int cap);

/* The hooks FERT's updates and draws call: a text part measured (its font, its text, localized by its
   stringID), and drawn. Installs Fert_MeasureText. */
void FEParts_Init(void);
/* A part at the control's origin (slot 5). */
void FEPart_Draw(FertPart *p, int32_t ox, int32_t oy);
/* A control's current state's parts at its position plus the offset (0x4c7e30 / 0x5250b0). */
void FEControl_Draw(FertControl *c, int32_t ox, int32_t oy);
/* A window: its controls, the last first (0x528390). */
void FEWindow_Draw(FertControl *w);

/* The application's own draws for controls whose look follows its data (an accessor's value): true when
   it drew the control (the window's draw then skips it). */
extern bool (*FE_ControlDrawHook)(FertControl *c, int32_t ox, int32_t oy);
/* The front end's styles (0x520780 ..), for the application's draws. */
void FEStyle_DropShadow(float x, float y, float w, float h, int a, int dx, int dy);
void FEStyle_Shine(float x, float y, float w, float h, int a);
void FEStyle_Inset(float x, float y, float w, float h, int a, int b);
/* A glow b pixels out round a box (0x5213e0): white at it, half white at the outside. */
void FEStyle_Glow(float x, float y, float w, float h, int a, int b);
