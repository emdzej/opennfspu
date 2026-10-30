/* The world draw of a view (0x468a20, Render_PendingWorld's port): the world's visible objects into the
   render lists and the lists drawn, between the parts of the view that aren't ported (pending notes). */
#include "eagl/eagl.h"
#include "game/track.h"
#include "game/world.h"
#include "pending.h"
#include "render/rdraw.h"
#include "render/render.h"
#include "render/render_pending.h"
#include "render/rlists.h"
#include "render/sky.h"

/* RenderState setters (0x5375e0: +0x0c and state 0x39; 0x5375a0: +0x04 and state 0x28). */
static void rs_set39(RenderState *rs, int v)
{
    rs->state39 = v;
    EAGL_RSSet(0x39, (uint32_t)v);
}

static void rs_set28(RenderState *rs, int v)
{
    rs->state28 = v;   /* +0x04 */
    EAGL_RSSet(0x28, (uint32_t)v);
}

/* The world pass (0x47fb50): the world's per-frame update (0x488c30), the visibility state from the
   camera's frame and view distance (0x47f560), the visible objects (0x47f8a0). */
static void world_pass(RCamera *c)
{
    PENDING_NOTE(0x488c30, "the world's per-frame update (animations, every 8th frame the scene objects)");
    PENDING_NOTE(0x47f560, "the visibility state: the view's ground area, the grid cells around the eye");
    if (!g_world) return;
    /* 0x47f8a0 walks the grid cells the state found (0x47ba50, 0x47f410), every object of a cell once (the
       draw objects' visited marks); the cells aren't ported, so every object of the world is visited. */
    PENDING_NOTE(0x47ba50, "the visibility pass's grid cells (every object is tested instead)");
    RDraw_ClearVisited();
    for (uint32_t i = 0; i < g_world->nobjects; i++) {
        const WObjectRef *o = &g_world->objects[i];
        World_VisitObject(o->bundle, o->index, c->view, c->frame.eye, c->viewdist2, g_world->vis, g_world->vis_off,
                          g_world->nvis);
    }
}

void Render_PendingWorld(RCamera *c)
{
    PENDING_NOTE(0x452590, "Render_World: 0x452590");
    PENDING_NOTE(0x451f70, "Render_World: 0x451f70");
    if (c->v12c != 0) Sky_DrawBefore();   /* (and 0x657db4, 0x619799 clear: replay / front-end flags) */
    if (c->flags[2]) world_pass(c);                            /* +0x11a */
    if (c->flags[3]) PENDING_NOTE(0x534000, "THRASH_setstate_glue (+0x11b)");
    rs_set39(g_render->device->rs, 1);
    rs_set28(g_render->device->rs, 3);
    RLists_Flush();                                            /* 0x4671d0 */
    PENDING_NOTE(0x455130, "Render_World: 0x455130 (particles)");
    PENDING_NOTE(0x43f610, "Render_World: 0x43f610 (the cars)");
    if (c->v12c == 2) Sky_DrawAfter();
    PENDING_NOTE(0x458ef0, "Render_World: 0x458ef0");
}
