/* Pending calls of EAGL (see pending.h): each logs once and does nothing. */
#include "eagl/eagl_pending.h"
#include "pending.h"

EaglLayer *EAGL_PendingLayerEnvColour(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    PENDING_NOTE(0x5741f0, "EAGL layer 0xc (environment colour)");
    return l->next;
}

EaglLayer *EAGL_PendingLayerNormals(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    PENDING_NOTE(0x574770, "EAGL layer 0xd (normals)");
    return l->next;
}

EaglLayer *EAGL_PendingLayerIndexUV(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    PENDING_NOTE(0x574880, "EAGL layer 0xf (indexed uv)");
    return l->next;
}

void EAGL_ClipOrtho(int i0, int i1, int i2, uint32_t codes, float guardband, const float *verts,
                    int diffuse, int specular, int uv, int uv2, float znear, float zfar, float **out, int *nout)
{
    PENDING_NOTE(0x549610, "EAGL_ClipOrtho");
    *out = NULL;
    *nout = 0;
}


void EAGL_DrawTwoStage(EaglContext *c, const uint32_t *diffuse, const uint32_t *specular, int mode0, int mode1,
                       const int16_t *stamps)
{
    PENDING_NOTE(0x542090, "EAGL_DrawTwoStage (0x542090 / 0x544780 / 0x546bb0)");
    c->flags &= 0xffffff07u;
}
