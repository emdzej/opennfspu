/* A recording THRASH for the EAGL tests (included by one test file per executable): answers like the
   port's driver where EAGL asks (about: "DX7 3rash", mode 1 = 640x480, a +-2048 guard band) and keeps
   every primitive passed to setstate(0x1b3), so the tests can check the exact vertex and index stream. */
#pragma once
#include "render/thrash.h"
#include <string.h>

typedef struct {
    uint32_t prim, fvf, nverts, nindices;
    ThrashVertex verts[512];
    uint16_t indices[2048];
} FakePrim;

static FakePrim fake_prims[16];
static int fake_nprims;
static int fake_states[0x400];      /* last value per state id (low 16 bits) */
static int fake_clip[4];

static ThrashMode fake_modes[6];
static ThrashAbout fake_about;

const ThrashAbout *THRASH_about(void)
{
    if (!fake_about.size) {
        fake_about.signature = 0x44334437;
        fake_about.size = 0xd0;
        memcpy(fake_about.name, "DX7 3rash", 10);
        fake_modes[1].width = 640;
        fake_modes[1].height = 480;
        fake_modes[1].bpp = 16;
        fake_about.nmodes = 6;
        fake_about.modes = fake_modes;
    }
    return &fake_about;
}

uintptr_t THRASH_setstate(uint32_t state, uintptr_t value)
{
    if (state == THRASH_STATE_GUARDBAND) {
        float *g = (float *)value;
        g[0] = -2048.0f;
        g[1] = 2047.0f;
        g[2] = -2048.0f;
        g[3] = 2047.0f;
        return 1;
    }
    if (state == THRASH_STATE_DRAWPRIMITIVE) {
        const ThrashPrimitive *p = (const ThrashPrimitive *)value;
        if (fake_nprims < 16) {
            FakePrim *r = &fake_prims[fake_nprims++];
            r->prim = p->prim;
            r->fvf = p->fvf;
            r->nverts = p->nverts;
            r->nindices = p->nindices;
            memcpy(r->verts, p->verts, (size_t)p->nverts * sizeof(ThrashVertex));
            if (p->indices) memcpy(r->indices, p->indices, (size_t)p->nindices * sizeof(uint16_t));
        }
        return 1;
    }
    fake_states[state & 0x3ff] = (int)value;
    return 1;
}

intptr_t THRASH_getstate(uint32_t state) { return fake_states[state & 0x3ff]; }

int THRASH_clip(int x0, int y0, int x1, int y1)
{
    fake_clip[0] = x0;
    fake_clip[1] = y0;
    fake_clip[2] = x1;
    fake_clip[3] = y1;
    return 1;
}

bool THRASH_flushwindow(void) { return true; }
void THRASH_clearwindow(void) {}
