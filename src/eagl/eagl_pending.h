/* What EAGL calls that isn't ported yet (definitions in pending.c, docs/internals/porting.md). */
#pragma once
#include "eagl/eagl.h"

/* EA real library, texture state (0x535fb0, a method of the render-state object's texture manager
   (*0x69ed0c)[1]): selects tex on stage (DTEX_settexture, then the texture's own states). */
void TEXTURE_select(void *tex, int stage);

/* Layers whose code is in the real library's range: environment colour (0x5741f0, factory 0xc),
   normals (0x574770, factory 0xd), indexed uv through a vertex list (0x574880, factory 0xf). */
EaglLayer *EAGL_PendingLayerEnvColour(EaglLayer *l, EaglStream *s0, EaglStream *s1);
EaglLayer *EAGL_PendingLayerNormals(EaglLayer *l, EaglStream *s0, EaglStream *s1);
EaglLayer *EAGL_PendingLayerIndexUV(EaglLayer *l, EaglStream *s0, EaglStream *s1);

/* The orthographic clipper (0x549610, views with projection 1). */
void EAGL_ClipOrtho(int i0, int i1, int i2, uint32_t codes, float guardband, const float *verts,
                    int diffuse, int specular, int uv, int uv2, float znear, float zfar, float **out, int *nout);
