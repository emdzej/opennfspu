/* The player's own picture decoders (front/feuserpic.c): small BMP, TGA and PCX files made here, read
   back pixel for pixel. Needs no game data. */
#include "front/feuserpic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

enum { W = 5, H = 3 };

static void colour(int x, int y, uint8_t c[3]) { c[0] = (uint8_t)(x * 50), c[1] = (uint8_t)(y * 100), c[2] = (uint8_t)(x * 10 + y); }

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, unsigned v) { put16(p, v & 0xffff), put16(p + 2, v >> 16); }

static void check(const char *what, const uint8_t *file, size_t n, const char *ext)
{
    int w = 0, h = 0;
    uint8_t *rgb = NULL;
    bool ok = FEUserPic_Decode(file, n, ext, &w, &h, &rgb);
    CHECK(ok && w == W && h == H);
    int bad = 0;
    for (int y = 0; ok && y < H; y++)
        for (int x = 0; x < W; x++) {
            uint8_t c[3];
            colour(x, y, c);
            if (memcmp(rgb + 3 * (y * W + x), c, 3)) bad++;
        }
    CHECK(bad == 0);
    printf("%s: %s\n", what, ok && !bad ? "ok" : "bad");
    free(rgb);
}

int main(void)
{
    /* a 24-bit BMP, bottom-up, rows padded to 4 */
    uint8_t bmp[54 + 16 * H] = {0};
    bmp[0] = 'B', bmp[1] = 'M';
    put32(bmp + 10, 54), put32(bmp + 14, 40), put32(bmp + 18, W), put32(bmp + 22, H), put16(bmp + 26, 1), put16(bmp + 28, 24);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint8_t c[3], *p = bmp + 54 + 16 * (H - 1 - y) + 3 * x;
            colour(x, y, c);
            p[0] = c[2], p[1] = c[1], p[2] = c[0];
        }
    check("bmp", bmp, sizeof bmp, ".bmp");

    /* a run-length TGA (type 10, 24 bits, top-down): each row one raw packet */
    uint8_t tga[18 + H * (1 + 3 * W)] = {0};
    tga[2] = 10, put16(tga + 12, W), put16(tga + 14, H), tga[16] = 24, tga[17] = 0x20;
    size_t p = 18;
    for (int y = 0; y < H; y++) {
        tga[p++] = W - 1;
        for (int x = 0; x < W; x++) {
            uint8_t c[3];
            colour(x, y, c);
            tga[p++] = c[2], tga[p++] = c[1], tga[p++] = c[0];
        }
    }
    check("tga", tga, p, ".TGA");

    /* a 24-bit PCX (three planes, each byte a run of one, coded 0xc1 v) */
    uint8_t pcx[128 + H * 3 * W * 2] = {0};
    pcx[0] = 0x0a, pcx[1] = 5, pcx[2] = 1, pcx[3] = 8, put16(pcx + 8, W - 1), put16(pcx + 10, H - 1), pcx[65] = 3, put16(pcx + 66, W);
    p = 128;
    for (int y = 0; y < H; y++)
        for (int k = 0; k < 3; k++)
            for (int x = 0; x < W; x++) {
                uint8_t c[3];
                colour(x, y, c);
                pcx[p++] = 0xc1, pcx[p++] = c[k];
            }
    check("pcx", pcx, p, ".pcx");

    int w, h;
    uint8_t *rgb;
    CHECK(!FEUserPic_Decode(bmp, 10, ".bmp", &w, &h, &rgb));
    printf(fails ? "userpic_test: %d failures\n" : "userpic_test: ok\n", fails);
    return fails ? 1 : 0;
}
