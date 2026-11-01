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
/* The user files in memory (the tests' runs start empty, as gasm's headless ones do). */
typedef struct { char name[128]; uint8_t *data; size_t size; } UserFile;
static UserFile s_files[64];
static int s_nfiles;

static UserFile *user_file(const char *name)
{
    for (int i = 0; i < s_nfiles; i++)
        if (!strcmp(s_files[i].name, name)) return &s_files[i];
    return NULL;
}

uint8_t *plat_load_user_file(const char *name, size_t *size)
{
    UserFile *f = user_file(name);
    if (!f) return NULL;
    uint8_t *b = malloc(f->size ? f->size : 1);
    if (!b) return NULL;
    memcpy(b, f->data, f->size);
    *size = f->size;
    return b;
}

bool plat_save_user_file(const char *name, const void *data, size_t size)
{
    UserFile *f = user_file(name);
    if (!f) {
        if (s_nfiles == 64 || strlen(name) >= sizeof f->name) return false;
        f = &s_files[s_nfiles++];
        snprintf(f->name, sizeof f->name, "%s", name);
        f->data = NULL;
    }
    uint8_t *b = malloc(size ? size : 1);
    if (!b) return false;
    memcpy(b, data, size);
    free(f->data);
    f->data = b;
    f->size = size;
    return true;
}

bool plat_delete_user_file(const char *name)
{
    UserFile *f = user_file(name);
    if (!f) return false;
    free(f->data);
    *f = s_files[--s_nfiles];
    return true;
}

static int name_cmp(const void *a, const void *b) { return strcmp(((const UserFile *)a)->name, ((const UserFile *)b)->name); }

int plat_user_file_count(void)
{
    qsort(s_files, (size_t)s_nfiles, sizeof *s_files, name_cmp);
    return s_nfiles;
}

bool plat_user_file_name(int i, char *dst, size_t cap)
{
    if (i < 0 || i >= s_nfiles) return false;
    snprintf(dst, cap, "%s", s_files[i].name);
    return true;
}
