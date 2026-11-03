/* The race HUD (HUD_Init 0x424420, HUD_Draw 0x425910): a list of visuals (HOverlay and its classes, vtable
   0x5b2d0c: +0x8 a flag, +0xc the state, 0 hidden .. 2 shown, 3 its own), drawn each frame in passes over
   the overlay camera with the HUD's 2D layer (render/hud2d.h): the front visuals once the race has
   started, the others always, then the flagged ones. The visuals are ported one by one; the first is
   the start's countdown (H321Go). */
#pragma once
#include "game/race.h"
#include "render/render.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct HudVisual HudVisual;
typedef struct {
    void (*destroy)(HudVisual *v);                /* slot 0 */
    void (*draw)(HudVisual *v);                   /* slot 1 */
    void (*reset)(HudVisual *v);                  /* slot 2: the layout again (0x426760) */
    void (*setstate)(HudVisual *v, int32_t s);    /* slot 5: 0x537790, the trays' 0x4343f0 */
    void (*setpos)(HudVisual *v, int x, int y);   /* slot 12 */
    int (*progress)(HudVisual *v);                /* slot 14: of the fade, 0..100 (0x434950) */
    bool (*done)(HudVisual *v);                   /* slot 3: a modal visual finished (it leaves the modal list) */
    int (*input)(HudVisual *v, int x, int y, int click);   /* slot 9: the mouse; the action chosen (0 none) */
    bool (*hidden)(HudVisual *v);                 /* slot 10: kept from showing (NULL: never; 0x50d420) */
} HudVisualVtbl;

struct HudVisual {
    const HudVisualVtbl *vt;
    uint8_t flag;          /* +0x08 */
    int32_t state;         /* +0x0c (slots 5 and 6: 0x537790, 0x42a360) */
};

/* The HUD for the race (the visuals ported so far). 0x424420 */
void HUD_Init(void);
void HUD_LoadArt(void);   /* 0x427a20 */
/* The local player's lap done: the announcer's lap, gap, place, best and final lap (0x427790). */
void HUD_LapDone(void);
void HUD_Free(void);
/* The race over: the results and the other race-over screens in turn (0x426a40 with 1). */
void HUD_RaceOver(void);
/* The race's start (0x426a40 with 0): the cars' damage kept for the career's safe-driving bonus. */
void HUD_RaceStart(void);
/* The overlay over the mirror's view: the mouse's pointer. 0x4257d0 */
void HUD_DrawOverlay(RenderSystem *r, RCamera *c);
/* Whether the rear-view mirror is on and fully shown (its view is then drawn). 0x427560 */
bool HUD_RearViewShown(void);
/* The HUD's keys, none while a modal visual is up: the HUD shown or hidden (0x427370), the map
   (0x427320), the mirror (0x427510), the map's modes (0x426700), its zoom (0x4275c0, 0x4275f0), the
   trays (0x427590). */
void HUD_ToggleHud(void);
void HUD_ToggleMap(void);
void HUD_ToggleRearView(void);
void HUD_CycleMap(void);
void HUD_ZoomMap(bool in);
void HUD_ToggleTrays(void);
/* The HUD hidden or shown again while a modal visual is up. 0x425710 */
void HUD_TogglePauseMenu(void);
/* Whether a modal visual (a race screen, the pause menu) is up. 0x426a20 */
bool HUD_ModalShown(void);
/* Every visual hidden, the modal list emptied (the replay's start). 0x425740 */
/* The replay panel's restart (its action 7 in HUD_HandleInput): the results' screens not shown when
   the replay starts again (0x60ca10), the next song (0x4ae2a0). */
void HUD_ReplayRestart(void);
void HUD_HideAll(void);
/* The pause menu up (0x4267c0: HPause into the modal list, opening). */
void HUD_Pause(void);
/* The mouse to the HUD (HUD_HandleInput 0x425eb0): the first modal visual's action, the menus' own
   moves done (resume 2 closes the pause, quit 0x1a opens the exit's, 0x18 goes back): the race loop's
   to act on (2 resume, 3 restart, 4 / 5 / 0x15 the race's end); in the race-over's replay the replay
   panel's (6 .. 0xd, 0x16, 0x17: Replay_Control's, 7 restart, 0xe summary). event: 2 the left button
   went down, 1 it came up, 0 neither (the buttons act as it comes up, the replay's as it goes down). */
int HUD_HandleInput(int x, int y, int event);
/* The replay panel's play button back to pause (0x42df90). */
void HUD_ReplayPlaying(void);

/* A time of ticks as mm:ss.hh, signed if asked. 0x435640 */
void HTime_Format(int t, char *out, bool sign);
/* The HUD over camera c (the overlay view). 0x425910 */
void HUD_Draw(RenderSystem *r, RCamera *c);
/* A key typed (its DirectInput code) into the HUD's characters (the window's WM_CHAR queue 0x610a80),
   which the replay's save takes its name from. */
void HUD_TypeKey(uint8_t code, bool shift);
