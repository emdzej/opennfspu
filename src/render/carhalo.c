/* The cars' light halos: see carhalo.h. */
#include "render/carhalo.h"
#include "exe.h"
#include "render/r3dcar.h"
#include "render/render.h"
#include "render/rtex.h"
#include "render/sky.h"
#include "render/sprite.h"
#include "render/thrash.h"
#include <math.h>
#include <string.h>

static float f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

/* (the bits halved and 0x1fc00000 added: a rough square root, as the original takes them) */
static float rough_sqrt(float v)
{
    uint32_t u;
    memcpy(&u, &v, 4);
    u = (uint32_t)((int32_t)u >> 1) + 0x1fc00000u;
    memcpy(&v, &u, 4);
    return v;
}

static void xform(const EaglMatrix *m, const float in[4], float out[4])   /* 0x53a0a0 */
{
    for (int j = 0; j < 4; j++)
        out[j] = in[0] * m->m[j] + in[1] * m->m[4 + j] + in[2] * m->m[8 + j] + in[3] * m->m[12 + j];
}

static double turns(double y, double x)   /* 0x5325d0: atan2 in turns (0x5bb2c8, 1 / 2 pi) */
{
    uint64_t u = (uint64_t)exe_u32(0x5bb2c8 + 4) << 32 | exe_u32(0x5bb2c8);
    double k;
    memcpy(&k, &u, 8);
    return atan2(y, x) * k;
}

/* The quad drawer (0x59ee20: coordinates, colours, uv, texture, render state, the draw) and its record
   (0x537760: 1, 2, 0, 0, 0, -1), here additive (0x5377c0(1)). */
static EaglMaterial s_mat;
static bool s_ready;
static int32_t s_rs[6];
static float s_pos[4][4];
static uint32_t s_col[4];
static const float s_uv[8] = {0, 0, 1, 0, 1, 1, 0, 1};

static void drawer_init(void)
{
    if (!s_ready) {
        EAGL_MaterialInit(&s_mat);
        EAGL_MaterialAddLayer(&s_mat, EAGL_CreateLayer(1, 7, 0));
        EAGL_MaterialAddLayer(&s_mat, EAGL_CreateLayer(6, 0, 0));
        EAGL_MaterialAddLayer(&s_mat, EAGL_CreateLayer(3, 0, 0));
        EAGL_MaterialAddLayer(&s_mat, EAGL_CreateLayer(4, 0, 0));
        EAGL_MaterialAddLayer(&s_mat, EAGL_CreateLayer(8, 0, 0));
        EAGL_MaterialAddLayer(&s_mat, EAGL_CreateLayer(0, 0, 0));
        EAGL_MaterialBind(&s_mat);
        s_ready = true;
    }
    s_rs[0] = 1;
    s_rs[1] = 2;
    s_rs[2] = s_rs[3] = s_rs[4] = 0;
    s_rs[5] = -1;
    s_rs[2] = 1;   /* 0x5377c0: additive */
}

static void drawer_draw(Texture *t)   /* 0x59ef80(4, 4) */
{
    EaglStream st[5] = {
        {4 * 16, s_pos}, {4 * 4, s_col}, {4 * 8, (void *)s_uv}, {0x54, t}, {0x18, s_rs},
    };
    EAGL_MaterialDraw(&s_mat, st, 4, 4);
}

/* The light's centre, its first axis's tip and its normal's tip (the cross of the second axis and the
   first) through m; it faces the eye by the normal against the camera's back row (types with the table's
   flag only within about 18 degrees: (f - 0.95) * 20); under 0.1 of its level none. The quad: the unit
   square turned by the centre's direction on screen less the second axis's, scaled across by the first
   axis's length less the second axis's screen length times its depth times (|second| / |first| - 1),
   up by the first axis's length, both by the intensity, turned back by the axis's direction; laid along
   the camera's right and up rows. Each texture of the type (0x5cd740: CARG and SPIK, GLNT) in its
   colour at the intensity's alpha, pulled towards the eye by a depth bias; then in a race the road glow
   by the sky's glow level, its alpha the intensity squared. */
void R3DCar_LightHalo(const float *light, float ground, const EaglMatrix *m, float level)
{
    if (!g_render || !g_render->camera) return;
    RCamera *cam = g_render->camera;
    drawer_init();
    const float *a1 = light + 4, *a2 = light + 8;
    float len1 = rough_sqrt(a1[2] * a1[2] + a1[1] * a1[1] + a1[0] * a1[0]);
    float len2 = rough_sqrt(a2[2] * a2[2] + a2[1] * a2[1] + a2[0] * a2[0]);
    float stretch = len2 / len1 - 1.0f;
    float c[4], tip2[4], tipn[4], q[4];
    xform(m, light, c);
    q[0] = light[0] + a2[0], q[1] = light[1] + a2[1], q[2] = light[2] + a2[2], q[3] = light[3];
    xform(m, q, tip2);
    float n[3] = {a2[1] * a1[2] - a2[2] * a1[1], a2[2] * a1[0] - a2[0] * a1[2], a2[0] * a1[1] - a2[1] * a1[0]};   /* 0x5325f0 */
    q[0] = n[0] + light[0], q[1] = n[1] + light[1], q[2] = n[2] + light[2], q[3] = light[3];
    xform(m, q, tipn);
    const float *eye = cam->frame.eye;
    float d[3] = {c[0] - eye[0], c[1] - eye[1], c[2] - eye[2]};
    float nv[3] = {tipn[0] - c[0], tipn[1] - c[1], tipn[2] - c[2]};
    float nl = sqrtf(nv[0] * nv[0] + nv[1] * nv[1] + nv[2] * nv[2]);   /* 0x532640 */
    if (nl > 0) nv[0] /= nl, nv[1] /= nl, nv[2] /= nl;
    const float *back = cam->frame.orient + 6;
    float f = -(back[0] * nv[0] + back[1] * nv[1] + back[2] * nv[2]);   /* 0x532680 */
    if (1.0f < f) f = 1.0f;
    int type = ((const uint8_t *)light)[0x4c];
    if (exe_data(0x5cd73c + 0x18u * (unsigned)type, 1)[0]) f = (f - f32(0x5b2610)) * f32(0x5b2470);   /* 0.95, 20 */
    float in = f * level;
    if (!(f32(0x5b2604) < in)) return;   /* 0.1 */
    const EaglMatrix *view = EAGL_StackTop(&cam->view->view);   /* 0x539ca0 */
    float vc[4], v2[4];
    xform(view, c, vc);
    xform(view, tip2, v2);
    float dx = v2[0] / v2[2] - vc[0] / vc[2];
    float dy = v2[1] / v2[2] - vc[1] / vc[2];
    float ac = (float)turns(vc[1], vc[0]), aa = (float)turns(dy, dx);
    float slen = -(rough_sqrt(dy * dy + dx * dx) * vc[2]);
    EaglMatrix mt, ms;
    EAGL_MatAxisAngle(&mt, (ac - aa) * f32(0x5b25cc), 0, 0, 1.0f);   /* 360 */
    EAGL_MatScale(&ms, (slen * stretch + len1) * in, in * len1, 0, 1.0f);
    EAGL_MatPostMul(&mt, &ms);
    EAGL_MatAxisAngle(&ms, aa * f32(0x5b25cc), 0, 0, 1.0f);
    EAGL_MatPostMul(&mt, &ms);
    static const float unit[4][4] = {{-1, -1, 0, 1}, {1, -1, 0, 1}, {1, 1, 0, 1}, {-1, 1, 0, 1}};
    float off[4][4];
    EAGL_MatTransform4(&mt, 4, &unit[0][0], 16, &off[0][0], 16);   /* 0x539e60 */
    const float *right = cam->frame.orient, *up = cam->frame.orient + 3;
    for (int i = 0; i < 4; i++) {
        s_pos[i][0] = up[0] * off[i][1] + right[0] * off[i][0] + c[0];
        s_pos[i][1] = up[1] * off[i][1] + right[1] * off[i][0] + c[1];
        s_pos[i][2] = up[2] * off[i][1] + right[2] * off[i][0] + c[2];
        s_pos[i][3] = c[3];
    }
    float d2 = d[2] * d[2] + d[1] * d[1] + d[0] * d[0];
    uint32_t alpha = (uint32_t)((int32_t)(in * 255.0f) & 0xff) << 24;
    const uint32_t *colours = (const uint32_t *)(light + 0x10);
    for (int i = 0; i < 2; i++) {
        uint32_t col = (colours[i] & 0xffffffu) | alpha;
        s_col[0] = s_col[1] = s_col[2] = s_col[3] = col;
        uint32_t name = exe_u32(0x5cd740 + 0xcu * (unsigned)(i + type * 2));
        Texture *t = g_rtex_common[0] ? RTexBank_find(g_rtex_common[0], 0, name) : NULL;   /* 0x44d1f0 */
        if (!t) continue;
        s_rs[4] = (int32_t)lrintf(in * f32(0x5b36ec) * g_render->depthfactor / d2);   /* -20000; 0x5377d0 */
        drawer_draw(t);
    }
    if (g_r3d_frontend) return;
    float g = Sky_GlowLevel();   /* 0x45dc40, 0x45cec0 */
    if (f32(0x5b2604) < g * in) Sprite_DrawCarGlow(c, ground, len1, colours[0], g * in * in);   /* 0x451c30 */
}

/* The fan's ring (0x5cd918: nine pairs, the last the first again), the near ring's scale on the axes is 1
   and the far one's 5 (0x5b2400), the forward push 8 (0x5b2608); the drawer's uv a zeroed buffer
   (0x6af340), its record 0x537760 with 0x5377c0(0) (blended, not additive) and the filter states 2, 1. */
void R3DCar_LightBeam(const float *light, const EaglMatrix *m1, const EaglMatrix *m2)
{
    static float pos[18][4];
    static uint32_t col[18];
    static float uv[18 * 2];
    int32_t rs[6] = {1, 2, 0, 0, 0, -1};   /* 0x5377a0(1), 0x5377b0(2) as the record has them */
    drawer_init();
    THRASH.fn.setstate(0x24, 0);
    float p[4], a[4], b[4], fwd[4];
    xform(m1, light, p);
    xform(m1, light + 8, a);
    xform(m1, light + 4, b);
    static const float back[4] = {0, 0, -1.0f, 0};
    xform(m2, back, fwd);
    float k = f32(0x5b2608), w = f32(0x5b2400);
    for (int j = 0; j < 3; j++) fwd[j] *= k;
    uint32_t colour = ((const uint32_t *)light)[0x10] & 0xffffffu;
    for (int i = 0; i < 9; i++) {
        float cx = f32(0x5cd918 + 8u * (unsigned)i), cy = f32(0x5cd91c + 8u * (unsigned)i);
        for (int j = 0; j < 3; j++) {
            pos[2 * i][j] = a[j] * cx + b[j] * cy + p[j];
            pos[2 * i + 1][j] = a[j] * w * cx + b[j] * w * cy + p[j] + fwd[j];
        }
        pos[2 * i][3] = pos[2 * i + 1][3] = p[3];
        col[2 * i] = colour | 0x10000000u;
        col[2 * i + 1] = colour;
    }
    EaglStream st[5] = {{18 * 16, pos}, {18 * 4, col}, {18 * 8, uv}, {0x54, NULL}, {0x18, rs}};
    EAGL_MaterialDraw(&s_mat, st, 1, 18);
    THRASH.fn.setstate(0x24, 10);
}
