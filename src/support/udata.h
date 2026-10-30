/* NFS Support/Utility chunk data (class CUData, 0x59f020-0x59f940): the tree of 16-byte records that
   .crp files (cars, tracks) and other game data are made of (docs/formats.md, CRP), and the loader of
   such files (read whole, unpacked if packed).

   A record (UData): id (a four-character code, read as a little-endian u32 of the bytes as stored), info,
   count, data. info bit 0: the id carries a 16-bit index in its low half (the code is two characters,
   e.g. 'pr' + index; comparisons normalise it to 'pr  '); bit 1: data is relative to the record; bit 2:
   the data is a copy this record owns; bit 3: the items are sorted by id. A record with children (an
   "article") has info >> 5 sub-articles followed by count items, starting data records (16 bytes each)
   from the record when relative; an item has info >> 8 bytes of data (count elements), data bytes from
   the record when relative.

   Callers in the original read the fields inline; in the port they go through UData_data and
   UData_children, which also resolve the non-relative records (copies made at run time) on hosts whose
   pointers don't fit the 32-bit field. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t id;     /* +0x0 */
    uint32_t info;   /* +0x4 */
    uint32_t count;  /* +0x8 */
    uint32_t data;   /* +0xc: offset (bit 1) or the data's address */
} UData;
_Static_assert(sizeof(UData) == 16, "the files' record size");

enum { UDATA_INDEXED = 1, UDATA_RELATIVE = 2, UDATA_OWNED = 4, UDATA_SORTED = 8 };

static inline uint32_t UData_fourcc(char a, char b, char c, char d)
{
    return (uint32_t)(uint8_t)a << 24 | (uint32_t)(uint8_t)b << 16 | (uint32_t)(uint8_t)c << 8 | (uint8_t)d;
}
/* The id as comparisons see it: indexed ids become their two characters + "  ". */
static inline uint32_t UData_key(const UData *r) { return r->info & UDATA_INDEXED ? (r->id & 0xffff2020u) | 0x2020 : r->id; }
static inline unsigned UData_articles(const UData *r) { return r->info >> 5; }
static inline unsigned UData_size(const UData *r) { return r->info >> 8; }

void *UData_data(const UData *r);            /* an item's data (inline in the original) */
UData *UData_children(const UData *r);       /* 0x59f620: the first sub-article; items follow them */
static inline UData *UData_items(const UData *r) { return UData_children(r) + UData_articles(r); }
static inline UData *UData_itemsend(const UData *r) { return UData_items(r) + r->count; }

/* Searches. index = -1: the id exactly; else the first record with that two-character code and an index
   from index to maxindex. A miss returns the end of the range searched (UData_items for articles,
   UData_itemsend for items); NULL if the record has no children. */
UData *UData_findarticle(const UData *r, uint32_t id, uint32_t index, uint32_t maxindex);   /* 0x59f2e0 */
UData *UData_finditem(const UData *r, uint32_t id, uint32_t index, uint32_t maxindex);      /* 0x59f4b0 */
/* The item with exactly this raw id (sorted items: binary search); the end of the items on a miss. */
UData *UData_find(const UData *r, uint32_t id);                                             /* 0x59f570 */
int UData_countarticles(const UData *r, uint32_t key);                                      /* 0x59f280 */
int UData_countitems(const UData *r, uint32_t key);                                         /* 0x59f440 */
/* (The original's two take a {record, end of its items} pair; the end is UData_itemsend.) */
const char *UData_name(const UData *r);       /* 0x59f020: the 'Name' item's data, NULL without one */
void *UData_dz(const UData *r, uint32_t index); /* 0x59f050: the 'dz' item (index -1: 'dz  ') */

/* Records made at run time own a copy of their data. */
void UData_init(UData *r, const void *data, uint32_t size, uint32_t count, uint32_t id, uint32_t index); /* 0x59f150 */
void UData_copy(UData *r, const UData *src);                                                   /* 0x59f0a0 */
void UData_set(UData *r, const void *data, uint32_t size, uint32_t count);                     /* 0x59f1f0 */
void UData_dtor(UData *r);                                                                      /* 0x59f1d0 */

/* A loaded data file: the file's bytes (unpacked), its root record at the start. */
typedef struct {
    struct UFileHandler *handler;  /* +0x0: kept while the data is the file's mapped view */
    void *data;                    /* +0x4 */
    uint32_t size;                 /* +0x8 */
    uint32_t reserved;             /* +0xc: the pages reserved for a read */
    bool done;                     /* +0x10: handed out (the original makes its pages read-only) */
} UDataFile;

void UDataFile_ctor(UDataFile *f);                          /* 0x59f630 */
void UDataFile_dtor(UDataFile *f);                          /* 0x59f650 */
/* Loads the file (the name as the game gives it, under the search path); unpacks it into reserved pages
   if it is packed. maponly: only a mapped view, never unpacked (no caller sets it). */
bool UDataFile_load(UDataFile *f, const char *name, bool maponly);   /* 0x59f6a0 */
void UDataFile_free(UDataFile *f);                          /* 0x59f900 */
UData *UDataFile_root(UDataFile *f);                        /* 0x59f940 (+ 0x59f270, the cast to the root) */
