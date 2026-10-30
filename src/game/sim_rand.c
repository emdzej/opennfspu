/* The simulation's random numbers (see sim_rand.h). */
#include "game/sim_rand.h"
#include "exe.h"
#include <stdbool.h>

static uint32_t state, mult, product;
static bool loaded;

/* Until a race seeds it, the generator starts from the exe's initial data. */
static void load(void)
{
    if (loaded) return;
    state = exe_u32(0x5d1028);
    mult = exe_u32(0x5d102c);
    loaded = exe_loaded();
}

uint32_t SimRand_Next(void)
{
    load();
    product = mult * state;
    state = product & 0xffff;
    return product >> 8 & 0xffff;
}

uint32_t SimRand_Seed(uint32_t n)
{
    uint32_t r = n / 500;
    mult = 123456789u;
    state = 987654321u;
    loaded = true;
    for (n %= 500; n; n--) {
        product = state * 123456789u;
        r = product & 0xffff;
        state = r;
    }
    return r;
}
