/* True widescreen without a parameter (wide.c): no shape chosen, and the options' list gets both wide
   shapes' sizes, smallest first. No game data needed. */
#include "wide.h"
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    unsetenv("OPENNFSPU_PARAM_ASPECT");
    CHECK(Wide_Shape() == WIDE_NONE && Wide_Now() == WIDE_NONE);
    int wh[2 * WIDE_SIZES][2];
    CHECK(Wide_List(wh) == 2 * WIDE_SIZES);
    CHECK(wh[0][0] == 1280 && wh[0][1] == 720 && wh[1][0] == 1280 && wh[1][1] == 800);
    CHECK(wh[4][0] == 1920 && wh[4][1] == 1080 && wh[5][0] == 1920 && wh[5][1] == 1200);
    int w = 1920, h = 1200;
    Wide_Follow(&w, &h);                       /* no shape to follow: kept */
    CHECK(w == 1920 && h == 1200);
    printf("wide_default_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
