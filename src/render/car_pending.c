/* Pending calls of the car code (car_pending.h) and the render-method stand-in. */
#include "render/car_pending.h"
#include "pending.h"
#include <math.h>
#include <string.h>

void R3DCar_PendingStyleExtents(R3DCar *car) { PENDING_NOTE(0x437a30, "R3DCar_LoadStyles (stripe, timed extents)"); }
void R3DCar_PendingPostLOD(R3DCar *car) { PENDING_NOTE(0x43b040, "R3DCar_v20"); }
bool R3DCar_PendingTexturesWanted(R3DCar *car)
{
    PENDING_NOTE(0x44deb0, "RTex_Info");
    return true;
}
void R3DCar_PendingGlobalTextures(R3DCar *car) { PENDING_NOTE(0x44d1f0, "RTex_Find (SPC1..SPC3, FEDU, FE, FESH)"); }
void R3DCar_PendingFallback(R3DCar *car, const char *name) { PENDING_NOTE(0x441940, "R3DCar_LoadCrp (stand-in car)"); }
void R3DCar_PendingDecals(R3DCar *car) { PENDING_NOTE(0x447b80, "R3DCar_Decals"); }
void R3DCar_PendingTexturePages(R3DCar *car) { PENDING_NOTE(0x4426c0, "R3DObject_LoadHeader (texture pages)"); }
void *R3DMaterial_PendingMethod(R3DCar *car, R3DMaterial *m)
{
    PENDING_NOTE(0x44ba40, "RMethod_Find");
    return NULL;
}
void R3DLOD_PendingBegin(R3DCar *car) { PENDING_NOTE(0x44a090, "R3DMaterialSet_Begin"); }
void R3DObject_PendingRenderState(R3DCar *car) { PENDING_NOTE(0x467ed0, "Render_SetCull"); }

/* ---- the stand-in for render methods ---- */

enum { MAXV = 256 };
static EaglMaterial standin;
static bool standin_ready;
static float pos[MAXV][4];
static uint32_t col[MAXV];

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

void R3DMaterial_Draw(R3DCar *car, R3DPart *part, R3DMaterial *mat, const uint32_t *pr, uint32_t n,
                      EaglMatStack *model)
{
    PENDING_NOTE(0x44a190, "R3DMaterial_Bind (render methods; the port's stand-in draws)");
    if (n == 0 || n > MAXV) return;
    const uint8_t *b = (const uint8_t *)pr;
    uint32_t nstreams = u32(b + 0x28), nlists = u32(b + 0x2c);
    const uint8_t *streams = b + 0x30, *lists = streams + 16 * nstreams;
    const uint8_t *area = lists + 8 * nlists;
    const float *vt = NULL, *nm = NULL;
    const uint8_t *vt_idx = NULL, *nm_idx = NULL;
    /* The streams as 0x44a190 resolves them: kinds below 0x100 are the part's items by kind and index,
       plus the stream's offset; a stream's index list (or none: element k uses entry k). */
    for (uint32_t i = 0; i < nstreams; i++) {
        const uint8_t *s = streams + 16 * i;
        uint16_t kind = u16(s + 10), index = u16(s + 12), list = u16(s + 14);
        if (kind > 1) continue;
        const uint8_t *data;
        if (kind == 0 && part->verts) data = (const uint8_t *)part->verts;   /* the part's own copy */
        else data = R3DPart_Item(part, kind, index == 0xffff ? 0xffffffffu : index);
        if (!data) continue;
        data += u32(s + 4);
        const uint8_t *idx = list == 0xffff || list >= nlists ? NULL : area + u32(lists + 8 * list + 4);
        if (kind == 0) {
            vt = (const float *)data;
            vt_idx = idx;
        } else {
            nm = (const float *)data;
            nm_idx = idx;
        }
    }
    if (!vt) return;
    /* A light from above, the front and the side, in car space. */
    const float L[3] = {0.35f, 0.8f, 0.48f};
    for (uint32_t k = 0; k < n; k++) {
        const float *v = vt + 4 * (vt_idx ? vt_idx[k] : k);
        memcpy(pos[k], v, 16);
        float shade = 0.55f;
        if (nm) {
            const float *q = nm + 4 * (nm_idx ? nm_idx[k] : k);
            float d = q[0] * L[0] + q[1] * L[1] + q[2] * L[2];
            shade = 0.35f + 0.6f * (d > 0 ? d : 0);
        }
        uint32_t g = (uint32_t)(shade * 255.0f);
        if (g > 255) g = 255;
        /* The second pass's primitives (translucent) tinted blue, so glass reads as glass. */
        uint32_t bl = pr[0] >> 16 & 1 ? (g + 60 > 255 ? 255 : g + 60) : g;
        col[k] = 0xff000000u | g << 16 | g << 8 | bl;
    }
    if (!standin_ready) {
        EAGL_MaterialInit(&standin);
        EAGL_MaterialAddLayer(&standin, EAGL_CreateLayer(1, 0, 0));   /* coords */
        EAGL_MaterialAddLayer(&standin, EAGL_CreateLayer(6, 0, 0));   /* colours */
        EAGL_MaterialAddLayer(&standin, EAGL_CreateLayer(0, 0, 0));   /* draw */
        EAGL_MaterialBind(&standin);
        standin_ready = true;
    }
    EaglStream s[4];
    memset(s, 0, sizeof s);
    s[0].data = pos;
    s[1].data = col;
    EAGL_MaterialDraw(&standin, s, (int16_t)(pr[0] & 0xffff), (int)n);
}
