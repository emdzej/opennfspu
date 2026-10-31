/* See random.h. */
#include "real/random.h"
#include "exe.h"
#include <stdbool.h>

static uint32_t w[6];   /* 0x5de910 .. 0x5de924 */
static bool loaded;

/* Until something seeds it, the words start as the exe's initial data. */
static void load(void)
{
    if (loaded) return;
    for (int i = 0; i < 6; i++) w[i] = exe_u32(0x5de910 + 4 * (uint32_t)i);
    loaded = exe_loaded();
}

uint32_t REAL_rand(void)
{
    load();
    /* Each word from the last up takes the sum of the one after it, with the carry. */
    uint32_t s = w[4] + w[5];
    uint32_t c = s < w[5] || s < w[4];
    w[4] = s;
    for (int i = 3; i >= 0; i--) {
        uint32_t t = s + c + w[i];
        c = t < w[i];
        w[i] = t;
        s = t;
    }
    /* w[0] took the sum without a carry out; then the counter, and on its wrap the chain of increments. */
    if (++w[5] == 0 && ++w[4] == 0 && ++w[3] == 0 && ++w[2] == 0 && ++w[1] == 0) w[0]++;
    return w[0];
}

void REAL_srand(uint32_t n)
{
    loaded = true;
    w[0] = n + 0xf22d0e56u;
    w[1] = w[0] - 0x69fbe76du;
    w[2] = w[1] + 0x3df3b646u;
    w[3] = w[2] + 0x40dde76du;
    w[4] = w[3] - 0x68cd851fu;
    w[5] = w[4] + 0xd1a9fbe7u;
}

float REAL_randf(void) { return (float)((double)(REAL_rand() & 0xffff) * (1.0 / 65536.0)); }   /* the AX half */
