/* EA real library files, see file.h. */
#include "real/file.h"
#include "real/locatbig.h"
#include "real/mem.h"
#include "real/pack.h"
#include "real/real.h"
#include "vfs.h"
#include <ctype.h>
#include <stdlib.h>

#define HLSFILE "\\real\\cmn\\hlsfile.c"

static const int file_priority = 0;   /* 0x5df9d4: the priority hlsfile.c's operations run at */

/* The drive letter's device (A: = 1); 0 without one, which is every path of the port. FILE_device
   0x568310 (a '|' in the name means device 0 as well) and 0x591510. */
int FILE_device(const char *name)
{
    for (const char *p = name; *p; p++)
        if (*p == '|') return 0;
    if (isalpha((unsigned char)name[0]) && name[1] == ':') return toupper((unsigned char)name[0]) - 0x40;
    return 0;
}

bool FILE_openwait(const char *name, int mode, int priority, FileHandle *h)
{
    (void)priority;
    /* Bit 0 = read; the game opens its data for reading only (1, or 9: read through a mapped view). */
    *h = mode & 1 ? vfs_open(name) : NULL;
    return *h != NULL;
}

/* The original reads 8 KB at a time until the length is done or a read comes back short (0x533990 with
   the read operation 0x567f90); one positional read gives the same bytes and count. */
int FILE_readwait(FileHandle h, uint32_t offset, void *dst, int len, int priority)
{
    (void)priority;
    if (!h || len <= 0) return 0;
    int64_t n = vfs_read_at(h, offset, dst, (size_t)len);
    return n < 0 ? 0 : (int)n;
}

int FILE_closewait(FileHandle h, int priority)
{
    (void)priority;
    if (!h) return 0;
    vfs_close(h);
    return 1;
}

int FILE_sizewait(FileHandle h, int priority)
{
    (void)priority;
    return h ? (int)vfs_file_size(h) : 0;
}

int FILE_existswait(const char *name, int priority)
{
    (void)priority;
    return vfs_exists(name);
}

int FILE_exists(const char *name) { return FILE_existswait(name, file_priority); }

/* ---- hlsfile.c: each takes the arguments block the public functions fill (name, address, size or
   memory flags, abort) and runs as an atomic operation of the file's device ---- */

typedef struct {
    const char *name;   /* +0x0 */
    void *addr;         /* +0x4: FILE_loadat's destination */
    int size;           /* +0x8: FILE_loadat's size, the others' memory flags */
    int abort;          /* +0xc: REAL_abort when the file can't be read */
} FileArgs;

/* FILE_size 0x569da0 */
static int FILE_size(int priority, const FileArgs *a)
{
    FileHandle h;
    if (FILE_openwait(a->name, 1, priority, &h)) {
        int size = FILE_sizewait(h, priority - 1);
        FILE_closewait(h, priority - 1);
        return size;
    }
    if (a->abort) REAL_abort(HLSFILE, 0x74, "FILE_size - unable to open file %s\n", a->name);
    return 0;
}

/* FILE_load 0x569ea0. The original then hands the block to an optional post-load hook (0x6af088), which
   the game never sets. */
static void *FILE_load(int priority, const FileArgs *a)
{
    FileHandle h;
    if (!FILE_openwait(a->name, 1, priority, &h)) {
        if (a->abort) REAL_abort(HLSFILE, 0xda, "FILE_load - unable to open file %s\n", a->name);
        return NULL;
    }
    int size = FILE_sizewait(h, priority - 1);
    void *p = MEM_alloc(a->name, size, (unsigned)a->size);
    if (!p) {
        if (a->abort) REAL_abort(HLSFILE, 0xd2, "FILE_load - unable to reserve memory for file %s\n", a->name);
        FILE_closewait(h, priority - 1);
        return NULL;
    }
    FILE_readwait(h, 0, p, size, priority - 1);
    FILE_closewait(h, priority - 1);
    return p;
}

/* FILE_loadat 0x56a050 */
static int FILE_loadat(int priority, const FileArgs *a)
{
    int size = a->size < 1 ? 0x7fffffff : a->size;
    FileHandle h;
    if (FILE_openwait(a->name, 1, priority, &h)) {
        int n = FILE_readwait(h, 0, a->addr, size, priority - 1);
        FILE_closewait(h, priority - 1);
        return n;
    }
    if (a->abort) REAL_abort(HLSFILE, 0x12f, "FILE_loadat - unable to open file %s\n", a->name);
    return 0;
}

/* FILE_loadbigheader 0x56a2c0: reads the first 0xa90 bytes, checks the archive kind, and if the directory
   is longer, moves them into a block of the directory's size and reads the rest. A directory shorter
   than 0xa90 bytes stays in the 0xa90-byte block. */
static void *FILE_loadbigheader(int priority, const FileArgs *a)
{
    enum { FIRST = 0xa90 };
    FileHandle h;
    if (!FILE_openwait(a->name, 1, priority, &h)) {
        if (a->abort) REAL_abort(HLSFILE, 0x21d, "FILE_loadbigheader - unable to open file %s\n", a->name);
        return NULL;
    }
    uint8_t *first = MEM_alloc(a->name, FIRST, (unsigned)a->size);
    if (!first) {
        if (a->abort) REAL_abort(HLSFILE, 0x215, "FILE_loadbigheader - unable to reserve memory for file %s\n", a->name);
        FILE_closewait(h, priority - 1);
        return NULL;
    }
    FILE_readwait(h, 0, first, FIRST, priority - 1);
    if (BIG_kind(first) == BIG_NONE) {
        if (a->abort) REAL_abort(HLSFILE, 0x1f0, "FILE_loadbigheader - invalid big file header in file %s\n", a->name);
        MEM_purge(first);
        FILE_closewait(h, priority - 1);
        return NULL;
    }
    uint32_t size = BIG_headersize(first);
    uint8_t *dir = first;
    if (size > FIRST) {
        dir = MEM_alloc(a->name, (int)size, (unsigned)a->size);
        if (!dir) {
            if (a->abort) REAL_abort(HLSFILE, 0x208, "FILE_loadbigheader - unable to reserve memory for file %s\n", a->name);
            MEM_purge(first);
            FILE_closewait(h, priority - 1);
            return NULL;
        }
        MEM_copy(dir, first, FIRST);
        MEM_purge(first);
        FILE_readwait(h, FIRST, dir + FIRST, (int)(size - FIRST), priority - 1);
    }
    FILE_closewait(h, priority - 1);
    return dir;
}

/* The public functions: fill the arguments and run the operation on the file's device. */

int FILE_getsize(const char *name)
{
    FileArgs a = {name, NULL, 0, 0};
    (void)FILE_device(name);
    return FILE_size(file_priority, &a);
}

void *FILE_loadabort(const char *name, unsigned memflags)
{
    FileArgs a = {name, NULL, (int)memflags, 1};
    return FILE_load(file_priority, &a);
}

void *FILE_loadfile(const char *name, unsigned memflags)
{
    FileArgs a = {name, NULL, (int)memflags, 0};
    return FILE_load(file_priority, &a);
}

int FILE_loadatfile(const char *name, void *dst, int size)
{
    FileArgs a = {name, dst, size, 0};
    return FILE_loadat(file_priority, &a);
}

void *FILE_loadbigheaderabort(const char *name, unsigned memflags)
{
    FileArgs a = {name, NULL, (int)memflags, 1};
    return FILE_loadbigheader(file_priority, &a);
}

void *FILE_loadbigheaderfile(const char *name, unsigned memflags)
{
    FileArgs a = {name, NULL, (int)memflags, 0};
    return FILE_loadbigheader(file_priority, &a);
}

/* ---- loadpack.c ---- */

/* The packed file goes into a temporary block (allocated from the other end of the pool: memflags ^ 0x10),
   the unpacked data into a block with the caller's flags. A file that isn't packed is returned as read.
   A file under 5 bytes: the original looks at the block's padding for a header; the port takes it as
   not packed (no file of the game is that short). */
void *FILE_loadpackfile(const char *name, unsigned memflags)
{
    uint8_t *raw = FILE_loadfile(name, memflags);
    if (!raw) return NULL;
    int size = MEM_size(raw);
    uint32_t unpacked = size >= 5 ? UNPACK_size(raw) : 0;
    if (!size || !unpacked) return raw;
    uint8_t *packed = MEM_alloc(name, size, memflags ^ 0x10);
    if (!packed) {
        MEM_purge(raw);
        return NULL;
    }
    MEM_copy(packed, raw, (size_t)size);
    MEM_purge(raw);
    uint8_t *out = MEM_alloc(name, (int)unpacked, memflags);
    if (out && !UNPACK_dispatch(packed, out)) {
        MEM_purge(out);
        out = NULL;
    }
    MEM_purge(packed);
    return out;
}

void *FILE_loadpack(const char *name, unsigned memflags)
{
    void *p = FILE_loadpackfile(name, memflags);
    if (!p) REAL_abort("\\real\\cmn\\loadpack.c", 0x92, "FILE_loadpack - unable to unpack file %s\n", name);
    return p;
}
