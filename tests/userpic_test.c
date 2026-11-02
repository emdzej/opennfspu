/* The player's own picture decoders (front/feuserpic.c): small BMP, TGA, PCX, TIFF and JPEG files made here, read
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

    /* a big-endian TIFF, RGB in one PackBits strip (each row one literal run), its IFD after the strip */
    uint8_t tif[8 + H * (1 + 3 * W) + 2 + 9 * 12 + 4 + 6] = {'M', 'M', 0, 42};
    p = 8;
    for (int y = 0; y < H; y++) {
        tif[p++] = 3 * W - 1;
        for (int x = 0; x < W; x++) colour(x, y, tif + p), p += 3;
    }
    size_t strip = p - 8, bps = p;
    tif[p++] = 0, tif[p++] = 8, tif[p++] = 0, tif[p++] = 8, tif[p++] = 0, tif[p++] = 8;
    size_t ifd = p;
    tif[6] = (uint8_t)(ifd >> 8), tif[7] = (uint8_t)ifd;
    static const uint16_t tags[9][3] = {{256, 3, W}, {257, 3, H}, {258, 3, 0}, {259, 3, 32773}, {262, 3, 2},
                                        {273, 4, 8}, {277, 3, 3}, {278, 3, H}, {279, 4, 0}};
    tif[p++] = 0, tif[p++] = 9;
    for (int i = 0; i < 9; i++) {
        uint32_t v = i == 2 ? (uint32_t)bps : i == 8 ? (uint32_t)strip : tags[i][2];
        uint8_t *e = tif + p;
        e[0] = (uint8_t)(tags[i][0] >> 8), e[1] = (uint8_t)tags[i][0], e[3] = (uint8_t)tags[i][1];
        e[7] = i == 2 ? 3 : 1;
        if (tags[i][1] == 3 && i != 2) e[8] = (uint8_t)(v >> 8), e[9] = (uint8_t)v;
        else e[10] = (uint8_t)(v >> 8), e[11] = (uint8_t)v;
        p += 12;
    }
    p += 4;
    check("tif", tif, p, ".tif");

    /* a grey baseline JPEG, 16 x 8: two blocks, flat 64 and 192 (DC only, quantiser 1). The DC table
       codes category 10 as 00 and 11 as 01; the AC table only EOB, as 0 */
    static const uint8_t head[] = {
        0xff, 0xd8, 0xff, 0xdb, 0, 67, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 0xff, 0xc0, 0, 11, 8, 0, 8, 0, 16, 1, 1, 0x11, 0, 0xff, 0xc4, 0, 21, 0x00, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 10, 11, 0xff, 0xc4, 0, 20, 0x10, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x00,
        0xff, 0xda, 0, 8, 1, 1, 0x00, 0, 63, 0};
    /* 00 0111111111 0 (-512: 64), 01 10000000000 0 (+1024: 192), padded with ones */
    static const uint8_t scan[] = {0x1f, 0xf3, 0x00, 0x1f, 0xff, 0xd9};
    uint8_t jpg[sizeof head + sizeof scan];
    memcpy(jpg, head, sizeof head), memcpy(jpg + sizeof head, scan, sizeof scan);
    int w, h;
    uint8_t *rgb;
    bool ok = FEUserPic_Decode(jpg, sizeof jpg, ".JPG", &w, &h, &rgb);
    CHECK(ok && w == 16 && h == 8);
    int bad = 0;
    for (int i = 0; ok && i < 16 * 8 * 3; i++)
        if (rgb[i] != (i / 3 % 16 < 8 ? 64 : 192)) bad++;
    CHECK(bad == 0);
    printf("jpg: %s\n", ok && !bad ? "ok" : "bad");
    free(rgb);

    /* the same picture progressive: its quantiser, then a DC scan of the top bits (Al 1: -256, +512 in
       categories 9 and 10), the DC's last bits (both 0) and an AC scan of two end-of-blocks */
    static const uint8_t prog[] = {
        0xff, 0xc2, 0, 11, 8, 0, 8, 0, 16, 1, 1, 0x11, 0, 0xff, 0xc4, 0, 21, 0x00, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 9, 10, 0xff, 0xc4, 0, 20, 0x10, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0xff, 0xda, 0,
        8, 1, 1, 0x00, 0, 0, 0x01, 0x1f, 0xec, 0x01, 0xff, 0xda, 0, 8, 1, 1, 0x00, 0, 0, 0x10, 0x3f, 0xff, 0xda, 0, 8,
        1, 1, 0x00, 1, 63, 0x00, 0x3f, 0xff, 0xd9};
    uint8_t pjpg[71 + sizeof prog];
    memcpy(pjpg, head, 71), memcpy(pjpg + 71, prog, sizeof prog);
    ok = FEUserPic_Decode(pjpg, sizeof pjpg, ".jpg", &w, &h, &rgb);
    CHECK(ok && w == 16 && h == 8);
    bad = 0;
    for (int i = 0; ok && i < 16 * 8 * 3; i++)
        if (rgb[i] != (i / 3 % 16 < 8 ? 64 : 192)) bad++;
    CHECK(bad == 0);
    printf("progressive jpg: %s\n", ok && !bad ? "ok" : "bad");
    free(rgb);

    CHECK(!FEUserPic_Decode(bmp, 10, ".bmp", &w, &h, &rgb));
    printf(fails ? "userpic_test: %d failures\n" : "userpic_test: ok\n", fails);
    return fails ? 1 : 0;
}
