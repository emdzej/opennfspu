/* True widescreen's shape (see wide.h). */
#include "wide.h"
#include "platform.h"
#include <math.h>
#include <string.h>

static bool s_auto;

/* the nearest of shapes first..2 (0 none, 1 16:9, 2 16:10) to the display's, -1 when it has no size */
static int nearest(int first)
{
    int w = 0, h = 0;
    plat_display_size(&w, &h);
    if (w <= 0 || h <= 0) return -1;
    const float r = (float)w / (float)h, k[3] = {4.0f / 3.0f, 16.0f / 9.0f, 16.0f / 10.0f};
    int best = first;
    for (int i = first + 1; i < 3; i++)
        if (fabsf(r - k[i]) < fabsf(r - k[best])) best = i;
    return best;
}

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
        int s = nearest(0);
        if (0 < s) shape = s, s_auto = true;
    }
    return shape;
}

int Wide_Now(void)
{
    int shape = Wide_Shape();
    if (!s_auto) return shape;
    int s = nearest(1);
    return s < 0 ? shape : s;
}

static const int sizes[2][WIDE_SIZES][2] = {{{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}},
                                            {{1280, 800}, {1680, 1050}, {1920, 1200}, {2560, 1600}}};

void Wide_Size(int shape, int i, int *w, int *h)
{
    *w = sizes[shape - 1][i][0];
    *h = sizes[shape - 1][i][1];
}

int Wide_List(int wh[][2])
{
    int now = Wide_Now(), n = 0;
    for (int i = 0; i < WIDE_SIZES; i++)
        for (int k = 1; k <= 2; k++)
            if (now == WIDE_NONE || now == k) Wide_Size(k, i, &wh[n][0], &wh[n][1]), n++;
    return n;
}

void Wide_Follow(int *w, int *h)
{
    int now = Wide_Now();
    if (now == WIDE_NONE) return;
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < WIDE_SIZES; i++)
            if (sizes[k][i][0] == *w && sizes[k][i][1] == *h) {
                Wide_Size(now, i, w, h);
                return;
            }
}
