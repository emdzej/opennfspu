/* EA real library files: the device layer's synchronous wrappers (0x533900-0x533c70), hlsfile.c (whole
   files, file parts, BIG headers) and loadpack.c (load + unpack), over the port's file layer (vfs.h).

   The original (\real\pc\nfile.c) runs file operations on up to 32 devices (one per drive letter, device
   = letter - '@', 0 without one; FILE_device 0x568310), each with a thread that works a queue of
   operations (FILE_queueop 0x567680, FILE_devicethread 0x5679b0); callers either wait for an operation
   (FILE_*wait below) or get a callback (FILESYS_callbackop 0x567540). hlsfile.c's functions run as
   "atomic" operations on the file's device at a priority (FILESYS_atomic 0x5681d0, priority 0x5df9d4).
   The port has one device (the mounted data) and completes every operation at once, in the caller,
   which is where the original's waiting callers see the result; the priorities are accepted and
   ignored. Paths are the game's (relative to the data root; '\\' or '/'). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct VfsFile *FileHandle;

int FILE_device(const char *name);                                            /* 0x568310 */
bool FILE_openwait(const char *name, int mode, int priority, FileHandle *h);  /* 0x533900 (mode 1 = read) */
int FILE_readwait(FileHandle h, uint32_t offset, void *dst, int len, int priority); /* 0x533960 */
int FILE_closewait(FileHandle h, int priority);                               /* 0x533b10 */
int FILE_sizewait(FileHandle h, int priority);                                /* 0x533b50 */
int FILE_existswait(const char *name, int priority);                          /* 0x533c70 */
int FILE_exists(const char *name);                                            /* 0x569d40 */

/* hlsfile.c. memflags are MEM_alloc's flags for the block; the *abort variants end the program (REAL_abort)
   when the file can't be read, the others return 0/NULL. */
int FILE_getsize(const char *name);                                           /* 0x569e20 (FILE_size 0x569da0) */
void *FILE_loadabort(const char *name, unsigned memflags);                    /* 0x569e60 (FILE_load 0x569ea0) */
void *FILE_loadfile(const char *name, unsigned memflags);                     /* 0x569fc0 (FILE_load 0x569ea0) */
/* Up to size bytes (size < 1: all) from the start of the file into dst; the count read. */
int FILE_loadatfile(const char *name, void *dst, int size);                   /* 0x56a0e0 (FILE_loadat 0x56a050) */
/* The directory of a BIG archive (.viv): its header and records, in a block of BIG_headersize bytes. */
void *FILE_loadbigheaderabort(const char *name, unsigned memflags);           /* 0x56a280 (FILE_loadbigheader 0x56a2c0) */
void *FILE_loadbigheaderfile(const char *name, unsigned memflags);            /* 0x56a4c0 */

/* loadpack.c: the file, unpacked if it is packed (pack.h); NULL if it can't be read or unpacked. */
void *FILE_loadpackfile(const char *name, unsigned memflags);                 /* 0x56e190 */
void *FILE_loadpack(const char *name, unsigned memflags);                     /* 0x56e250: aborts on failure */
