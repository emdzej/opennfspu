/* EA real library: the 3D driver's entry points (\real\patch3\pc\set3d.c). The game calls the THRASH
   driver only through a table of pointers (0x6bd850..0x6bd8f8), filled by THRASH_opendll from the driver
   DLL's exports (GetProcAddress of "_THRASH_xxx@n"), and saved / switched as whole sets of up to 8
   drivers. The port's drivers are tables of the same entry points registered under their DLL name
   (THRASH_registerdll: what LoadLibrary would find); the GL driver is "dx7z" (render/thrash_dll.c).
   Ported code calls the driver the original's way: THRASH.setstate(...). */
#pragma once
#include "render/thrash.h"
#include <stdbool.h>
#include <stdint.h>

/* A driver's exports (the "DLL"). An entry may be NULL (not exported). */
typedef struct {
    const char *name;   /* "dx7z", "voodoo2z" */
    const ThrashAbout *(*about)(void);
    void (*clearwindow)(void);
    int (*clip)(int x0, int y0, int x1, int y1);
    void (*drawline)(const void *a, const void *b);
    void (*drawlinemesh)(int count, const void *verts, const int *idx);
    void (*drawlinestrip)(int count, const void *verts, const int *idx);
    void (*drawpoint)(const void *a);
    void (*drawpointmesh)(int count, const void *verts, const int *idx);
    void (*drawquad)(const void *a, const void *b, const void *c, const void *d);
    void (*drawquadmesh)(int count, const void *verts, const int *idx);
    void (*drawtri)(const void *a, const void *b, const void *c);
    void (*drawtrifan)(int count, const void *verts, const int *idx);
    void (*drawtrimesh)(int count, const void *verts, const int *idx);
    void (*drawtristrip)(int count, const void *verts, const int *idx);
    void (*drawsprite)(const void *a, const void *b);
    void (*drawspritemesh)(int count, const void *verts, const int *idx);
    bool (*flushwindow)(void);
    int (*init)(void);
    int (*is)(void);
    ThrashSurface *(*lockwindow)(void);
    void (*pageflip)(void);
    int (*readrect)(int x, int y, int w, int h, void *dst);
    int (*restore)(void);
    int (*selectdisplay)(int display);
    uintptr_t (*setstate)(uint32_t state, uintptr_t value);
    intptr_t (*getstate)(uint32_t state);
    int (*settexture)(ThrashTexture t);
    bool (*setvideomode)(int mode, int backbuffers, int bpp);
    int (*sync)(int how);
    ThrashTexture (*talloc)(int width, int height, int format, int clut, uint32_t mipstage);
    int (*tfree)(ThrashTexture t);
    int (*treset)(void);
    ThrashTexture (*tupdate)(ThrashTexture t, const void *pixels, const uint32_t *clut);
    int (*unlockwindow)(ThrashSurface *s);
    bool (*window)(int window);
    int (*writerect)(int x, int y, int w, int h, const void *src);
} ThrashDll;

/* The pointer table (0x6bd850): the exports above (each slot's address in the comment of set3d.c), the
   alias slots of setstate (0x6bd858, 0x6bd860) and getstate (0x6bd8d8, 0x6bd8e0) the renderer calls,
   and three slots the open never fills (0x6bd854, 0x6bd8c0, 0x6bd8e4; saved and switched with the rest). */
typedef struct {
    ThrashDll fn;
    uintptr_t (*setstate_alias)(uint32_t, uintptr_t), (*setstate_alias2)(uint32_t, uintptr_t);
    intptr_t (*getstate_alias)(uint32_t), (*getstate_alias2)(uint32_t);
    void *unfilled[3];
} ThrashTable;

extern ThrashTable THRASH;            /* 0x6bd850 */
extern bool THRASH_is3dfx;            /* 0x5deb70: the driver's about block says 3Dfx ('XFD3', '2FD3') */

void THRASH_registerdll(const ThrashDll *dll);   /* the port's LoadLibrary search list */
/* Loads the named driver into the table; 0 (after REAL_abort) if it can't, 1 if it did. 0x574d50 */
int THRASH_opendll(const char *name);
void THRASH_selecttable(int set);   /* 0x594a80: the table (and driver) of set 0..7 (others: 0) */
void THRASH_savetable(int set);     /* 0x594cb0 */

/* The GL driver (render/thrash_dll.c; gasm build only). */
extern const ThrashDll THRASH_dx7z;
