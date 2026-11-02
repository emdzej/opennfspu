/* The front end's mouse pointer: see fepointer.h. */
#include "front/fepointer.h"
#include "app.h"
#include "exe.h"
#include "front/fe2d.h"
#include "front/fe_parts.h"
#include "real/mouse.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int s_want;          /* 0x690720: the shape asked for this frame */
static int s_shape = -1;    /* 0x690794: the one shown */
static int32_t s_since;     /* 0x690790: when it changed (or the button last went down) */
static int32_t s_down_at;   /* 0x690724: when the button went down */
static bool s_pressed;      /* 0x69cae8: the last mouse button to change is down (FERT_EventPump_v5) */
static uint32_t s_buttons;
static char s_path[300];    /* 0x690728 */
static bool s_loaded;

static int32_t timer_ticks(void) { return (int32_t)((uint64_t)app_ticks_ms() * 128u / 1000u); }   /* TIMER_ticks 0x53c050 */

static int32_t div4(int32_t v) { return v / 4; }    /* (truncated toward zero, as the original's) */
static int32_t div16(int32_t v) { return v / 16; }

void FEPointer_Begin(void) { s_want = 0; }

void FEPointer_Want(int shape)
{
    if (s_want < shape) s_want = shape;
}

/* 0x4e1400: "<art>Pointer.fsh" ("%s%s.fsh" 0x5cccc8) loaded. */
static void load(void)
{
    if (s_loaded) return;
    s_loaded = true;
    FE_ResolvePath("Pointer.fsh", s_path, sizeof s_path);
    FEShapes_Load(s_path);
}

void FEPointer_Free(void)
{
    if (s_loaded) FEShapes_Release(s_path);
    s_loaded = false;
}

void FEPointer_Draw(void)
{
    int32_t mx, my;
    uint32_t b;
    if (!MOUSE_state(&mx, &my, &b)) return;
    /* (FERT's event pump: the last of the buttons but the middle one to change, down or up: the left,
       the right and the fourth, gasm's bits 0, 1 and 3) */
    uint32_t changed = (b ^ s_buttons) & 0xbu;
    for (int i = 0; i < 4; i++)
        if (changed >> i & 1) s_pressed = (b >> i & 1) != 0;
    s_buttons = b;
    load();
    int32_t now = timer_ticks();
    if (s_want != s_shape) {
        s_shape = s_want;
        if (s_want != 3) s_down_at = 0;
        s_since = now;
    }
    /* the shape's frames, first and last (0x5d9c8c: two ints a shape) */
    int32_t lo = (int32_t)exe_u32(0x5d9c8c + 8u * (uint32_t)s_shape), hi = (int32_t)exe_u32(0x5d9c90 + 8u * (uint32_t)s_shape);
    int32_t frame;
    if (s_shape == 1) {   /* down from the last over and over, a frame every 16 ticks */
        frame = hi - div16(now - s_since) % (hi - lo);
        if (frame < lo) frame = lo;
    } else if (!s_pressed) {   /* back toward the first, a frame every 4 ticks */
        int32_t dt = now - s_since;
        s_down_at = now;
        frame = hi - div4(dt);
        if (frame < lo) frame = lo;
    } else {   /* toward the last while down */
        int32_t dt = now - s_down_at;
        s_since = now;
        frame = lo + div4(dt);
        if (frame > hi) frame = hi;
    }
    FeImage *im = FEShapes_Image(s_path, frame);   /* 0x4d74e0 */
    if (!im) return;
    float x = (float)mx, y = (float)my;
    if (s_shape == 2) {   /* (centred: 6 up and left, 0x5b2460) */
        uint32_t k = exe_u32(0x5b2460);
        float f;
        memcpy(&f, &k, 4);
        x -= f, y -= f;
    }
    FE2D_Image(im, x, y, (float)im->w, (float)im->h, 0xffffffffu, 0, false);   /* 0x4d95a0 */
}
