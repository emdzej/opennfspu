/* NFS Support/Utility files (d:\Nfs\Support\Utility, 0x59d000-0x5a0b80): the search path in front of the
   real library's files, and UFileHandler, the file object the chunk files are read with.

   Search path: once Sys_InitPaths (0x59d400) has run (0x6af2ac set), a name is tried under the game's
   directory (0x6af0a4, from the exe's path) and then under a second directory (0x6af1a8, set when the
   game runs from the CD); if neither has it, the error hook (0x6afb24, the network game's) is told and the
   open fails. The port's data root is the game's directory, so the first prefix is empty and there is no
   second one. */
#pragma once
#include "real/file.h"
#include <stdbool.h>
#include <stdint.h>

/* The name under the search path into out (at least 260 bytes); false if no directory has it. */
bool UTIL_searchpath(const char *name, char *out);
/* FILE_openwait of the name under the search path. Util_OpenFile 0x59ddf0 */
bool UTIL_openfile(const char *name, int mode, int priority, FileHandle *h);
/* SHAPE_loadfile (loadshp.c, real/shape.h) of the name under the search path. Util_LoadShape 0x59e4d0 */
void *UTIL_loadshape(const char *name, unsigned memflags);

/* UFileHandler (vtable 0x5c18ec, RTTI ".?AVUFileHandler@@"): the fields the port keeps, commented with
   their offsets in the original object (0x2c bytes; nothing outside the class reads them). */
typedef struct UFileHandler UFileHandler;
typedef struct {
    void (*destroy)(UFileHandler *self, int free_self);   /* slot 0: UFileHandler_v0 0x5a0b80 */
} UFileHandlerVtbl;
struct UFileHandler {
    const UFileHandlerVtbl *vt;  /* +0x00 */
    int priority;                /* +0x10: 100 */
    FileHandle handle;           /* +0x14 */
    uint32_t pos;                /* +0x18: the read position */
    bool open;                   /* +0x26 */
    bool flag27;                 /* +0x27: bit 0 of the open mode */
};

UFileHandler *UFileHandler_new(void);                                     /* operator new + ctor 0x5a0b30 */
bool UFileHandler_open(UFileHandler *f, const char *name, int mode);     /* 0x5a0bf0 */
int UFileHandler_read(UFileHandler *f, void *dst, int len);              /* 0x5a0c40: from pos, advances it */
int UFileHandler_size(UFileHandler *f);                                  /* 0x5a0c80 */
void UFileHandler_close(UFileHandler *f);                                /* 0x5a0cb0 */
/* The file's memory-mapped view (opened with mode bit 8). The port maps no files: NULL, so the callers
   take their read path (0x5a0ce0 -> 0x568350 -> 0x592240). */
void *UFileHandler_mapview(UFileHandler *f);
