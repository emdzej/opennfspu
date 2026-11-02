/* What EAGL calls that isn't ported yet (definitions in pending.c, docs/internals/porting.md). */
#pragma once
#include "eagl/eagl.h"


/* Layers whose code is in the real library's range: environment colour (0x5741f0, factory 0xc),
   normals (0x574770, factory 0xd), indexed uv through a vertex list (0x574880, factory 0xf). */
EaglLayer *EAGL_PendingLayerEnvColour(EaglLayer *l, EaglStream *s0, EaglStream *s1);
EaglLayer *EAGL_PendingLayerNormals(EaglLayer *l, EaglStream *s0, EaglStream *s1);
EaglLayer *EAGL_PendingLayerIndexUV(EaglLayer *l, EaglStream *s0, EaglStream *s1);

