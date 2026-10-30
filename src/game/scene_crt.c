/* See scene_crt.h. */
#include "game/scene_crt.h"
#include "support/ufile.h"
#include "vfs.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* UTIL_fopen 0x59e400: the name with the game directory (and the CD directory) in front, the first that
   exists (UTIL_searchpath, as the utility's other opens), then the runtime's fopen. The port reads the
   whole file through the file layer and does the text-mode translation up front: what the runtime's
   buffered reads would return is the same bytes in the same order. */
CrtFile *UTIL_fopen(const char *name, const char *mode)
{
    (void)mode;
    char path[260];
    if (!UTIL_searchpath(name, path)) return NULL;
    size_t n = 0;
    uint8_t *raw = vfs_read_all(path, &n);
    if (!raw) return NULL;
    CrtFile *f = calloc(1, sizeof *f);
    char *t = malloc(n + 1);
    if (!f || !t) { free(f); free(t); free(raw); return NULL; }
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (raw[i] == 0x1a) break;                                   /* Ctrl-Z: end of a text file */
        if (raw[i] == '\r' && i + 1 < n && raw[i + 1] == '\n') continue;   /* CR LF -> LF */
        t[k++] = (char)raw[i];
    }
    t[k] = 0;
    free(raw);
    f->data = t;
    f->len = k;
    return f;
}

void crt_fclose(CrtFile *f)
{
    if (!f) return;
    free(f->data);
    free(f);
}

/* The runtime's getc: the next character or -1 (and the end flag) at the end. */
static int rd(CrtFile *f)
{
    if (f->pos >= f->len) { f->eof = true; return -1; }
    return (unsigned char)f->data[f->pos++];
}
static void unrd(CrtFile *f, int c) { if (c >= 0) f->pos--; }

/* isspace in the C locale: space, \t \n \v \f \r. */
static bool sp(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

static int skip_ws(CrtFile *f)
{
    int c;
    do c = rd(f); while (c >= 0 && sp(c));
    return c;
}

int crt_fscanf(CrtFile *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int stored = 0;
    bool input_failed = false;
    for (const char *p = fmt; *p; p++) {
        if (sp((unsigned char)*p)) {                        /* white space: any amount, none included */
            int c = skip_ws(f);
            unrd(f, c);
            continue;
        }
        if (*p != '%') {                                     /* an ordinary character must match */
            int c = rd(f);
            if (c != (unsigned char)*p) { unrd(f, c); if (c < 0) input_failed = true; break; }
            continue;
        }
        p++;
        if (*p == 'd') {
            int c = skip_ws(f);
            if (c < 0) { input_failed = true; break; }
            bool neg = false;
            if (c == '-' || c == '+') { neg = c == '-'; c = rd(f); }
            if (c < '0' || c > '9') { unrd(f, c); break; }
            uint32_t v = 0;                                   /* the runtime accumulates in a long, wrapping */
            while (c >= '0' && c <= '9') { v = v * 10u + (uint32_t)(c - '0'); c = rd(f); }
            unrd(f, c);
            if (neg) v = 0u - v;
            memcpy(va_arg(ap, void *), &v, 4);
            stored++;
        } else if (*p == 'f') {
            /* The runtime collects sign, digits, point, digits, exponent and converts the text (_fltin);
               strtod of the same text, rounded to float, gives the stored value. */
            char buf[64];
            size_t n = 0;
            int c = skip_ws(f);
            if (c < 0) { input_failed = true; break; }
            if (c == '-' || c == '+') { buf[n++] = (char)c; c = rd(f); }
            bool digits = false;
            while (c >= '0' && c <= '9' && n < sizeof buf - 8) { buf[n++] = (char)c; c = rd(f); digits = true; }
            if (c == '.') {
                buf[n++] = (char)c;
                c = rd(f);
                while (c >= '0' && c <= '9' && n < sizeof buf - 8) { buf[n++] = (char)c; c = rd(f); digits = true; }
            }
            if (digits && (c == 'e' || c == 'E')) {
                buf[n++] = (char)c;
                c = rd(f);
                if (c == '-' || c == '+') { buf[n++] = (char)c; c = rd(f); }
                while (c >= '0' && c <= '9' && n < sizeof buf - 1) { buf[n++] = (char)c; c = rd(f); }
            }
            unrd(f, c);
            if (!digits) break;
            buf[n] = 0;
            float v = (float)strtod(buf, NULL);
            memcpy(va_arg(ap, void *), &v, 4);
            stored++;
        } else if (*p == 's') {
            int c = skip_ws(f);
            if (c < 0) { input_failed = true; break; }
            char *d = va_arg(ap, char *);
            while (c >= 0 && !sp(c)) { *d++ = (char)c; c = rd(f); }
            unrd(f, c);
            *d = 0;
            stored++;
        } else {
            break;   /* no other conversion appears in the scene formats */
        }
    }
    va_end(ap);
    return stored == 0 && input_failed ? -1 : stored;
}

char *crt_fgets(char *dst, int n, CrtFile *f)
{
    if (n <= 0) return NULL;
    int k = 0;
    while (k < n - 1) {
        int c = rd(f);
        if (c < 0) break;
        dst[k++] = (char)c;
        if (c == '\n') break;
    }
    if (k == 0 && f->eof) return NULL;
    dst[k] = 0;
    return dst;
}
