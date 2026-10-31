/* The render options (render.h RenderOptions) from fe.txt, the settings file the game reads at start and
   writes back. The options module (Options_Read 0x4b6450: "name = value" lines, '#' comments, names
   case-insensitive, Options_Parse 0x4b0dc0's records) isn't ported yet; this reads only the render
   system's keys the same way. Missing keys keep the value the option globals have before the file is read
   (0, and "" for the driver, which then means the WIDTH x HEIGHT mode of the options). */
#include "render/render.h"
#include "platform.h"
#include "vfs.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

RenderOptions g_renderopts;

static struct { const char *name; int *v; } ints[] = {
    {"width", &g_renderopts.width}, {"height", &g_renderopts.height}, {"depth", &g_renderopts.depth},
    {"window", &g_renderopts.window}, {"renderdevice", &g_renderopts.renderdevice},
    {"widescreen", &g_renderopts.widescreen}, {"carchrome", &g_renderopts.carchrome},
    {"cardetail", &g_renderopts.cardetail}, {"specialfx", &g_renderopts.specialfx},
    {"adjustableviewdistance", &g_renderopts.adjustableviewdistance}, {"headlights", &g_renderopts.headlights},
    {"gammacorrection", &g_renderopts.gammacorrection}, {"drawsync", &g_renderopts.drawsync},
    {"viewdistance", &g_renderopts.viewdistance}, {"skyrendering", &g_renderopts.skyrendering},
};

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

void RenderOptions_Load(void)
{
    memset(&g_renderopts, 0, sizeof g_renderopts);
    g_renderopts.skyrendering = 2;   /* the front end's option (see render.h) */
    size_t n = 0;
    char *d = (char *)vfs_read_all("fe.txt", &n);
    if (!d) return;
    char *text = realloc(d, n + 1);
    if (!text) { free(d); return; }
    text[n] = 0;
    for (char *line = text, *next; line; line = next) {
        next = strpbrk(line, "\r\n");
        if (next) *next++ = 0;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = trim(line), *val = trim(eq + 1);
        if (!strcasecmp(key, "driver")) {
            size_t l = strlen(val);
            if (l >= sizeof g_renderopts.driver) l = sizeof g_renderopts.driver - 1;
            memcpy(g_renderopts.driver, val, l);
            g_renderopts.driver[l] = 0;
            continue;
        }
        for (size_t i = 0; i < sizeof ints / sizeof *ints; i++)
            if (!strcasecmp(key, ints[i].name)) *ints[i].v = atoi(val);
    }
    free(text);
    char v[8];
    if (plat_param("sky_detail", v, sizeof v)) g_renderopts.skyrendering = atoi(v);
    if (plat_param("carchrome", v, sizeof v)) g_renderopts.carchrome = atoi(v);   /* (a viewer aid: the reflections) */
}
