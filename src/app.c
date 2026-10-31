/* Application states (see app.h). For now: the exe check and the render system brought up as the game
   does (Render_Init: the 3D Setup's driver "dx7z", mode 1 = 640x480 at 16 bits); the front end and the race come as they are
   ported (docs/re/port-plan.md, docs/guide/status.md). --param thrash_test=1 draws the driver's self-test
   scene instead of a blank frame, thrash_test=2 EAGL's;
   car=<name> shows a car (CAR_selftest). */
#include "app.h"
#include "snd/snd.h"
#include "exe.h"
#include "platform.h"
#include "eagl/eagl.h"
#include "real/set3d.h"
#include "render/car_selftest.h"
#include "render/track_selftest.h"
#include "render/drive_selftest.h"
#include "render/render.h"
#include "render/thrash.h"
#include "static_init.h"
#include <stdio.h>
#include <string.h>

static int selftest;   /* --param thrash_test: 1 the THRASH scene, 2 the EAGL scene */
static char car_name[64];     /* --param car=<name>: that car in the viewer (GameData/CarModel/<name>.crp) */
static char track_name[32];   /* --param track=<name>: the track viewer (TRACK_selftest) */
static char drive_name[32];   /* --param drive=<track>: drive car=<name> (default 964) there (DRIVE_selftest) */

bool app_init(void)
{
    PORT_StaticInit();   /* the original's global constructors, before WinMain */
    char err[256], m[300];
    if (!exe_init(err, sizeof err)) {
        snprintf(m, sizeof m, "OpenNFSPU: %s", err);
        plat_log(m);
        return false;
    }
    /* The port's drivers, what LoadLibrary would find (THRASH_opendll 0x574d50), then the game's start:
       the options (fe.txt), the render system (Render_Init 0x4673b0). */
    THRASH_registerdll(&THRASH_dx7z);
    RenderOptions_Load();
    Render_Init();
    if (!g_render || !g_render->device || !g_render->device->rs) {
        plat_log("OpenNFSPU: the render system could not start (OpenGL ES 3.0 needed)");
        return false;
    }
    char v[8];
    selftest = plat_param("thrash_test", v, sizeof v) ? v[0] - '0' : 0;
    if (!plat_param("car", car_name, sizeof car_name)) car_name[0] = 0;
    if (!plat_param("track", track_name, sizeof track_name)) track_name[0] = 0;
    if (!plat_param("drive", drive_name, sizeof drive_name)) drive_name[0] = 0;
    return true;
}

bool app_frame(void)
{
    if (plat_frame_shown()) {
        if (drive_name[0]) DRIVE_selftest(drive_name, car_name);
        else if (track_name[0]) TRACK_selftest(track_name);
        else if (car_name[0]) CAR_selftest(car_name);
        else if (selftest == 1) THRASH_selftest();
        else if (selftest == 2) EAGL_selftest();
        else {
            THRASH.fn.clearwindow();
            THRASH.fn.pageflip();
        }
    }
    THRASH_present();
    return true;
}

/* The sound library's device (DirectSound and its thread in the original): its fill, as 16-bit stereo
   (0x58f970), to the platform's floats. */
void app_audio(float *stereo, unsigned frames)
{
    static int16_t buf[4096 * 2];
    while (frames) {
        unsigned n = frames < 4096 ? frames : 4096;
        SND_Fill(buf, (int)n);
        for (unsigned i = 0; i < n * 2; i++) stereo[i] = (float)buf[i] * (1.0f / 32768.0f);
        stereo += n * 2;
        frames -= n;
    }
}

void app_exit(void) { Render_Shutdown(); }
