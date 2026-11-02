/* A player's own picture (see feuserpic.h). gimme.dll is EA's image library: ConvertFile 0x100010e0 opens
   the file, takes its image at the index given, converts it to the 16-bit format (0x36) and writes the FSH.
   The port reads the three simple formats itself and puts the picture's 565 pixels in a window made a
   texture, as the front end's movie frames are (femovie.c), at the picture's own size. */
#include "front/feuserpic.h"
#include "front/fejpeg.h"
#include "exe.h"
#include "pending.h"
#include "real/texture.h"
#include "real/window.h"
#include "render/render.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

static bool alloc_rgb(int w, int h, uint8_t **rgb)
{
    if (w <= 0 || h <= 0 || w > 0x4000 || h > 0x4000) return false;
    *rgb = calloc((size_t)w * (size_t)h, 3);
    return *rgb != NULL;
}

/* BMP: the Windows DIB, 1/4/8 bits through its palette, 16 (555), 24 and 32 bits; uncompressed (or 32
   bits with the standard masks); bottom-up unless the height is negative. */
static bool bmp(const uint8_t *d, size_t n, int *w, int *h, uint8_t **rgb)
{
    if (n < 54 || d[0] != 'B' || d[1] != 'M') return false;
    uint32_t off = le32(d + 10), hs = le32(d + 14);
    if (hs < 40 || 14 + hs > n) return false;
    int32_t W = (int32_t)le32(d + 18), H = (int32_t)le32(d + 22);
    int bpp = (int)le16(d + 28);
    uint32_t comp = le32(d + 30), ncol = le32(d + 46);
    bool topdown = H < 0;
    if (topdown) H = -H;
    if (comp != 0 && !(comp == 3 && (bpp == 32 || bpp == 16))) return false;
    if (!alloc_rgb(W, H, rgb)) return false;
    const uint8_t *pal = d + 14 + hs;
    if (bpp <= 8 && ncol == 0) ncol = 1u << bpp;
    size_t stride = ((size_t)W * (size_t)bpp + 31) / 32 * 4;
    if ((size_t)off + stride * (size_t)H > n || (bpp <= 8 && (size_t)(pal - d) + 4 * ncol > n)) {
        free(*rgb);
        return false;
    }
    for (int y = 0; y < H; y++) {
        const uint8_t *row = d + off + stride * (size_t)(topdown ? y : H - 1 - y);
        uint8_t *o = *rgb + (size_t)y * (size_t)W * 3;
        for (int x = 0; x < W; x++, o += 3) {
            uint32_t r = 0, g = 0, b = 0;
            if (bpp <= 8) {
                uint32_t i = bpp == 8 ? row[x] : bpp == 4 ? (row[x / 2] >> (x & 1 ? 0 : 4)) & 0xf : (row[x / 8] >> (7 - (x & 7))) & 1;
                if (i < ncol) b = pal[4 * i], g = pal[4 * i + 1], r = pal[4 * i + 2];
            } else if (bpp == 16) {
                uint32_t v = le16(row + 2 * x);
                r = (v >> 10 & 0x1f) * 255 / 31, g = (v >> 5 & 0x1f) * 255 / 31, b = (v & 0x1f) * 255 / 31;
            } else if (bpp == 24 || bpp == 32) {
                const uint8_t *p = row + (size_t)x * (size_t)(bpp / 8);
                b = p[0], g = p[1], r = p[2];
            } else {
                free(*rgb);
                return false;
            }
            o[0] = (uint8_t)r, o[1] = (uint8_t)g, o[2] = (uint8_t)b;
        }
    }
    *w = W, *h = H;
    return true;
}

/* TGA: colour-mapped (1, 9), true colour (2, 10) and grey (3, 11), run-length coded from 9; 8, 15, 16, 24 and
   32 bits; bottom-up unless the descriptor's bit 5, right to left with its bit 4. */
static bool tga(const uint8_t *d, size_t n, int *w, int *h, uint8_t **rgb)
{
    if (n < 18) return false;
    int idlen = d[0], cmtype = d[1], type = d[2], bpp = d[16], desc = d[17];
    int cmfirst = (int)le16(d + 3), cmlen = (int)le16(d + 5), cmbits = d[7];
    int W = (int)le16(d + 12), H = (int)le16(d + 14);
    int base = type & 7;
    if (base < 1 || base > 3 || (type & ~8) != base) return false;
    if (base == 1 && (cmtype != 1 || bpp != 8)) return false;
    size_t p = 18 + (size_t)idlen;
    const uint8_t *cmap = d + p;
    int cmb = (cmbits + 7) / 8;
    if (cmtype == 1) p += (size_t)cmlen * (size_t)cmb;
    if (p > n || !alloc_rgb(W, H, rgb)) return false;
    int pb = (bpp + 7) / 8;
    if (pb < 1 || pb > 4) {
        free(*rgb);
        return false;
    }
    int count = 0;
    bool run = false;
    uint8_t px[4] = {0};
    for (int i = 0; i < W * H; i++) {
        if (type & 8) {
            if (count == 0) {
                if (p >= n) break;
                uint8_t c = d[p++];
                run = (c & 0x80) != 0;
                count = (c & 0x7f) + 1;
                if (run) {
                    if (p + (size_t)pb > n) break;
                    memcpy(px, d + p, (size_t)pb);
                    p += (size_t)pb;
                }
            }
            if (!run) {
                if (p + (size_t)pb > n) break;
                memcpy(px, d + p, (size_t)pb);
                p += (size_t)pb;
            }
            count--;
        } else {
            if (p + (size_t)pb > n) break;
            memcpy(px, d + p, (size_t)pb);
            p += (size_t)pb;
        }
        uint32_t r, g, b;
        const uint8_t *c = px;
        int cb = pb;
        if (base == 1) {
            int k = px[0] - cmfirst;
            if (k < 0 || k >= cmlen) k = 0;
            c = cmap + (size_t)k * (size_t)cmb;
            cb = cmb;
        }
        if (base == 3) r = g = b = c[0];
        else if (cb == 2) {
            uint32_t v = le16(c);
            r = (v >> 10 & 0x1f) * 255 / 31, g = (v >> 5 & 0x1f) * 255 / 31, b = (v & 0x1f) * 255 / 31;
        } else if (cb >= 3) b = c[0], g = c[1], r = c[2];
        else r = g = b = c[0];
        int x = i % W, y = i / W;
        if (desc & 0x10) x = W - 1 - x;
        if (!(desc & 0x20)) y = H - 1 - y;
        uint8_t *o = *rgb + ((size_t)y * (size_t)W + (size_t)x) * 3;
        o[0] = (uint8_t)r, o[1] = (uint8_t)g, o[2] = (uint8_t)b;
    }
    *w = W, *h = H;
    return true;
}

/* PCX (ZSoft, run-length coded): 8 bits in one plane through the palette after its 0x0c at the end, or
   three planes of 8 bits (red, green, blue lines). */
static bool pcx(const uint8_t *d, size_t n, int *w, int *h, uint8_t **rgb)
{
    if (n < 128 || d[0] != 0x0a || d[2] != 1 || d[3] != 8) return false;
    int W = (int)le16(d + 8) - (int)le16(d + 4) + 1, H = (int)le16(d + 10) - (int)le16(d + 6) + 1;
    int planes = d[65], bpl = (int)le16(d + 66);
    if ((planes != 1 && planes != 3) || bpl < W) return false;
    const uint8_t *pal = planes == 1 && n >= 769 && d[n - 769] == 0x0c ? d + n - 768 : NULL;
    if (planes == 1 && !pal) return false;
    if (!alloc_rgb(W, H, rgb)) return false;
    size_t line = (size_t)bpl * (size_t)planes;
    uint8_t *buf = malloc(line);
    if (!buf) {
        free(*rgb);
        return false;
    }
    size_t p = 128;
    for (int y = 0; y < H; y++) {
        size_t k = 0;
        while (k < line && p < n) {
            uint8_t c = d[p++];
            int cnt = 1;
            if ((c & 0xc0) == 0xc0) {
                cnt = c & 0x3f;
                if (p >= n) break;
                c = d[p++];
            }
            while (cnt-- > 0 && k < line) buf[k++] = c;
        }
        uint8_t *o = *rgb + (size_t)y * (size_t)W * 3;
        for (int x = 0; x < W; x++, o += 3) {
            if (planes == 1) memcpy(o, pal + 3 * buf[x], 3);
            else o[0] = buf[x], o[1] = buf[bpl + x], o[2] = buf[2 * bpl + x];
        }
    }
    free(buf);
    *w = W, *h = H;
    return true;
}

bool FEUserPic_Decode(const uint8_t *d, size_t n, const char *ext, int *w, int *h, uint8_t **rgb)
{
    *rgb = NULL;
    if (!strcasecmp(ext, ".bmp")) return bmp(d, n, w, h, rgb);
    if (!strcasecmp(ext, ".tga")) return tga(d, n, w, h, rgb);
    if (!strcasecmp(ext, ".pcx")) return pcx(d, n, w, h, rgb);
    if (!strcasecmp(ext, ".jpg")) return FEJpeg_Decode(d, n, w, h, rgb);
    PENDING_NOTE(0x100010e0, "gimme.dll ConvertFile: a TIFF picture");
    return false;
}

static struct {
    char name[64];
    bool valid;
    UserPicState state;
    Window *win;
    FeImage *img;
} s_pic;

static void pic_free(void)
{
    if (s_pic.img) {
        if (s_pic.img->tex) Texture_dtor(s_pic.img->tex, 1);
        free(s_pic.img);
    }
    if (s_pic.win) WINDOW_destroy(s_pic.win);
    memset(&s_pic, 0, sizeof s_pic);
}

/* The picture's 565 pixels (0x36) in a 256 x 256 window, made the texture "pic16" (the FSH's name,
   0x5ccce0), as the movies' frames are; a picture over 256 keeps its top-left 256 x 256. */
static FeImage *make_image(const uint8_t *rgb, int w, int h)
{
    TexManager *tm = g_render ? g_render->textures : NULL;
    if (!tm) return NULL;
    int tw = 256, th = 256, cw = w < tw ? w : tw, ch = h < th ? h : th;   /* (as the movies' window) */
    s_pic.win = WINDOW_new(tw, th, 16, 0);
    if (!s_pic.win) return NULL;
    for (int y = 0; y < th; y++) {
        uint16_t *o = (uint16_t *)(s_pic.win->pixels + (size_t)y * (size_t)s_pic.win->stride);
        for (int x = 0; x < tw; x++) {
            const uint8_t *c = x < cw && y < ch ? rgb + ((size_t)y * (size_t)w + (size_t)x) * 3 : NULL;
            o[x] = c ? (uint16_t)((c[0] >> 3) << 11 | (c[1] >> 2) << 5 | c[2] >> 3) : 0;
        }
    }
    FeImage *im = calloc(1, sizeof *im);
    Texture *t = calloc(1, sizeof *t);
    if (!im || !t || !Texture_fromshape(t, s_pic.win->shape, tm, 0, exe_str(0x5ccce0))) {
        free(im);
        free(t);
        return NULL;
    }
    TextureStates st = t->states;
    TextureStates_settile(&st, 0);
    Texture_setstates(t, &st);
    im->tex = t;
    im->w = cw, im->h = ch;
    im->u0 = im->v0 = 0;
    im->u1 = (float)cw / (float)tw;
    im->v1 = (float)ch / (float)th;
    snprintf(im->name, sizeof im->name, "%s", exe_str(0x5ccce0));
    return im;
}

UserPicState FEUserPic_Get(const char *name, FeImage **img)
{
    *img = NULL;
    if (!s_pic.valid || strcmp(s_pic.name, name)) {
        pic_free();
        snprintf(s_pic.name, sizeof s_pic.name, "%s", name);
        s_pic.valid = true;
        s_pic.state = USERPIC_NONE;
        for (int i = 0; i < 5; i++) {   /* 0x5d97b0 .. 0x5d97c4: the first file there is */
            char path[96];
            snprintf(path, sizeof path, "%s%s", name, exe_str(exe_u32(0x5d97b0 + 4u * (unsigned)i)));
            if (!vfs_exists(path)) continue;
            size_t n = 0;
            uint8_t *d = vfs_read_all(path, &n), *rgb = NULL;
            int w = 0, h = 0;
            s_pic.state = USERPIC_FAILED;
            if (d && FEUserPic_Decode(d, n, strrchr(path, '.'), &w, &h, &rgb)) {
                s_pic.img = make_image(rgb, w, h);
                if (s_pic.img) s_pic.state = USERPIC_SHOWN;
            }
            free(rgb);
            free(d);
            break;
        }
    }
    *img = s_pic.img;
    return s_pic.state;
}
