/* EA real library: shared pieces (abort). */
#pragma once

/* The library's fatal error (REAL_abort 0x5647c0, reached through the handler pointer 0x5debf0 that the
   game points at NFS_ErrorHandler): the message with the source file and line the caller recorded
   (0x5deb74/0x5deb78), then the program ends. The port logs "Abort Message: ..." and exits with 1. */
void REAL_abort(const char *file, int line, const char *fmt, ...);
