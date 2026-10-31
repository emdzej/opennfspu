/* The game's EAGL layer classes (d:\Nfs\Nfs5\game\Common\Render\RCustomLayers.cpp and the R3D and
   Headlights layers, 0x445430-0x44a070, 0x452fc0-0x453b70) and their factory (0x449a90, registered by
   0x449a60 for layer types 0x8001-0x8011 and 0x8100-0x8105).

   Every class has the EAGL layer vtable (eagl.h): execute, destroy, setparam, its dependencies (a table of
   (layer type, id) pairs in the exe), reset, whether it reads a stream (slot 5) and keeps method data
   (slot 6), what it provides (slot 7) and its data size (slot 8). The port keeps those per class in a
   table; the method build (rmaterial.c) counts stream slots with them exactly as the original. */
#pragma once
#include "eagl/eagl.h"
#include <stdbool.h>
#include <stdint.h>

/* Registers the factory with EAGL and fills the identity byte table 0x618e74 (its pointer at 0x619758).
   0x449a60 */
void RLayers_Register(void);
/* The factory: a game layer of type with the entry's id, or NULL for other types. 0x449a90 */
EaglLayer *RLayers_Create(uint32_t type, uint32_t id);

/* Scene state the layers read (the render system's in the original):
   - the camera's eye in world space (the game's render context 0x628130 -> +0x10 -> +0x10c..+0x114), for
     back-face culling;
   - the light environment (0x6115cc, floats by byte offset: per light index i ambient at 0x18 + 16 i
     (a, r, g, b), diffuse at 0x58 + 16 i, the light direction at 0x48, diffuse multipliers at 0x90). */
extern float g_rlayer_eye[3];
extern float *g_rlayer_lights;      /* 0x6115cc */
/* (port) the environment's textures by level: the road slice's for the sky map (the original's +0xd0
   words) and the car's sphere maps for the specular texture (+0xdc, 0x440870). */
extern struct Texture **g_rlayer_envtex;
extern struct Texture **g_rlayer_spectex;
/* (port) the nearest dynamic light's texture (+0x18c), NULL for none. */
extern struct Texture **g_rlayer_lighttex;

/* The vertex cache of TransformIndexCoords (0x613e10: 4 slots of a source, its count, 0x1000 bytes of
   transformed positions and 0x400 of clip codes, an age stamp and the context flags of the transform),
   emptied for every draw by 0x44a090; 0x619761 (bit 9 of a primitive list's type word) bypasses it. */
void RLayers_ResetCache(void);             /* 0x44a090 */
void RLayers_SetNoCache(uint32_t prflags); /* 0x44a0d0: the type word's high half */

/* LayerFullAmbient's once-only fill (0x619760). */
extern bool g_rlayer_fullambient_fill;
