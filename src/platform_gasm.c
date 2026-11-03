/* gasm platform backend for opennfspu.wasm (gasm ABI 0, gasm:gl, raw keyboard). The only file that
   includes gasm.h. Exports: gasm_init (mount the data, app_init), gasm_frame (app_frame), gasm_exit.

   Game data (the first that has Porsche.exe):
   - a disc image of the installed game as asset `cd` (gasm-run --asset cd=NFS_PU.iso: ISO 9660 + Joliet,
     read through asset_read_at; `iso` and `rom` are accepted too);
   - the installed game's files as assets named by their paths (gasm-run --asset-dir <game folder>:
     Porsche.exe, GameData/..., FEData/...). Names are tried as the game spells them (the runner's
     lookup is exact; the player's OPFS import and gasm-run's folders keep the files' own case).
   The original opens paths like "GameData\CarModel\928d.fsh": '\' -> '/', ".." resolved. */
#include "app.h"
#include "platform.h"
#include "vfs.h"
#include <gasm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <strings.h>
#include <wasi/api.h>

/* The window title (gasm 0.6 custom section, also readable by launchers without running the module). */
GASM_TITLE("Need for Speed: Porsche Unleashed");

_Static_assert(PLAT_NET_CONNECTING == GASM_NET_CONNECTING && PLAT_NET_OPEN == GASM_NET_OPEN && PLAT_NET_CLOSED == GASM_NET_CLOSED &&
               PLAT_NET_ERROR == GASM_NET_ERROR, "PLAT_NET_* must be gasm:net's states");
_Static_assert(PAD_A == GASM_BTN_A && PAD_B == GASM_BTN_B && PAD_X == GASM_BTN_X && PAD_Y == GASM_BTN_Y &&
               PAD_L == GASM_BTN_L && PAD_R == GASM_BTN_R && PAD_SELECT == GASM_BTN_SELECT &&
               PAD_START == GASM_BTN_START && PAD_UP == GASM_BTN_UP && PAD_DOWN == GASM_BTN_DOWN &&
               PAD_LEFT == GASM_BTN_LEFT && PAD_RIGHT == GASM_BTN_RIGHT, "PAD_* must be the gasm button bits");

static uint32_t pads[4];

void plat_log(const char *msg) { gasm_log(msg, (uint32_t)strlen(msg)); }
uint32_t plat_pad(int player) { return player >= 0 && player < 4 ? pads[player] : 0; }
bool plat_param(const char *name, char *dst, size_t cap) { return gasm_param_str(name, dst, (uint32_t)cap); }
static double frame_hz = APP_FRAME_HZ;
void plat_set_frame_rate(double hz)
{
    frame_hz = hz;
    gasm_set_frame_rate(hz);
}
void plat_display_size(int *w, int *h)
{
    *w = (int)gasm_gl_width();
    *h = (int)gasm_gl_height();
}

bool plat_gamepad(int slot, float axes[16], float buttons[32], char *name, size_t cap)
{
    uint8_t b[GASM_GAMEPAD_BYTES];
    uint32_t flags;
    if (slot < 0 || slot > 3 || gasm_gamepad((uint32_t)slot, b, sizeof b) != GASM_GAMEPAD_BYTES) return false;
    memcpy(&flags, b + GASM_GAMEPAD_OFF_FLAGS, 4);
    if (!(flags & GASM_GAMEPAD_CONNECTED)) return false;
    memcpy(buttons, b + GASM_GAMEPAD_OFF_BUTTONS, 4 * GASM_GAMEPAD_BUTTONS);
    memcpy(axes, b + GASM_GAMEPAD_OFF_AXES, 4 * GASM_GAMEPAD_AXES);
    if (name && cap) {
        int32_t n = gasm_gamepad_name((uint32_t)slot, name, (uint32_t)cap - 1);
        name[n > 0 && (size_t)n < cap ? n : 0] = 0;
    }
    return true;
}

bool plat_mouse(float *x, float *y, uint32_t *buttons, float *wheel)
{
    uint8_t p[GASM_POINTER_BYTES];
    *x = *y = *wheel = 0;
    *buttons = 0;
    if (gasm_pointer(p, sizeof p) < (int32_t)sizeof p) return false;
    uint32_t b, pressed;
    memcpy(x, p + GASM_POINTER_OFF_X, 4);
    memcpy(y, p + GASM_POINTER_OFF_Y, 4);
    memcpy(wheel, p + GASM_POINTER_OFF_WHEEL_Y, 4);
    memcpy(&b, p + GASM_POINTER_OFF_BUTTONS, 4);
    memcpy(&pressed, p + GASM_POINTER_OFF_PRESSED, 4);
    /* A button pressed since the last frame counts as down for this one even if it is already up again:
       DirectInput's buffered mouse never loses a click, a held-state read does when a frame is long. */
    *buttons = (b | pressed) & 7u;
    return true;
}

/* ---- raw keyboard (gasm 0.5) ---- */

/* GASM_KEY_* (W3C KeyboardEvent.code) -> DirectInput code (DIK_*, from OpenBallance): the set-1 scan
   code with 0x80 for extended keys, what the game reads (see platform.h). */
static const struct { uint8_t key, dik; } DIK_OF[] = {
    {GASM_KEY_ESCAPE, 0x01}, {GASM_KEY_DIGIT1, 0x02}, {GASM_KEY_DIGIT2, 0x03}, {GASM_KEY_DIGIT3, 0x04},
    {GASM_KEY_DIGIT4, 0x05}, {GASM_KEY_DIGIT5, 0x06}, {GASM_KEY_DIGIT6, 0x07}, {GASM_KEY_DIGIT7, 0x08},
    {GASM_KEY_DIGIT8, 0x09}, {GASM_KEY_DIGIT9, 0x0a}, {GASM_KEY_DIGIT0, 0x0b}, {GASM_KEY_MINUS, 0x0c},
    {GASM_KEY_EQUAL, 0x0d}, {GASM_KEY_BACKSPACE, 0x0e}, {GASM_KEY_TAB, 0x0f}, {GASM_KEY_KEY_Q, 0x10},
    {GASM_KEY_KEY_W, 0x11}, {GASM_KEY_KEY_E, 0x12}, {GASM_KEY_KEY_R, 0x13}, {GASM_KEY_KEY_T, 0x14},
    {GASM_KEY_KEY_Y, 0x15}, {GASM_KEY_KEY_U, 0x16}, {GASM_KEY_KEY_I, 0x17}, {GASM_KEY_KEY_O, 0x18},
    {GASM_KEY_KEY_P, 0x19}, {GASM_KEY_BRACKET_LEFT, 0x1a}, {GASM_KEY_BRACKET_RIGHT, 0x1b},
    {GASM_KEY_ENTER, 0x1c}, {GASM_KEY_CONTROL_LEFT, 0x1d}, {GASM_KEY_KEY_A, 0x1e}, {GASM_KEY_KEY_S, 0x1f},
    {GASM_KEY_KEY_D, 0x20}, {GASM_KEY_KEY_F, 0x21}, {GASM_KEY_KEY_G, 0x22}, {GASM_KEY_KEY_H, 0x23},
    {GASM_KEY_KEY_J, 0x24}, {GASM_KEY_KEY_K, 0x25}, {GASM_KEY_KEY_L, 0x26}, {GASM_KEY_SEMICOLON, 0x27},
    {GASM_KEY_QUOTE, 0x28}, {GASM_KEY_BACKQUOTE, 0x29}, {GASM_KEY_SHIFT_LEFT, 0x2a}, {GASM_KEY_BACKSLASH, 0x2b},
    {GASM_KEY_KEY_Z, 0x2c}, {GASM_KEY_KEY_X, 0x2d}, {GASM_KEY_KEY_C, 0x2e}, {GASM_KEY_KEY_V, 0x2f},
    {GASM_KEY_KEY_B, 0x30}, {GASM_KEY_KEY_N, 0x31}, {GASM_KEY_KEY_M, 0x32}, {GASM_KEY_COMMA, 0x33},
    {GASM_KEY_PERIOD, 0x34}, {GASM_KEY_SLASH, 0x35}, {GASM_KEY_SHIFT_RIGHT, 0x36}, {GASM_KEY_NUMPAD_MULTIPLY, 0x37},
    {GASM_KEY_ALT_LEFT, 0x38}, {GASM_KEY_SPACE, 0x39}, {GASM_KEY_CAPS_LOCK, 0x3a}, {GASM_KEY_F1, 0x3b},
    {GASM_KEY_F2, 0x3c}, {GASM_KEY_F3, 0x3d}, {GASM_KEY_F4, 0x3e}, {GASM_KEY_F5, 0x3f}, {GASM_KEY_F6, 0x40},
    {GASM_KEY_F7, 0x41}, {GASM_KEY_F8, 0x42}, {GASM_KEY_F9, 0x43}, {GASM_KEY_F10, 0x44}, {GASM_KEY_NUM_LOCK, 0x45},
    {GASM_KEY_SCROLL_LOCK, 0x46}, {GASM_KEY_NUMPAD7, 0x47}, {GASM_KEY_NUMPAD8, 0x48}, {GASM_KEY_NUMPAD9, 0x49},
    {GASM_KEY_NUMPAD_SUBTRACT, 0x4a}, {GASM_KEY_NUMPAD4, 0x4b}, {GASM_KEY_NUMPAD5, 0x4c}, {GASM_KEY_NUMPAD6, 0x4d},
    {GASM_KEY_NUMPAD_ADD, 0x4e}, {GASM_KEY_NUMPAD1, 0x4f}, {GASM_KEY_NUMPAD2, 0x50}, {GASM_KEY_NUMPAD3, 0x51},
    {GASM_KEY_NUMPAD0, 0x52}, {GASM_KEY_NUMPAD_DECIMAL, 0x53}, {GASM_KEY_INTL_BACKSLASH, 0x56}, {GASM_KEY_F11, 0x57},
    {GASM_KEY_F12, 0x58}, {GASM_KEY_F13, 0x64}, {GASM_KEY_F14, 0x65}, {GASM_KEY_F15, 0x66}, {GASM_KEY_INTL_RO, 0x73},
    {GASM_KEY_INTL_YEN, 0x7d}, {GASM_KEY_NUMPAD_EQUAL, 0x8d}, {GASM_KEY_NUMPAD_ENTER, 0x9c},
    {GASM_KEY_CONTROL_RIGHT, 0x9d}, {GASM_KEY_NUMPAD_COMMA, 0xb3}, {GASM_KEY_NUMPAD_DIVIDE, 0xb5},
    {GASM_KEY_PRINT_SCREEN, 0xb7}, {GASM_KEY_ALT_RIGHT, 0xb8}, {GASM_KEY_PAUSE, 0xc5}, {GASM_KEY_HOME, 0xc7},
    {GASM_KEY_ARROW_UP, 0xc8}, {GASM_KEY_PAGE_UP, 0xc9}, {GASM_KEY_ARROW_LEFT, 0xcb}, {GASM_KEY_ARROW_RIGHT, 0xcd},
    {GASM_KEY_END, 0xcf}, {GASM_KEY_ARROW_DOWN, 0xd0}, {GASM_KEY_PAGE_DOWN, 0xd1}, {GASM_KEY_INSERT, 0xd2},
    {GASM_KEY_DELETE, 0xd3}, {GASM_KEY_META_LEFT, 0xdb}, {GASM_KEY_META_RIGHT, 0xdc}, {GASM_KEY_CONTEXT_MENU, 0xdd},
};

static uint8_t scan_of[256];   /* GASM_KEY_* -> DIK code, 0 = none */

static void init_keys(void)
{
    for (size_t i = 0; i < sizeof DIK_OF / sizeof *DIK_OF; i++) scan_of[DIK_OF[i].key] = DIK_OF[i].dik;
}

bool plat_keys(uint8_t keys[KEY_COUNT])
{
    uint8_t bits[GASM_KEY_STATE_BYTES];
    memset(keys, 0, KEY_COUNT);
    if (gasm_key_state(bits, sizeof bits) < 0) return false;
    for (int k = 0; k < 256; k++)
        if (scan_of[k] && (bits[k >> 3] & (1u << (k & 7)))) keys[scan_of[k]] = 1;
    return true;
}

static uint8_t presses[64];
static int npresses;

static void poll_key_events(void)
{
    uint8_t ev[4 * 64];
    npresses = 0;
    int32_t n = gasm_key_events(ev, sizeof ev);
    if (n < 0 || n > (int32_t)sizeof ev) return;   /* none, or more than a frame's worth: dropped */
    for (int32_t i = 0; i + 4 <= n; i += 4) {
        uint16_t key = (uint16_t)(ev[i] | ev[i + 1] << 8);
        if (ev[i + 2] && key < 256 && scan_of[key] && npresses < 64) presses[npresses++] = scan_of[key];
    }
}

int plat_key_presses(uint8_t *codes, int cap)
{
    int n = npresses < cap ? npresses : cap;
    memcpy(codes, presses, (size_t)n * sizeof *codes);
    return n;
}

void plat_key_flush(void) { npresses = 0; }

/* ---- storage ---- */

/* Storage keys are [A-Za-z0-9._-]: a file's name is its key with each other byte, and '-', as '-' and two
   hex digits (a player's name may hold spaces and more, CHEAT_IsValidChar's filter). */
static bool user_key(const char *name, char *key, size_t cap)
{
    static const char hex[] = "0123456789abcdef";
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        bool plain = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '_';
        if (n + (plain ? 1 : 3) >= cap) return false;
        if (plain) key[n++] = (char)*p;
        else key[n++] = '-', key[n++] = hex[*p >> 4], key[n++] = hex[*p & 15];
    }
    key[n] = 0;
    return n > 0;
}

static int hexval(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }

uint8_t *plat_load_user_file(const char *name, size_t *size)
{
    char key[160];
    if (!user_key(name, key, sizeof key)) return NULL;
    int32_t n = gasm_storage_get(key, (uint32_t)strlen(key), NULL, 0);
    if (n < 0) return NULL;
    uint8_t *b = malloc(n ? (size_t)n : 1);
    if (gasm_storage_get(key, (uint32_t)strlen(key), b, (uint32_t)n) != n) { free(b); return NULL; }
    *size = (size_t)n;
    return b;
}

bool plat_save_user_file(const char *name, const void *data, size_t size)
{
    char key[160];
    if (!user_key(name, key, sizeof key)) return false;
    return gasm_storage_set(key, (uint32_t)strlen(key), data, (uint32_t)size) == 0;
}

bool plat_delete_user_file(const char *name)
{
    char key[160];
    if (!user_key(name, key, sizeof key)) return false;
    return gasm_storage_delete(key, (uint32_t)strlen(key)) == 0;
}

int plat_user_file_count(void) { return (int)gasm_storage_count(); }

bool plat_user_file_name(int i, char *dst, size_t cap)
{
    char key[160];
    int32_t n = gasm_storage_key((uint32_t)i, key, sizeof key - 1);
    if (n < 0 || n >= (int32_t)sizeof key) return false;
    key[n] = 0;
    size_t o = 0;
    for (int32_t k = 0; k < n && o + 1 < cap; k++) {
        if (key[k] == '-' && k + 2 < n && hexval(key[k + 1]) >= 0 && hexval(key[k + 2]) >= 0) {
            dst[o++] = (char)(hexval(key[k + 1]) << 4 | hexval(key[k + 2]));
            k += 2;
        } else {
            dst[o++] = key[k];
        }
    }
    dst[o] = 0;
    return o > 0;
}

/* ---- data: the game's files as assets ---- */

typedef struct { int64_t size; char name[]; } FileAsset;

/* Game path -> asset name: '\\' -> '/', "." and ".." resolved; leading ".." components are dropped. */
static void norm_path(const char *rel, char *out, size_t cap)
{
    size_t n = 0;
    while (*rel && n + 1 < cap) {
        while (*rel == '/' || *rel == '\\') rel++;
        const char *seg = rel;
        while (*rel && *rel != '/' && *rel != '\\') rel++;
        size_t len = (size_t)(rel - seg);
        if (!len || (len == 1 && seg[0] == '.')) continue;
        if (len == 2 && seg[0] == '.' && seg[1] == '.') {
            while (n && out[n - 1] != '/') n--;
            if (n) n--;
            continue;
        }
        if (n && n + 1 < cap) out[n++] = '/';
        for (size_t k = 0; k < len && n + 1 < cap; k++) out[n++] = seg[k];
    }
    out[n] = 0;
}

static void *files_open(void *ctx, const char *rel, uint64_t *size)
{
    char name[512];
    norm_path(rel, name, sizeof name);
    int64_t n = gasm_asset_size64(name, (uint32_t)strlen(name));
    if (n < 0) return NULL;
    size_t l = strlen(name);
    FileAsset *f = malloc(sizeof *f + l + 1);
    if (!f) return NULL;
    f->size = n;
    memcpy(f->name, name, l + 1);
    *size = (uint64_t)n;
    return f;
}

static int64_t files_read_at(void *ctx, void *file, uint64_t off, void *dst, size_t len)
{
    const FileAsset *f = file;
    size_t done = 0;
    while (done < len) {
        uint32_t want = len - done > (1u << 30) ? 1u << 30 : (uint32_t)(len - done);
        int32_t k = gasm_asset_read_at64(f->name, (uint32_t)strlen(f->name), off + done, (uint8_t *)dst + done, want);
        if (k < 0) return done ? (int64_t)done : -1;
        if (k == 0) break;
        done += (size_t)k;
    }
    return (int64_t)done;
}

static void files_close(void *ctx, void *file) { free(file); }

/* Entries directly under dir, from the asset names (folder entries are relative paths; the game lists
   directories case-insensitively, e.g. its saved games and car folders). Directories are reported once
   per name, at their first file. */
static bool files_list(void *ctx, const char *dir, VfsListFn fn, void *user)
{
    char d[512], name[512], last[256] = "";
    norm_path(dir, d, sizeof d);
    size_t dl = strlen(d);
    bool any = false;
    for (uint32_t i = 0, n = gasm_asset_count(); i < n; i++) {
        int32_t l = gasm_asset_name(i, name, sizeof name - 1);
        if (l < 0 || l >= (int32_t)sizeof name - 1) continue;
        name[l] = 0;
        const char *rest = name;
        if (dl) {
            if ((size_t)l <= dl || strncasecmp(name, d, dl) != 0 || name[dl] != '/') continue;
            rest = name + dl + 1;
        }
        const char *slash = strchr(rest, '/');
        size_t el = slash ? (size_t)(slash - rest) : strlen(rest);
        if (!el || el >= sizeof last) continue;
        any = true;
        if (slash) {
            if (strlen(last) == el && !strncasecmp(last, rest, el)) continue;
            memcpy(last, rest, el);
            last[el] = 0;
            fn(last, true, user);
        } else {
            char e[256];
            memcpy(e, rest, el);
            e[el] = 0;
            fn(e, false, user);
        }
    }
    return any;
}

static bool has_game(void) { return vfs_exists("Porsche.exe"); }

/* A disc image given as a named asset: ISO 9660 over asset_read_at. */
typedef struct { char name[8]; } ImageAsset;

static int64_t image_read_at(void *ctx, uint64_t off, void *dst, size_t len)
{
    const ImageAsset *a = ctx;
    size_t done = 0;
    while (done < len) {
        uint32_t want = len - done > (1u << 30) ? 1u << 30 : (uint32_t)(len - done);
        int32_t k = gasm_asset_read_at64(a->name, (uint32_t)strlen(a->name), off + done, (uint8_t *)dst + done, want);
        if (k < 0) return done ? (int64_t)done : -1;
        if (k == 0) break;
        done += (size_t)k;
    }
    return (int64_t)done;
}

static void image_close(void *ctx) { free(ctx); }

static bool mount_game(void)
{
    static const char *const images[] = {"cd", "iso", "rom"};
    for (size_t i = 0; i < sizeof images / sizeof *images; i++) {
        int64_t size = gasm_asset_size64(images[i], (uint32_t)strlen(images[i]));
        if (size < 0) continue;
        ImageAsset *a = calloc(1, sizeof *a);
        if (!a) return false;
        snprintf(a->name, sizeof a->name, "%s", images[i]);
        VfsSource src = {a, (uint64_t)size, image_read_at, image_close};
        char d[32];
        snprintf(d, sizeof d, "asset %s", images[i]);
        if (vfs_mount_image(&src, 0, d) && has_game()) return true;
        vfs_unmount();
    }
    VfsBackend b = {NULL, files_open, files_read_at, files_close, files_list, NULL};
    vfs_mount(&b, "gasm assets");
    if (has_game()) return true;
    vfs_unmount();
    return false;
}

/* ---- exports ---- */

static bool running;
static double audio_acc;   /* fractional audio frames carried between frames */

/* gasm-run 0.16.0's window drops the queued samples while it buffers again after running dry, so after
   one long frame (a load) it never fills up again and stays silent. After a gap that emptied it, a block
   of silence longer than its largest target (150 ms) lets it start again. Headless time is the frames':
   no gaps, the hashes as they were. (Until a runner with the fix.) */
static void refill_after_gap(void)
{
    static float silence[APP_AUDIO_RATE * 160 / 1000 * 2];
    static double last = -1;
    double now = gasm_time_ms();
    if (0 <= last && now - last > 80.0) gasm_audio_push(silence, APP_AUDIO_RATE * 160 / 1000);
    last = now;
}

/* --param frametime=1: the frames slower than the frame rate (the time in the game's frame, the time since
   the previous one), to see where a window run falls behind its audio. */
static bool frametime_on;
static double wall_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
static void frametime_log(double t0)
{
    static double last;
    static uint32_t n;
    double t1 = wall_ms(), gap = last ? t1 - last : 0;
    last = t1;
    n++;
    if (t1 - t0 > 1000.0 / frame_hz || gap > 2000.0 / frame_hz) {
        char m[96];
        snprintf(m, sizeof m, "frametime: frame %u game %.1f ms, since the last %.1f ms", n, t1 - t0, gap);
        plat_log(m);
    }
}

#ifdef PU_LOOP_THREADS
#define PU_EXPORT(name)
#else
#define PU_EXPORT(name) GASM_EXPORT(name)
PU_EXPORT("gasm_abi_version") int32_t pu_gasm_abi_version(void) { return GASM_ABI_VERSION; }
#endif

PU_EXPORT("gasm_init") int32_t pu_gasm_init(void)
{
    gasm_set_frame_rate(APP_FRAME_HZ);
    gasm_audio_config(APP_AUDIO_RATE, 2);
    gasm_input_mode(GASM_INPUT_KEYS_RAW | GASM_INPUT_POINTER_HIDDEN);   /* (the front end draws its own pointer) */
    init_keys();
    if (!mount_game()) {
        plat_log("OpenNFSPU: game data not found. Pass the installed game folder (gasm-run opennfspu.wasm "
                 "--asset-dir <folder with Porsche.exe>) or a disc image of it (--asset cd=<NFS_PU.iso>)");
        return 1;
    }
    char m[640];
    snprintf(m, sizeof m, "OpenNFSPU: game data from %s", vfs_describe());
    plat_log(m);
    if (!app_init()) return 1;
    char ft[8];
    frametime_on = plat_param("frametime", ft, sizeof ft) && ft[0] == '1';
    running = true;
    return 0;
}

PU_EXPORT("gasm_frame") void pu_gasm_frame(void)
{
    static float audio_buf[2048 * 2];
    if (!running) return;
    for (uint32_t p = 0; p < 4; p++) pads[p] = gasm_input_pad(p);
    poll_key_events();
    double t0 = frametime_on ? wall_ms() : 0;
    if (!app_frame()) {
        running = false;
        app_exit();
        __wasi_proc_exit(0);
    }
    audio_acc += APP_AUDIO_RATE / frame_hz;
    unsigned n = (unsigned)audio_acc;
    audio_acc -= n;
    app_audio(audio_buf, n);
    refill_after_gap();
    gasm_audio_push(audio_buf, n);
    if (frametime_on) frametime_log(t0);
}

PU_EXPORT("gasm_exit") void pu_gasm_exit(void)
{
    if (!running) return;
    running = false;
    app_exit();
}

#ifdef PU_LOOP_THREADS
/* gasm_loop.c's exports: the game's frames from its main loop, which waits a frame between them (the
   network library's threads run in those waits). */
#include "gasm_loop.h"
int gasm_loop_init(void) { return pu_gasm_init(); }
void gasm_loop_exit(void) { pu_gasm_exit(); }
int gasm_main(void)
{
    for (;;) {
        pu_gasm_frame();
        gasm_wait_frame();
    }
}
#endif

/* ---- threads (gasm_thread.h; the LOOP THREADS build only) ---- */

#ifdef PU_LOOP_THREADS
#include "gasm_thread.h"

PlatThread *plat_thread_create(int (*fn)(void *arg), void *arg) { return (PlatThread *)gasm_thread_create(fn, arg, 0); }
void plat_thread_detach(PlatThread *t) { if (t) gasm_thread_detach((gasm_thread *)t); }
bool plat_thread_is_main(void) { return gasm_thread_id(gasm_thread_self()) == 0; }
uint32_t plat_thread_id(void) { return gasm_thread_id(gasm_thread_self()); }
void plat_thread_sleep_ms(double ms) { gasm_thread_sleep_ms(ms); }
void plat_thread_yield(void) { gasm_thread_yield(); }

struct PlatMutex { gasm_mutex m; };
PlatMutex *plat_mutex_create(void)
{
    PlatMutex *m = calloc(1, sizeof *m);
    if (m) m->m = (gasm_mutex)GASM_MUTEX_RECURSIVE_INIT;
    return m;
}
void plat_mutex_lock(PlatMutex *m) { if (m) gasm_mutex_lock(&m->m); }
void plat_mutex_unlock(PlatMutex *m) { if (m) gasm_mutex_unlock(&m->m); }

struct PlatEvent { gasm_mutex m; gasm_cond c; bool manual, set; };
PlatEvent *plat_event_create(bool manual)
{
    PlatEvent *e = calloc(1, sizeof *e);
    if (e) e->manual = manual;
    return e;
}
void plat_event_set(PlatEvent *e)
{
    if (!e) return;
    gasm_mutex_lock(&e->m);
    e->set = true;
    if (e->manual) gasm_cond_broadcast(&e->c);
    else gasm_cond_signal(&e->c);
    gasm_mutex_unlock(&e->m);
}
void plat_event_reset(PlatEvent *e) { if (e) e->set = false; }
bool plat_event_wait(PlatEvent *e, double ms)
{
    if (!e) return false;
    double deadline = ms < 0 ? GASM_FOREVER : gasm_time_ms() + ms;
    gasm_mutex_lock(&e->m);
    while (!e->set)
        if (gasm_cond_wait(&e->c, &e->m, deadline) && !e->set) break;
    bool got = e->set;
    if (got && !e->manual) e->set = false;
    gasm_mutex_unlock(&e->m);
    return got;
}
#else
PlatThread *plat_thread_create(int (*fn)(void *arg), void *arg) { return NULL; }
void plat_thread_detach(PlatThread *t) {}
bool plat_thread_is_main(void) { return true; }
uint32_t plat_thread_id(void) { return 0; }
void plat_thread_sleep_ms(double ms) {}
void plat_thread_yield(void) {}
struct PlatMutex { int unused; };
PlatMutex *plat_mutex_create(void) { return calloc(1, sizeof(PlatMutex)); }
void plat_mutex_lock(PlatMutex *m) {}
void plat_mutex_unlock(PlatMutex *m) {}
struct PlatEvent { bool manual, set; };
PlatEvent *plat_event_create(bool manual)
{
    PlatEvent *e = calloc(1, sizeof *e);
    if (e) e->manual = manual;
    return e;
}
void plat_event_set(PlatEvent *e) { if (e) e->set = true; }
void plat_event_reset(PlatEvent *e) { if (e) e->set = false; }
bool plat_event_wait(PlatEvent *e, double ms)
{
    if (!e || !e->set) return false;
    if (!e->manual) e->set = false;
    return true;
}
#endif
void plat_mutex_destroy(PlatMutex *m) { free(m); }
void plat_event_destroy(PlatEvent *e) { free(e); }
double plat_time_ms(void) { return gasm_time_ms(); }

/* ---- message connections (gasm:net) ---- */

int32_t plat_net_open(const char *url) { return gasm_net_open(url, (uint32_t)strlen(url)); }
int plat_net_state(int32_t conn) { return conn > 0 ? (int)gasm_net_state(conn) : PLAT_NET_ERROR; }
bool plat_net_send(int32_t conn, const void *data, uint32_t len) { return conn > 0 && len && gasm_net_send(conn, data, len) == 0; }
int32_t plat_net_recv(int32_t conn, void *dst, uint32_t cap) { return conn > 0 ? gasm_net_recv(conn, dst, cap) : -1; }
void plat_net_close(int32_t conn) { if (conn > 0) gasm_net_close(conn); }
