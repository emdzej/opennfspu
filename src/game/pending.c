/* Pending calls of the game code (see track_pending.h): each logs once and returns a neutral value. */
#include "game/track_pending.h"
#include <stddef.h>

void *RDrawArticleBundle_new(struct W3DObjectBundle *b) { PENDING_NOTE(0x450b10, "RDrawArticleBundle ctor"); return NULL; }
void *RDrawAnimArticleBundle_new(struct W3DObjectBundle *b) { PENDING_NOTE(0x44fe30, "RDrawAnimArticleBundle ctor"); return NULL; }
void *PCollideArticleBundle_new(struct W3DObjectBundle *b) { PENDING_NOTE(0x474b20, "PCollideArticleBundle ctor"); return NULL; }
void *WAnim_create(struct W3DObjectBundle *b) { PENDING_NOTE(0x4713f0, "WAnim_create"); return NULL; }
const uint8_t *WObjectDefs_find(uint32_t type) { PENDING_NOTE(0x47ec50, "WObjectDefs_find"); return NULL; }
uint32_t Scene_CountGeom(void) { PENDING_NOTE(0x46d910, "Scene_CountGeom"); return 0; }
void *R3DTrack_new(void *desc) { PENDING_NOTE(0x44a520, "R3DTrack ctor"); return NULL; }
void R3DTrack_load(void *r3d, void *root) { PENDING_NOTE(0x44a6b0, "R3DTrack_load"); }
void Track_LoadScenes(const char *track, int mode, uint32_t arg) { PENDING_NOTE(0x46cc00, "Scene_LoadStarts / start scenes"); }
void WMidGround_LoadObjectDefs(const char *path) { PENDING_NOTE(0x47ea60, "WMidGround_ParseObjectDefs"); }
void *WLineSet_new(float a, int b) { PENDING_NOTE(0x4835e0, "WLineSet ctor"); return NULL; }
void World_BuildBundles(void *world) { PENDING_NOTE(0x487940, "World_BuildBundles (line bundle, links, edges, Vis walls' users)"); }
void World_LoadSim(void *root) { PENDING_NOTE(0x47ff20, "World_LoadSim (SimD/SimT)"); }
void World_LoadJunctions(const char *path) { PENDING_NOTE(0x480f20, "World_LoadJunctions (.jnc)"); }
