/* The platform contract for the native tests and tools: log to stderr, launch parameters from the
   environment (OPENNFSPU_PARAM_<name>), no input, no display, user files in memory only. */
#include "platform.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void plat_log(const char *msg) { fprintf(stderr, "%s\n", msg); }
uint32_t plat_pad(int player) { return 0; }
bool plat_keys(uint8_t keys[KEY_COUNT]) { memset(keys, 0, KEY_COUNT); return false; }
int plat_key_presses(uint8_t *codes, int cap) { return 0; }

bool plat_param(const char *name, char *dst, size_t cap)
{
    char env[96] = "OPENNFSPU_PARAM_";
    size_t n = strlen(env);
    for (const char *p = name; *p && n + 1 < sizeof env; p++) env[n++] = (char)toupper((unsigned char)*p);
    env[n] = 0;
    const char *v = getenv(env);
    if (!v) return false;
    snprintf(dst, cap, "%s", v);
    return true;
}

void plat_set_frame_rate(double hz) {}
void plat_display_size(int *w, int *h) { *w = 640; *h = 480; }
bool plat_mouse(float *x, float *y, uint32_t *buttons, float *wheel)
{
    *x = *y = *wheel = 0;
    *buttons = 0;
    return false;
}
bool plat_frame_shown(void) { return true; }
uint8_t *plat_load_user_file(const char *name, size_t *size) { return NULL; }
bool plat_save_user_file(const char *name, const void *data, size_t size) { return false; }
