/* The C runtime's text streams as the scene files (.scn) are read with them: the utility's fopen under the
   search path (0x59e400) in text mode, and MSVC 6's fscanf (0x5a2373) and fgets (0x5a2311). The original
   reads every scene element field with fscanf straight into the element object, so the port keeps each
   element as its original byte image and these functions write into it exactly as the runtime does: %d
   stores 4 bytes (an int) and %f 4 bytes (a float) wherever the argument points, even where the field
   there is a 16-bit or 8-bit one (the next field gets the int's upper bytes until its own read). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A text-mode stream over a whole file: CR LF reads as LF and Ctrl-Z ends the file, as MSVC's _read in
   text mode. eof is the runtime's _IOEOF flag (+0x0c bit 0x10 of a FILE): set when a read hits the end. */
typedef struct {
    char *data;
    size_t len, pos;
    bool eof;
} CrtFile;

/* The name under the search path (UTIL_searchpath), opened for reading in text mode; NULL if missing.
   The mode string is "r" at every call site the port has. UTIL_fopen 0x59e400 */
CrtFile *UTIL_fopen(const char *name, const char *mode);
void crt_fclose(CrtFile *f);   /* fclose 0x5a1dd4 */

/* fscanf: whitespace in the format skips any amount of input whitespace (newlines too), %d and %f skip
   leading whitespace and store 4 bytes, %s stores a run of non-whitespace and a NUL, other characters
   must match. Returns the number of fields stored, or -1 if the input ended before the first. 0x5a2373 */
int crt_fscanf(CrtFile *f, const char *fmt, ...);
/* fgets: up to n - 1 characters, through the first newline, NUL-terminated; NULL (dst untouched) if the
   stream is already at its end. 0x5a2311 */
char *crt_fgets(char *dst, int n, CrtFile *f);
