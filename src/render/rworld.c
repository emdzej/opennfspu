/* The world draw of a view (Render_World 0x468a20): the world's visible objects into the render lists
   and the lists drawn. */
#include "eagl/eagl.h"
#include "game/track.h"
#include "game/sim_math.h"
#include "game/wgrid.h"
#include "game/smack.h"
#include "game/wtree.h"
#include "game/world.h"
#include "exe.h"
#include "render/headlights.h"
#include "render/rdraw.h"
#include "render/render.h"
#include "render/rworld.h"
#include "render/rlists.h"
#include "render/sky.h"
#include "render/sprite.h"
#include "render/particles.h"
#include "render/carparticle.h"
#include "render/weather.h"
#include "render/r3dcar.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* RenderState setters (0x5375e0: +0x0c and state 0x39; 0x5375a0: +0x04 and state 0x28). */
static void rs_set39(RenderState *rs, int v)
{
    rs->state39 = v;
    EAGL_RSSet(0x39, (uint32_t)v);
}

static void rs_set28(RenderState *rs, int v)
{
    rs->state28 = v;   /* +0x04 */
    EAGL_RSSet(0x28, (uint32_t)v);
}

static float f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static double f64(uint32_t va)
{
    uint64_t u = (uint64_t)exe_u32(va + 4) << 32 | exe_u32(va);
    double d;
    memcpy(&d, &u, 8);
    return d;
}

/* The visibility state (0x47f560's object): the eye, the view's ground area (the hull of the eye and the
   far corners of the view, x and z) with the circle about it, the camera's rotation and distance, the
   nodes found, and the two side planes through the eye (none when the view is steep). */
typedef struct {
    float eye[3];               /* +0x00 */
    float trans[3];             /* +0x0c */
    float area[7][4];           /* +0x18: (five in the original) */
    uint32_t narea;             /* +0x68 */
    float cx, cz, r2;           /* +0x6c: the area's centre and the circle's radius squared */
    float rot[9];               /* +0x78 */
    float dist, dist2;          /* +0x9c, +0xa0 */
    WGridNode **nodes;          /* +0xa4: the nodes found (a vector) */
    uint32_t nnodes, cap;
    uint8_t steep;              /* +0xa8 */
    float planes[2][4];         /* +0xac: x and z of each normal (outwards) */
} WorldVis;

static WorldVis s_vis;
static uint32_t s_passes;       /* 0x628c4c */

/* 0x47f220: the eye and the view's far corners at the distance (x by the tangent of the half field of
   view, y by 0.75 of that, z the distance), to the world from the eye, flattened (x, 0, z, 0). The
   original turns them by its view rotation transposed, its view space's z the front; the port's camera
   frame has the front as its third row before RFrame_ToView turns it, so they go by the frame's rows. */
static void view_area(WorldVis *v, const float o[9], float halffov, float dist, float aspect, float out[5][4])
{
    double turns = (double)(halffov * f32(0x5b3e84)), tp = f64(0x5bb2c0);   /* (2 pi) */
    float sn = (float)sin(turns * tp), cs = (float)cos(turns * tp);
    float x = -((sn / cs) * dist), y = (sn / cs) * dist * aspect;
    const float cam[5][3] = {{0, 0, 0}, {x, y, dist}, {-x, y, dist}, {-x, -y, dist}, {x, -y, dist}};
    for (int k = 0; k < 5; k++) {
        float w[3];
        for (int j = 0; j < 3; j++)   /* (0x5327f0: the eye added) */
            w[j] = cam[k][0] * o[j] + cam[k][1] * o[3 + j] + cam[k][2] * o[6 + j] + v->eye[j];
        out[k][0] = w[0];
        out[k][1] = 0.0f;
        out[k][2] = w[2];
        out[k][3] = 0.0f;
    }
}

/* 0x47f560: from the camera's rotation, translation and view distance. */
static void vis_begin(WorldVis *v, const RCamera *c)
{
    /* The original's frame has been made the view's in place by now (0x4661a0); the port's camera makes
       it in a copy (RCamera_Setup), so it is made again here for the eye. */
    RFrame fr = c->frame;
    RFrame_ToView(&fr);
    const float *rot = fr.rot, *trans = fr.trans;
    for (int j = 0; j < 3; j++)   /* (the translation by the transpose, negated: the eye) */
        v->eye[j] = -(trans[0] * rot[j * 3] + trans[1] * rot[j * 3 + 1] + trans[2] * rot[j * 3 + 2]);
    memcpy(v->trans, trans, sizeof v->trans);
    memcpy(v->rot, rot, sizeof v->rot);
    v->dist2 = c->viewdist * c->viewdist;
    v->dist = c->viewdist;
    float pts[5][4];
    view_area(v, c->frame.orient, c->halffov, c->viewdist, 0.75f, pts);
    v->narea = (uint32_t)WTree_Hull(5, &pts[0][0], &v->area[0][0]);   /* (room for 7) */
    float mnx = 1e9f, mnz = 1e9f, mxx = -1e9f, mxz = f32(0x5b4688);
    for (uint32_t i = 0; i < v->narea; i++) {
        if (v->area[i][0] < mnx) mnx = v->area[i][0];
        if (mxx < v->area[i][0]) mxx = v->area[i][0];
        if (v->area[i][2] < mnz) mnz = v->area[i][2];
        if (mxz < v->area[i][2]) mxz = v->area[i][2];
    }
    v->r2 = 0.0f;
    v->cx = (mxx + mnx) * f32(0x5b23f0);
    v->cz = (mxz + mnz) * f32(0x5b23f0);
    for (uint32_t i = 0; i < v->narea; i++) {
        float dx = fabsf(v->area[i][0] - v->cx), dz = fabsf(v->area[i][2] - v->cz);
        float d = dx * dx + dz * dz;
        if (v->r2 < d) v->r2 = d;
    }
    /* The view's front (+0x100; the port's frame's third row, as above): steep (more than 0.8 up or down, 0x5b37b8)
       gives no side planes and a 50 m reach; else the level front turned by the half field of view
       either way, a side's normal square to it. */
    float f[3] = {c->frame.orient[6], c->frame.orient[7], c->frame.orient[8]};
    if (!((double)fabsf(f[1]) <= f64(0x5b37b8))) {
        v->steep = 1;
        v->dist = 50.0f;
    } else {
        v->steep = 0;
        f[1] = 0.0f;
        Vec3_Unit(f, f);
        float in[4] = {f[0], f[1], f[2], 1.0f}, o[4];
        EaglMatrix m;
        EAGL_MatAxisAngle(&m, c->halffov, 0.0f, 1.0f, 0.0f);
        EAGL_MatTransform4(&m, 1, in, 16, o, 16);   /* 0x53a0a0 */
        v->planes[1][0] = o[2];
        v->planes[1][1] = 0.0f;
        v->planes[1][2] = -o[0];
        v->planes[1][3] = 0.0f;
        EAGL_MatAxisAngle(&m, -c->halffov, 0.0f, 1.0f, 0.0f);
        EAGL_MatTransform4(&m, 1, in, 16, o, 16);
        v->planes[0][0] = -o[2];
        v->planes[0][1] = 0.0f;
        v->planes[0][2] = o[0];
        v->planes[0][3] = 0.0f;
    }
    if (g_world) WVis_Cull(g_world->vis, g_world->vis_off, g_world->nvis, v->cx, v->cz, v->r2);
}

/* 0x47f320: the circle of radius r about (x, z) against the view's reach and side planes: out, or in
   (partly when it crosses one). */
static void vis_test(const WorldVis *v, float x, float z, float r, bool *in, bool *partly)
{
    *in = true;
    *partly = false;
    float dx = x - v->eye[0], dz = z - v->eye[2];
    float d2 = dx * dx + dz * dz;
    int32_t i;
    memcpy(&i, &d2, 4);
    i = (i >> 1) + 0x1fc00000;   /* (the square root's estimate) */
    float d;
    memcpy(&d, &i, 4);
    d -= v->dist;
    float zero = f32(0x5b23f8);
    if (d <= zero) {
        if (-r < d) *partly = true;
    } else if (r < d) {
        *in = false;
        return;
    } else {
        *partly = true;
    }
    if (v->steep) return;
    for (int k = 0; k < 2; k++) {
        float s = dx * v->planes[k][0] + dz * v->planes[k][2];
        if (s <= zero) {
            if (-r < s) *partly = true;
        } else if (s <= r) {
            *partly = true;
        } else {
            *in = false;
            return;
        }
    }
}

/* 0x47f410: the nodes with records from n down that the view may see; a node wholly in stops the tests
   below it, as does level 9. */
static void vis_walk(WorldVis *v, const WGrid *g, WGridNode *n, bool test)
{
    if (test) {
        float mn[3], mx[3];
        WGrid_Box(g, n, mn, mx);
        bool in, partly;
        vis_test(v, (mx[0] + mn[0]) * f32(0x5b23f0), (mx[2] + mn[2]) * f32(0x5b23f0),
                 (float)((double)fabsf(mx[0] - mn[0]) * f64(0x5b47b0)), &in, &partly);   /* (half the diagonal) */
        if (0x80000000u < (n->id & 0xf0000000u)) partly = false;
        test = partly;
        if (!in) return;
    }
    if (n->count) {
        if (v->nnodes == v->cap) {
            uint32_t cap = v->cap ? v->cap * 2 : 64;
            WGridNode **q = realloc(v->nodes, cap * sizeof *q);
            if (!q) return;
            v->nodes = q;
            v->cap = cap;
        }
        v->nodes[v->nnodes++] = n;
    }
    if (n->flags & 1)
        for (int q = 0; q < 4; q++)
            if (n->child[q]) vis_walk(v, g, n->child[q], test);
}

/* The world pass (0x47fb50): the world's per-frame update (0x488c30: the moving bundles filed again every
   8th pass), the visibility state (0x47f560), the visible objects (0x47f8a0: the nodes the view may see,
   each record's object once by the draw objects' visited marks). */
static void world_pass(RCamera *c)
{
    if (!g_world) return;
    s_passes++;
    /* 0x488c30: the knockable objects' tick, the instance bundle's then the scene bundle's */
    if (g_world->instances && g_world->instances->smack) WAnimatable_Tick(g_world->instances->smack);   /* 0x4767a0 */
    if (g_world->scene && g_world->scene->smack) WAnimatable_Tick(g_world->scene->smack);
    if (!(s_passes & 7) && g_world->f10)
        for (int i = 0; i < g_world->nbundles; i++)
            if (g_world->bundles[i]->b5) WGrid_FileBundle(g_world->f10, g_world->bundles[i]);
    WorldVis *v = &s_vis;
    vis_begin(v, c);
    const WGrid *g = g_world->f10;
    if (!g || !g->root) return;
    v->nnodes = 0;
    vis_walk(v, g, g->root, true);
    for (uint32_t k = 0; k < v->nnodes; k++) {
        const WGridNode *n = v->nodes[k];
        for (int i = 0; i < n->count; i++)   /* (a node's records can't change during the pass) */
            World_VisitObject(n->recs[i].bundle, n->recs[i].index, n->recs[i].flags, c->view, v->eye, v->dist2,
                              g_world->vis, g_world->vis_off, g_world->nvis);
    }
    v->nnodes = 0;
}

void (*g_render_cars)(RCamera *c);

void Render_World(RCamera *c)
{
    Headlights_Update();     /* 0x452590 (each view: the original's 0x468a20 starts with it) */
    Sprite_CameraGround();   /* 0x451f70 */
    if (c->v12c != 0) Sky_DrawBefore();   /* (and 0x657db4, 0x619799 clear: replay / front-end flags) */
    if (c->flags[2]) world_pass(c);                            /* +0x11a */
    if (c->flags[3] && g_render_cars) g_render_cars(c);       /* +0x11b: the cars queued (0x443540) */
    rs_set39(g_render->device->rs, 1);
    rs_set28(g_render->device->rs, 3);
    RLists_Flush();                                            /* 0x4671d0 */
    Particles_Draw();                                          /* 0x455130 */
    CarParticles_DrawAll();                                    /* 0x43f610 */
    if (c->v12c == 2) Sky_DrawAfter();
    Weather_Frame();                                           /* 0x458ef0 */
}
