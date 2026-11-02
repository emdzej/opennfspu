/* Pending calls of the render system (render/pending.h). */
#include "render/render_pending.h"
#include "pending.h"

void Render_PendingAfterViews(void) { PENDING_NOTE(0x44fd70, "Render_AfterViews"); }
