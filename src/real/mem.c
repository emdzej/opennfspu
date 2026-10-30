/* EA real library memory, see mem.h. */
#include "real/mem.h"
#include <stdlib.h>
#include <string.h>

/* The original rounds every block up to at least 8 bytes and to the pool's alignment; callers only see
   the pointer and MEM_size. */
void *MEM_alloc(const char *name, int size, unsigned flags)
{
    (void)name;
    if (size < 0) return NULL;
    int body = size < 8 ? 8 : size;
    /* Zeroed: the original's blocks hold whatever the heap held, and some of it reaches the output (the
       parts of a car's texture pages nothing draws over); zero makes every run and every data source give
       the same bytes. */
    MemBlock *b = calloc(1, sizeof *b + (size_t)body);
    if (!b) return NULL;   /* the original calls its out-of-memory hook (0x69cb00) and retries */
    b->tag = MEM_TAG_USED;
    b->flags = (uint16_t)flags;
    b->size = size;
    b->next = b->prev = 0;
    return b + 1;
}

int MEM_purge(void *p)
{
    if (p) {
        MemBlock *b = (MemBlock *)p - 1;
        b->tag = MEM_TAG_FREE;
        free(b);
    }
    return 1;
}

int MEM_size(const void *p) { return ((const MemBlock *)p - 1)->size; }

void MEM_copy(void *dst, const void *src, size_t n) { memmove(dst, src, n); }

void *MEM_reserve(size_t *size)
{
    enum { PAGE = 4096 };   /* GetSystemInfo's page size on the original's platforms */
    *size = (*size + PAGE - 1) / PAGE * PAGE;
    return calloc(1, *size ? *size : PAGE);   /* VirtualAlloc commits zeroed pages */
}

void MEM_release(void *p) { free(p); }
