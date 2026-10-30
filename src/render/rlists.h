/* The render system's draw lists (the object at +0x14 of the render system, made by 0x466240): what the
   world's draw objects queue during the visibility pass, drawn and emptied by Render_FlushLists at the end
   of the world draw. Each list is an MSVC std::multimap keyed on an entry's last word; only the order of
   an in-order walk matters, so the port keeps each as an array kept sorted with the multimap's rule (a new
   entry goes after the entries with an equal key). */
#pragma once
#include <stdint.h>

struct RDrawObject;

/* An entry (16 bytes, copied into the map node at +0x10). */
typedef struct {
    void *item;                  /* +0x00: the queued item's record (a 'pr' or 'ef' list) */
    struct RDrawObject *obj;     /* +0x04: the draw object that queued it */
    uint8_t b8;                  /* +0x08 */
    uint16_t index;              /* +0x0a: the object's entry */
    uint32_t key;                /* +0x0c */
} RListEntry;

/* The draw objects' virtual methods (the slots the lists and the world pass call). */
typedef struct {
    void (*dtor)(struct RDrawObject *o, int free_self);                       /* 0 */
    void (*queue)(struct RDrawObject *o, int index, float dist2);             /* 1: visible this frame */
    void (*draw)(struct RDrawObject *o, int index, const RListEntry *e);      /* 2 */
    void (*special)(struct RDrawObject *o, int index, const RListEntry *e);   /* 3: effects */
    void (*s4)(struct RDrawObject *o);                                        /* 4 */
    void (*sphere)(struct RDrawObject *o, int index, float out[4]);           /* 5 */
    uint32_t (*flags)(struct RDrawObject *o, int index);                      /* 6 */
} RDrawVtbl;

typedef struct RDrawObject { const RDrawVtbl *vt; } RDrawObject;

/* The lists by their offset in the original's object: +0x04 ascending, the others descending. */
enum { RLIST_PRIORITY = 1, RLIST_TRANSLUCENT = 2, RLIST_OPAQUE = 3, RLIST_EFFECTS = 4, RLIST_COUNT = 8 };

void RLists_Init(void);                              /* 0x466240 (the lists of the render system) */
void RLists_Insert(int list, const RListEntry *e);   /* 0x4667b0 (+4), 0x466880 (+8), 0x466950 (+0xc), 0x466a20 (+0x10) */
/* Draws the lists in the original's order and empties them. 0x466520 (through 0x4671d0) */
void RLists_Flush(void);
uint32_t RLists_Count(int list);
