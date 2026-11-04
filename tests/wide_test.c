/* True widescreen's shape (wide.c): aspect=auto takes the display's nearest shape at the start, and the
   wide sizes then follow the window's shape when it is resized. No game data needed. */
#include "wide.h"
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    setenv("OPENNFSPU_PARAM_ASPECT", "auto", 1);
    setenv("OPENNFSPU_DISPLAY", "1920x1200", 1);
    CHECK(Wide_Shape() == WIDE_16_10);
    int w = 1920, h = 1080;
    Wide_Follow(&w, &h);                       /* a 16:9 size kept in fe.txt, taken as 16:10 */
    CHECK(w == 1920 && h == 1200);

    setenv("OPENNFSPU_DISPLAY", "2560x1440", 1);   /* the window resized to 16:9 */
    CHECK(Wide_Shape() == WIDE_16_10);         /* chosen once: there are wide sizes */
    CHECK(Wide_Now() == WIDE_16_9);
    w = 1680, h = 1050;
    Wide_Follow(&w, &h);
    CHECK(w == 1600 && h == 900);
    w = 1024, h = 768;                         /* the original's sizes stay */
    Wide_Follow(&w, &h);
    CHECK(w == 1024 && h == 768);

    setenv("OPENNFSPU_DISPLAY", "1440x1080", 1);   /* 4:3 now: the nearer wide shape */
    CHECK(Wide_Now() == WIDE_16_10);
    setenv("OPENNFSPU_DISPLAY", "0x0", 1);         /* no size: the start's shape */
    CHECK(Wide_Now() == WIDE_16_10);

    printf("wide_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
