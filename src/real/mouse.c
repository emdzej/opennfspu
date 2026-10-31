/* The mouse (mouse.h). */
#include "real/mouse.h"
#include "platform.h"
#include "render/render.h"

bool MOUSE_state(int32_t *x, int32_t *y, uint32_t *buttons)
{
    float fx, fy, wheel;
    uint32_t b;
    if (!plat_mouse(&fx, &fy, &b, &wheel)) return false;
    int dw, dh;
    plat_display_size(&dw, &dh);
    int mw = g_display.width, mh = g_display.height;
    if (dw <= 0 || dh <= 0 || mw <= 0 || mh <= 0) return false;
    /* the present's letterbox undone (thrash_dev.c's dev_present) */
    float sx = 1, sy = 1, a = (float)mw / (float)mh, da = (float)dw / (float)dh;
    if (da > a) sx = a / da;
    else sy = da / a;
    float nx = (fx / (float)dw * 2.0f - 1.0f) / sx, ny = (fy / (float)dh * 2.0f - 1.0f) / sy;
    *x = (int32_t)((nx + 1.0f) * 0.5f * (float)mw);
    *y = (int32_t)((ny + 1.0f) * 0.5f * (float)mh);
    *buttons = b;
    return true;
}
