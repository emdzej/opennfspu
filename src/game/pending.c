/* Pending calls of the game code (see track_pending.h): each logs once and returns a neutral value. */
#include "game/track_pending.h"
#include <stddef.h>

void World_BuildBundles(void *world) { PENDING_NOTE(0x487940, "World_BuildBundles (line bundle, links, edges, Vis walls' users)"); }
