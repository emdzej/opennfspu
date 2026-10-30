/* The game's textures (render module, 0x44cf30-0x44e760; no source file name in the exe): FSH shapes
   made into EA textures (real/texture.h) the way the game's quality settings say, kept in banks.

   Quality: a level (0..3, 0x619770, from the machine: RTex_detectquality) picks a row of the class table
   (0x5ce59c, 4 bytes per class, 6 classes): byte 0 the depth class (0x10: 16-bit textures prefer 1555 or
   565 and 4444 for alpha; else 8888), byte 1 the sizes to halve (bit 0 512, 1 256, 2 128, 3 64, 4 32),
   byte 2 flags (RTEX_Q_*). Class 5 is used for everything when 0x619800 is set.

   A bank (RTexBank) maps (id, name) keys to textures, several per key when they differ in their states
   (copies named "<name>:CLONE" that share the first one's DTEX texture). Upload priorities per texture
   type come from the track's .dtx file (11 counts, one per size and format) or a table per quality. */
#pragma once
#include "real/texture.h"
#include <stdbool.h>
#include <stdint.h>

enum {
    RTEX_Q_MIPS = 0x02,        /* make mip levels (log2(width) - 2) */
    RTEX_Q_SHRINK = 0x04,      /* halve with the shape's own mip level (RTex_ShrinkMip); mipmapping on */
    RTEX_Q_COMMIT = 0x08,      /* Texture_commit after making */
    RTEX_Q_COPY = 0x10,        /* convert from a private copy of the shape */
    RTEX_Q_ALPHACHECK = 0x20,  /* the shape's +8 says whether it has alpha */
};

/* The class row of the current quality. RTex_Quality 0x44deb0 */
const uint8_t *RTex_quality(int texclass);

extern int RTex_level;              /* 0x619770 */
extern bool RTex_forceclass5;       /* 0x619800 */
extern bool RTex_flagsallowed;      /* 0x6197a8: caller texture flags are kept (else 0) */
extern bool RTex_mipsallowed;       /* 0x6197ae: mip levels are made */

/* The machine the quality level is chosen for (the original measures it: CPU_detect, GlobalMemoryStatus,
   the card's memory): CPU clock in Hz (0x5ce908), memory in bytes (0x53c140), video memory (0x619790),
   display depth (0x61978c). */
typedef struct { int32_t cpuhz; int64_t memory; uint32_t vram; int32_t depth; } RTexMachine;
/* The level for a machine: 0..3. 0x44e240 */
int RTex_levelfor(const RTexMachine *m);
/* Sets RTex_level from a machine (3 only with a display deeper than 16 bits). RTex_DetectQuality 0x44de80 */
void RTex_detectquality(const RTexMachine *m);

/* The result of RTex_convertshape (0x1c bytes, on the stack in the original). */
typedef struct {
    bool changed;              /* +0x00 */
    int32_t mips;              /* +0x04: mip levels to make */
    int32_t mipmapped;         /* +0x08 */
    const uint8_t *shape;      /* +0x0c: the shape to make the texture from */
    const uint8_t *original;   /* +0x10 */
    uint8_t *copy;             /* +0x14: to MEM_purge after */
    uint8_t *halved;           /* +0x18: to MEM_purge after */
} RTexConv;

/* Prepares shape for texture class texclass: halved / shrunk / copied as the quality says, and the DTEX
   preference lists for 565 and 8888 sources set for the class. RTex_ConvertShape 0x44d6b0 */
RTexConv *RTex_convertshape(RTexConv *c, const uint8_t *shape, int texclass);
/* The next mip level of a shape with mip levels, as its own shape (mip count one less). 0x44d5e0 */
uint8_t *RTex_shrinkmip(const uint8_t *shape);
/* A texture of a shape for a class: named by name (a 4-character code; below 0x20 the code is "CAR" plus
   that byte), made by manager m. RTex_Make 0x44d8e0 */
Texture *RTex_make(const uint8_t *shape, uint32_t name, int texclass, TexManager *m, uint32_t flags);
/* Loads a shape into an existing texture (prepared for its class). 0x44d870 */
void RTex_reload(Texture *t, const uint8_t *shape, int texclass);
/* DTEX priorities from a table of rows {size (2 bytes), format, mips?, prio pass 0, prio pass 1}. 0x44dd30 */
void RTex_setprios(const uint8_t *rows, int n);
/* The priorities for a track: its .dtx file (GameData\Track\<name>.dtx: "%s%s.dtx" with the track
   directory), else the table of the quality level. 0x44dde0 (0x44dee0 reads the file) */
void RTex_setuppriorities(const char *trackdir, const char *track);
void RTex_defaultprios(void);       /* 0x44dd20 */

/* A bank: an MSVC std::multimap of (id, name) -> texture, as a sorted array with the map's order (keys
   compared id first; equal keys in insertion order). */
typedef struct {
    uint32_t id, name;
    Texture *tex;
    uint8_t flag;              /* +0x1c of the tree node (0) */
    uint8_t managed;           /* +0x1d: released through the manager (else deleted) */
} RTexEntry;

typedef struct {
    TexManager *manager;       /* +0x0 */
    int32_t texclass;          /* +0x4 */
    RTexEntry *entries;        /* +0x8: the tree in the original */
    int32_t n, cap;
} RTexBank;

RTexBank *RTexBank_ctor(RTexBank *b, TexManager *m, int texclass);   /* 0x44cf80 */
void RTexBank_dtor(RTexBank *b);                                      /* 0x44d030 */
void RTexBank_clear(RTexBank *b);                                     /* 0x44d530 */
Texture *RTexBank_find(const RTexBank *b, uint32_t id, uint32_t name); /* 0x44d1f0: the first, or NULL */
/* The texture for (id, name) with states (NULL = any): an existing one with the same states, a clone of
   the key's last one with these states, or a new one made from shape. commit = 0 commits the new
   texture. RTex_Clone 0x44d250 */
Texture *RTexBank_get(RTexBank *b, const uint8_t *shape, uint32_t id, uint32_t name, const TextureStates *states,
                      int unused, bool nocommit, uint32_t flags);
/* Adds a copy of texture t under (id, name). 0x44d480 */
void RTexBank_insert(RTexBank *b, Texture *t, uint32_t id, uint32_t name, const TextureStates *states,
                     int unused, bool nocommit);
/* Every image of an FSH file into the bank (id 0, name = the entry's code). False if it can't be read. 0x44d140 */
bool RTexBank_loadfile(RTexBank *b, const char *fsh, uint32_t flags);
