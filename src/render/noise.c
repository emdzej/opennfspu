/* See noise.h. */
#include "render/noise.h"
#include "real/random.h"
#include <stdint.h>

static float grad[267];       /* 0x619804 .. 0x619c30 */
static int16_t perm[0x1000];  /* 0x619c30 .. 0x61bc30 */

void Noise_Init(void)
{
    uint32_t keep = REAL_rand();
    REAL_srand(0x3039);
    for (int i = 0; i < 267; i++) {
        /* The whole 32-bit value times 2^-32 (0x5b3ca4), less 0.5, doubled: -1 .. 1. */
        float f = (float)((double)REAL_rand() * (double)2.3283064365386963e-10f - 0.5);
        grad[i] = f + f;
    }
    for (int i = 0; i < 0x1000; i++) perm[i] = (int16_t)i;
    for (int i = 0xfff; i >= 0; i--) {
        uint32_t r = REAL_rand() & 0xfff;
        int16_t t = perm[i];
        perm[i] = perm[r];
        perm[r] = t;
    }
    REAL_srand(keep);
}

/* A corner's byte: the permutation's low byte at z ^ perm[y ^ perm[x]]. */
static int corner(int x, int y, int z)
{
    int a = perm[x & 0xfff];
    int b = perm[(y & 0xfff) ^ a];
    return (uint8_t)perm[(z & 0xfff) ^ b];
}

static double term(int u, double dx, double dy, double dz)
{
    return (double)grad[u] * 0.5 + (double)grad[u + 1] * dx + (double)grad[u + 2] * dy + (double)grad[u + 3] * dz;
}

static float ease(float t) { return (3.0f - (t + t)) * t * t; }   /* 0x5b2600 */

float Noise3(float x, float y, float z)
{
    x += 10000.0f;   /* less -10000 (0x5b2760) */
    y += 10000.0f;
    z += 10000.0f;
    int ix = (int)x, iy = (int)y, iz = (int)z;   /* _ftol: towards zero */
    float fx0 = x - (float)ix, fy0 = y - (float)iy, fz0 = z - (float)iz;
    float fx1 = x - (float)(ix + 1), fy1 = y - (float)(iy + 1), fz1 = z - (float)(iz + 1);
    float sx = ease(fx0), sy = ease(fy0), sz = ease(fz0);
    double w00 = (double)((1.0f - sy) * (1.0f - sx)), w10 = (double)((1.0f - sy) * sx);
    double w01 = (double)((1.0f - sx) * sy), w11 = (double)(sy * sx);
    double z1 = sz, z0 = 1.0f - sz;
    return (float)(w11 * z1 * term(corner(ix + 1, iy + 1, iz + 1), fx1, fy1, fz1) +
                   w01 * z1 * term(corner(ix, iy + 1, iz + 1), fx0, fy1, fz1) +
                   w10 * z1 * term(corner(ix + 1, iy, iz + 1), fx1, fy0, fz1) +
                   w00 * z1 * term(corner(ix, iy, iz + 1), fx0, fy0, fz1) +
                   w11 * z0 * term(corner(ix + 1, iy + 1, iz), fx1, fy1, fz0) +
                   w01 * z0 * term(corner(ix, iy + 1, iz), fx0, fy1, fz0) +
                   w10 * z0 * term(corner(ix + 1, iy, iz), fx1, fy0, fz0) +
                   w00 * z0 * term(corner(ix, iy, iz), fx0, fy0, fz0));
}
