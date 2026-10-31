/* Calls of the track and world code into modules not ported yet (docs/internals/porting.md); defined in
   src/game/pending.c. The drawing objects belong to the 3D renderer (0x437700-0x458000), the collision
   objects to the collision module, the rest to the scene and object-definition code. */
#pragma once
#include "game/scene.h"   /* Scene_CountGeom, Track_LoadScenes (ported: scene.c) */
#include "pending.h"
#include "game/road.h"   /* World_LoadSim (ported: road.c) */
#include "game/objdefs.h"   /* WObjectDefs_find, WMidGround_LoadObjectDefs (ported: objdefs.c) */
#include <stdbool.h>
#include <stdint.h>

struct W3DObjectBundle;
void *RDrawArticleBundle_new(struct W3DObjectBundle *b);       /* operator new(0x14) + 0x450b10 */
void *RDrawAnimArticleBundle_new(struct W3DObjectBundle *b);   /* operator new(0x14) + 0x44fe30 */
void *R3DTrack_new(void *desc);                                /* operator new(0x28) + 0x44a520 (the track renderer) */
void R3DTrack_load(void *r3d, void *root);                     /* 0x44a6b0 */
void World_BuildBundles(void *world);                          /* the rest of 0x487940 after the article bundles */
/* The world's scene bundle as the scene's geometry elements fill it (Scene_PlaceGeometry 0x46d120; in
   world.c): */
void WSceneBundle_Reset(void);                                 /* 0x482af0: the slots back to DFLT placeholders */
void WSceneBundle_Add(uint32_t type, const float pos[3], const float mat[9]);   /* 0x482f70 */
void WSceneBundle_Update(bool keep);                           /* 0x482fc0 */
void WSceneBundle_Trigger(uint32_t type, const float pos[3]);  /* 0x482fa0 -> 0x482c80: a pop-up object shown */
