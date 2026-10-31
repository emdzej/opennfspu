/* FERT's expressions (fert_expr.h). */
#include "front/fert_expr.h"
#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- the symbol table */

typedef struct {
    char *name;
    char *text;
    bool busy;     /* +0x1c: being expanded */
} Sym;

static Sym *s_syms;
static int s_n, s_cap;

void FertSyms_Clear(void)
{
    for (int i = 0; i < s_n; i++) {
        free(s_syms[i].name);
        free(s_syms[i].text);
    }
    s_n = 0;
}

static Sym *find(const char *name, size_t len)
{
    for (int i = 0; i < s_n; i++)
        if (strlen(s_syms[i].name) == len && !memcmp(s_syms[i].name, name, len)) return &s_syms[i];
    return NULL;
}

static char *dup(const char *s)
{
    size_t n = strlen(s);
    char *d = malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

void FertSyms_Define(const char *name, const char *text)
{
    Sym *s = find(name, strlen(name));
    if (s) {
        char *t = dup(text ? text : "");
        if (t) {
            free(s->text);
            s->text = t;
        }
        return;
    }
    if (s_n == s_cap) {
        int cap = s_cap ? s_cap * 2 : 128;
        Sym *a = realloc(s_syms, (size_t)cap * sizeof *a);
        if (!a) return;
        s_syms = a;
        s_cap = cap;
    }
    s_syms[s_n] = (Sym){dup(name), dup(text ? text : ""), false};
    if (s_syms[s_n].name && s_syms[s_n].text) s_n++;
}

const char *FertSyms_Get(const char *name)
{
    Sym *s = find(name, strlen(name));
    return s ? s->text : NULL;
}

/* ---------------------------------------------------------------- expansion (0x52df30) */

static bool ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.'; }   /* 0x52e2b0 */
static bool ident_char(char c) { return ident_start(c) || (c >= '0' && c <= '9'); }   /* 0x52e2e0 */

typedef struct {
    char *p;
    int n, cap;
    bool over;
} Buf;

static void put(Buf *b, const char *s, size_t n)
{
    if ((size_t)(b->cap - 1 - b->n) < n) {
        b->over = true;
        n = (size_t)(b->cap - 1 - b->n);
    }
    memcpy(b->p + b->n, s, n);
    b->n += (int)n;
    b->p[b->n] = 0;
}

static bool expand(const char *s, Buf *out)
{
    while (*s) {
        /* an identifier (0x52e0f0 with 0) */
        size_t n = 0;
        if (ident_start(s[0])) {
            n = 1;
            while (s[n] && ident_char(s[n])) n++;
        }
        if (n) {
            Sym *y = find(s, n);
            if (!y) {
                put(out, s, n);
            } else {
                if (y->busy) return false;
                y->busy = true;
                bool ok = expand(y->text, out);
                y->busy = false;
                if (!ok) return false;
            }
            s += n;
            continue;
        }
        /* the rest up to the next identifier or digit (0x52e0f0 with 1: the first character taken) */
        n = 1;
        while (s[n] && !ident_char(s[n])) n++;
        if (!(n == 2 && s[0] == '#' && s[1] == '#')) put(out, s, n);
        s += n;
    }
    return true;
}

bool FertExpr_Expand(const char *text, char *out, int cap)
{
    if (cap <= 0) return false;
    out[0] = 0;
    Buf b = {out, 0, cap, false};
    if (!expand(text ? text : "", &b)) {
        out[0] = 0;
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------- the C expression evaluator */

typedef struct {
    const char *p;
    jmp_buf fail;
    int skipping;   /* in a branch C wouldn't evaluate: its arithmetic errors don't count */
} Ev;

static void skip(Ev *e)
{
    while (*e->p && isspace((unsigned char)*e->p)) e->p++;
}

static _Noreturn void fail(Ev *e) { longjmp(e->fail, 1); }

static int32_t ternary(Ev *e);

static int32_t call(Ev *e, const char *name, size_t n)
{
    /* the interpreter's library (0x5ddeac ..): one argument each */
    skip(e);
    if (*e->p != '(') fail(e);
    e->p++;
    int32_t a = ternary(e);
    skip(e);
    if (*e->p != ')') fail(e);
    e->p++;
#define IS(s) (n == sizeof(s) - 1 && !memcmp(name, s, n))
    if (IS("abs")) return a < 0 ? -a : a;
    if (IS("sqrt")) return a < 0 ? 0 : (int32_t)sqrt((double)a);
    /* (intsin and intcos: degrees in, scaled by 1024 out, the port's reading; no template uses them) */
    if (IS("intsin")) return (int32_t)(sin((double)a * 3.14159265358979323846 / 180.0) * 1024.0);
    if (IS("intcos")) return (int32_t)(cos((double)a * 3.14159265358979323846 / 180.0) * 1024.0);
    unsigned char c = (unsigned char)a;
    if (IS("isalpha")) return isalpha(c) != 0;
    if (IS("isupper")) return isupper(c) != 0;
    if (IS("islower")) return islower(c) != 0;
    if (IS("isdigit")) return isdigit(c) != 0;
    if (IS("isxdigit")) return isxdigit(c) != 0;
    if (IS("isalnum")) return isalnum(c) != 0;
    if (IS("isspace")) return isspace(c) != 0;
    if (IS("ispunct")) return ispunct(c) != 0;
    if (IS("iscntrl")) return iscntrl(c) != 0;
    if (IS("isprint")) return isprint(c) != 0;
    if (IS("isgraph")) return isgraph(c) != 0;
    if (IS("isascii")) return a >= 0 && a < 0x80;
    if (IS("tolower")) return tolower(c);
    if (IS("toupper")) return toupper(c);
    if (IS("toascii")) return a & 0x7f;
#undef IS
    fail(e);
}

static int32_t primary(Ev *e)
{
    skip(e);
    char c = *e->p;
    if (c == '(') {
        e->p++;
        int32_t v = ternary(e);
        skip(e);
        if (*e->p != ')') fail(e);
        e->p++;
        return v;
    }
    if (c == '-') { e->p++; return (int32_t)(0u - (uint32_t)primary(e)); }
    if (c == '+') { e->p++; return primary(e); }
    if (c == '!') { e->p++; return !primary(e); }
    if (c == '~') { e->p++; return ~primary(e); }
    if (c >= '0' && c <= '9') {
        char *end;
        unsigned long v = strtoul(e->p, &end, 0);   /* decimal, 0x hexadecimal, 0 octal */
        e->p = end;
        while (*e->p == 'l' || *e->p == 'L' || *e->p == 'u' || *e->p == 'U') e->p++;
        return (int32_t)(uint32_t)v;
    }
    if (c == '\'') {
        int32_t v = (unsigned char)e->p[1];
        if (e->p[1] && e->p[2] == '\'') {
            e->p += 3;
            return v;
        }
        fail(e);
    }
    if (isalpha((unsigned char)c) || c == '_') {
        const char *s = e->p;
        while (isalnum((unsigned char)*e->p) || *e->p == '_') e->p++;
        return call(e, s, (size_t)(e->p - s));   /* (an undefined name is an error) */
    }
    fail(e);
}

static int prec(const char *p, int *len)
{
    *len = 2;
    if (!strncmp(p, "||", 2)) return 1;
    if (!strncmp(p, "&&", 2)) return 2;
    if (!strncmp(p, "==", 2) || !strncmp(p, "!=", 2)) return 6;
    if (!strncmp(p, "<=", 2) || !strncmp(p, ">=", 2)) return 7;
    if (!strncmp(p, "<<", 2) || !strncmp(p, ">>", 2)) return 8;
    *len = 1;
    switch (*p) {
    case '|': return 3;
    case '^': return 4;
    case '&': return 5;
    case '<': case '>': return 7;
    case '+': case '-': return 9;
    case '*': case '/': case '%': return 10;
    default: return 0;
    }
}

static int32_t apply(Ev *e, const char *op, int len, int32_t a, int32_t b)
{
    uint32_t ua = (uint32_t)a, ub = (uint32_t)b;
    if (len == 2) {
        switch (op[0]) {
        case '|': return a || b;
        case '&': return a && b;
        case '=': return a == b;
        case '!': return a != b;
        case '<': return op[1] == '=' ? a <= b : (int32_t)(ua << (ub & 31));
        case '>': return op[1] == '=' ? a >= b : a >> (ub & 31);
        }
    }
    switch (op[0]) {
    case '|': return a | b;
    case '^': return a ^ b;
    case '&': return a & b;
    case '<': return a < b;
    case '>': return a > b;
    case '+': return (int32_t)(ua + ub);
    case '-': return (int32_t)(ua - ub);
    case '*': return (int32_t)(ua * ub);
    case '/':
    case '%':
        if (!b || (a == INT32_MIN && b == -1)) {
            if (e->skipping) return 0;
            fail(e);
        }
        return op[0] == '/' ? a / b : a % b;
    }
    fail(e);
}

static int32_t binary(Ev *e, int min)
{
    int32_t a = primary(e);
    for (;;) {
        skip(e);
        int len, p = prec(e->p, &len);
        if (!p || p < min) return a;
        const char *op = e->p;
        e->p += len;
        bool shortcut = len == 2 && ((op[0] == '|' && op[1] == '|' && a) || (op[0] == '&' && op[1] == '&' && !a));
        e->skipping += shortcut;
        int32_t b = binary(e, p + 1);
        e->skipping -= shortcut;
        a = apply(e, op, len, a, b);
    }
}

static int32_t ternary(Ev *e)
{
    int32_t c = binary(e, 1);
    skip(e);
    if (*e->p != '?') return c;
    e->p++;
    e->skipping += !c;
    int32_t a = ternary(e);
    e->skipping -= !c;
    skip(e);
    if (*e->p != ':') fail(e);
    e->p++;
    e->skipping += !!c;
    int32_t b = ternary(e);
    e->skipping -= !!c;
    return c ? a : b;
}

int32_t FertExpr_Eval(const char *text)
{
    static char buf[0x200];   /* (the original's 512-byte copy, 0x52dcb0) */
    if (!FertExpr_Expand(text, buf, sizeof buf)) return 0;
    for (char *p = buf; *p; p++)
        if (*p == '.') *p = 'X';
    Ev e;
    memset(&e, 0, sizeof e);
    e.p = buf;
    if (setjmp(e.fail)) return 0;
    skip(&e);
    if (!*e.p) return 0;
    int32_t v = ternary(&e);
    skip(&e);
    if (*e.p) return 0;   /* (trailing text: an error) */
    return v;
}
