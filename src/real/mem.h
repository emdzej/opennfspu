/* EA real library memory (\real\cmn memory pools, 0x531a10-0x531f60, and the page allocator of NFS
   Support/Utility 0x59eaf0). The original carves blocks out of up to 16 pools with a 16-byte header in
   front of each; the port backs every block with the C heap but keeps the header, since callers read it
   (MEM_size). Flags: low nibble = pool, 0x10 = from the top of the pool, 0x20 = best fit; the pool's own
   flags add 0x100 (name stored), 0x200 (end guard), 0x800 (cleared tail): debugging aids the port omits. */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* The header in front of a block: tag 'MB' used (0x424d), 'BF' free (0x4246); the requested size. */
typedef struct {
    uint16_t tag;        /* +0x0 */
    uint16_t flags;      /* +0x2: the allocation flags (pool in the low nibble) */
    int32_t size;        /* +0x4: bytes asked for */
    uint32_t next, prev; /* +0x8, +0xc: pool links (unused by the port) */
} MemBlock;
enum { MEM_TAG_USED = 0x424d, MEM_TAG_FREE = 0x4246 };

void *MEM_alloc(const char *name, int size, unsigned flags);   /* 0x531a10; NULL if size < 0 or no memory */
int MEM_purge(void *p);                                        /* 0x531d00; NULL is fine; returns 1 */
int MEM_size(const void *p);                                   /* 0x5563a0: the size asked for */
void MEM_copy(void *dst, const void *src, size_t n);           /* 0x532150 (memcpy; MMX variants alike) */
/* Whole pages (zeroed), for files and pools: *size is rounded up to the page size (4096). */
void *MEM_reserve(size_t *size);                               /* 0x59eaf0 */
void MEM_release(void *p);                                     /* 0x59eb40 */
