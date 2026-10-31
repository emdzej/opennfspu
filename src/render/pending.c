/* Pending calls of the render system (render/pending.h). */
#include "render/render_pending.h"
#include "pending.h"

void Render_PendingCameraUpdate(RCamera *c) { PENDING_NOTE(0x461060, "Camera_Track (the camera update)"); }
void Render_PendingSkyLight(void) { PENDING_NOTE(0x459940, "Sky_LightEnvironment"); }
void Render_PendingAfterViews(void) { PENDING_NOTE(0x44fd70, "Render_AfterViews"); }
void Render_PendingParticles(void) { PENDING_NOTE(0x468960, "Particles_Tick"); }
void Render_PendingFrameSetup(void) { PENDING_NOTE(0x44fb80, "Render_FrameSetup"); }
void Render_PendingHud(RCamera *c) { PENDING_NOTE(0x468be0, "the overlay view (0x468be0) and HUD_DrawOverlay (0x4257d0)"); }
