/* EAGL primitive kernels, clipper and depth sort (0x540a40, 0x543840, 0x54bd90, 0x5490e0, 0x553630..).

   Only the DX7 path of each kernel is ported: the kernels test EAGL_CF_DX7 (set when the driver's about
   block names itself "DX7", which dx7z.dll and the port's THRASH do) and the other path, for Glide-era
   drivers drawing through drawtrimesh with int indices, is never taken with them.

   A kernel works on the context's vertex array, where the coords layer left clip-space vertices
   (x, y, z, 1/z) and the codes layer per-primitive codes:
   1. colours, specular and uv streams newer than what this draw already copied (the stamps: a stream's
      layer position) are copied into the vertices;
   2. if the positions were transformed in this draw, vertices of triangles inside the guard band are
      projected to the screen (x * sx * rhw + ox, ...; z = (z * sz + oz) * rhw + depth bias) and indexed;
      triangles that need clipping go through the clipper, whose polygon is projected after the input
      vertices and fanned into triangles;
   3. the result goes to THRASH in one setstate(0x1b3): an indexed triangle list, or with every vertex
      inside, the vertices as they are (a list or strip without indices).
   A material with several draw layers runs the kernel again: with nothing new it draws the previous
   result once more (multi-pass). */
#include "eagl/eagl.h"
#include "eagl/eagl_pending.h"
#include "pending.h"
#include "real/set3d.h"
#include <string.h>

#define VSTRIDE 8   /* floats per work vertex (32 bytes, one texture coordinate set) */

static uint32_t ubits(float f) { union { float f; uint32_t u; } c = {f}; return c.u; }
static float ufloat(uint32_t u) { union { uint32_t u; float f; } c = {u}; return c.f; }

static void project(const EaglContext *c, float *v)
{
    float x = v[0], y = v[1], z = v[2], w = v[3];
    v[0] = x * c->sx * w + c->ox;
    v[1] = y * c->sy * w + c->oy;
    v[2] = (z * c->sz + c->oz) * w + c->depthbias;
}

static void project_to(const EaglContext *c, const float *src, float *dst)
{
    float x = src[0], y = src[1], z = src[2], w = src[3];
    dst[0] = x * c->sx * w + c->ox;
    dst[1] = y * c->sy * w + c->oy;
    dst[2] = (z * c->sz + c->oz) * w + c->depthbias;
}

static void reset_format(EaglContext *c)
{
    if (!c->twouv) return;
    if (c->blend0 != 8) {
        THRASH.fn.setstate(0x29, 8);
        c->blend0 = 8;
    }
    if (c->blend1 != 0) {
        THRASH.fn.setstate(0x10029, 0);
        c->blend1 = 0;
    }
    THRASH.fn.setstate(0x2a, 0);
    c->twouv = 0;
}

/* The draw at the end of every kernel (the THRASH primitive lives in the context, +0x117f0). */
static void submit(EaglContext *c, uint32_t unindexed_prim, bool sort)
{
    if (!c->out_nindices) return;
    TEXTURE_select(c->texture, 0);
    reset_format(c);
    c->draw.prim = 4;
    c->draw.fvf = 0x1c4;
    c->draw.verts = c->verts;
    if (sort && c->sort) EAGL_SortTriangles(c);
    if (!(c->flags & EAGL_CF_INSIDE)) {
        c->draw.nverts = c->out_nverts;
        c->draw.indices = c->indices;
        c->draw.nindices = c->out_nindices;
    } else {
        c->draw.prim = unindexed_prim;
        c->draw.nverts = (uint16_t)c->nverts;
        c->draw.indices = NULL;
        c->draw.nindices = 0;
    }
    THRASH.fn.setstate(THRASH_STATE_DRAWPRIMITIVE, (uintptr_t)&c->draw);
}

/* Copies a clipped polygon's attributes after it (rhw only for new positions). */
static void copy_attrs(float *out, const float *poly, int n, bool rhw, bool d, bool s, bool uv, bool uv2)
{
    for (; n; n--, out += VSTRIDE, poly += VSTRIDE) {
        if (rhw) out[3] = poly[3];
        if (d) out[4] = poly[4];
        if (s) out[5] = poly[5];
        if (uv) {
            out[6] = poly[6];
            out[7] = poly[7];
        }
        if (uv2) {   /* two-set vertices don't fit the 32-byte work vertex: kept as the original writes them */
            out[8] = poly[8];
            out[9] = poly[9];
        }
    }
}

static void take_stamps(EaglContext *c, const uint32_t **diffuse, const uint32_t **specular, const float **uv,
                        int sd, int ss, int su)
{
    if (c->stamp_diffuse < sd) c->stamp_diffuse = sd;
    else *diffuse = NULL;
    if (c->stamp_specular < ss) c->stamp_specular = ss;
    else *specular = NULL;
    if (c->stamp_uv < su) c->stamp_uv = su;
    else *uv = NULL;
}

static void pending_glide_path(EaglContext *c)
{
    PENDING_NOTE(0x540a40, "EAGL kernels: the non-DX7 path (drawtrimesh)");
    c->flags &= 0xffffff07u;
}

/* ---- triangle lists (0x543840) ---- */

void EAGL_DrawList(EaglContext *c, const uint32_t *diffuse, const uint32_t *specular, const float *uv,
                   int sd, int ss, int su)
{
    take_stamps(c, &diffuse, &specular, &uv, sd, ss, su);
    uint32_t f = c->flags;
    if (!(f & EAGL_CF_DX7)) {
        pending_glide_path(c);
        return;
    }
    float *v = (float *)c->verts;
    int n = c->nverts;
    EaglClipFn clip = (f & EAGL_CF_ORTHO) ? EAGL_ClipOrtho : EAGL_ClipPerspective;
    bool has_s = f & EAGL_CF_SPECULAR, moved = f & EAGL_CF_TRANSFORMED, has_uv = f & EAGL_CF_UV;
    bool has_d = f & EAGL_CF_DIFFUSE, uv2 = f & EAGL_CF_UV2;
    int ntri3 = (n + 2) / 3;   /* the loops round up (n is a multiple of 3 in practice) */
    if (diffuse && !has_d) {
        if (!(f & EAGL_CF_INSIDE)) {
            for (int t = 0; t < ntri3; t++)
                if (c->primcodes[t][0] == 0)
                    for (int k = 0; k < 3; k++) v[(3 * t + k) * VSTRIDE + 4] = ufloat(diffuse[3 * t + k]);
        } else {
            for (int i = 0; i < n; i++) v[i * VSTRIDE + 4] = ufloat(diffuse[i]);
        }
        has_d = true;
    }
    if (specular && !has_s) {
        if (!(f & EAGL_CF_INSIDE)) {
            for (int t = 0; t < ntri3; t++)
                if (c->primcodes[t][0] == 0)
                    for (int k = 0; k < 3; k++) v[(3 * t + k) * VSTRIDE + 5] = ufloat(specular[3 * t + k]);
        } else {
            for (int i = 0; i < n; i++) v[i * VSTRIDE + 5] = ufloat(specular[i]);
        }
        has_s = true;
    }
    if (uv && !has_uv) {
        if (!(f & EAGL_CF_INSIDE)) {
            for (int t = 0; t < ntri3; t++)
                if (c->primcodes[t][0] == 0)
                    for (int k = 0; k < 3; k++) {
                        v[(3 * t + k) * VSTRIDE + 6] = uv[2 * (3 * t + k)];
                        v[(3 * t + k) * VSTRIDE + 7] = uv[2 * (3 * t + k) + 1];
                    }
        } else {
            for (int i = 0; i < n; i++) {
                v[i * VSTRIDE + 6] = uv[2 * i];
                v[i * VSTRIDE + 7] = uv[2 * i + 1];
            }
        }
        has_uv = true;
    }
    uint16_t *idx = c->indices;
    int nout = 0;
    if (uv2 || has_uv || has_s || has_d || moved) {
        if (!(f & EAGL_CF_INSIDE)) {
            float *out = v + n * VSTRIDE;
            nout = n;
            uint16_t base = 0;
            for (int t = 0; t < ntri3; t++, base += 3) {
                float *tv = v + 3 * t * VSTRIDE;
                if (c->primcodes[t][0] == 0 && c->primcodes[t][1] == 0) {
                    if (moved) {
                        for (int k = 0; k < 3; k++) project(c, tv + k * VSTRIDE);
                        idx[0] = base;
                        idx[1] = (uint16_t)(base + 1);
                        idx[2] = (uint16_t)(base + 2);
                        idx += 3;
                    }
                } else if (c->primcodes[t][0] == 0) {
                    float *poly;
                    int cnt;
                    clip(0, 1, 2, c->primcodes[t][1], c->guardband, tv, has_d, has_s, has_uv, uv2, c->znear, c->zfar,
                         &poly, &cnt);
                    if (cnt > 2) {
                        if (moved) {
                            for (int k = 0; k < cnt; k++) project_to(c, poly + k * VSTRIDE, out + k * VSTRIDE);
                            for (int k = 0; k < cnt - 2; k++) {
                                idx[0] = (uint16_t)nout;
                                idx[1] = (uint16_t)(nout + 1 + k);
                                idx[2] = (uint16_t)(nout + 2 + k);
                                idx += 3;
                            }
                            nout += cnt;
                        }
                        copy_attrs(out, poly, cnt, moved, has_d, has_s, has_uv, uv2);
                        out += cnt * VSTRIDE;
                    }
                }
            }
            if (moved) {
                c->out_nverts = (uint16_t)nout;
                c->out_nindices = (uint16_t)(idx - c->indices);
            }
        } else if (moved) {
            for (int i = 0; i < n; i++) project(c, v + i * VSTRIDE);
            c->out_nverts = (uint16_t)n;
            c->out_nindices = (uint16_t)n;
        }
    }
    submit(c, 4, true);
    c->flags &= 0xffffff07u;
}

/* ---- strips (0x540a40) ---- */

/* Which copy of each strip vertex the triangles use (0x6bd060): the vertex itself when every
   triangle through it is inside, else a projected copy after the input (the clipper needs the
   clip-space original). */
static int remap[256];

void EAGL_DrawStrip(EaglContext *c, const uint32_t *diffuse, const uint32_t *specular, const float *uv,
                    int sd, int ss, int su)
{
    take_stamps(c, &diffuse, &specular, &uv, sd, ss, su);
    uint32_t f = c->flags;
    if (!(f & EAGL_CF_DX7)) {
        pending_glide_path(c);
        return;
    }
    float *v = (float *)c->verts;
    int n = c->nverts;
    EaglClipFn clip = (f & EAGL_CF_ORTHO) ? EAGL_ClipOrtho : EAGL_ClipPerspective;
    bool moved = f & EAGL_CF_TRANSFORMED, has_d = f & EAGL_CF_DIFFUSE, has_s = f & EAGL_CF_SPECULAR;
    bool has_uv = f & EAGL_CF_UV, uv2 = f & EAGL_CF_UV2;
    bool spread = !(f & (EAGL_CF_INSIDE | EAGL_CF_TRANSFORMED));   /* new streams reach the copies too */
    if (diffuse && !has_d) {
        for (int i = 0; i < n; i++) v[i * VSTRIDE + 4] = ufloat(diffuse[i]);
        if (spread)
            for (int i = n - 1; i >= 0; i--)
                if (remap[i] != i) v[remap[i] * VSTRIDE + 4] = v[i * VSTRIDE + 4];
        has_d = true;
    }
    if (specular && !has_s) {
        for (int i = 0; i < n; i++) v[i * VSTRIDE + 5] = ufloat(specular[i]);
        if (spread)
            for (int i = n - 1; i >= 0; i--)
                if (remap[i] != i) v[remap[i] * VSTRIDE + 5] = v[i * VSTRIDE + 5];
        has_s = true;
    }
    if (uv && !has_uv) {
        for (int i = 0; i < n; i++) {
            v[i * VSTRIDE + 6] = uv[2 * i];
            v[i * VSTRIDE + 7] = uv[2 * i + 1];
        }
        if (spread)
            for (int i = n - 1; i >= 0; i--)
                if (remap[i] != i) {
                    v[remap[i] * VSTRIDE + 6] = v[i * VSTRIDE + 6];
                    v[remap[i] * VSTRIDE + 7] = v[i * VSTRIDE + 7];
                }
        has_uv = true;
    }
    uint16_t *idx = c->indices;
    /* Vertex n is scratch: it keeps where the copies end and the next index between the kernel runs of a
       multi-pass material (the original stores the pointer itself; the port an index). */
    float *scratch = v + n * VSTRIDE;
    if (moved) {
        if (!(f & EAGL_CF_INSIDE)) {
            int next = n + 1, outv = n + 1;
            uint32_t hist = 0;
            int i = 0;
            /* n < 2 would make the original's unsigned triangle counter wrap: strips have 3+ vertices. */
            for (; i < n - 2; i++) {
                remap[i] = i;
                if (c->primcodes[i][1] == 0 && c->primcodes[i][0] == 0) hist = hist << 8 | 0xf;
                else {
                    hist <<= 8;
                    if (c->primcodes[i][0] == 0) hist |= 0xf0;
                }
                if (hist & 0xf0f0f) {
                    if (!(hist & 0xf0f0f0)) {
                        project(c, v + i * VSTRIDE);
                    } else {
                        memcpy(v + outv * VSTRIDE, v + i * VSTRIDE, VSTRIDE * sizeof *v);
                        project(c, v + outv * VSTRIDE);
                        outv++;
                        remap[i] = next++;
                    }
                }
            }
            /* The last two vertices with the last triangle's history (it still holds the triangle two
               back, which doesn't use vertex n-2: kept). */
            if (hist & 0xf0f0f) {
                if (!(hist & 0xf0f0f0)) {
                    project(c, v + i * VSTRIDE);
                    project(c, v + (i + 1) * VSTRIDE);
                    remap[i] = i;
                    remap[i + 1] = i + 1;
                } else {
                    memcpy(v + outv * VSTRIDE, v + i * VSTRIDE, 2 * VSTRIDE * sizeof *v);
                    project(c, v + outv * VSTRIDE);
                    project(c, v + (outv + 1) * VSTRIDE);
                    outv += 2;
                    remap[i] = next;
                    remap[i + 1] = next + 1;
                    next += 2;
                }
            }
            scratch[0] = ufloat((uint32_t)outv);
            scratch[1] = ufloat((uint32_t)next);
        } else {
            for (int i = 0; i < n; i++) project(c, v + i * VSTRIDE);
            idx = c->indices + n;
            c->out_nverts = (uint16_t)n;
        }
    }
    if ((uv2 || has_uv || has_s || has_d || moved) && !(f & EAGL_CF_INSIDE)) {
        int outv = (int)ubits(scratch[0]), next = (int)ubits(scratch[1]);
        for (int i = 0; i < n - 2; i++) {
            float *tv = v + i * VSTRIDE;
            if (c->primcodes[i][0] == 0 && c->primcodes[i][1] == 0) {
                if (moved) {
                    if ((i & 1) == 0) {
                        idx[0] = (uint16_t)remap[i];
                        idx[1] = (uint16_t)remap[i + 1];
                        idx[2] = (uint16_t)remap[i + 2];
                    } else {
                        idx[0] = (uint16_t)remap[i + 2];
                        idx[1] = (uint16_t)remap[i + 1];
                        idx[2] = (uint16_t)remap[i];
                    }
                    idx += 3;
                }
            } else if (c->primcodes[i][0] == 0) {
                float *poly;
                int cnt;
                if ((i & 1) == 0)
                    clip(0, 1, 2, c->primcodes[i][1], c->guardband, tv, has_d, has_s, has_uv, uv2, c->znear, c->zfar,
                         &poly, &cnt);
                else
                    clip(2, 1, 0, c->primcodes[i][1], c->guardband, tv, has_d, has_s, has_uv, uv2, c->znear, c->zfar,
                         &poly, &cnt);
                if (cnt > 2) {
                    float *out = v + outv * VSTRIDE;
                    if (moved) {
                        for (int k = 0; k < cnt; k++) project_to(c, poly + k * VSTRIDE, out + k * VSTRIDE);
                        for (int k = 0; k < cnt - 2; k++) {
                            idx[0] = (uint16_t)next;
                            idx[1] = (uint16_t)(next + 1 + k);
                            idx[2] = (uint16_t)(next + 2 + k);
                            idx += 3;
                        }
                        next += cnt;
                    }
                    copy_attrs(out, poly, cnt, moved, has_d, has_s, has_uv, uv2);
                    outv += cnt;
                }
            }
        }
        if (moved) c->out_nverts = (uint16_t)next;
    }
    if (moved) c->out_nindices = (uint16_t)(idx - c->indices);
    submit(c, 5, false);
    c->flags &= 0xffffff07u;
}

/* ---- the perspective clipper (0x54bd90) ---- */

/* Two polygon buffers of 10 vertices, adjacent as in the original (0x6bc340, 0x6bc480: a second
   texture coordinate set written past a vertex lands in the next one). */
static float clipbuf[2 * 10 * VSTRIDE + 2];
#define BUF_A (clipbuf)
#define BUF_B (clipbuf + 10 * VSTRIDE)

/* Colour lerp per channel with the float-to-int trick of the original: delta * t + 1.5 * 2^k leaves
   the rounded scaled delta in the low bits. */
static uint32_t lerp_colour(uint32_t c0, uint32_t c1, float t)
{
    uint32_t a = ubits((float)(int)((c1 >> 24) - (c0 >> 24)) * t + 12582912.0f);
    uint32_t r = ubits((float)(int)((c1 & 0xff0000u) - (c0 & 0xff0000u)) * t + 824633720832.0f);
    uint32_t g = ubits((float)(int)((c1 & 0xff00u) - (c0 & 0xff00u)) * t + 3221225472.0f);
    uint32_t b = ubits(t * (float)(int)((c1 & 0xffu) - (c0 & 0xffu)) + 12582912.0f);
    return (((c0 & 0xff00u) >> 8) + g & 0xff) << 8 | ((c0 & 0xffu) + b & 0xff) | ((c0 >> 24) + a) * 0x1000000u |
           ((((c0 & 0xff0000u) >> 16) + r) & 0xff) << 16;
}

enum { P_NEAR, P_FAR, P_TOP, P_BOTTOM, P_LEFT, P_RIGHT };

/* The two sides of a plane's test: a vertex is outside when lhs <= rhs (on the plane when also
   rhs <= lhs). */
static void plane_sides(int p, const float *v, float gb, float zn, float zf, float *lhs, float *rhs)
{
    switch (p) {
    case P_NEAR: *lhs = v[2]; *rhs = zn; break;
    case P_FAR: *lhs = zf; *rhs = v[2]; break;
    case P_TOP: *lhs = gb * v[2]; *rhs = v[1]; break;
    case P_BOTTOM: *lhs = gb * v[2]; *rhs = -v[1]; break;
    case P_LEFT: *lhs = gb * v[2]; *rhs = -v[0]; break;
    default: *lhs = gb * v[2]; *rhs = v[0]; break;
    }
}

static void intersect(int p, const float *cur, const float *prev, float *o, float gb, float zn, float zf,
                      int d, int s, int uv, int uv2)
{
    float t, z;
    switch (p) {
    case P_NEAR:
    case P_FAR: {
        float zp = p == P_NEAR ? zn : zf;
        t = (zp - prev[2]) / (cur[2] - prev[2]);
        o[2] = zp;
        o[3] = 1.0f / zp;
        o[0] = (cur[0] - prev[0]) * t + prev[0];
        o[1] = (cur[1] - prev[1]) * t + prev[1];
        break;
    }
    case P_TOP:
        t = (gb * prev[2] - prev[1]) / ((cur[1] - prev[1]) - (cur[2] - prev[2]) * gb);
        z = (cur[2] - prev[2]) * t + prev[2];
        o[2] = z;
        o[3] = 1.0f / z;
        o[0] = (cur[0] - prev[0]) * t + prev[0];
        o[1] = gb * o[2];
        break;
    case P_BOTTOM:
        t = (gb * prev[2] + prev[1]) / ((prev[1] - cur[1]) + (prev[2] - cur[2]) * gb);
        z = (cur[2] - prev[2]) * t + prev[2];
        o[2] = z;
        o[3] = 1.0f / z;
        o[0] = (cur[0] - prev[0]) * t + prev[0];
        o[1] = -(gb * o[2]);
        break;
    case P_LEFT:
        t = (gb * prev[2] + prev[0]) / ((prev[0] - cur[0]) + (prev[2] - cur[2]) * gb);
        z = (cur[2] - prev[2]) * t + prev[2];
        o[2] = z;
        o[3] = 1.0f / z;
        o[0] = -(gb * o[2]);
        o[1] = (cur[1] - prev[1]) * t + prev[1];
        break;
    default:
        t = (gb * prev[2] - prev[0]) / ((cur[0] - prev[0]) - (cur[2] - prev[2]) * gb);
        z = (cur[2] - prev[2]) * t + prev[2];
        o[2] = z;
        o[3] = 1.0f / z;
        o[0] = gb * o[2];
        o[1] = (cur[1] - prev[1]) * t + prev[1];
        break;
    }
    if (d) o[4] = ufloat(lerp_colour(ubits(prev[4]), ubits(cur[4]), t));
    if (s) o[5] = ufloat(lerp_colour(ubits(prev[5]), ubits(cur[5]), t));
    if (uv) {
        o[6] = (cur[6] - prev[6]) * t + prev[6];
        o[7] = (cur[7] - prev[7]) * t + prev[7];
    }
    if (uv2) {
        o[8] = (cur[8] - prev[8]) * t + prev[8];
        o[9] = (cur[9] - prev[9]) * t + prev[9];
    }
}

/* One Sutherland-Hodgman pass; prev starts at the last vertex. */
static int clip_plane(int p, const float *in, int n, float *out, float gb, float zn, float zf, int d, int s, int uv, int uv2)
{
    const float *prev = in + (n - 1) * VSTRIDE;
    int m = 0;
    for (int i = 0; i < n; i++, in += VSTRIDE) {
        const float *cur = in;
        float cl, cr, pl, pr;
        plane_sides(p, cur, gb, zn, zf, &cl, &cr);
        plane_sides(p, prev, gb, zn, zf, &pl, &pr);
        if (cl <= cr) {                        /* outside, or on the plane */
            if (cr <= cl) {
                memcpy(out + m * VSTRIDE, cur, VSTRIDE * sizeof *out);
                m++;
            } else if (pr < pl) {              /* leaving: the crossing */
                intersect(p, cur, prev, out + m * VSTRIDE, gb, zn, zf, d, s, uv, uv2);
                m++;
            }
        } else {
            if (pl < pr) {                     /* entering: the crossing, then the vertex */
                intersect(p, cur, prev, out + m * VSTRIDE, gb, zn, zf, d, s, uv, uv2);
                m++;
            }
            memcpy(out + m * VSTRIDE, cur, VSTRIDE * sizeof *out);
            m++;
        }
        prev = cur;
    }
    return m;
}

void EAGL_ClipPerspective(int i0, int i1, int i2, uint32_t codes, float gb, const float *verts,
                          int d, int s, int uv, int uv2, float zn, float zf, float **out, int *nout)
{
    memcpy(BUF_A, verts + i0 * VSTRIDE, VSTRIDE * sizeof(float));
    memcpy(BUF_A + VSTRIDE, verts + i1 * VSTRIDE, VSTRIDE * sizeof(float));
    memcpy(BUF_A + 2 * VSTRIDE, verts + i2 * VSTRIDE, VSTRIDE * sizeof(float));
    float *cur = BUF_A, *other = BUF_B;
    int n = 3;
    for (int p = 0; p < 6; p++) {
        if (!(codes >> p & 1) || n == 0) continue;
        n = clip_plane(p, cur, n, other, gb, zn, zf, d, s, uv, uv2);
        float *t = cur;
        cur = other;
        other = t;
    }
    *out = cur;
    *nout = n;
}

/* ---- the depth sort (0x5490e0) with MSVC 6's std::sort (0x553630..0x553ca0) ---- */

typedef struct { float key; uint16_t i[3]; uint16_t pad; } TriKey;   /* 12 bytes, the original's record */

static void unguarded_insert(TriKey *last, TriKey val)                     /* 0x553b10 */
{
    for (TriKey *next = last - 1; val.key < next->key; next--) {
        *last = *next;
        last = next;
    }
    *last = val;
}

static void insertion_sort(TriKey *first, TriKey *last)                    /* 0x553860 */
{
    if (first == last) return;
    for (TriKey *i = first + 1; i != last; i++) {
        TriKey val = *i;
        if (first->key <= val.key) {
            unguarded_insert(i, val);
        } else {
            memmove(first + 1, first, (size_t)(i - first) * sizeof *first);
            *first = val;
        }
    }
}

static void unguarded_insertion_sort(TriKey *first, TriKey *last)          /* 0x553a40 */
{
    for (; first != last; first++) unguarded_insert(first, *first);
}

static TriKey *median(TriKey *a, TriKey *b, TriKey *c)                     /* 0x553770 */
{
    if (b->key <= a->key) {
        if (a->key < c->key) return a;
        if (b->key < c->key) return c;
    } else if (c->key <= b->key) {
        if (c->key <= a->key) return a;
        return c;
    }
    return b;
}

static TriKey *partition(TriKey *first, TriKey *last, float pivot)         /* 0x5537d0 */
{
    for (;;) {
        while (first->key < pivot) first++;
        do last--;
        while (pivot < last->key);
        if (last <= first) return first;
        TriKey t = *first;
        *first = *last;
        *last = t;
        first++;
    }
}

static void push_heap(TriKey *first, int hole, int top, TriKey val)        /* 0x553ca0 */
{
    while (top < hole) {
        int parent = (hole - 1) / 2;
        if (val.key <= first[parent].key) break;
        first[hole] = first[parent];
        hole = parent;
    }
    first[hole] = val;
}

static void adjust_heap(TriKey *first, int hole, int len, TriKey val)      /* 0x553bf0 */
{
    int top = hole, child2 = hole * 2, child = child2 + 2;
    while (child < len) {
        if (first[child].key < first[child - 1].key) child = child2 + 1;
        first[hole] = first[child];
        hole = child;
        child2 = hole * 2;
        child = child2 + 2;
    }
    if (child == len) {
        first[hole] = first[child - 1];
        hole = child2 + 1;
    }
    push_heap(first, hole, top, val);
}

static void pop_heap(TriKey *first, TriKey *last, TriKey *result, TriKey val)   /* 0x553aa0 */
{
    *result = *first;
    adjust_heap(first, 0, (int)(last - first), val);
}

static void make_heap(TriKey *first, TriKey *last)                         /* 0x553b60 */
{
    int n = (int)(last - first);
    if (n < 2) return;
    for (int i = (n - 2) / 2;; i--) {
        adjust_heap(first, i, n, first[i]);
        if (i == 0) break;
    }
}

static void sort_heap(TriKey *first, TriKey *last)
{
    while (last - first > 1) {
        last--;
        pop_heap(first, last, last, *last);
    }
}

static void partial_sort(TriKey *first, TriKey *mid, TriKey *last)         /* 0x553940 */
{
    make_heap(first, mid);
    for (TriKey *p = mid; p < last; p++)
        if (p->key < first->key) pop_heap(first, mid, p, *p);
    sort_heap(first, mid);
}

static void intro_loop(TriKey *first, TriKey *last, int ideal)            /* 0x553630 */
{
    if (last - first <= 16) return;
    while (ideal) {
        ideal--;
        TriKey pivot = *median(first, first + (last - first) / 2, last - 1);
        TriKey *cut = partition(first, last, pivot.key);
        intro_loop(cut, last, ideal);
        last = cut;
        if (last - first < 17) return;
    }
    make_heap(first, last);
    sort_heap(first, last);
}

static TriKey sortbuf[0x5e8 / 3];   /* 0x69ed18 */

void EAGL_SortTriangles(EaglContext *c)
{
    int stride = c->twouv ? 0x28 : 0x20;
    int ntri = c->out_nindices / 3;
    /* The index list is the last indexed draw's when the vertices were drawn unindexed (every vertex
       inside): the original sorts it anyway, reading wherever its indices point. The port bounds the
       reads to the work area (such a sort has no effect on the picture, the draw has no indices). */
    for (int t = 0; t < ntri; t++) {
        TriKey *k = &sortbuf[t];
        k->key = 1e9f;
        for (int j = 0; j < 3; j++) {
            uint16_t i = c->indices[3 * t + j];
            k->i[j] = i;
            size_t off = (size_t)i * (size_t)stride + 8;
            float z = off + sizeof(float) <= sizeof c->verts ? *(const float *)(c->verts + off) : 0.0f;
            if (!(k->key <= z)) k->key = z;
        }
    }
    if (ntri) {
        TriKey *first = sortbuf, *last = sortbuf + ntri;
        int ideal = 0;
        for (int k = ntri; k != 1; k >>= 1) ideal++;
        ideal *= 2;
        TriKey *end = last;
        while (last - first > 16) {
            if (ideal == 0) {
                partial_sort(first, last, last);
                break;
            }
            ideal--;
            TriKey pivot = *median(first, first + (last - first) / 2, last - 1);
            TriKey *cut = partition(first, last, pivot.key);
            intro_loop(cut, last, ideal);
            last = cut;
        }
        if (ntri < 17) insertion_sort(first, end);
        else {
            insertion_sort(first, first + 16);
            unguarded_insertion_sort(first + 16, end);
        }
    }
    for (int t = 0; t < ntri; t++)
        for (int j = 0; j < 3; j++) c->indices[3 * t + j] = sortbuf[t].i[j];
    c->sort = 0;
}
