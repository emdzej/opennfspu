/* The HUD's 2D drawing (see hud2d.h). */
#include "render/hud2d.h"
#include "eagl/eagl.h"
#include "eagl/eagl_pending.h"
#include "exe.h"
#include "pending.h"
#include "render/render.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { QUADS = 700, PLAIN = 200, BUCKETS = 20, PER_BUCKET = 25, TEXTS = 64, LINES = 64 };

bool g_hud_opaque;

/* The queues (0x60ab10 ..): positions (x y z w), colours and texture coordinates per vertex; plain quads
   from 0, bucket b's from 200 + 25 b. */
static float *pos;            /* 0x60ab18 */
static uint32_t *col;         /* 0x60ab10 */
static float *uvs;            /* 0x60ab1c */
static int nplain;            /* 0x60c7f4 */
static int nbucket[BUCKETS];  /* 0x60ab20 */
static Texture *btex[BUCKETS];/* 0x60c798 */
static int nqueued;           /* 0x60c7f8 */

typedef struct { const char *s; uint32_t colour; float x, y; HudFont *font; float sx, sy; } QText;
static QText texts[TEXTS];    /* 0x60ab78, 0x24 bytes each */
static int ntexts;            /* 0x60c7ec */
typedef struct { float p[8]; uint32_t c[2]; } QLine;
static QLine lines[LINES];    /* 0x608bd0, 0x28 bytes each */
static int nlines;            /* 0x60c7f0 */

/* The drawing object (0x59ee20): a material of coordinates (id 7), colours, uv, texture, render state and
   the draw layer, its five streams, and its render-state record. */
static EaglMaterial mat;
static EaglStream streams[5];
static int32_t rs[6];

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

void Hud2D_Init(void)
{
    if (!pos) {
        pos = calloc(QUADS * 4, 16);
        col = calloc(QUADS * 4, 4);
        uvs = calloc(QUADS * 4, 8);
        EAGL_MaterialInit(&mat);
        EAGL_MaterialAddLayer(&mat, EAGL_CreateLayer(1, 7, 0));   /* LayerCoords */
        EAGL_MaterialAddLayer(&mat, EAGL_CreateLayer(6, 0, 0));   /* LayerColours */
        EAGL_MaterialAddLayer(&mat, EAGL_CreateLayer(3, 0, 0));   /* LayerUV */
        EAGL_MaterialAddLayer(&mat, EAGL_CreateLayer(4, 0, 0));   /* LayerTexture */
        EAGL_MaterialAddLayer(&mat, EAGL_CreateLayer(8, 0, 0));   /* LayerState */
        EAGL_MaterialAddLayer(&mat, EAGL_CreateLayer(0, 0, 0));   /* the draw layer (0x53f370) */
        EAGL_MaterialBind(&mat);
    }
    /* The record (0x537760, then 0x537790(0), 0x5377d0(0)). */
    rs[0] = 1;
    rs[1] = 2;
    rs[2] = rs[3] = rs[4] = 0;
    rs[5] = -1;
    nqueued = 0;
    memset(nbucket, 0, sizeof nbucket);
}

static void put_colour(uint32_t *dst, const uint32_t c[4])
{
    for (int k = 0; k < 4; k++) dst[k] = g_hud_opaque ? c[k] | 0xff000000u : c[k];
}

void Hud2D_Quad(const float corners[16], const uint32_t colours[4])
{
    if (!pos || nplain >= PLAIN) return;   /* (the original has no bound) */
    int q = nplain++;
    for (int k = 0; k < 4; k++) {
        float *p = pos + 16 * q + 4 * k;
        p[0] = corners[4 * k];
        p[1] = corners[4 * k + 1];
        p[2] = 0;
        p[3] = 1.0f;
        uvs[8 * q + 2 * k] = exe_f32(0x5b2838 + 8u * (unsigned)k);   /* the untextured quads' fixed uv */
        uvs[8 * q + 2 * k + 1] = exe_f32(0x5b283c + 8u * (unsigned)k);
    }
    put_colour(col + 4 * q, colours);
}

void Hud2D_TexQuad(int b, Texture *t, const uint32_t colours[4], const float corners[16], const float uv[8])
{
    if (!pos || b < 0 || b >= BUCKETS || nbucket[b] >= PER_BUCKET) return;
    int q = PLAIN + PER_BUCKET * b + nbucket[b];
    memcpy(pos + 16 * q, corners, 16 * sizeof *corners);
    put_colour(col + 4 * q, colours);
    memcpy(uvs + 8 * q, uv, 8 * sizeof *uv);
    btex[b] = t;
    nbucket[b]++;
    nqueued++;
}

void Hud2D_Text(const char *s, uint32_t colour, float x, float y, HudFont *f, float sx, float sy)
{
    if (ntexts >= TEXTS) return;
    texts[ntexts++] = (QText){s, g_hud_opaque ? colour | 0xff000000u : colour, x, y, f, sx, sy};
}

void Hud2D_Line(const float points[8], const uint32_t colours[2])
{
    if (nlines >= LINES) return;
    QLine *l = &lines[nlines++];
    memcpy(l->p, points, sizeof l->p);
    l->c[0] = colours[0];
    l->c[1] = colours[1];
    if (g_hud_opaque) {
        l->c[0] |= 0xff000000u;
        l->c[1] |= 0xff000000u;
    }
}

/* n quads from quad index q, with texture t (0x59ef80: stream sizes, then the material's draw). */
static void draw_quads(int q, int n, Texture *t)
{
    streams[0] = (EaglStream){(uint32_t)n * 4 * 16, pos + 16 * q};
    streams[1] = (EaglStream){(uint32_t)n * 4 * 4, col + 4 * q};
    streams[2] = (EaglStream){(uint32_t)n * 4 * 8, uvs + 8 * q};
    streams[3] = (EaglStream){0x54, t};
    streams[4] = (EaglStream){0x18, rs};
    EAGL_MaterialDraw(&mat, streams, 4, n * 4);
}

/* At once, with the line object's material (0x62810c, 0x59ee20; the queues' here): prim 2 lines, 4 quads. */
static void draw_now(int prim, int n, const float *p, const uint32_t *c, const float *uv, Texture *t)
{
    static float nouv[2 * 0x200];
    if (n <= 0 || 0x200 < n) return;
    streams[0] = (EaglStream){(uint32_t)n * 16, (void *)p};
    streams[1] = (EaglStream){(uint32_t)n * 4, (void *)c};
    streams[2] = (EaglStream){(uint32_t)n * 8, uv ? (void *)uv : nouv};
    streams[3] = (EaglStream){0x54, t};
    streams[4] = (EaglStream){0x18, rs};
    EAGL_MaterialDraw(&mat, streams, prim, n);
}

void Hud2D_LineNow(const float points[8], const uint32_t colours[2]) { draw_now(2, 2, points, colours, NULL, NULL); }

void Hud2D_LinesNow(int n, const float *points, uint32_t colour)
{
    static uint32_t c[0x200];
    if (n <= 0 || 0x200 < n) return;
    for (int i = 0; i < n; i++) c[i] = colour;
    draw_now(2, n, points, c, NULL, NULL);
}

void Hud2D_QuadNow(const float corners[16], uint32_t colour)
{
    uint32_t c[4] = {colour, colour, colour, colour};
    draw_now(4, 4, corners, c, NULL, NULL);
}

void Hud2D_TriNow(const float corners[12], uint32_t colour)
{
    uint32_t c[3] = {colour, colour, colour};
    draw_now(3, 3, corners, c, NULL, NULL);
}

void Hud2D_TexQuadNow(Texture *t, const uint32_t colours[4], const float corners[16], const float uv[8])
{
    draw_now(4, 4, corners, colours, uv, t);
}

void Hud2D_Flush(void)
{
    if (nplain) draw_quads(0, nplain, NULL);
    nplain = 0;
    for (int b = 0; b < BUCKETS; b++) {
        if (nbucket[b]) draw_quads(PLAIN + PER_BUCKET * b, nbucket[b], btex[b]);
        nbucket[b] = 0;
    }
    nqueued = 0;
    for (int i = 0; i < ntexts; i++) {
        QText *t = &texts[i];
        if (!t->font) continue;
        FONT_setstate(t->font->font, 0xc, t->colour);   /* 0x536a30 */
        if (t->sx != 1.0f || t->sy != 1.0f) {           /* 0x536a50 */
            FONT_setstatef(t->font->font, 2, t->sx);
            FONT_setstatef(t->font->font, 3, t->sy);
        }
        RenderSystem *r = g_render;
        const EaglView *v = r && r->camera ? r->camera->view : NULL;
        if (v) HudFont_print(t->font, v, t->x, t->y, -1.0f, t->s);   /* 0x536770 */
    }
    ntexts = 0;
    /* The lines, one draw each with the line object (0x62810c, the 0x59ee20 material): two points, their
       colours, no texture coordinates (0x465f10). */
    static const float nouv[4];
    for (int i = 0; i < nlines; i++) {
        streams[0] = (EaglStream){2 * 16, lines[i].p};
        streams[1] = (EaglStream){2 * 4, lines[i].c};
        streams[2] = (EaglStream){2 * 8, (void *)nouv};
        streams[3] = (EaglStream){0x54, NULL};
        streams[4] = (EaglStream){0x18, rs};
        EAGL_MaterialDraw(&mat, streams, 2, 2);
    }
    nlines = 0;
}

void Hud2D_View(RenderSystem *r)
{
    RCamera *c = r ? r->camera : NULL;
    if (!c || !c->view) return;
    EaglView *v = c->view;
    float w = (float)g_display.width, h = (float)g_display.height;
    EAGL_ViewSetOrtho(v, 1.0f, 0, 10.0f);
    EAGL_MatTranslation(EAGL_StackTopForWrite(&v->view), -1.0f, 1.0f, -0.1f);
    EaglMatrix s;
    EAGL_MatScale(&s, exe_f32(0x5b2580) / w, exe_f32(0x5b2628) / h, -1.0f, 1.0f);
    EAGL_MatPreMul(EAGL_StackTopForWrite(&v->view), &s);
    if (c->active) EAGL_ContextViewport(g_eagl);
}

HudFont *HudFont_new(Font *f)
{
    HudFont *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    h->font = f;
    TextureStates_init(&h->states);
    h->rs[0] = 1;
    h->rs[1] = 2;
    h->rs[5] = -1;
    return h;
}

void HudFont_print(HudFont *h, const EaglView *v, float x, float y, float z, const char *s)
{
    if (!h || !h->font || !TEXTURE_islive()) return;
    TexManager *tm = g_texgraphics ? g_texgraphics->textures : NULL;
    if (tm) TEXTURE_select(tm, NULL, NULL);
    EAGL_ContextSetVertexFormat(g_eagl, 0);
    TEXTURE_sendstage(&h->states, 0);
    EAGL_RSApply(h->rs);   /* 0x537460 */
    float vw, vh, vx, vy, mnz, mxz;
    EAGL_ViewGetViewport(v, &vw, &vh, &vx, &vy, &mnz, &mxz);
    FONT_setclip(h->font, vx, vy, vw, vh);
    EAGL_ViewUpdate((EaglView *)v);
    float p[4] = {x, y, z, 1.0f}, q[4];
    float sx, sy, depth;
    if (v->projection == 1) {
        p[2] = -p[2];
        EAGL_MatTransform4(&v->combined, 1, p, 16, q, 16);   /* 0x53a0a0 */
        sx = (float)floor((double)((q[0] + 1.0f) * vw * 0.5f + vx));
        sy = (float)floor((double)((q[1] + 1.0f) * vh * 0.5f + vy));
        depth = q[2];
    } else {
        EAGL_MatTransform4(&v->combined, 1, p, 16, q, 16);
        /* The third coordinate carries the depth the projection divides by (z' = -z). */
        sx = (float)(vw * 0.5f + floor((double)(q[0] * vw * 0.5f)) / q[2] + vx);
        sy = (float)(vh * 0.5f + floor((double)(q[1] * vh * 0.5f)) / q[2] + vy);
        depth = (v->zscale * q[2] + v->zoffset) / q[2];
    }
    if (0 <= depth && depth <= 1.0f) {
        if (h->z != depth) {
            h->z = depth;
            FONT_setstatef(h->font, 0x16, depth);
        }
        FONT_print(h->font, sx, sy, s);
    }
    if (tm) TexManager_restore(tm);
}
