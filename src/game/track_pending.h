/* Calls of the track and world code into modules not ported yet (docs/internals/porting.md); defined in
   src/game/pending.c. The drawing objects belong to the 3D renderer (0x437700-0x458000), the collision
   objects to the collision module, the rest to the scene and object-definition code. */
#pragma once
#include "game/scene.h"   /* Scene_CountGeom, Track_LoadScenes (ported: scene.c) */
#include "pending.h"
#include <stdbool.h>
#include <stdint.h>

struct W3DObjectBundle;
void *RDrawArticleBundle_new(struct W3DObjectBundle *b);       /* operator new(0x14) + 0x450b10 */
void *RDrawAnimArticleBundle_new(struct W3DObjectBundle *b);   /* operator new(0x14) + 0x44fe30 */
void *PCollideArticleBundle_new(struct W3DObjectBundle *b);    /* operator new(0xc) + 0x474b20 */
void *WAnim_create(struct W3DObjectBundle *b);                 /* the anim bundle's animations, 0x4713f0 per entry */
const uint8_t *WObjectDefs_find(uint32_t type);                /* 0x47ec50: animdefs.txt's definition of a type */
void *R3DTrack_new(void *desc);                                /* operator new(0x28) + 0x44a520 (the track renderer) */
void R3DTrack_load(void *r3d, void *root);                     /* 0x44a6b0 */
void WMidGround_LoadObjectDefs(const char *path);              /* 0x47ed00 + WMidGround_ParseObjectDefs 0x47ea60 */
void *WLineSet_new(float a, int b);                            /* operator new(8) + 0x4835e0 (0x46000000 = 8192.0, 15) */
void World_BuildBundles(void *world);                          /* the rest of 0x487940 after the article bundles */
void World_LoadSim(void *root);                                /* 0x47fbd0 + 0x47ff20: 'SimD' / 'SimT' */
void World_LoadJunctions(const char *path);                    /* 0x480f20: <track>.jnc */
/* The world's scene bundle as the scene's geometry elements fill it (Scene_PlaceGeometry 0x46d120): */
void WSceneBundle_Reset(void);                                 /* 0x482af0: the slots back to DFLT placeholders */
void WSceneBundle_Add(uint32_t type, const float pos[3], const float mat[9]);   /* 0x482f70 */
void WSceneBundle_Update(bool keep);                           /* 0x482fc0 */
