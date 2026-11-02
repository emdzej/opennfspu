/* The 2D display hook (see ddraw.h). */
#include "real/ddraw.h"
#include "real/set3d.h"
#include "real/window.h"
#include <stddef.h>
#include <string.h>

static bool (*s_window)(int);   /* 0x6a6400: the driver's THRASH_window */
uintptr_t g_ddraw_mode;        /* 0x5deaa8 */

/* 0x5757d0: the driver's state calls (setstate 0x27): 0x3a gives the mode. */
static void on_state(uint32_t state, uintptr_t v)
{
    if (state == 0x3a) g_ddraw_mode = v;
}

/* 0x575730: the current window let go (0x5341a0) and cleared to the display's type and depth; when the
   driver takes the window, the display's window without pixels or shape, standing for that window. The
   driver's answer is passed on. */
static bool ddraw_window(int id)
{
    WINDOW_unlockcurrent();
    memset(&WINDOW_cur, 0, offsetof(Window, rec));
    WINDOW_cur.type = WINDOW_display.type;
    WINDOW_cur.depth = WINDOW_display.depth;
    bool ok = s_window(id);
    if (ok) {
        memcpy(&WINDOW_cur, &WINDOW_display, offsetof(Window, rec));
        WINDOW_cur.lock = (int8_t)id;
        WINDOW_cur.pixels = NULL;
        WINDOW_cur.shape = NULL;
    }
    return ok;
}

void DDRAW_init(void)   /* 0x5757a0 */
{
    THRASH.fn.setstate(0x27, (uintptr_t)on_state);
    if (!s_window) {
        s_window = THRASH.fn.window;
        THRASH.fn.window = ddraw_window;
    }
}
