/* The screen viewer (fe_selftest.h). */
#include "front/fe_selftest.h"
#include "front/fe2d.h"
#include "front/fe_parts.h"
#include "front/fert.h"
#include "vfs.h"
#include <stdlib.h>
#include "render/render.h"
#include <stdio.h>
#include <string.h>

static bool ready;
static char loaded[64];
static FertControl *window;
static RCamera *camera;

/* fe.txt's language (Options_Language; the options module isn't ported yet) */
static int language(void)
{
    size_t n = 0;
    char *t = (char *)vfs_read_all("fe.txt", &n);
    int v = 0;
    if (!t) return 0;
    char *b = realloc(t, n + 1);
    if (!b) { free(t); return 0; }
    b[n] = 0;
    for (char *l = b; l && *l; ) {
        char *e = strpbrk(l, "\r\n");
        if (e) *e++ = 0;
        while (*l == ' ') l++;
        if (!strncmp(l, "language", 8) && (l[8] == ' ' || l[8] == '=')) {
            char *q = strchr(l, '=');
            if (q) v = atoi(q + 1);
        }
        l = e;
    }
    free(b);
    return v;
}

static void setup(void)
{
    ready = true;
    Fert_ListControls(FE_DIR_CONTROLS);
    FEStrings_Load(language());
    FEParts_Init();
    FE2D_Init();
    camera = Render_CreateCamera(g_render, 5, 0, 0, 1, 1, 0xff000000u, 1.0f, 100.0f);
}

bool FE_selftest(const char *layout)
{
    if (!g_render) return false;
    if (!ready) setup();
    if (strcmp(loaded, layout)) {
        char path[300];
        snprintf(path, sizeof path, "%s%s", FE_DIR_LAYOUTS, layout);
        FertControl_free(window);
        window = FertControl_new(true);
        if (!window || !FertWindow_LoadLayout(window, path)) return false;
        /* (a second update: the text parts' sizes measured by the first) */
        if (window->screen) FertControl_Update(window->screen);
        for (int i = 0; i < window->nchildren; i++) FertControl_Update(window->children[i]);
        snprintf(loaded, sizeof loaded, "%s", layout);
    }
    TexManager *tm = g_texgraphics ? g_texgraphics->textures : NULL;
    Render_BeginFrame(g_render);
    if (tm) TexManager_begin(tm);
    View_Begin(g_render, camera);
    RCamera_Setup2D(camera);
    g_fe_depth = 0.99998474f;
    FEWindow_Draw(window);
    FE2D_Flush();
    View_End(g_render);
    if (tm) TexManager_end(tm);
    Render_EndFrame(g_render);
    return true;
}
