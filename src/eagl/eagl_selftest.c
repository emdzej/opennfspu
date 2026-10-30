/* Port addition: a test scene through EAGL and THRASH (no game data needed beyond the exe's tables): a
   vertex-coloured cube (triangle list), a floor strip running behind the camera (near-plane clipping)
   and a triangle reaching past the left guard band (side clipping), through views, model matrices,
   materials and the kernels exactly as the game's renderers use them. Call once per frame after
   THRASH_setvideomode (it clears, draws and flips; the caller presents). */
#include "eagl/eagl.h"
#include "real/set3d.h"
#include <math.h>
#include <string.h>

static bool ready;
static EaglView view;
static EaglMaterial mat;
static EaglStream streams[4];
static float cube_pos[36][4], floor_pos[2 * 21][4], wide_pos[3][4];
static uint32_t cube_col[36], floor_col[2 * 21], wide_col[3];
static int frame;

static void cube(void)
{
    static const float corner[8][3] = {
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1},
    };
    static const int face[6][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7}, {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0}};
    static const uint32_t colour[6] = {0xffe04040, 0xff40e040, 0xff4040e0, 0xffe0e040, 0xff40e0e0, 0xffe040e0};
    int k = 0;
    for (int f = 0; f < 6; f++) {
        static const int tri[6] = {0, 1, 2, 0, 2, 3};
        for (int j = 0; j < 6; j++, k++) {
            const float *c = corner[face[f][tri[j]]];
            cube_pos[k][0] = c[0];
            cube_pos[k][1] = c[1];
            cube_pos[k][2] = c[2];
            cube_pos[k][3] = 1;
            /* One corner of each face at half brightness so the Gouraud shading shows. */
            cube_col[k] = tri[j] == 2 ? 0xff000000u | (colour[f] >> 1 & 0x7f7f7f) : colour[f];
        }
    }
}

static void setup(void)
{
    EAGL_ContextCreate();
    EAGL_ContextInit(g_eagl, 1);
    EAGL_ContextResetStages(g_eagl);
    EAGL_SetDisplaySize(640, 480);
    EAGL_ViewInit(&view, NULL);
    EAGL_ViewSetViewport(&view, 640, 480, 0, 0, 1, 1000);
    view.fov = 75;
    view.aspect = 0.75f;
    view.znear = 1;
    view.zfar = 1000;
    view.background = 0xff182028;
    EAGL_MaterialInit(&mat);
    EAGL_MaterialAddLayer(&mat, EAGL_CreateLayer(1, 0, 0));   /* coords */
    EAGL_MaterialAddLayer(&mat, EAGL_CreateLayer(6, 0, 0));   /* colours */
    EAGL_MaterialAddLayer(&mat, EAGL_CreateLayer(0, 0, 0));   /* draw */
    EAGL_MaterialBind(&mat);
    cube();
    /* The floor: a strip from 2 units behind the camera to 40 ahead, 1.5 below eye height. */
    for (int i = 0; i < 21; i++) {
        float z = 2.0f - (float)i * 2.1f;
        for (int s = 0; s < 2; s++) {
            float *p = floor_pos[2 * i + s];
            p[0] = s ? 4.0f : -4.0f;
            p[1] = -1.5f;
            p[2] = z;
            p[3] = 1;
            floor_col[2 * i + s] = (i & 1) ? 0xff605040 : 0xff807060;
        }
    }
    /* Reaching far past the left edge of the view (and the guard band). */
    static const float wide[3][3] = {{-60, 0.5f, -8}, {-1.5f, 0.5f, -8}, {-1.5f, 2.5f, -8}};
    for (int i = 0; i < 3; i++) {
        memcpy(wide_pos[i], wide[i], sizeof wide[i]);
        wide_pos[i][3] = 1;
        wide_col[i] = i == 0 ? 0xffffffff : 0xffff8000;
    }
    ready = true;
}

static void draw(float (*pos)[4], uint32_t *col, int prim, int n, const EaglMatrix *model)
{
    *EAGL_StackTopForWrite(&view.model) = *model;
    streams[0].data = pos;
    streams[1].data = col;
    EAGL_MaterialDraw(&mat, streams, prim, n);
}

void EAGL_selftest(void)
{
    if (!ready) setup();
    THRASH.fn.setstate(THRASH_STATE_SHADE, 1);
    THRASH.fn.setstate(THRASH_STATE_ZBUFFER, 1);
    THRASH.fn.setstate(THRASH_STATE_CULL, 0);
    THRASH.fn.setstate(THRASH_STATE_BLEND, 0);
    EAGL_ViewClear(&view);
    EAGL_ViewBegin(&view);
    EaglMatrix m;
    float a = 0.6f + (float)frame * 0.02f, c = cosf(a), s = sinf(a), c2 = cosf(0.4f), s2 = sinf(0.4f);
    /* A rotation about y, then a tilt about x, then 6 units ahead (row vectors). */
    float rot[9] = {c, s * s2, -s * c2, 0, c2, s2, s, -c * s2, c * c2};
    float tr[3] = {0.8f, 0.3f, -6};
    EAGL_MatFromRotTrans(&m, rot, tr);
    draw(cube_pos, cube_col, 3, 36, &m);
    EAGL_MatIdentity(&m);
    draw(floor_pos, floor_col, 1, 2 * 21, &m);
    draw(wide_pos, wide_col, 3, 3, &m);
    EAGL_ViewEnd(&view);
    THRASH.fn.pageflip();
    frame++;
}
