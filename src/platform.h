/* Platform contract: what the portable core needs from a host (backend: platform_gasm.c). The game data
   comes through the file layer (vfs.h); 3D goes to OpenGL ES 3.0 (<GLES3/gl3.h>), which only the THRASH
   driver (render/thrash.c) calls. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Virtual pad buttons, the gasm ABI layout (SNES positions: A east, B south, X north, Y west). */
enum {
    PAD_A = 1 << 0, PAD_B = 1 << 1, PAD_X = 1 << 2, PAD_Y = 1 << 3, PAD_L = 1 << 4, PAD_R = 1 << 5,
    PAD_SELECT = 1 << 6, PAD_START = 1 << 7, PAD_UP = 1 << 8, PAD_DOWN = 1 << 9, PAD_LEFT = 1 << 10,
    PAD_RIGHT = 1 << 11,
};

/* Keys are DirectInput codes (DIK_*: set-1 scan code, 0x80 for extended keys), as the original reads
   the keyboard through IDirectInputDevice::GetDeviceState: Enter 0x1c, keypad Enter 0x9c, Up 0xc8. */
enum { KEY_COUNT = 256 };

void plat_log(const char *msg);
uint32_t plat_pad(int player);          /* stable within a frame */
/* Held keys: keys[code] = 1. False if the runner has no keyboard. */
bool plat_keys(uint8_t keys[KEY_COUNT]);
/* Key presses since the previous frame, in order (no auto-repeat); count, at most cap. */
int plat_key_presses(uint8_t *codes, int cap);
/* The mouse: its position in drawable pixels (plat_display_size's), its buttons held (1 left, 2 right,
   4 middle), the wheel's lines this frame (> 0 down). False without a pointer. */
bool plat_mouse(float *x, float *y, uint32_t *buttons, float *wheel);
/* Launch parameter (gasm --param / URL query); false if unset. */
bool plat_param(const char *name, char *dst, size_t cap);
/* Calls per second of app_frame from now on; the audio per frame follows. */
void plat_set_frame_rate(double hz);
/* The drawable (default framebuffer) size in pixels; follows window resizes. */
void plat_display_size(int *w, int *h);
/* False during catch-up frames, whose drawing is never shown (a step may skip drawing). */
bool plat_frame_shown(void);
/* Persistent user files (saves, settings). */
uint8_t *plat_load_user_file(const char *name, size_t *size);
bool plat_save_user_file(const char *name, const void *data, size_t size);
bool plat_delete_user_file(const char *name);
/* The user files there are, by index (0 .. count - 1, in name order). */
int plat_user_file_count(void);
bool plat_user_file_name(int i, char *dst, size_t cap);
