/* The track's renderer objects as Track_LoadWorld and the bundle loads make them: the track's material
   set (operator new(0x28) + 0x44a520 with the render system's texture manager, class 0; built by 0x44a6b0
   from the root, with the track directory for its shape files) and the article bundles' draw objects
   (operator new(0x14) + 0x450b10). */
#include "game/track.h"
#include "game/track_pending.h"
#include "render/rdraw.h"
#include "render/render.h"
#include "render/rmaterial.h"
#include <stdlib.h>

void *RDrawArticleBundle_new(struct W3DObjectBundle *b)
{
    RDrawBundle *d = calloc(1, sizeof *d);
    return d ? RDrawArticleBundle_ctor(d, b) : NULL;
}

void *R3DTrack_new(void *desc)
{
    R3DMaterialSet *ms = calloc(1, sizeof *ms);
    return ms ? R3DMaterialSet_ctor(ms, g_render ? g_render->textures : NULL, 0) : NULL;
}

void R3DTrack_load(void *r3d, void *root)
{
    if (r3d) R3DMaterialSet_Build(r3d, root, g_track_dir);
}
