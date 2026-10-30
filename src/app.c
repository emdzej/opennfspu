/* Application states (see app.h). For now: the exe check and the THRASH driver brought up as the game
   does (THRASH_init, then mode 1 = 640x480 at 16 bits); the front end and the race come as they are
   ported (docs/re/port-plan.md, docs/guide/status.md). --param thrash_test=1 draws the driver's self-test
   scene instead of a blank frame, thrash_test=2 EAGL's;
   car=<name> shows a car (CAR_selftest). */
#include "app.h"
#include "exe.h"
#include "platform.h"
#include "eagl/eagl.h"
#include "real/set3d.h"
#include "render/car_selftest.h"
#include "render/thrash.h"
#include <stdio.h>
#include <string.h>

static int selftest;   /* --param thrash_test: 1 the THRASH scene, 2 the EAGL scene */
static char car_name[64];   /* --param car=<name>: that car in the viewer (GameData/CarModel/<name>.crp) */

bool app_init(void)
{
    char err[256], m[300];
    if (!exe_init(err, sizeof err)) {
        snprintf(m, sizeof m, "OpenNFSPU: %s", err);
        plat_log(m);
        return false;
    }
    /* The driver as the game binds it: the DLL's exports into the table (THRASH_opendll 0x574d50). */
    THRASH_registerdll(&THRASH_dx7z);
    if (!THRASH_opendll("dx7z") || !THRASH.fn.init() || !THRASH.fn.setvideomode(1, 2, 1)) {
        plat_log("OpenNFSPU: the THRASH driver could not start (OpenGL ES 3.0 needed)");
        return false;
    }
    char v[8];
    selftest = plat_param("thrash_test", v, sizeof v) ? v[0] - '0' : 0;
    if (!plat_param("car", car_name, sizeof car_name)) car_name[0] = 0;
    return true;
}

bool app_frame(void)
{
    if (plat_frame_shown()) {
        if (car_name[0]) CAR_selftest(car_name);
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

void app_audio(float *stereo, unsigned frames) { memset(stereo, 0, (size_t)frames * 2 * sizeof *stereo); }

void app_exit(void) { THRASH.fn.restore(); }
