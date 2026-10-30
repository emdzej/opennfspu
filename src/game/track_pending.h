/* Calls of the track and world code into modules not ported yet (docs/internals/porting.md); defined in
   src/game/pending.c. The drawing objects belong to the 3D renderer (0x437700-0x458000), the collision
   objects to the collision module, the rest to the scene and object-definition code. */
#pragma once
#include "pending.h"
#include <stdint.h>

struct W3DObjectBundle;
void *RDrawArticleBundle_new(struct W3DObjectBundle *b);       /* operator new(0x14) + 0x450b10 */
void *RDrawAnimArticleBundle_new(struct W3DObjectBundle *b);   /* operator new(0x14) + 0x44fe30 */
void *PCollideArticleBundle_new(struct W3DObjectBundle *b);    /* operator new(0xc) + 0x474b20 */
void *WAnim_create(struct W3DObjectBundle *b);                 /* the anim bundle's animations, 0x4713f0 per entry */
const uint8_t *WObjectDefs_find(uint32_t type);                /* 0x47ec50: animdefs.txt's definition of a type */
uint32_t Scene_CountGeom(void);                                /* 0x46d910: the scene's geometry elements */
void *R3DTrack_new(void *desc);                                /* operator new(0x28) + 0x44a520 (the track renderer) */
void R3DTrack_load(void *r3d, void *root);                     /* 0x44a6b0 */
void Track_LoadScenes(const char *track, int mode, uint32_t arg); /* Scene_LoadStarts 0x46cc00 + _LoadStartScenes 0x46d550 / _LoadSkillTest 0x46d280 */
void WMidGround_LoadObjectDefs(const char *path);              /* 0x47ed00 + WMidGround_ParseObjectDefs 0x47ea60 */
void *WLineSet_new(float a, int b);                            /* operator new(8) + 0x4835e0 (0x46000000 = 8192.0, 15) */
void World_BuildBundles(void *world);                          /* the rest of 0x487940 after the article bundles */
void World_LoadSim(void *root);                                /* 0x47fbd0 + 0x47ff20: 'SimD' / 'SimT' */
void World_LoadJunctions(const char *path);                    /* 0x480f20: <track>.jnc */
