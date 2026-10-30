/* NFS Support/Utility chunk data, see udata.h. The records are used in place, as the original does: the
   hosts the port runs on are little-endian, like the files. */
#include "support/udata.h"
#include "real/mem.h"
#include "real/pack.h"
#include "real/systask.h"
#include "support/ufile.h"
#include <stdlib.h>
#include <string.h>

/* ---- data addresses in the 32-bit field ---- */

#if UINTPTR_MAX == 0xffffffffu
static uint32_t ptr_store(void *p) { return (uint32_t)(uintptr_t)p; }
static void *ptr_load(uint32_t v) { return (void *)(uintptr_t)v; }
#else
/* 64-bit hosts (the native tests): addresses go into a table, the field holds index + 1. */
static void **ptrs;
static uint32_t nptrs, capptrs;
static uint32_t ptr_store(void *p)
{
    if (!p) return 0;
    for (uint32_t i = nptrs; i > 0; i--)
        if (ptrs[i - 1] == p) return i;
    if (nptrs == capptrs) {
        capptrs = capptrs ? capptrs * 2 : 256;
        ptrs = realloc(ptrs, capptrs * sizeof *ptrs);
    }
    ptrs[nptrs++] = p;
    return nptrs;
}
static void *ptr_load(uint32_t v) { return v ? ptrs[v - 1] : NULL; }
#endif

void *UData_data(const UData *r)
{
    if (r->info & UDATA_RELATIVE) return (uint8_t *)r + r->data;
    return ptr_load(r->data);
}

UData *UData_children(const UData *r)
{
    if (r->info & UDATA_RELATIVE) return (UData *)r + r->data;
    return ptr_load(r->data);
}

/* Binary search over n records from *p for key (raw ids): on a hit *p is the first record with it and
   the key is returned; else *p is the first record above it (or the end) and its id returned (0 at the
   end). 0x59f390 */
static uint32_t udata_lowerbound(UData **p, uint32_t n, uint32_t key)
{
    if (!n) return 0;
    UData *base = *p, *q = base;
    uint32_t m = n, h;
    do {
        h = m >> 1;
        if (q[h].id <= key) {
            if (key <= q[h].id) {   /* equal: back to the first of the run */
                while (h > 0 && q[h - 1].id == key) h--;
                *p = q + h;
                return q[h].id;
            }
            q += h + 1;
            h = m - 1 - h;
        }
        m = h;
    } while (h != 0);
    uint32_t i = (uint32_t)(q - base);
    if (i < n) {
        if (q->id < key) i++;
        *p = base + i;
        if (i < n) return base[i].id;
    } else {
        *p = q;
    }
    return 0;
}

/* The search of 0x59f2e0 / 0x59f4b0 over [first, first + n), end on a miss. */
static UData *udata_search(UData *first, uint32_t n, UData *end, uint32_t id, uint32_t index, uint32_t maxindex)
{
    UData *p = first;
    if (index == 0xffffffffu) {
        if (udata_lowerbound(&p, n, id) != id) return end;
    } else {
        uint32_t got = udata_lowerbound(&p, n, (id & 0xffff0000u) | index);
        if (((got & 0xffff2020u) | 0x2020) != id || maxindex < (got & 0xffff)) return end;
    }
    return p;
}

UData *UData_findarticle(const UData *r, uint32_t id, uint32_t index, uint32_t maxindex)
{
    UData *first = UData_children(r);
    if (!first) return NULL;
    return udata_search(first, UData_articles(r), first + UData_articles(r), id, index, maxindex);
}

UData *UData_finditem(const UData *r, uint32_t id, uint32_t index, uint32_t maxindex)
{
    if (!UData_children(r)) return NULL;
    UData *items = UData_items(r);
    return udata_search(items, r->count, items + r->count, id, index, maxindex);
}

UData *UData_find(const UData *r, uint32_t id)
{
    if (!UData_children(r)) return NULL;
    UData *p = UData_items(r), *end = UData_itemsend(r);
    if (!(r->info & UDATA_SORTED)) {
        while (p < end && p->id != id) p++;
        return p;
    }
    return udata_lowerbound(&p, r->count, id) == id ? p : end;
}

static int udata_count(const UData *p, const UData *end, uint32_t key)
{
    int n = 0;
    for (; p != end; p++)
        if (UData_key(p) == key) n++;
    return n;
}

int UData_countarticles(const UData *r, uint32_t key) { return udata_count(UData_children(r), UData_items(r), key); }
int UData_countitems(const UData *r, uint32_t key) { return udata_count(UData_items(r), UData_itemsend(r), key); }

const char *UData_name(const UData *r)
{
    UData *e = UData_find(r, 0x4e616d65);   /* 'Name' */
    return e != UData_itemsend(r) ? UData_data(e) : NULL;
}

void *UData_dz(const UData *r, uint32_t index)
{
    UData *e = UData_find(r, index == 0xffffffffu ? 0x647a2020u : (0x647a0000u | index));   /* 'dz' */
    return e != UData_itemsend(r) ? UData_data(e) : NULL;
}

/* Copies come from the object heap (0x59ea60, tag "UData") and go back with MEM_free (0x59ea80). */
static void *udata_alloc(uint32_t size) { return malloc(size ? size : 1); }

void UData_init(UData *r, const void *data, uint32_t size, uint32_t count, uint32_t id, uint32_t index)
{
    if (index != 0xffffffffu) id = (id & 0xffff0000u) | index;
    r->id = id;
    r->count = count;
    r->info = (size & 0xffffff) << 8 | (index != 0xffffffffu) | UDATA_OWNED;
    void *copy = udata_alloc(size & 0xffffff);
    memcpy(copy, data, size & 0xffffff);
    r->data = ptr_store(copy);
}

/* A copy of an owned record owns a copy of the data; of any other record, it points at the same data
   (absolute, so the copy can live anywhere). Flag bits 0 and 2-7 and the size come from the source. */
void UData_copy(UData *r, const UData *src)
{
    r->id = src->id;
    r->info = (src->info & ~(uint32_t)UDATA_RELATIVE);
    r->count = src->count;
    if (r->info & UDATA_OWNED) {
        void *copy = udata_alloc(UData_size(src));
        memcpy(copy, ptr_load(src->data), UData_size(src));
        r->data = ptr_store(copy);
    } else {
        r->data = ptr_store(UData_data(src));
    }
}

/* Keeps its block when the new data fits in it with at most 256 bytes to spare. */
void UData_set(UData *r, const void *data, uint32_t size, uint32_t count)
{
    uint32_t have = UData_size(r);
    if (have < size || have - size > 0x100) {
        if ((r->info & UDATA_OWNED) && r->data) free(ptr_load(r->data));
        r->info |= UDATA_OWNED;
        r->data = ptr_store(udata_alloc(size));
    }
    memcpy(ptr_load(r->data), data, size);
    r->count = count;
    r->info = (r->info & 0xff) | size << 8;
}

void UData_dtor(UData *r)
{
    if ((r->info & UDATA_OWNED) && r->data) free(ptr_load(r->data));
}

/* ---- data files ---- */

void UDataFile_ctor(UDataFile *f) { memset(f, 0, sizeof *f); }

static void udatafile_drop(UDataFile *f)
{
    if (f->handler) {
        f->handler->vt->destroy(f->handler, 1);
        f->handler = NULL;
    } else {
        MEM_release(f->data);
    }
    f->data = NULL;
    f->size = 0;
}

void UDataFile_dtor(UDataFile *f)
{
    if (f->data) udatafile_drop(f);
}

void UDataFile_free(UDataFile *f) { udatafile_drop(f); }

static void udatafile_dropthandler(UDataFile *f)
{
    if (f->handler) f->handler->vt->destroy(f->handler, 1);
    f->handler = NULL;
}

/* Reading: the file's mapped view if there is one (kept with its handler), else the file read into
   reserved pages. Packed data is unpacked into new reserved pages (4 bytes more than it unpacks to) and
   the read copy dropped; while it works through a file of a page or more, the original calls the sync
   tasks every 16 pages. */
bool UDataFile_load(UDataFile *f, const char *name, bool maponly)
{
    if (f->data) {
        udatafile_drop(f);
    }
    f->handler = UFileHandler_new();
    f->reserved = 0;
    uint8_t *buf = NULL;
    int size = 0;
    bool mapped = UFileHandler_open(f->handler, name, (maponly ? 0 : 1) | 8);
    if (mapped) {
        buf = UFileHandler_mapview(f->handler);
        size = UFileHandler_size(f->handler);
        if (!size) {
            udatafile_dropthandler(f);
            return false;
        }
        if (!buf) {
            UFileHandler_close(f->handler);
            mapped = false;
        }
    }
    if (!mapped) {
        if (maponly || !UFileHandler_open(f->handler, name, 1)) {
            udatafile_dropthandler(f);
            return false;
        }
        size = UFileHandler_size(f->handler);
        size_t r = (size_t)size;
        buf = MEM_reserve(&r);
        f->reserved = (uint32_t)r;
        int got = UFileHandler_read(f->handler, buf, size);
        udatafile_dropthandler(f);
        if (!got) {
            MEM_release(buf);
            return false;
        }
    }
    uint32_t unpacked = UNPACK_size(buf);
    f->size = unpacked;
    if (!unpacked) {
        f->size = (uint32_t)size;
        f->data = buf;
        return true;
    }
    if (maponly) {
        udatafile_dropthandler(f);
        return false;
    }
    size_t r = unpacked + 4;
    uint8_t *out = MEM_reserve(&r);
    f->reserved = (uint32_t)r;
    enum { PAGE = 4096 };
    if (size >= PAGE) {   /* the original touches each page of the packed data here */
        int pages = (size + PAGE - 1) / PAGE;
        for (int i = 0; i < pages; i++)
            if ((i & 15) == 0) SYNCTASK_run(0);
    }
    bool ok = UNPACK_dispatch(buf, out) != 0;
    if (f->handler) udatafile_dropthandler(f);
    else MEM_release(buf);
    if (ok) {
        f->data = out;
        return true;
    }
    MEM_release(out);
    return false;
}

UData *UDataFile_root(UDataFile *f)
{
    f->done = true;   /* the original makes the pages read-only here (0x5aedf0) when it read them itself */
    return f->data;
}
