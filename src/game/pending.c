/* Pending calls of the game code (see track_pending.h): each logs once and returns a neutral value. */
#include "game/track_pending.h"
#include <stddef.h>

void *RDrawAnimArticleBundle_new(struct W3DObjectBundle *b) { PENDING_NOTE(0x44fe30, "RDrawAnimArticleBundle ctor"); return NULL; }
void *WAnim_create(struct W3DObjectBundle *b) { PENDING_NOTE(0x4713f0, "WAnim_create"); return NULL; }
void World_BuildBundles(void *world) { PENDING_NOTE(0x487940, "World_BuildBundles (line bundle, links, edges, Vis walls' users)"); }
void WSceneBundle_Reset(void) { PENDING_NOTE(0x482af0, "WSceneBundle reset"); }
void WSceneBundle_Add(uint32_t type, const float pos[3], const float mat[9]) { PENDING_NOTE(0x482f70, "WSceneBundle add"); }
void WSceneBundle_Update(bool keep) { PENDING_NOTE(0x482fc0, "WSceneBundle update"); }
