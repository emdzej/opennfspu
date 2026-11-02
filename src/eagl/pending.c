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
