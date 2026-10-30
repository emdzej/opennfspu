/* EAGL against a recording THRASH (eagl_fake_thrash.h): matrices and stacks, the transformer (the
   PACODE routine against the C routine on the same points: their clip codes must agree), views, the
   clipper, and whole material draws through the list and strip kernels, checked vertex by vertex.
   The clip-code tables and the draw layers' dependency tables come from Porsche.exe: without the game
   data the test is skipped. */
#include "eagl/eagl.h"
#include "eagl_fake_thrash.h"
#include "exe.h"
#include "vfs.h"
#include "vfs_host.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define NEAR(a, b, eps) (fabsf((a) - (b)) <= (eps))

static void test_math(void)
{
    EaglMatrix a, b, ab, t;
    EAGL_MatIdentity(&a);
    for (int i = 0; i < 16; i++) b.m[i] = (float)(i + 1);
    EAGL_MatMul(&a, &b, &ab);
    CHECK(!memcmp(&ab, &b, sizeof b));
    /* (row vectors) translation then scale: the point moves, then scales. */
    EAGL_MatTranslation(&a, 1, 2, 3);
    EAGL_MatScale(&b, 2, 2, 2, 1);
    EAGL_MatMul(&a, &b, &ab);
    float p[4] = {1, 1, 1, 1}, q[4];
    EAGL_MatTransform4(&ab, 1, p, 16, q, 16);
    CHECK(q[0] == 4 && q[1] == 6 && q[2] == 8 && q[3] == 1);
    /* An orthonormal inverse undoes a rotation + translation. */
    float c = cosf(0.3f), s = sinf(0.3f);
    float rot[9] = {c, s, 0, -s, c, 0, 0, 0, 1}, tr[3] = {5, -2, 7};
    EAGL_MatFromRotTrans(&a, rot, tr);
    t = a;
    EAGL_MatInvertOrtho(&t);
    EAGL_MatMul(&a, &t, &ab);
    for (int i = 0; i < 16; i++) CHECK(NEAR(ab.m[i], (i % 5 == 0) ? 1.0f : 0.0f, 1e-5f));
    float r2[9], t2[3];
    EAGL_MatToRotTrans(&a, r2, t2);
    CHECK(!memcmp(r2, rot, sizeof rot) && !memcmp(t2, tr, sizeof tr));
    /* Stacks: push copies the top, pop restores, both count as changes. */
    EaglMatStack st;
    EAGL_StackInit(&st, 3);
    int s0 = st.serial;
    EAGL_MatTranslation(EAGL_StackTopForWrite(&st), 1, 0, 0);
    EAGL_StackPush(&st);
    CHECK(st.index == 1 && st.top->m[12] == 1);
    EAGL_MatIdentity(EAGL_StackTopForWrite(&st));
    EAGL_StackPush(&st);
    EAGL_StackPush(&st);          /* full: ignored */
    CHECK(st.index == 2);
    EAGL_StackPop(&st);
    EAGL_StackPop(&st);
    CHECK(st.index == 0 && st.top->m[12] == 1 && st.serial > s0);
    EAGL_StackFree(&st);
}

static uint32_t lcg = 12345;
static float frand(float lo, float hi)
{
    lcg = lcg * 1103515245u + 12345u;
    return lo + (hi - lo) * (float)(lcg >> 8) / 16777216.0f;
}

static void test_transform(void)
{
    /* Points in camera-ish space through a projection like the game's (z' = -z, rhw = 1/z'): the PACODE
       routine (flags 0x20b) and the C routine (0x28b: bit 7 keeps PACODE out, same code routine 2)
       must give the same codes. */
    EaglMatrix m;
    EAGL_MatIdentity(&m);
    m.m[0] = 1.2f;
    m.m[5] = -1.6f;
    m.m[10] = -1.0f;
    float nearfar[2] = {1.0f, 500.0f};
    float src[200][4], a[200][4], b[200][4];
    uint32_t ca[200], cb[200];
    for (int i = 0; i < 200; i++) {
        src[i][0] = frand(-600, 600);
        src[i][1] = frand(-600, 600);
        src[i][2] = frand(-700, 50);
        src[i][3] = 1;
    }
    src[0][0] = 0; src[0][1] = 0; src[0][2] = -10;     /* straight ahead */
    src[1][0] = 0; src[1][1] = 0; src[1][2] = -0.5f;   /* nearer than near */
    src[2][0] = 0; src[2][1] = 0; src[2][2] = -600;    /* farther than far */
    src[3][0] = 25; src[3][1] = 0; src[3][2] = -10;    /* right of the view, outside the guard band */
    src[4][0] = 12; src[4][1] = 0; src[4][2] = -10;    /* right of the view, inside the guard band */
    EAGL_SetTransformMatrix(&m);
    EAGL_SetNearFar(nearfar);
    EAGL_SetGuardBand(1);
    EAGL_SetSourceStride(16);
    EAGL_SetDestStride(16);
    EAGL_SetCodeStride(4);
    EAGL_SetCodeOutput(ca);
    EAGL_Transform(0x20b, 200, src, a);
    EAGL_SetCodeOutput(cb);
    EAGL_Transform(0x28b, 200, src, b);
    int same = 0;
    for (int i = 0; i < 200; i++) {
        same += ca[i] == cb[i];
        for (int k = 0; k < 3; k++) CHECK(NEAR(a[i][k], b[i][k], fabsf(a[i][k]) * 1e-6f + 1e-6f));
    }
    CHECK(same == 200);
    CHECK(a[0][2] == 10.0f && a[0][3] == 0.1f && ca[0] == 0);
    CHECK((ca[1] & 3) == 1);                       /* near */
    CHECK((ca[2] & 3) == 2);                       /* far */
    CHECK((ca[3] & EAGL_CODE_CLIP) && (ca[3] & 0x3c00));    /* outside both */
    CHECK(!(ca[4] & EAGL_CODE_CLIP) && (ca[4] & 0x3c00));   /* outside the view, inside the guard band */
    EAGL_SetCodeOutput(NULL);
    EAGL_SetNearFar(NULL);
    EAGL_SetTransformMatrix(NULL);
}

static EaglView view;

static void setup_view(void)
{
    EAGL_SetDisplaySize(640, 480);
    EAGL_ViewInit(&view, NULL);
    EAGL_ViewSetViewport(&view, 640, 480, 0, 0, 1, 1000);   /* depth range: the sphere test's near/far */
    view.fov = 90;
    view.aspect = 0.75f;
    view.znear = 1;
    view.zfar = 1000;
    EAGL_ViewBegin(&view);
    CHECK(fake_clip[0] == 0 && fake_clip[1] == 0 && fake_clip[2] == 640 && fake_clip[3] == 480);
    CHECK(view.proj.m[10] == -1.0f && NEAR(view.proj.m[0], 1.0f, 1e-5f) && NEAR(view.proj.m[5], -0.75f, 1e-5f));
    CHECK(NEAR(view.zscale, 1000.0f / 999.0f, 1e-6f));
    float p[4] = {0, 0, -10, 1}, s[4];
    EAGL_ViewProject(&view, 1, p, s);
    CHECK(NEAR(s[0], 320.01f, 1e-3f) && NEAR(s[1], 240.01f, 1e-3f) && s[3] == 1.0f);
    float c1[3] = {0, 0, -50}, c2[3] = {0, 0, 50}, c3[3] = {-200, 0, -50};
    CHECK(EAGL_ViewSphereVisible(&view, c1, 1));
    CHECK(!EAGL_ViewSphereVisible(&view, c2, 1));      /* behind the camera */
    CHECK(!EAGL_ViewSphereVisible(&view, c3, 10));     /* far to the left */
}

static void test_clipper(void)
{
    /* A triangle crossing the near plane: one vertex in front of it, two behind (clip space z = depth). */
    float v[3 * 8] = {
        0, 0, 0.5f, 2.0f, 0, 0, 0, 0,
        1, 0, 4, 0.25f, 0, 0, 1, 0,
        0, 1, 4, 0.25f, 0, 0, 0, 1,
    };
    uint32_t col[3] = {0xff000000u, 0xff00ff00u, 0xffffffffu};
    for (int i = 0; i < 3; i++) memcpy(&v[i * 8 + 4], &col[i], 4);
    float *poly;
    int n;
    EAGL_ClipPerspective(0, 1, 2, 1, 2.0f, v, 1, 0, 1, 0, 1.0f, 1000.0f, &poly, &n);
    CHECK(n == 4);
    int on_near = 0;
    for (int i = 0; i < n; i++) on_near += poly[i * 8 + 2] == 1.0f && poly[i * 8 + 3] == 1.0f;
    CHECK(on_near == 2);
    /* Edge 0 -> 1 (t = 1/7): the colour lerp of the original's float trick. */
    uint32_t c;
    memcpy(&c, &poly[4], 4);
    CHECK((c >> 24) == 0xff);
}

static EaglStream streams[8];
static float coords[8][4];
static uint32_t colours[8];
static float uvs[8][2];
static int texture_token;

static EaglMaterial *build_material(bool sort)
{
    static EaglMaterial m;
    EAGL_MaterialInit(&m);
    EAGL_MaterialAddLayer(&m, EAGL_CreateLayer(1, 0, 0));    /* coords */
    EAGL_MaterialAddLayer(&m, EAGL_CreateLayer(6, 0, 0));    /* colours (type 5) */
    EAGL_MaterialAddLayer(&m, EAGL_CreateLayer(3, 0, 0));    /* uv (type 2) */
    EAGL_MaterialAddLayer(&m, EAGL_CreateLayer(4, 0, 0));    /* texture (type 3) */
    if (sort) EAGL_MaterialAddLayer(&m, EAGL_CreateLayer(9, 0, 0));
    EAGL_MaterialAddLayer(&m, EAGL_CreateLayer(0, 0, 0));    /* draw */
    EAGL_MaterialBind(&m);
    return &m;
}

static void set_streams(void)
{
    streams[0].data = coords;
    streams[1].data = colours;
    streams[2].data = uvs;
    streams[3].data = &texture_token;
}

static void expect_projected(const ThrashVertex *v, const float *p, uint32_t colour, const float *uv)
{
    /* The game's formula on the port's own transform result (z' = -z). */
    float z = -p[2], rhw = 1.0f / z;
    float x = p[0] * view.proj.m[0], y = p[1] * view.proj.m[5];
    CHECK(NEAR(v->x, x * view.vp_sx * rhw + view.vp_cx + 0.01f, 1e-3f));
    CHECK(NEAR(v->y, y * view.vp_sy * rhw + view.vp_cy + 0.01f, 1e-3f));
    CHECK(NEAR(v->z, (z * view.zscale + view.zoffset) * rhw, 1e-6f));
    CHECK(v->diffuse == colour && v->u == uv[0] && v->v == uv[1]);
}

static void test_draws(void)
{
    EaglMaterial *m = build_material(false);
    set_streams();
    CHECK(m->layers && m->layers->slot0 == 0);
    /* Two triangles in front of the camera: drawn without indices. */
    float tri[6][3] = {{-1, -1, -10}, {1, -1, -10}, {0, 1, -10}, {-2, 0, -20}, {2, 0, -20}, {0, 2, -20}};
    for (int i = 0; i < 6; i++) {
        memcpy(coords[i], tri[i], sizeof tri[i]);
        coords[i][3] = 1;
        colours[i] = 0xff102030u + (uint32_t)i;
        uvs[i][0] = (float)i;
        uvs[i][1] = 0.5f;
    }
    fake_nprims = 0;
    EAGL_MaterialDraw(m, streams, 3, 6);
    CHECK(fake_nprims == 1);
    if (fake_nprims == 1) {
        CHECK(fake_prims[0].prim == 4 && fake_prims[0].nverts == 6 && fake_prims[0].nindices == 0);
        CHECK(fake_prims[0].fvf == 0x1c4);
        for (int i = 0; i < 6; i++) expect_projected(&fake_prims[0].verts[i], coords[i], colours[i], uvs[i]);
    }
    /* The second triangle now reaches behind the near plane: indexed, the first as it is, the second
       clipped into a polygon after the input vertices. */
    coords[5][2] = 5;
    fake_nprims = 0;
    EAGL_MaterialDraw(m, streams, 3, 6);
    CHECK(fake_nprims == 1);
    if (fake_nprims == 1) {
        FakePrim *p = &fake_prims[0];
        CHECK(p->prim == 4 && p->nindices >= 6 && p->nindices % 3 == 0);
        CHECK(p->indices[0] == 0 && p->indices[1] == 1 && p->indices[2] == 2);
        for (int i = 0; i < 3; i++) expect_projected(&p->verts[i], coords[i], colours[i], uvs[i]);
        uint32_t clipped = (p->nindices - 3) / 3 + 2;
        CHECK(p->nverts == 6 + clipped);
        for (uint32_t i = 3; i < p->nindices; i++) CHECK(p->indices[i] >= 6 && p->indices[i] < p->nverts);
        for (uint32_t i = 6; i < p->nverts; i++) CHECK(p->verts[i].z >= -1e-6f && p->verts[i].z <= 1.0f);
    }
    /* Wholly behind the camera: nothing. */
    for (int i = 0; i < 6; i++) coords[i][2] = 50;
    fake_nprims = 0;
    EAGL_MaterialDraw(m, streams, 3, 6);
    CHECK(fake_nprims == 0);
    /* A strip in front: unindexed strip. */
    float strip[4][3] = {{-1, -1, -10}, {-1, 1, -10}, {1, -1, -10}, {1, 1, -10}};
    for (int i = 0; i < 4; i++) {
        memcpy(coords[i], strip[i], sizeof strip[i]);
        coords[i][3] = 1;
    }
    fake_nprims = 0;
    EAGL_MaterialDraw(m, streams, 1, 4);
    CHECK(fake_nprims == 1 && fake_prims[0].prim == 5 && fake_prims[0].nverts == 4 && fake_prims[0].nindices == 0);
    /* The same strip half behind the near plane: indexed list with alternating winding kept. */
    coords[0][2] = 2;
    fake_nprims = 0;
    EAGL_MaterialDraw(m, streams, 1, 4);
    CHECK(fake_nprims == 1 && fake_prims[0].prim == 4 && fake_prims[0].nindices >= 3);
}

static void test_sort(void)
{
    EaglMaterial *m = build_material(true);
    set_streams();
    /* 24 triangles, the first vertex of each a little behind the near plane so the draw is indexed
       (sorted) and the rest straight ahead at varying depth. */
    static float cs[72][4];
    for (int t = 0; t < 24; t++) {
        float depth = -(float)(10 + ((t * 7) % 24));
        float tv[3][3] = {{-1, -1, depth}, {1, -1, depth}, {0, 1, depth}};
        if (t == 5) tv[0][2] = 3;           /* one clipped triangle */
        for (int k = 0; k < 3; k++) {
            memcpy(cs[3 * t + k], tv[k], sizeof tv[k]);
            cs[3 * t + k][3] = 1;
        }
    }
    static uint32_t ccol[72];
    static float cuv[72][2];
    streams[0].data = cs;
    streams[1].data = ccol;
    streams[2].data = cuv;
    fake_nprims = 0;
    EAGL_MaterialDraw(m, streams, 3, 72);
    CHECK(fake_nprims == 1);
    if (fake_nprims == 1) {
        FakePrim *p = &fake_prims[0];
        float last = -1e9f;
        bool ascending = true;
        for (uint32_t t = 0; t < p->nindices / 3; t++) {
            float k = 1e9f;
            for (int j = 0; j < 3; j++) {
                float z = p->verts[p->indices[3 * t + j]].z;
                if (z < k) k = z;
            }
            if (k < last) ascending = false;
            last = k;
        }
        CHECK(ascending);          /* the original's order: nearest first */
    }
    CHECK(g_eagl->sort == 0);
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("eagl_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("eagl_test: %s\n", err); return 1; }
    test_math();
    test_transform();
    EAGL_ContextCreate();
    EAGL_ContextInit(g_eagl, 1);
    CHECK((g_eagl->flags & EAGL_CF_DX7) && (g_eagl->flags & EAGL_CF_GUARDBAND) && g_eagl->guardband == 2.0f);
    EAGL_ContextResetStages(g_eagl);
    setup_view();
    test_clipper();
    test_draws();
    test_sort();
    EAGL_ViewEnd(&view);
    printf("eagl_test: %d checks, %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
