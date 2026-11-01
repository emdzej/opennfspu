/* The world bundles' draw objects (the 3D renderer's side of the track: RDrawArticleBundle and its base,
   0x44fca0..0x4511b0). The visibility pass hands each visible object to its bundle's draw object (slot 1,
   with the squared distance); the object picks the level of detail and queues the primitive lists into the
   render system's lists (rlists.h), which call it back to draw them (slot 2). */
#pragma once
#include "render/rlists.h"
#include <stdint.h>

struct W3DObjectBundle;

/* The draw object of a bundle (0x14 bytes). */
typedef struct RDrawBundle {
    RDrawObject base;                 /* +0x00 */
    struct W3DObjectBundle *bundle;   /* +0x04 */
    uint8_t *visited;                 /* +0x08: per entry, set once queued this frame (the visibility pass) */
    struct RDrawBundle *next;         /* +0x0c: the list of all draw objects (0x61bc4c) */
    struct W3DObjectBundle *bundle2;  /* +0x10: (RDrawArticleBundle: the bundle again) */
} RDrawBundle;

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(RDrawBundle) == 0x14, "the original's layout");
#endif

extern RDrawBundle *g_rdraw_list;     /* 0x61bc4c */
extern uint32_t g_rdraw_key;          /* 0x61975c: the key of the list being drawn (for the layers) */

RDrawBundle *RDrawArticleBundle_ctor(RDrawBundle *d, struct W3DObjectBundle *b);   /* 0x450b10 (base 0x44fca0) */
/* The transformed bundles' (scene, animated): each entry whole, under its own matrix. 0x44fe30 */
RDrawBundle *RDrawAnimArticleBundle_ctor(RDrawBundle *d, struct W3DObjectBundle *b);
/* The line bundle's (the skid marks' cells, game/skid.h): the shared queue (0x44fe90), the cell's strips
   drawn (0x4512f0 -> 0x479ff0), its sphere from the bundle's slot 3 (0x479f50), flags 0xe (0x451300). 0x4512a0 */
RDrawBundle *RDrawLineBundle_ctor(RDrawBundle *d, struct W3DObjectBundle *b);
/* The visited marks of every draw object cleared (the start of a frame's visibility pass). */
void RDraw_ClearVisited(void);
