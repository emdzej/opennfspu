/* NFS Support/Utility files, see ufile.h. */
#include "support/ufile.h"
#include "real/file.h"
#include "real/shape.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char game_dir[] = "";   /* 0x6af0a4: the game's directory = the data root */

bool UTIL_searchpath(const char *name, char *out)
{
    snprintf(out, 260, "%s%s", game_dir, name);
    return FILE_exists(out) != 0;   /* no second directory (0x6af1a8) in the port */
}

bool UTIL_openfile(const char *name, int mode, int priority, FileHandle *h)
{
    char path[260];
    if (!UTIL_searchpath(name, path)) {
        *h = NULL;
        return false;
    }
    return FILE_openwait(path, mode, priority, h);
}

void *UTIL_loadshape(const char *name, unsigned memflags)
{
    char path[260];
    if (!UTIL_searchpath(name, path)) return NULL;
    return SHAPE_loadfile(path, memflags, 0);
}

/* ---- UFileHandler ---- */

static void UFileHandler_destroy(UFileHandler *f, int free_self)
{
    if (f->open) {
        FILE_closewait(f->handle, f->priority);
        f->open = false;
    }
    if (free_self) free(f);
}

static const UFileHandlerVtbl ufilehandler_vt = { UFileHandler_destroy };

/* The constructor also takes a critical section (0x531f60) and sets an operation callback (0x5a0cf0) for
   asynchronous reads, which the port doesn't have. */
UFileHandler *UFileHandler_new(void)
{
    UFileHandler *f = calloc(1, sizeof *f);
    if (!f) return NULL;
    f->vt = &ufilehandler_vt;
    f->priority = 100;
    return f;
}

bool UFileHandler_open(UFileHandler *f, const char *name, int mode)
{
    f->flag27 = (mode & 1) != 0;
    f->pos = 0;
    f->open = UTIL_openfile(name, mode, f->priority, &f->handle);
    return f->open;
}

int UFileHandler_read(UFileHandler *f, void *dst, int len)
{
    int n = FILE_readwait(f->handle, f->pos, dst, len, f->priority);
    f->pos += (uint32_t)n;
    return n;
}

int UFileHandler_size(UFileHandler *f) { return FILE_sizewait(f->handle, f->priority); }

void UFileHandler_close(UFileHandler *f)
{
    FILE_closewait(f->handle, f->priority);
    f->open = false;
}

void *UFileHandler_mapview(UFileHandler *f) { return NULL; }

void *UTIL_loadfile(const char *name, unsigned memflags)
{
    char path[260];
    return UTIL_searchpath(name, path) ? FILE_loadfile(path, memflags) : NULL;
}

void *UTIL_loadfileabort(const char *name, unsigned memflags)
{
    char path[260];
    return UTIL_searchpath(name, path) ? FILE_loadabort(path, memflags) : NULL;
}
