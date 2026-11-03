/* EAGL views (0x538a80..0x539470): the camera and object matrix stacks, projection, viewport, the
   per-frame scene begin / end, and the sphere test the renderers cull with. */
#include "eagl/eagl.h"
#include "real/set3d.h"
#include <math.h>
#include <string.h>

/* Constants of the original (algorithmic): degrees to half-angle radians, pi/2, the half-pixel nudge. */
#define DEG_TO_HALF_RAD 0.008726639673113823f   /* 0x5bb890, 0x5bb894 */
#define HALF_PI 1.5707950592041016f              /* 0x5bb88c */
#define PIXEL_NUDGE 0.009999999776482582f        /* 0x5b27a8 */

static float display_w = 640.0f, display_h = 480.0f;   /* the device's answer to slot 6 of *0x69ed0c */

void EAGL_SetDisplaySize(float w, float h)
{
    display_w = w;
    display_h = h;
}

/* (a port addition, render.c's --param aspect=16:9: the display's width to height over 4:3's. The
   original's projections take a 640 x 480 screen; wider, the vertical field stays and the horizontal one
   widens by this, Hor+. 1 leaves them as they were.) */
static float wide = 1.0f;

void EAGL_SetWide(float w) { wide = w; }
float EAGL_Wide(void) { return wide; }

/* The device that owns a view keeps the view it is drawing (+0x14 of the original's object). */
typedef struct { uint32_t pad[5]; EaglView *current; } EaglOwner;

EaglView *EAGL_ViewInit(EaglView *v, void *owner)
{
    memset(v, 0, sizeof *v);
    EAGL_StackInit(&v->view, 0x28);
    EAGL_StackInit(&v->model, 0x28);
    v->background = 0x3f;
    EAGL_MatIdentity(&v->combined);
    EAGL_MatIdentity(&v->proj);
    EAGL_MatIdentity(&v->modelview);
    v->proj_serial = 0;
    v->proj_seen = -1;
    v->owner = owner;
    if (owner) ((EaglOwner *)owner)->current = v;
    return v;
}

void EAGL_ViewFree(EaglView *v)
{
    EAGL_StackFree(&v->model);
    EAGL_StackFree(&v->view);
}

void EAGL_ViewSetViewport(EaglView *v, float w, float h, float x, float y, float minz, float maxz)
{
    /* The rectangle is clamped to the display, then its centre and half sizes kept; an off-centre or
       clipped rectangle also gets the projection scale/offset that keeps the picture where the
       unclipped one would have put it. */
    float sw = display_w, sh = display_h;
    float x0 = x, y0 = y, x1 = w + x, y1 = h + y;
    if (x0 < 0.0f) x0 = 0.0f;
    if (y0 < 0.0f) y0 = 0.0f;
    if (x1 < 0.0f) x1 = 0.0f;
    if (y1 < 0.0f) y1 = 0.0f;
    if (x0 > sw) x0 = sw;
    if (y0 > sh) y0 = sh;
    if (x1 > sw) x1 = sw;
    if (y1 > sh) y1 = sh;
    float cw = x1 - x0, ch = y1 - y0;
    if (cw * ch == 0.0f) {
        v->vp_minz = minz;
        v->vp_w = v->vp_h = 0.0f;
        v->vp_sx = v->vp_sy = 0.0f;
        v->vp_x = v->vp_y = 0.0f;
        v->vp_maxz = maxz;
        v->vp_cx = v->vp_cy = 0.0f;
        v->ortho_sy = v->ortho_sx = v->proj_sy = v->proj_sx = 1.0f;
    } else {
        v->vp_w = cw;
        float hx = cw * 0.5f;
        v->vp_h = ch;
        v->vp_minz = minz;
        v->vp_sx = hx;
        float hy = ch * 0.5f;
        v->vp_x = x0;
        v->vp_y = y0;
        v->vp_maxz = maxz;
        v->vp_sy = hy;
        hx = hx + x0;
        v->vp_cx = hx;
        hy = hy + y0;
        v->vp_cy = hy;
        if (cw != w || ch != h) {
            float dx = (w * 0.5f + x) - hx;
            float dy = (h * 0.5f + y) - hy;
            v->ortho_sx = v->proj_sx = w / cw;
            v->ortho_sy = v->proj_sy = h / ch;
            v->ortho_ox = v->proj_ox = (dx + dx) / cw;
            v->ortho_oy = v->proj_oy = (dy + dy) / ch;
            return;
        }
        v->ortho_sy = v->ortho_sx = v->proj_sy = v->proj_sx = 1.0f;
    }
    v->ortho_oy = v->ortho_ox = v->proj_oy = v->proj_ox = 0.0f;
}

void EAGL_ViewGetViewport(const EaglView *v, float *w, float *h, float *x, float *y, float *minz, float *maxz)
{
    *w = v->vp_w;
    *h = v->vp_h;
    *x = v->vp_x;
    *y = v->vp_y;
    *minz = v->vp_minz;
    *maxz = v->vp_maxz;
}

/* x87 transcendental results in 24-bit precision: the correctly rounded float of the double result. */
static float ftan(float a) { return (float)tan((double)a); }
static float fsin(float a) { return (float)sin((double)a); }

void EAGL_ViewSetPerspective(EaglView *v, float fov, float aspect, float znear, float zfar)
{
    EaglMatrix *p = &v->proj;
    EAGL_MatIdentity(p);
    float t = ftan(fov * DEG_TO_HALF_RAD);
    float cot = 1.0f / t;
    p->m[0] = cot * v->proj_sx;
    if (wide != 1.0f) p->m[0] /= wide;
    p->m[5] = -(cot * v->proj_sy * aspect);
    p->m[8] = -v->proj_ox;
    p->m[9] = -v->proj_oy;
    p->m[10] = -1.0f;
    p->m[15] = 1.0f;
    v->znear = znear;
    float zs = zfar / (zfar - znear);
    v->aspect = aspect;
    v->zfar = zfar;
    v->fov = fov;
    v->projection = 0;
    v->proj_serial++;
    v->zscale = zs;
    v->zoffset = -(zs * znear);
    float half = fov * DEG_TO_HALF_RAD;
    v->cull_tanx = ftan(half);
    v->cull_cosx = fsin(HALF_PI - half);
    if (wide != 1.0f) {   /* (the wider horizontal half angle; the vertical tests stay) */
        float halfx = atanf(v->cull_tanx * wide);
        v->cull_tanx = tanf(halfx);
        v->cull_cosx = cosf(halfx);
    }
    float halfy = half * aspect;
    v->cull_tany = ftan(halfy);
    v->cull_cosy = fsin(HALF_PI - halfy);
}

void EAGL_ViewSetOrtho(EaglView *v, float height, float znear, float zfar)
{
    EaglMatrix *p = &v->proj;
    v->projection = 1;
    EAGL_MatIdentity(p);
    p->m[0] = v->ortho_sx;
    p->m[5] = -(height * v->ortho_sy);
    float zs = -1.0f / (zfar - znear);
    p->m[12] = v->ortho_ox;
    p->m[13] = v->ortho_oy;
    p->m[10] = zs;
    p->m[14] = -(znear / (zfar - znear));
    v->ortho_height = height;
    v->ortho_near = znear;
    v->ortho_far = zfar;
    v->ortho_zscale = 1.0f;
    v->ortho_zoffset = 0.0f;
    v->proj_serial++;
}

/* __ftol: truncation toward zero. */
static int ftol(float f) { return (int)f; }

void EAGL_ViewBegin(EaglView *v)
{
    if (v->owner) ((EaglOwner *)v->owner)->current = v;
    g_eagl->view = v;
    if (v->projection == 0) EAGL_ViewSetPerspective(v, v->fov, v->aspect, v->znear, v->zfar);
    else if (v->projection == 1) EAGL_ViewSetOrtho(v, v->ortho_height, v->ortho_near, v->ortho_far);
    THRASH.fn.clip(ftol(v->vp_x), ftol(v->vp_y), ftol(v->vp_w + v->vp_x), ftol(v->vp_h + v->vp_y));
}

void EAGL_ViewEnd(EaglView *v)
{
    THRASH.fn.flushwindow();
    if (v->owner) ((EaglOwner *)v->owner)->current = NULL;
    g_eagl->view = NULL;
}

void EAGL_ViewClear(EaglView *v)
{
    THRASH.fn.setstate(3, v->background);
    THRASH.fn.clearwindow();
}

/* Recombines model * view (when either stack changed) and that times the projection (when it or the
   projection changed). */
void EAGL_ViewUpdate(EaglView *v)
{
    bool model_changed = v->model.seen != v->model.serial;
    if (model_changed) v->model.seen = v->model.serial;
    bool view_changed = v->view.seen != v->view.serial;
    if (view_changed) v->view.seen = v->view.serial;
    if (model_changed + view_changed > 0) {
        EAGL_MatMulInto(EAGL_StackTop(&v->model), EAGL_StackTop(&v->view), &v->modelview);
        EAGL_MatMulInto(&v->modelview, &v->proj, &v->combined);
        if (v->proj_serial != v->proj_seen) v->proj_seen = v->proj_serial;
    } else if (v->proj_serial != v->proj_seen) {
        v->proj_seen = v->proj_serial;
        EAGL_MatMulInto(&v->modelview, &v->proj, &v->combined);
    }
}

void EAGL_ViewProject(EaglView *v, int n, const void *src, float *dst)
{
    EAGL_ViewUpdate(v);
    EAGL_SetTransformMatrix(&v->combined);
    EAGL_Transform(0x20b, n, src, dst);
    for (float *p = dst; n; n--, p += 4) {
        float rhw = p[3];
        p[0] = rhw * p[0];
        p[1] = rhw * p[1];
        p[0] = p[0] * v->vp_sx + v->vp_cx + PIXEL_NUDGE;
        p[1] = p[1] * v->vp_sy + v->vp_cy + PIXEL_NUDGE;
        if (v->projection == 0) p[2] = (v->zscale * p[2] + v->zoffset) * rhw;
        else p[2] = v->ortho_zscale * p[2] + v->ortho_zoffset;
        p[3] = 1.0f;
    }
}

/* Squared distance from a point to a box against r^2 (0x539390): false when the sphere is outside. */
static bool box_sphere(const float *p, float r, float x0, float x1, float y0, float y1, float z0, float z1)
{
    float r2 = r * r, acc, d;
    if (z0 <= p[2]) {
        acc = 0.0f;
        if (z1 < p[2]) {
            acc = p[2] - z1;
            goto zsq;
        }
    } else {
        acc = p[2] - z0;
zsq:
        acc = acc * acc;
        if (acc > r2) return false;
    }
    if (x0 <= p[0]) {
        if (x1 < p[0]) {
            d = p[0] - x1;
            goto xsq;
        }
    } else {
        d = p[0] - x0;
xsq:
        acc = d * d + acc;
        if (acc > r2) return false;
    }
    if (y0 <= p[1]) {
        if (p[1] <= y1) return true;
        d = p[1] - y1;
    } else {
        d = p[1] - y0;
    }
    acc = d * d + acc;
    return !(acc > r2);
}

/* The view-space sphere test of the renderers' culling (0x5391f0): the centre goes through the camera
   stack's top; perspective views test near/far (vp_minz / vp_maxz) and the four side planes, ortho views
   the box of the viewport. */
bool EAGL_ViewSphereVisible(EaglView *v, const float *centre, float r)
{
    float p[3];
    const float *m = EAGL_StackTop(&v->view)->m;
    p[0] = centre[0] * m[0] + m[4] * centre[1] + m[8] * centre[2] + m[12];
    p[1] = m[5] * centre[1] + m[9] * centre[2] + m[1] * centre[0] + m[13];
    p[2] = m[6] * centre[1] + m[10] * centre[2] + m[2] * centre[0] + m[14];
    if (v->projection == 1)
        return box_sphere(p, r, -v->vp_x, v->vp_x, -v->vp_y, v->vp_y, v->vp_minz, v->vp_maxz);
    if (v->projection != 0) return true;
    float d = p[2] + v->vp_minz;
    if (d <= 0.0f) {
        d = -(p[2] + v->vp_maxz);
        if (d > r) return false;
    } else if (d > r) {
        return false;
    }
    float t = p[2] * v->cull_tanx;
    d = t + p[0];
    if (d <= 0.0f) {
        float e = p[0] - t;
        if (e < 0.0f && -(e * v->cull_cosx) > r) return false;
    } else if (d * v->cull_cosx > r) {
        return false;
    }
    t = p[2] * v->cull_tany;
    d = t + p[1];
    if (d <= 0.0f) {
        float e = p[1] - t;
        if (!(e < 0.0f)) return true;
        d = -(e * v->cull_cosy);
    } else {
        d = d * v->cull_cosy;
    }
    return !(d > r);
}
