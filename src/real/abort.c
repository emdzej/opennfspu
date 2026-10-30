/* REAL_abort 0x5647c0, see real.h. */
#include "real/real.h"
#include "platform.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void REAL_abort(const char *file, int line, const char *fmt, ...)
{
    static int aborting;   /* 0x6a5b6c: a second abort while reporting the first returns */
    if (aborting) return;
    aborting = 1;
    char msg[512], out[640];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    snprintf(out, sizeof out, "Abort Message: %s (%s line %d)", msg, file, line);
    plat_log(out);
    exit(1);
}
