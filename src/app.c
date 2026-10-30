/* Application states (see app.h). For now: the exe check and the THRASH driver brought up as the game
   does (THRASH_init, then mode 1 = 640x480 at 16 bits); the front end and the race come as they are
   ported (docs/re/port-plan.md, docs/guide/status.md). --param thrash_test=1 draws the driver's self-test
   scene instead of a blank frame. */
#include "app.h"
#include "exe.h"
#include "platform.h"
#include "render/thrash.h"
#include <stdio.h>
#include <string.h>

static bool selftest;

bool app_init(void)
{
    char err[256], m[300];
    if (!exe_init(err, sizeof err)) {
        snprintf(m, sizeof m, "OpenNFSPU: %s", err);
        plat_log(m);
        return false;
    }
    if (!THRASH_init() || !THRASH_setvideomode(1, 2, 1)) {
        plat_log("OpenNFSPU: the THRASH driver could not start (OpenGL ES 3.0 needed)");
        return false;
    }
    char v[8];
    selftest = plat_param("thrash_test", v, sizeof v) && v[0] == '1';
    return true;
}

bool app_frame(void)
{
    if (plat_frame_shown()) {
        if (selftest) THRASH_selftest();
        else {
            THRASH_clearwindow();
            THRASH_pageflip();
        }
    }
    THRASH_present();
    return true;
}

void app_audio(float *stereo, unsigned frames) { memset(stereo, 0, (size_t)frames * 2 * sizeof *stereo); }

void app_exit(void) { THRASH_restore(); }
