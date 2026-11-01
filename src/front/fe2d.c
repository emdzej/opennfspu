/* The front end's 2D drawing (fe2d.h). The original's queue is a std::multiset of 0x80-byte records;
   the port keeps a sorted array, inserting after equal keys as the set does. */
#include "front/fe2d.h"
#include "eagl/eagl.h"
#include "exe.h"
#include "render/render.h"
#include "render/sky_levels.h"
#include "render/thrash.h"
#include <stdlib.h>
#include <string.h>

/* A queued item (0x80 bytes): kind 0 a quad, else a text whose drawer is in tex. */
typedef struct {
    int32_t kind;            /* +0x00 */
    int32_t order;           /* +0x04: the translucent ones' count when queued */
    uint8_t opaque;          /* +0x08 */
    void *tex;               /* +0x0c: the texture, or a text's drawer */
    union {
        struct {
            float pos[4][4]; /* +0x10: the first corner's z is the depth */
            uint32_t col[4]; /* +0x50 */
            float uv[4][2];  /* +0x60 */
        } q;
        struct {
            float x, y, z;   /* +0x10 */
            uint32_t colour; /* +0x1c */
            const char *s;   /* +0x20 */
        } t;
    } u;
} Item;

static float depth_of(const Item *i) { return i->kind == 0 ? i->u.q.pos[0][2] : i->u.t.z; }

/* The texture's place in the sort: the original compares the pointers, an order its heap gives; the
   port numbers the textures as it first queues them, so that runs repeat whatever the allocator does. */
static const void *s_seen[512];
static int s_nseen;
static uint32_t tex_key(const void *t)
{
    if (!t) return 0;
    for (int i = 0; i < s_nseen; i++)
        if (s_seen[i] == t) return (uint32_t)i + 1;
    if (s_nseen < 512) s_seen[s_nseen++] = t;
    return (uint32_t)s_nseen;
}

/* 0x4d8f00: farther first; then the order; then the texture. */
static bool before(const Item *a, const Item *b)
{
    float za = depth_of(a), zb = depth_of(b);
    if (za == zb) {
        if ((uint32_t)a->order == (uint32_t)b->order) {
            /* The original's last key is the textures' pointers, its heap's order. A text queued with
               the same order as the image before it (a text takes the count, an image counts first) is
               drawn over it: the port puts texts after quads there, as the original's screens show (a
               selected tab's caption over its highlight). */
            if (a->kind != b->kind) return a->kind == 0;
            return tex_key(a->tex) < tex_key(b->tex);
        }
        return (uint32_t)a->order < (uint32_t)b->order;
    }
    return zb < za;
}

static Item *s_items;
static int s_n, s_cap;
static int32_t s_order;        /* 0x65b970 */
float g_fe_depth = 0.99998474f; /* 0x65b954 (0x3f7fff00) */
static float s_saved = 0.99998474f;   /* 0x65b988 */
/* (0x65b984, set while a depth is pinned, is never read) */

static EaglMaterial s_mat;     /* 0x65b978 (its layers 0x65b97c) */
static bool s_init;
static float (*s_pos)[4];      /* 0x65b950: 50 quads */
static uint32_t *s_col;        /* 0x65b960 */
static float (*s_uv)[2];       /* 0x65b964 */
static char *s_arena;          /* 0x65b98c (20000 bytes) */
static int s_used;             /* 0x65b96c */
static SkyBlend s_rs_opaque;   /* 0x65b948: no blending */
static SkyBlend s_rs_blend;    /* 0x65b968 */

enum { BATCH = 50, ARENA = 20000 };

void FE2D_Init(void)
{
    if (!s_init) {
        EAGL_MaterialInit(&s_mat);
        EAGL_MaterialAddLayer(&s_mat, EAGL_CreateLayer(1, 7, 0));      /* coordinates */
        EAGL_MaterialAddLayer(&s_mat, EAGL_CreateLayer(6, 0, 0));      /* colours */
        EAGL_MaterialAddLayer(&s_mat, EAGL_CreateLayer(3, 0, 0));      /* texture coordinates */
        EAGL_MaterialAddLayer(&s_mat, EAGL_CreateLayer(4, 0, 0));      /* the texture */
        EAGL_MaterialAddLayer(&s_mat, EAGL_CreateLayer(8, 0, 0));      /* the render states */
        EAGL_MaterialAddLayer(&s_mat, EAGL_CreateLayer(0, 0, 0));      /* the draw (0x53f370, which Ghidra
                                                                           names EAGL_LayerDraw2_ctor) */
        EAGL_MaterialBind(&s_mat);
        s_pos = calloc(BATCH * 4, sizeof *s_pos);
        s_col = calloc(BATCH * 4, sizeof *s_col);
        s_uv = calloc(BATCH * 4, sizeof *s_uv);
        s_arena = calloc(ARENA, 1);
        s_init = true;
    }
    s_used = 0;
    s_n = 0;
    SkyBlend_ctor(&s_rs_opaque);
    s_rs_opaque.v[0] = 1;   /* 0x5377a0 */
    s_rs_opaque.v[1] = 0;   /* 0x5377b0 */
    s_rs_opaque.v[2] = 0;   /* 0x5377c0 */
    s_rs_opaque.v[3] = 0;   /* 0x537790 */
    SkyBlend_ctor(&s_rs_blend);
    s_rs_blend.v[0] = 1;
    s_rs_blend.v[1] = 2;
    s_rs_blend.v[2] = 0;
    s_rs_blend.v[3] = 0;
}

void FE2D_Free(void)
{
    free(s_pos);
    free(s_col);
    free(s_uv);
    free(s_arena);
    free(s_items);
    s_pos = NULL;
    s_col = NULL;
    s_uv = NULL;
    s_arena = NULL;
    s_items = NULL;
    s_n = s_cap = 0;
    s_init = false;
}

static void insert(const Item *it)
{
    if (s_n == s_cap) {
        int cap = s_cap ? s_cap * 2 : 256;
        Item *a = realloc(s_items, (size_t)cap * sizeof *a);
        if (!a) return;
        s_items = a;
        s_cap = cap;
    }
    int lo = 0, hi = s_n;   /* after the last that isn't after it */
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if (before(it, &s_items[m])) hi = m;
        else lo = m + 1;
    }
    memmove(&s_items[lo + 1], &s_items[lo], (size_t)(s_n - lo) * sizeof *s_items);
    s_items[lo] = *it;
    s_n++;
}

static void quad(Item *it, const float xy[4][2], const uint32_t c[4], const float *uv, bool opaque, void *tex)
{
    memset(it, 0, sizeof *it);
    for (int k = 0; k < 4; k++) {
        it->u.q.pos[k][0] = xy[k][0];
        it->u.q.pos[k][1] = xy[k][1];
        it->u.q.pos[k][2] = g_fe_depth;
        it->u.q.pos[k][3] = 1.0f;
        it->u.q.col[k] = c[k];
    }
    memcpy(it->u.q.uv, uv, sizeof it->u.q.uv);
    it->opaque = opaque;
    it->tex = tex;
    if (!opaque) s_order++;
    it->order = s_order;
}

static const float *exe_uv(uint32_t va)
{
    static float b[3][8];
    static const uint32_t vas[3] = {0x5b6958, 0x5b6978, 0x5b6998};
    for (int i = 0; i < 3; i++)
        if (vas[i] == va) {
            for (int k = 0; k < 8; k++) {
                uint32_t u = exe_u32(va + 4u * (uint32_t)k);
                memcpy(&b[i][k], &u, 4);
            }
            return b[i];
        }
    return b[0];
}

void FE2D_Rect(float x, float y, float w, float h, uint32_t colour)
{
    const float xy[4][2] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
    const uint32_t c[4] = {colour, colour, colour, colour};
    Item it;
    quad(&it, xy, c, exe_uv(0x5b6958), (colour & 0xff000000u) == 0xff000000u, NULL);
    insert(&it);
}

void FE2D_Frame(float x, float y, float w, float h, uint32_t colour)
{
    FE2D_Rect(x, y, w, 1.0f, colour);
    FE2D_Rect(x, (y + h) - 1.0f, w, 1.0f, colour);
    FE2D_Rect(x, y, 1.0f, h, colour);
    FE2D_Rect((x + w) - 1.0f, y, 1.0f, h, colour);
}

static bool all_opaque(const uint32_t c[4])
{
    for (int k = 0; k < 4; k++)
        if ((c[k] & 0xff000000u) != 0xff000000u) return false;
    return true;
}

void FE2D_Gouraud(float x, float y, float w, float h, uint32_t tl, uint32_t tr, uint32_t bl, uint32_t br)
{
    const float xy[4][2] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
    const uint32_t c[4] = {tl, tr, br, bl};
    Item it;
    quad(&it, xy, c, exe_uv(0x5b6978), all_opaque(c), NULL);
    insert(&it);
}

void FE2D_Quad(const float in[8], const uint32_t colours[4])
{
    /* (the points come as top left, top right, bottom left, bottom right) */
    const float xy[4][2] = {{in[0], in[1]}, {in[2], in[3]}, {in[6], in[7]}, {in[4], in[5]}};
    const uint32_t c[4] = {colours[0], colours[1], colours[3], colours[2]};
    Item it;
    quad(&it, xy, c, exe_uv(0x5b6998), all_opaque(c), NULL);
    insert(&it);
}

/* x past a whole pixel moves to the next (0x5a0d48 truncates, the sum kept otherwise). */
static float edge(float v)
{
    float t = (float)(int32_t)v;
    return t == v ? v : v + 1.0f;
}

void FE2D_Image(const FeImage *img, float x, float y, float w, float h, uint32_t colour, uint32_t flags, bool opaque)
{
    float x0 = (float)(int32_t)x, y0 = (float)(int32_t)y, x1 = edge(x + w), y1 = edge(y + h);
    const float xy[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    const uint32_t c[4] = {colour, colour, colour, colour};
    const float d = 0.001953125f;   /* 0x5b4a7c: half a texel of 256 */
    float a = (flags & 1) ? img->u1 - d : img->u0, b = (flags & 1) ? img->u0 - d : img->u1;
    float cc = (flags & 2) ? img->v1 - d : img->v0, dd = (flags & 2) ? img->v0 - d : img->v1;
    if (Render_CardType() == 0xe) {   /* 0x6197b0 */
        a += d;
        b += d;
        cc += d;
        dd += d;
    }
    const float uv[8] = {a, cc, b, cc, b, dd, a, dd};
    Item it;
    quad(&it, xy, c, uv, opaque, img->tex);
    insert(&it);
}

void FE2D_ImageClip(const FeImage *img, float x, float y, float w, float h, const int32_t clip[4], uint32_t colour,
                    uint32_t flags, bool opaque)
{
    /* the right and bottom cut by the image's own size at (x, y); the left and top move the corner */
    float r = w, l = 0, b = h, t = 0;
    if ((float)clip[2] < (float)img->w + x) r = (float)clip[2] - x;
    if (x < (float)clip[0]) {
        l = (float)clip[0] - x;
        x = (float)clip[0];
    }
    if ((float)clip[3] < (float)img->h + y) b = (float)clip[3] - y;
    if (y < (float)clip[1]) {
        t = (float)clip[1] - y;
        y = (float)clip[1];
    }
    if (!(0.0f <= r && l <= w && t <= h && 0.0f <= b)) return;   /* (0x5b23f8: 0) */
    float x0 = (float)(int32_t)x, y0 = (float)(int32_t)y, x1 = edge((r - l) + x), y1 = edge((b - t) + y);
    const float xy[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    const uint32_t c[4] = {colour, colour, colour, colour};
    float ua = (flags & 1) ? img->u1 : img->u0, ub = (flags & 1) ? img->u0 : img->u1;
    float va = (flags & 2) ? img->v1 : img->v0, vb = (flags & 2) ? img->v0 : img->v1;
    float u0 = (l / w) * (ub - ua) + ua, u1 = (r / w) * (ub - ua) + ua;
    float v0 = (t / h) * (vb - va) + va, v1 = (b / h) * (vb - va) + va;
    const float uv[8] = {u0, v0, u1, v0, u1, v1, u0, v1};
    Item it;
    quad(&it, xy, c, uv, opaque, img->tex);
    insert(&it);
}

void FE2D_Text(HudFont *f, float x, float y, int align, uint32_t colour, const char *s, float sx, float sy)
{
    if (!colour) return;
    float z = g_fe_depth;
    if (f && f->font) {
        FONT_setstatef(f->font, 2, sx);   /* 0x536a50 */
        FONT_setstatef(f->font, 3, sy);
    }
    if (align && f && f->font) {
        float ex = 0, ey = 0, ew = 0, eh = 0;
        FONT_extent(f->font, s, &ex, &ey, &ew, &eh);   /* 0x5366e0 */
        float nx = align == 1 ? (x - ex) - ew * 0.5f : align == 2 ? x - ew : x;
        x = (float)(int32_t)nx;
        y = (float)(int32_t)y;
    }
    Item it;
    memset(&it, 0, sizeof it);
    it.kind = 1;
    it.order = s_order++;
    it.opaque = 0;
    it.tex = f;
    it.u.t.x = x;
    it.u.t.y = y;
    it.u.t.z = z;
    it.u.t.colour = colour;
    it.u.t.s = s;
    insert(&it);
}

static void draw_quads(int n, void *tex, bool opaque)
{
    EaglStream st[5] = {
        {(uint32_t)n << 6, s_pos},
        {(uint32_t)n << 4, s_col},
        {(uint32_t)n << 5, s_uv},
        {0x54, tex},
        {0x18, opaque ? s_rs_opaque.v : s_rs_blend.v},
    };
    EAGL_MaterialDraw(&s_mat, st, 4, n * 4);
}

static void draw_texts(int from, int to)
{
    THRASH.fn.setstate(10, 2);
    FONT_begin();   /* 0x53e300 */
    const EaglView *v = g_render && g_render->camera ? g_render->camera->view : NULL;
    for (int i = from; i < to; i++) {
        const Item *t = &s_items[i];
        HudFont *f = t->tex;
        if (!f || !f->font || !v) continue;
        FONT_setstate(f->font, 0xc, t->u.t.colour);   /* 0x536a30 */
        HudFont_print(f, v, t->u.t.x, t->u.t.y, -t->u.t.z, t->u.t.s);   /* 0x536710 at the drawer's depth */
    }
    FONT_end();     /* 0x53e320 */
}

void FE2D_Flush(void)
{
    s_order = 0;
    if (!s_n) return;
    int head = 0, k = 0;
    for (int i = 0;; i++) {
        bool end = i == s_n;
        const Item *h = &s_items[head];
        if (k && (end || k == BATCH || s_items[i].kind != h->kind || s_items[i].tex != h->tex ||
                  s_items[i].opaque != h->opaque)) {
            if (h->kind == 0) draw_quads(k, h->tex, h->opaque);
            else draw_texts(head, i);
            k = 0;
            head = i;
        }
        if (end) break;
        const Item *it = &s_items[i];
        if (it->kind == 0) {
            memcpy(s_pos[4 * k], it->u.q.pos, sizeof it->u.q.pos);
            memcpy(&s_col[4 * k], it->u.q.col, sizeof it->u.q.col);
            memcpy(s_uv[4 * k], it->u.q.uv, sizeof it->u.q.uv);
        }
        k++;
    }
    s_n = 0;
    s_used = 0;
}

void FE2D_Clear(void)
{
    g_fe_depth = 0.99998474f;
    if (THRASH.fn.window(3)) {
        THRASH.fn.clearwindow();
        THRASH.fn.window(2);
        return;
    }
    THRASH.fn.window(2);
    RenderState *rs = g_render->device->rs;
    int s28 = rs->state28;
    rs->state28 = 7;
    EAGL_RSSet(0x28, 7);
    g_fe_depth = s_saved = 0.99998474f;
    FE2D_Rect(0, 0, (float)g_display.width, (float)g_display.height, 0);   /* 0x5deac8, 0x5deacc */
    THRASH.fn.setstate(0x24, 0);
    FE2D_Flush();
    THRASH.fn.setstate(0x24, 1);
    g_fe_depth = s_saved;
    rs->state28 = s28;
    EAGL_RSSet(0x28, (uint32_t)s28);
}

void FE2D_DepthStep(void)
{
    if (0.75f < g_fe_depth) g_fe_depth -= 0.05f;          /* 0x5b260c, 0x5b26a0 */
    else if (0.0f < g_fe_depth) g_fe_depth -= 0.01f;      /* 0x5b6a58, 0x5b27a8 */
    else g_fe_depth -= 0.001f;                            /* 0x5b27b4 */
}

static float step(float z)
{
    if (0.75f < z) return z - 0.05f;
    if (0.0f < z) return z - 0.01f;
    return z - 0.001f;
}

void FE2D_DepthPush(float z) { g_fe_depth = s_saved = z; }
void FE2D_DepthPop(void) { g_fe_depth = s_saved; }

void FE2D_DepthPushStep(void) { g_fe_depth = s_saved = step(step(g_fe_depth)); }

static int s_zfunc;            /* 0x65b944 */
static uint8_t s_overlay;      /* 0x65b990 */

void FE2D_OverlayBegin(void)
{
    FE2D_Flush();
    RenderState *rs = g_render->device->rs;
    s_zfunc = rs->state28;
    rs->state28 = 6;
    EAGL_RSSet(0x28, 6);
    s_overlay = 1;
    g_fe_depth += 0.001f;   /* 0x5b27b4 */
}

void FE2D_OverlayEnd(void)
{
    s_overlay = 0;
    FE2D_Flush();
    RenderState *rs = g_render->device->rs;
    rs->state28 = s_zfunc;
    EAGL_RSSet(0x28, (uint32_t)s_zfunc);
    g_fe_depth = s_saved;
}

bool FE2D_InOverlay(void) { return s_overlay; }

char *FE2D_Alloc(int n)
{
    if (!s_arena || n < 0 || ARENA < s_used + n) return NULL;   /* (the original has no bound) */
    char *p = s_arena + s_used;
    s_used += n;
    return p;
}
