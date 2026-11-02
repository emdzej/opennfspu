/* The race's controls ([input] 0x411970-0x4138f0): the bindings fe.txt gives (ids 0x14e..0x21d, the
   option values "type,value": C1_* / C2_* the players' 80 each, I_* the 32 interface keys, PSX_* 16
   debug ones), read each sample by their type's reader (0x5cc5c8: Null, Analog, Button, Key, Intface,
   Pov; held means more than 0x40) into the players' packets (Input_Sample 0x412e90) and into a ring of
   the interface keys held (0x5e954c, 32 samples), which the race loop walks (Input_Action 0x4138f0).

   A binding is value << 8 | type. Key: the scan code (0x5de028, the window's key messages: the scan
   code & 0x7f, so the arrows share the keypad's). Intface: a key, else the joystick's directions (while
   paused). Analog: device << 20 | axis << 16 | low << 8 | high. Button: device << 16 | button. Pov:
   device << 16 | the direction (0..7, eighths). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The interface keys (the ring's bits, I_* in order from 0x1ee). */
enum {
    ACT_MAINEXIT, ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_SELECT, ACT_CANCEL, ACT_SWITCH, ACT_PAUSE,
    ACT_PAUSEJOY, ACT_ROTCAMCCLOCK, ACT_ROTCAMSTOP, ACT_ROTCAMCLOCK, ACT_SCREENSHOT, ACT_TOGGLEPAUSEMENU,
    ACT_VIEWDISTANCE, ACT_HORIZON, ACT_SFX, ACT_MUSIC, ACT_LOOKBEHIND, ACT_LOOKLEFT, ACT_LOOKRIGHT,
    ACT_LOOKFORWARD, ACT_CYCLECAMERA, ACT_TOGGLEHUD, ACT_CYCLEHUD, ACT_TOGGLEREARVIEW, ACT_TOGGLEMAP,
    ACT_CYCLEMAP, ACT_ZOOMINMAP, ACT_ZOOMOUTMAP, ACT_HUDTRANSPARENCY,
};

/* The bindings cleared, the latches and the ring with them (0x412e20); then the options' (the race
   loop's Option_Apply over ids 0x14e..0x21d) and the interface defaults over them (0x4117c0). */
void Controls_Load(void);
/* A binding by its id (0x14e..0x21d, at 0x5e9130 + 4 id); 0 unbound. */
uint32_t Controls_Binding(int id);
/* An option value's binding: the type's name, ',', the value (0x4b5220, 0x4b5040). */
uint32_t Controls_Parse(const char *text);
/* The players' packets (g_input_packet the first's) and the interface ring's next. 0x412e90 */
void Input_Sample(void);
/* The samples taken (0x5e99b8) and the race loop's cursor into the ring (0x606ac8). */
extern int32_t g_input_samples, g_input_cursor;
extern uint16_t g_input_psx;   /* 0x5e9548: the PSX_* keys held (the debug pad's) */
/* Is interface key n held at the cursor, or (edge) pressed since the one before. 0x4138f0 */
bool Input_Action(int n, bool edge);
