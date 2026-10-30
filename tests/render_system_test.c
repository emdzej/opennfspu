/* The render system without a driver (render/render.h): the 3D Setup defaults from the exe's registry
   table, the render options of fe.txt, a camera frame turned into a view (RFrame_ToView 0x4661a0), a camera
   (Render_CreateCamera 0x467230) and the render-state cull (0x537600, 0x467ed0). Skips without data. */
#include "exe.h"
#include "render/render.h"
#include "vfs.h"
#include "vfs_host.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static bool near(float a, float b) { return fabsf(a - b) < 1e-5f; }

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("render_system_test: no game data, skipped\n"); return 0; }
    char err[256];
    CHECK(exe_init(err, sizeof err));

    /* The registry's defaults (the values RegSetup.reg also writes). */
    char s[64];
    RRegistry_GetString(RREG_DRIVER, s, sizeof s);
    CHECK(!strcmp(s, "dx"));
    RRegistry_GetString(RREG_RESOLUTION, s, sizeof s);
    CHECK(!strcmp(s, "640x480"));
    CHECK(RRegistry_GetInt(RREG_DEVICE) == 0);
    CHECK(RRegistry_GetInt(RREG_HWACCEL) == 1);

    /* fe.txt's render section. */
    RenderOptions_Load();
    CHECK(!strcmp(g_renderopts.driver, "registry"));
    CHECK(g_renderopts.width == 640 && g_renderopts.height == 480 && g_renderopts.depth == 16);
    CHECK(g_renderopts.specialfx == 3 && g_renderopts.drawsync == 1 && g_renderopts.gammacorrection == 100);
    CHECK(g_renderopts.carchrome == 0 && g_renderopts.headlights == 0);

    /* A frame: the eye at (1, 2, 3) looking along +z with right +x and up +y: the view turns the forward
       row to the front, so a point 5 ahead of the eye ends at camera z = -5 (EAGL's front is -z). */
    RFrame f;
    RFrame_Init(&f);
    f.eye[0] = 1; f.eye[1] = 2; f.eye[2] = 3;
    RFrame_ToView(&f);
    float p[3] = {1, 2, 8}, q[3];
    for (int j = 0; j < 3; j++) q[j] = p[0] * f.rot[j] + p[1] * f.rot[3 + j] + p[2] * f.rot[6 + j] + f.trans[j];
    CHECK(near(q[0], 0) && near(q[1], 0) && near(q[2], -5));
    CHECK(near(f.orient[8], -1));   /* the input's forward row is negated in place, as the original */

    /* A camera over the whole 640x480 display. */
    g_display.stamp = 1;
    g_display.width = 640;
    g_display.height = 480;
    RenderSystem rs = {0};
    RDevice dev = {0};
    RenderState st;
    memset(&st, 0, sizeof st);
    st.cull = 2;
    dev.rs = &st;
    rs.device = &dev;
    g_render = &rs;
    RCamera *c = Render_CreateCamera(&rs, 0, 0, 0, 1, 1, 0xff77bbffu, 0.15f, 700.0f);
    CHECK(c && c->view && c->halffov == 45.0f && c->view->background == 0xff77bbffu);
    CHECK(c && near(c->view->vp_w, 640) && near(c->view->vp_h, 480));
    CHECK(c && c->frame.orient[0] == 1 && c->frame.orient[4] == 1 && c->frame.orient[8] == 1);
    /* The car's draw flips the cull and flips it back. */
    Render_SetCull(&rs);
    CHECK(st.cull == 1);
    Render_SetCull(&rs);
    CHECK(st.cull == 2);
    RCamera_Free(c);
    g_render = NULL;

    printf("render_system_test: %d checks, %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
