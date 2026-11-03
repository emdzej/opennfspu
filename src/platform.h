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
/* This frame's presses dropped (the keyboard buffer emptied). */
void plat_key_flush(void);
/* The mouse: its position in drawable pixels (plat_display_size's), its buttons held (1 left, 2 right,
   4 middle), the wheel's lines this frame (> 0 down). False without a pointer. */
bool plat_mouse(float *x, float *y, uint32_t *buttons, float *wheel);
/* A game controller in slot 0..3, in connection order (gasm's raw gamepad; the W3C standard order when
   it has one: buttons 0 south .. 12-15 the d-pad, axes 0/1 the left stick, 2/3 the right, y down): its
   axes (-1..1) and buttons (0..1), its name (cap bytes, may be empty). False if none is connected. */
bool plat_gamepad(int slot, float axes[16], float buttons[32], char *name, size_t cap);
/* Launch parameter (gasm --param / URL query); false if unset. */
bool plat_param(const char *name, char *dst, size_t cap);
/* Calls per second of app_frame from now on; the audio per frame follows. */
void plat_set_frame_rate(double hz);
/* The drawable (default framebuffer) size in pixels; follows window resizes. */
void plat_display_size(int *w, int *h);
/* Persistent user files (saves, settings). */
uint8_t *plat_load_user_file(const char *name, size_t *size);
bool plat_save_user_file(const char *name, const void *data, size_t size);
bool plat_delete_user_file(const char *name);
/* The user files there are, by index (0 .. count - 1, in name order). */
int plat_user_file_count(void);
bool plat_user_file_name(int i, char *dst, size_t cap);

/* Threads, cooperative (gasm's, in the LOOP THREADS build: a thread runs until it blocks, sleeps or
   yields; the main thread's frame ends when none is ready). Without them (the native build, gasm's plain
   exports) plat_thread_create returns NULL and the rest does nothing. Sleeps end at the first frame at
   or after their time; the time is the frames' (virtual in headless runs). */
typedef struct PlatThread PlatThread;
PlatThread *plat_thread_create(int (*fn)(void *arg), void *arg);
void plat_thread_detach(PlatThread *t);
bool plat_thread_is_main(void);
/* The calling thread's number: 0 the main thread, then 1, 2, ... in creation order. */
uint32_t plat_thread_id(void);
void plat_thread_sleep_ms(double ms);
void plat_thread_yield(void);
double plat_time_ms(void);
/* A recursive lock (a Windows critical section). */
typedef struct PlatMutex PlatMutex;
PlatMutex *plat_mutex_create(void);
void plat_mutex_destroy(PlatMutex *m);
void plat_mutex_lock(PlatMutex *m);
void plat_mutex_unlock(PlatMutex *m);
/* A Windows event: manual (stays set until reset) or automatic (one waiter takes it). The wait: ms < 0
   forever; true if it was set. */
typedef struct PlatEvent PlatEvent;
PlatEvent *plat_event_create(bool manual);
void plat_event_destroy(PlatEvent *e);
void plat_event_set(PlatEvent *e);
void plat_event_reset(PlatEvent *e);
bool plat_event_wait(PlatEvent *e, double ms);

/* Message connections (gasm:net: WebSocket semantics, reliable and ordered, binary): a ws:// or wss://
   URL; the runner may refuse it (gasm-run --allow-net). open: a handle > 0, or -1. state: PLAT_NET_*.
   send: false unless open. recv: the next message's length (copied only if it fits in cap, else it stays
   queued), 0 if none, -1 once closed and drained. Not in the native build (open fails). */
enum { PLAT_NET_CONNECTING, PLAT_NET_OPEN, PLAT_NET_CLOSED, PLAT_NET_ERROR };
int32_t plat_net_open(const char *url);
int plat_net_state(int32_t conn);
bool plat_net_send(int32_t conn, const void *data, uint32_t len);
int32_t plat_net_recv(int32_t conn, void *dst, uint32_t cap);
void plat_net_close(int32_t conn);
