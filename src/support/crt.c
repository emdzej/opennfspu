/* The C runtime's pieces (see crt.h). */
#include "support/crt.h"

static uint32_t s_seed = 1;   /* (the thread's +0x14) */

int CRT_rand(void)
{
    s_seed = s_seed * 0x343fdu + 0x269ec3u;
    return (int)(s_seed >> 16 & 0x7fff);
}
