/* The render options (render.h RenderOptions) from fe.txt, the settings file the game reads at start and
   writes back, through the options module (game/options.c: Options_Read 0x4b6450's lines), by the render
   system's keys. Missing keys keep the value the option globals have before the file is read (0, and ""
   for the driver, which then means the WIDTH x HEIGHT mode of the options). */
#include "render/render.h"
#include "game/options.h"
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
    {"lensflare", &g_renderopts.lensflare},
    {"gammacorrection", &g_renderopts.gammacorrection}, {"drawsync", &g_renderopts.drawsync},
    {"viewdistance", &g_renderopts.viewdistance}, {"skyrendering", &g_renderopts.skyrendering},
    {"nomip", &g_renderopts.nomip},
};

void RenderOptions_Load(void)
{
    memset(&g_renderopts, 0, sizeof g_renderopts);
    g_renderopts.skyrendering = 2;   /* the front end's option (see render.h) */
    const char *drv = Options_Str("driver");
    if (drv) {
        size_t l = strcspn(drv, " \t#");
        if (l >= sizeof g_renderopts.driver) l = sizeof g_renderopts.driver - 1;
        memcpy(g_renderopts.driver, drv, l);
        g_renderopts.driver[l] = 0;
    }
    for (size_t i = 0; i < sizeof ints / sizeof *ints; i++) *ints[i].v = Options_Get(ints[i].name, *ints[i].v);
    char v[8];
    if (plat_param("sky_detail", v, sizeof v)) g_renderopts.skyrendering = atoi(v);
    if (plat_param("carchrome", v, sizeof v)) g_renderopts.carchrome = atoi(v);   /* (a viewer aid: the reflections) */
    if (plat_param("lensflare", v, sizeof v)) g_renderopts.lensflare = atoi(v);   /* (and the sun's flare) */
    if (plat_param("cardetail", v, sizeof v)) g_renderopts.cardetail = atoi(v);   /* (and the cars' detail: their dents) */
    if (plat_param("widescreen", v, sizeof v)) g_renderopts.widescreen = atoi(v);   /* (and the letterbox) */
}
