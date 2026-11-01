/* Pending calls of the render system (render/pending.h). */
#include "render/render_pending.h"
#include "pending.h"

void Render_PendingAfterViews(void) { PENDING_NOTE(0x44fd70, "Render_AfterViews"); }
void Render_PendingHud(RCamera *c) { PENDING_NOTE(0x468be0, "the overlay view (0x468be0) and HUD_DrawOverlay (0x4257d0)"); }
