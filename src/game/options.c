/* The game's options (options.h). */
#include "game/options.h"
#include "exe.h"
#include "game/hud_config.h"
#include "platform.h"
#include "vfs.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { MAXLINES = 1024 };

static char *s_lines[MAXLINES];
static int s_nlines;
static bool s_loaded, s_dirty;

static void add_line(const char *s, size_t n)
{
    if (s_nlines == MAXLINES) return;
    char *l = malloc(n + 1);
    if (!l) return;
    memcpy(l, s, n);
    l[n] = 0;
    s_lines[s_nlines++] = l;
}

void Options_Load(void)
{
    if (s_loaded) return;
    s_loaded = true;
    size_t n = 0;
    uint8_t *d = plat_load_user_file("fe.txt", &n);
    if (!d) d = vfs_read_all("fe.txt", &n);
    if (!d) return;
    const char *p = (const char *)d, *e = p + n;
    while (p < e) {
        const char *q = p;
        while (q < e && *q != '\n' && *q != '\r') q++;
        add_line(p, (size_t)(q - p));
        if (q < e && *q == '\r') q++;
        if (q < e && *q == '\n') q++;
        p = q;
    }
    free(d);
}

/* The line of a key: its name before '=' (spaces round it), case-insensitive; '#' a comment. */
static int find(const char *key, const char **val)
{
    Options_Load();
    size_t kl = strlen(key);
    int found = -1;
    for (int i = 0; i < s_nlines; i++) {
        const char *l = s_lines[i];
        while (*l == ' ' || *l == '\t') l++;
        if (*l == '#' || strncasecmp(l, key, kl)) continue;
        const char *r = l + kl;
        while (*r == ' ' || *r == '\t') r++;
        if (*r != '=') continue;
        r++;
        while (*r == ' ' || *r == '\t') r++;
        if (val) *val = r;
        found = i;   /* (the last one counts, as the reader's later lines overwrite) */
    }
    return found;
}

const char *Options_Str(const char *key)
{
    const char *v = NULL;
    return find(key, &v) >= 0 ? v : NULL;
}

int Options_Get(const char *key, int def)
{
    const char *v = Options_Str(key);
    return v ? atoi(v) : def;
}

int Options_Int(uint32_t key, int def) { return Options_Get(exe_str(key), def); }

void Options_SetStr(const char *name, const char *v)
{
    if (!name || !*name) return;
    char b[160];
    snprintf(b, sizeof b, "%s = %s", name, v);
    for (char *c = b; *c && *c != ' '; c++) *c = (char)tolower((unsigned char)*c);
    int i = find(name, NULL);
    if (i >= 0) {
        if (!strcmp(s_lines[i], b)) return;
        free(s_lines[i]);
        s_lines[i] = strdup(b);
    } else {
        add_line(b, strlen(b));
    }
    s_dirty = true;
}

void Options_SetInt(uint32_t key, int v)
{
    char b[16];
    snprintf(b, sizeof b, "%d", v);
    Options_SetStr(exe_str(key), b);
}

void Options_Save(void)
{
    if (!s_dirty) return;
    size_t n = 0;
    for (int i = 0; i < s_nlines; i++) n += strlen(s_lines[i]) + 2;
    char *b = malloc(n + 1), *p = b;
    if (!b) return;
    for (int i = 0; i < s_nlines; i++) p += sprintf(p, "%s\r\n", s_lines[i]);
    if (plat_save_user_file("fe.txt", b, (size_t)(p - b))) s_dirty = false;
    free(b);
}

extern int32_t g_hud_opponentlist;

void Options_Apply(void)
{
    g_hud_splits = Options_Int(0x5d5f58, g_hud_splits);              /* SPLITTIMES -> 0x657dd8 */
    g_hud_opponentlist = Options_Int(0x5d5f64, g_hud_opponentlist);  /* OPPONENTLIST -> 0x657dd4 */
    g_hud_imperial = Options_Int(0x5d5fa4, g_hud_imperial);          /* SPEEDUNITS -> 0x657dc0 */
    g_hud_damage_display = Options_Int(0x5d5f84, g_hud_damage_display);   /* DAMAGEDISPLAY -> 0x657dc8 */
}
