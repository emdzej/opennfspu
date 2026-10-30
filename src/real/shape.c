/* EA real library shapes, see shape.h. */
#include "real/shape.h"
#include "exe.h"
#include "real/file.h"
#include "real/locatbig.h"
#include "real/mem.h"
#include "real/real.h"
#include <stdio.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

void *SHAPE_loadfile(const char *name, unsigned memflags, int abort)
{
    char path[128];
    snprintf(path, sizeof path, "%s", name);
    /* An extension is looked for back to the first '.', ':', '/' or '\\' from the end. */
    size_t n = strlen(path);
    const char *p = n ? path + n - 1 : path;
    while (p > path && *p != '.' && *p != ':' && *p != '/' && *p != '\\') p--;
    if (*p != '.') {
        const char *ext = exe_str(exe_u32(0x5e663c));   /* the default extension */
        strncat(path, ext, sizeof path - strlen(path) - 1);
    }
    uint8_t *shp = FILE_loadpackfile(path, memflags);
    if (!shp) {
        if (abort) REAL_abort("\\real\\cmn\\loadshp.c", 0x7c, "SHAPE_loadfile - UNABLE TO LOAD FILE %s\n", path);
        return NULL;
    }
    uint32_t magic = BIG_getnum(shp, 4);
    if (magic != 0x53485049 && magic != 0x53485053) {   /* 'SHPI', 'SHPS' */
        if (abort)
            REAL_abort("\\real\\cmn\\loadshp.c", 0x74,
                       "SHAPE_loadfile - '%s' UNRECOGNIZED SHAPEFILE SIGNATURE.\nFOUND '%4.4s'. REQUIRES 'SHPI'.  "
                       "USE GX TO CONVERT?  ie GX %s -fsh\n", path, (const char *)shp, path);
        MEM_purge(shp);
        return NULL;
    }
    return shp;
}

uint32_t SHAPE_count(const void *shp) { return rd32((const uint8_t *)shp + 8); }

uint32_t SHAPE_entryname(const void *shp, int i)
{
    return i < (int)SHAPE_count(shp) ? rd32((const uint8_t *)shp + 0x10 + 8 * i) : 0;
}

const uint8_t *SHAPE_entry(const void *shp, int i)
{
    return (const uint8_t *)shp + rd32((const uint8_t *)shp + 0x14 + 8 * i);
}

int SHAPE_typevalid(const uint8_t *rec)
{
    const uint8_t *table = exe_data(0x5de890, 0x80);
    return table ? table[rec[0] & 0x7f] : 0;
}

const uint8_t *SHAPE_clut(const uint8_t *rec)
{
    if (!rec) return NULL;
    if (!SHAPE_typevalid(rec))
        REAL_abort("\\real\\pc\\shpclut.c", 0x45, "SHAPE_clut - INVALID SHAPE TYPE ($%02x)\n", rec[0]);
    for (;;) {
        uint32_t h = rd32(rec);
        switch (h & 0xff) {
        case 0x22: case 0x23: case 0x24: case 0x29: case 0x2a: case 0x2c: case 0x2d: case 0x2e:
            return rec;
        }
        if (!(h & 0xffffff00u)) return NULL;
        rec += (int32_t)h >> 8;   /* signed, as the original shifts it */
    }
}
