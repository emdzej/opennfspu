/* \real\patch3\pc\set3d.c, see set3d.h. */
#include "real/set3d.h"
#include "real/real.h"
#include <string.h>
#include <strings.h>

/* Slots: about 874, clearwindow 85c, clip 8f4, drawline 890, drawlinemesh 87c, drawlinestrip 8d4,
   drawpoint 864, drawpointmesh 8a0, drawquad 8e8, drawquadmesh 884, drawtri 868, drawtrifan 8cc,
   drawtrimesh 8ec, drawtristrip 8dc, drawsprite 8b4, drawspritemesh 850, flushwindow 8b0, init 89c,
   is 8d0, lockwindow 8a4, pageflip 888, readrect 8ac, restore 878, selectdisplay 8f8, setstate 8bc,
   getstate 8c4, settexture 894, setvideomode 898, sync 8b8, talloc 8a8, tfree 88c, treset 8c8,
   tupdate 880, unlockwindow 86c, window 8f0, writerect 870 (all 0x6bd8xx). */
ThrashTable THRASH;
bool THRASH_is3dfx;

static const ThrashDll *dlls[8];
static const ThrashDll *library;         /* 0x69e5e8: the loaded driver */
static int table_set;                     /* 0x69e5ec */
static const char *driver_name;           /* 0x6a63f4 */
/* 0x69e5f0: 8 sets of 45 words (the driver, the table, one unused word). */
static struct { const ThrashDll *library; ThrashTable table; } sets[8];

void THRASH_registerdll(const ThrashDll *dll)
{
    for (int i = 0; i < 8; i++)
        if (!dlls[i] || dlls[i] == dll) { dlls[i] = dll; return; }
}

/* What LoadLibrary finds: the original tries two name patterns (0x5ccb84, 0x5c0dcc: the name with and
   without the ".dll"); a registered driver matches either. */
static const ThrashDll *load_library(const char *name)
{
    size_t n = strlen(name);
    if (n > 4 && !strcasecmp(name + n - 4, ".dll")) n -= 4;
    for (int i = 0; i < 8 && dlls[i]; i++)
        if (strlen(dlls[i]->name) == n && !strncasecmp(dlls[i]->name, name, n)) return dlls[i];
    return NULL;
}

static intptr_t no_getstate(uint32_t state) { return 0; }   /* 0x574d30 */
static int no_tfree(ThrashTexture t) { return 0; }          /* 0x574d40 */

int THRASH_opendll(const char *name)
{
    const char *file = "\\real\\patch3\\pc\\set3d.c";
    if (!name) {
        REAL_abort(file, 0x76, "THRASH_opendll - NO LIBRARY SPECIFIED.\n");
        return 0;
    }
    int ok = 0;
    library = load_library(name);
    if (!library) {
        /* The original asks ddraw.dll for the DirectX version first and words the message after it. */
        REAL_abort(file, 0x111, "THRASH_opendll - LOADLIBRARY FAILED ON \"%s\".\n", name);
    } else {
        THRASH.fn = *library;
        THRASH.fn.name = library->name;
        /* The aliases are copied before the missing-entry defaults below, as in the original. */
        THRASH.getstate_alias = THRASH.getstate_alias2 = THRASH.fn.getstate;
        THRASH.setstate_alias = THRASH.setstate_alias2 = THRASH.fn.setstate;
        const ThrashDll *d = &THRASH.fn;
        /* Every export but getstate and tfree must be there. */
        if (!d->about || !d->clearwindow || !d->clip || !d->drawline || !d->drawlinemesh || !d->drawlinestrip ||
            !d->drawpoint || !d->drawpointmesh || !d->drawquad || !d->drawquadmesh || !d->drawtri || !d->drawtrifan ||
            !d->drawtrimesh || !d->drawtristrip || !d->flushwindow || !d->init || !d->is || !d->lockwindow ||
            !d->pageflip || !d->readrect || !d->restore || !d->selectdisplay || !d->setstate || !d->settexture ||
            !d->setvideomode || !d->sync || !d->talloc || !d->treset || !d->tupdate || !d->unlockwindow ||
            !d->window || !d->writerect) {
            library = NULL;
            REAL_abort(file, 0xf8, "THRASH_opendll - not a 3rash DLL");
        } else {
            ok = 1;
            if (!THRASH.fn.getstate) THRASH.fn.getstate = no_getstate;
            if (!THRASH.fn.tfree) THRASH.fn.tfree = no_tfree;
        }
    }
    THRASH_is3dfx = false;
    if (ok) {
        driver_name = name;
        THRASH.fn.setstate(0x1f, 0x6b);
        THRASH_savetable(table_set);
        const ThrashAbout *a = THRASH.fn.about();
        uint32_t magic;
        if (a) {
            memcpy(&magic, a, 4);
            THRASH_is3dfx = magic == 0x33444658 || magic == 0x33444632;
        }
    }
    (void)driver_name;
    return ok;
}

void THRASH_selecttable(int set)
{
    if (set > 7 || set < 0) set = 0;
    table_set = set;
    library = sets[set].library;
    THRASH = sets[set].table;
}

void THRASH_savetable(int set)
{
    if (set > 7 || set < 0) set = 0;
    sets[set].library = library;
    sets[set].table = THRASH;
}
