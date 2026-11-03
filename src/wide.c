/* True widescreen's shape (see wide.h). */
#include "wide.h"
#include "platform.h"
#include <math.h>
#include <string.h>

int Wide_Shape(void)
{
    static int shape = -1;
    if (shape >= 0) return shape;
    char v[8];
    shape = WIDE_NONE;
    if (!plat_param("aspect", v, sizeof v)) return shape;
    if (strcmp(v, "16:9") == 0) shape = WIDE_16_9;
    else if (strcmp(v, "16:10") == 0) shape = WIDE_16_10;
    else if (strcmp(v, "auto") == 0) {
        int w = 0, h = 0;
        plat_display_size(&w, &h);
        if (0 < w && 0 < h) {
            /* the nearest of the three shapes by the width over the height */
            const float r = (float)w / (float)h, k[3] = {4.0f / 3.0f, 16.0f / 9.0f, 16.0f / 10.0f};
            int best = 0;
            for (int i = 1; i < 3; i++)
                if (fabsf(r - k[i]) < fabsf(r - k[best])) best = i;
            shape = best;   /* (0 none, 1 16:9, 2 16:10: k's order) */
        }
    }
    return shape;
}
