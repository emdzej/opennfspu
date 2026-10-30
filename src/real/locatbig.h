/* EA real library: BIG archive directories in memory (\real\cmn\locatbig.c). Two archive kinds share
   the code: "BIGF" (the .viv files) and the compact 0xC0FB kind. An archive is located by entry name
   (case-insensitive) or by index; offsets are from the start of the archive. */
#pragma once
#include <stdint.h>
#include <stddef.h>

enum { BIG_NONE = 0, BIG_C0FB = 1, BIG_BIGF = 2 };

int BIG_kind(const uint8_t *big);                        /* 0x5659a0 */
uint32_t BIG_headersize(const uint8_t *big);             /* 0x5659e0 */
uint32_t BIG_count(const uint8_t *big);                  /* 0x565d00 */
/* Entry `name` (if non-NULL) or entry number `index`: its directory record past the two numbers (the
   name), with *offset / *size set; NULL (and 0, 0) if there is none. BIG_locateentry 0x565b70 is the
   public entry (it adds a diagnostic message the release build never prints). */
const char *BIG_findentry(const uint8_t *big, const char *name, int index, uint32_t *offset, uint32_t *size); /* 0x565a30 */
const char *BIG_locateentry(const uint8_t *big, const char *name, int index, uint32_t *offset, uint32_t *size); /* 0x565b70 */

/* Big-endian n-byte read (n = 1..4) 0x56e040 */
uint32_t BIG_getnum(const uint8_t *p, int n);
