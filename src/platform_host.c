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
void plat_key_flush(void) {}

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
bool plat_gamepad(int slot, float axes[16], float buttons[32], char *name, size_t cap)
{
    (void)slot, (void)axes, (void)buttons, (void)name, (void)cap;
    return false;
}

bool plat_mouse(float *x, float *y, uint32_t *buttons, float *wheel)
{
    *x = *y = *wheel = 0;
    *buttons = 0;
    return false;
}
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

/* No threads in the native build (tests and tools): the network library finds none. */
PlatThread *plat_thread_create(int (*fn)(void *arg), void *arg) { return NULL; }
void plat_thread_detach(PlatThread *t) {}
bool plat_thread_is_main(void) { return true; }
uint32_t plat_thread_id(void) { return 0; }
void plat_thread_sleep_ms(double ms) {}
void plat_thread_yield(void) {}
double plat_time_ms(void) { return 0; }
struct PlatMutex { int unused; };
PlatMutex *plat_mutex_create(void) { return calloc(1, sizeof(PlatMutex)); }
void plat_mutex_destroy(PlatMutex *m) { free(m); }
void plat_mutex_lock(PlatMutex *m) {}
void plat_mutex_unlock(PlatMutex *m) {}
struct PlatEvent { bool manual, set; };
PlatEvent *plat_event_create(bool manual)
{
    PlatEvent *e = calloc(1, sizeof *e);
    if (e) e->manual = manual;
    return e;
}
void plat_event_destroy(PlatEvent *e) { free(e); }
void plat_event_set(PlatEvent *e) { if (e) e->set = true; }
void plat_event_reset(PlatEvent *e) { if (e) e->set = false; }
bool plat_event_wait(PlatEvent *e, double ms)
{
    if (!e || !e->set) return false;
    if (!e->manual) e->set = false;
    return true;
}

int32_t plat_net_open(const char *url) { return -1; }
int plat_net_state(int32_t conn) { return PLAT_NET_ERROR; }
bool plat_net_send(int32_t conn, const void *data, uint32_t len) { return false; }
int32_t plat_net_recv(int32_t conn, void *dst, uint32_t cap) { return -1; }
void plat_net_close(int32_t conn) {}
