/* FERT's expressions (fert_expr.h). */
#include "front/fert_expr.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
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

/* ---------------------------------------------------------------- the interpreter (0x52f120 ..) */

/* Its tables (0x52f2b0): a binary precedence by operator code, a unary one at +0x100. Codes over 0x7f
   are the two-character operators (0x80 <<, 0x81 >>, 0x82 <=, 0x83 >=, 0x84 ==, 0x85 != <>, 0x86 && **,
   0x87 || ++, 0x88 --, 0x89 !:, 0x8a !?), the unary ones (0x8b [ index, 0x8c -, 0x8d ~, 0x8e !, 0x8f <
   high word, 0x90 > low word, 0x91 << doubled, 0x92 >> halved) and the functions (0x93 ..). */
static uint8_t s_prec[0x200];
static bool s_tables;

static const char *const s_funcs[] = {   /* 0x5dde5c */
    "abs", "isalpha", "isupper", "islower", "isdigit", "isxdigit", "isalnum", "isspace", "ispunct",
    "iscntrl", "isprint", "isgraph", "isascii", "tolower", "toupper", "toascii", "intsin", "intcos", "sqrt",
};
enum { NFUNCS = sizeof s_funcs / sizeof *s_funcs, SENTINEL = 0x7ffffffb };

static void tables(void)
{
    if (s_tables) return;
    s_tables = true;
    uint8_t *t = s_prec;
    t['.'] = 0xf;
    t[0x8b] = 0xf;
    for (int i = 0; i < NFUNCS; i++) t[0x193 + i] = 0xf;
    for (int c = 0x18c; c <= 0x192; c++) t[c] = 0xe;
    t['*'] = 0xd, t[0x100 + '*'] = 0xe, t['/'] = 0xd, t['%'] = 0xd;
    t['+'] = 0xc, t[0x100 + '+'] = 0xe, t['-'] = 0xc, t[0x100 + '-'] = 0xe;
    t[0x80] = 0xb, t[0x81] = 0xb;
    t['<'] = 10, t[0x100 + '<'] = 0xe, t[0x82] = 10, t['>'] = 10, t[0x100 + '>'] = 0xe, t[0x83] = 10;
    t[0x84] = 9, t[0x85] = 9, t['='] = 9, t['#'] = 9;
    t['&'] = 8;
    t['^'] = 7, t['~'] = 7, t[0x100 + '~'] = 0xe;
    t['|'] = 6, t['!'] = 6, t[0x100 + '!'] = 0xe;
    t['?'] = 3, t[':'] = 3, t[0x89] = 3, t[0x8a] = 3;
    t[0x86] = 5, t[0x87] = 4, t[0x88] = 4;
    t[')'] = 2, t[0x100 + ')'] = 2, t[']'] = 2, t[0x100 + ']'] = 2;
    t['('] = 1, t[0x100 + '('] = 1, t['['] = 1, t[0x100 + '['] = 1;
    t[','] = 1, t[0x100 + ','] = 1, t[';'] = 1, t[0x100 + ';'] = 1;
    t[0] = 1, t[0x100] = 1;
}

enum { DEPTH = 64 };

typedef struct {
    int32_t val[DEPTH + 4];   /* (four zeros below the first, what an operator short of operands reads) */
    int nval;
    uint8_t op[DEPTH];
    int8_t opprec[DEPTH];
    int nop;
} Interp;

static int32_t *top(Interp *in) { return &in->val[4 + in->nval - 1]; }

static void push_val(Interp *in, int32_t v)
{
    if (in->nval < DEPTH) in->val[4 + in->nval++] = v;
}

static void push_op(Interp *in, uint8_t op, int prec)
{
    if (in->nop < DEPTH) {
        in->op[in->nop] = op;
        in->opprec[in->nop++] = (int8_t)prec;
    }
}

static int32_t ctype_fn(int code, int32_t v)
{
    if (v & ~0xff) return code == 0xa0 || code == 0xa1 ? v : 0;
    int c = v;
    switch (code) {
    case 0x94: return isalpha(c) != 0;
    case 0x95: return isupper(c) != 0;
    case 0x96: return islower(c) != 0;
    case 0x97: return isdigit(c) != 0;
    case 0x98: return isxdigit(c) != 0;
    case 0x99: return isalnum(c) != 0;
    case 0x9a: return isspace(c) != 0;
    case 0x9b: return ispunct(c) != 0;
    case 0x9c: return iscntrl(c) != 0;
    case 0x9d: return isprint(c) != 0;
    case 0x9e: return isgraph(c) != 0;
    case 0x9f: return c < 0x80;
    case 0xa0: return tolower(c);
    case 0xa1: return toupper(c);
    default: return v;
    }
}

/* The operators above precedence p applied (0x52fd10): left to right, the unary ones (0xe) right to
   left. */
static void reduce(Interp *in, int p)
{
    while (in->nop) {
        int q = in->opprec[in->nop - 1];
        if (q <= p && (q != p || p == 0xe)) return;
        uint8_t op = in->op[--in->nop];
        if (op < 0x8c) {   /* binary: the two values below make one */
            int32_t b = *top(in);
            if (in->nval) in->nval--;
            int32_t *a = in->nval ? top(in) : &in->val[3];
            uint32_t ua = (uint32_t)*a, ub = (uint32_t)b;
            switch (op) {
            case '!': case '|': *a = (int32_t)(ua | ub); break;
            case '#': case 0x85: *a = *a != b; break;
            case '%': if (b) *a = *a % b; break;   /* (by 0: as it was) */
            case '&': *a = (int32_t)(ua & ub); break;
            case '*': *a = (int32_t)(ub * ua); break;
            case '+': case '.': case 0x8b: *a = (int32_t)(ua + ub); break;
            case '-': *a = (int32_t)(ua - ub); break;
            case '/': if (b) *a = *a / b; break;
            case ':': case 0x8a: if (*a == SENTINEL) *a = b; break;
            case '<': *a = *a < b; break;
            case '=': case 0x84: *a = *a == b; break;
            case '>': *a = b < *a; break;
            case '?': case 0x89: *a = *a == 0 ? SENTINEL : b; break;
            case '^': case '~': *a = (int32_t)(ua ^ ub); break;
            case 0x80: *a = (int32_t)(ua << (ub & 0x1f)); break;
            case 0x81: *a = *a >> (ub & 0x1f); break;
            case 0x82: *a = *a <= b; break;
            case 0x83: *a = b <= *a; break;
            case 0x86: *a = *a != 0 && b != 0; break;
            case 0x87: *a = *a != 0 || b != 0; break;
            case 0x88: *a = (*a != 0) != (b != 0); break;
            default: break;   /* (error 7) */
            }
            if (!in->nval) in->nval = 1, in->val[4] = *a;
            continue;
        }
        int32_t *v = in->nval ? top(in) : &in->val[3];
        uint32_t u = (uint32_t)*v;
        switch (op) {
        case 0x8c: *v = (int32_t)(0u - u); break;
        case 0x8d: *v = (int32_t)~u; break;
        case 0x8e: *v = u == 0; break;
        case 0x8f: *v = *v >> 16 & 0xffff; break;
        case 0x90: *v = (int32_t)(u & 0xffff); break;
        case 0x91: *v = (int32_t)(u << 1); break;
        case 0x92: *v = *v >> 1; break;
        case 0x93: *v = (int32_t)((*v < 0 ? 0u - u : u) & 0x7fffffffu); break;
        case 0xa2: *v = (int32_t)(u & 0x7f); break;
        case 0xa3: *v = (int32_t)sin((double)*v * 0.006135923151542565); break;   /* 0x5bb2a8: 2 pi / 1024 */
        case 0xa4: *v = (int32_t)cos((double)*v * 0.006135923151542565); break;
        case 0xa5: *v = *v < 0 ? 0 : (int32_t)sqrt((double)*v); break;
        default: *v = ctype_fn(op, *v); break;
        }
    }
}

static int digit(int c)
{
    int d = c - '0';
    if ((unsigned)d > 9) d = (uint8_t)((c | 0x20) + 0xa9);
    return d;
}

/* An operand (0x530750): a number in the radix (10; "0x" and '$' '&' hexadecimal, '%' binary, '@' octal,
   a b d h o q suffix after a digit), quoted characters, ^X; anything else (names: FERT's are expanded
   before) is 0. */
static int32_t operand(const char *t, int radix)
{
    size_t n = strlen(t);
    if (!n) return 0;   /* (error 7) */
    const char *p = t;
    if (t[0] < 'A' || radix > 10) {
        if (n == 1) {
            int d = digit((unsigned char)t[0]);
            if (d < radix) return d;
            if (t[0] == '*') return 0;   /* (the location counter) */
            goto symbol;
        }
        int r = radix;
        if (t[0] > '/') {
            if (t[0] == '0' && t[1] == 'x' && r < 0x22) r = 16, p = t + 2;
            else if (t[0] == '@') r = 8, p = t + 1;
        } else if (t[0] == '$' || t[0] == '&') {
            r = 16, p = t + 1;
        } else if (t[0] == '%') {
            r = 2, p = t + 1;
        } else if (t[0] == '"' || t[0] == '\'') {
            uint32_t v = 0;
            int k = 0;
            for (const char *q = t + 1; *q && *q != t[0] && k < 4; q++, k++) v = v << 8 | (uint8_t)*q;
            return (int32_t)v;
        } else {
            r = 0;
        }
        uint32_t v = 0;
        const char *q = p;
        for (; *q; q++) {
            int d = digit((unsigned char)*q);
            if (d < r) v = v * (uint32_t)r + (uint32_t)d;
            else if (*q != '_' || r == 10) break;
        }
        if (!*q) return (int32_t)v;
        if (isdigit((unsigned char)t[0])) {
            int s = 0;
            switch (t[n - 1] | 0x20) {
            case 'b': s = 2; break;
            case 'd': s = 10; break;
            case 'h': s = 16; break;
            case 'o': case 'q': s = 8; break;
            }
            if (s) {
                char b[0x200];
                snprintf(b, sizeof b, "%.*s", (int)(n - 1), t);
                return operand(b, s);
            }
        }
    }
symbol:
    if (t[0] == '^' && n == 2 && (uint8_t)((t[1] | 0x20) + 0xa0) < 0x1b) return (int8_t)((t[1] | 0x20) + 0xa0);
    return 0;   /* (no symbols: error 5, not reported) */
}

/* The expression's value (0x52f5a0), 0 when it can't be read (an operator where none can be, brackets
   that don't match). */
static int32_t interpret(const char *s)
{
    tables();
    const uint8_t *t = s_prec;
    /* a single operand: the first character no prefix operator or bracket, the rest no binary operator */
    const char *p = s;
    if (*p && !t[0x100 + (uint8_t)*p]) {
        p++;
        while (*p && !t[(uint8_t)*p]) p++;
    }
    if (!*p) return operand(s, 10);
    Interp in;
    memset(&in, 0, sizeof in);
    push_op(&in, ' ', -1);
    int parens = 0;
    p = s;
    for (;;) {
        /* brackets and prefix operators */
        for (;;) {
            while (*p == ' ') p++;
            if (*p == '(' || *p == '[') {
                push_op(&in, (uint8_t)*p, t[(uint8_t)*p]);
                parens++;
                p++;
                continue;
            }
            int u = 0;
            switch (*p) {
            case '+': p++; continue;
            case '-': u = 0x8c; break;
            case '~': u = 0x8d; break;
            case '!': u = 0x8e; break;
            case '<': u = p[1] == '<' ? 0x91 : 0x8f; break;
            case '>': u = p[1] == '>' ? 0x92 : 0x90; break;
            }
            if (!u) break;
            p += (u == 0x91 || u == 0x92) ? 2 : 1;
            push_op(&in, (uint8_t)u, t[0x100 + u]);
        }
        /* an operand */
        char tok[0x200];
        int n = 0;
        if (*p == '*') {
            tok[n++] = '*';
            p++;
        } else if (*p && (*p == '\'' || !t[0x100 + (uint8_t)*p])) {
            bool quoted = *p == '\'';
            tok[n++] = *p++;
            while (*p && n < (int)sizeof tok - 1) {
                if (*p == '\'') quoted = !quoted;
                if ((!quoted && t[(uint8_t)*p]) || *p == ' ') break;
                tok[n++] = *p++;
            }
        }
        tok[n] = 0;
        int implied = 0;
        if (*p == '(') {
            int f = -1;
            for (int i = 0; i < NFUNCS; i++)
                if (!strcmp(tok, s_funcs[i])) f = i;
            if (f >= 0) {
                push_op(&in, (uint8_t)(0x93 + f), t[0x193 + f]);
                continue;   /* its '(' next */
            }
            implied = '*';
        }
        if (*p == '[') implied = 0x8b;
        push_val(&in, operand(tok, 10));
        while (*p == ' ') p++;
        while (*p == ')' || *p == ']') {
            if (!parens) return 0;   /* (error 7) */
            reduce(&in, t[(uint8_t)*p]);
            if (in.nop && (in.op[in.nop - 1] == '(' || in.op[in.nop - 1] == '[')) {
                in.nop--;
                parens--;
                p++;
            } else {
                return 0;   /* (a stray bracket) */
            }
        }
        if (!*p) {
            if (parens) return 0;
            reduce(&in, 0);
            return in.nval ? *top(&in) : 0;   /* (a count other than one: error 7, the top all the same) */
        }
        /* a binary operator: one character, or two */
        int op = implied;
        if (!op) {
            while (*p && (uint8_t)*p <= 0x20) p++;
            op = (uint8_t)*p;
            const char *q = p + 1;
            int two = 0;
            switch (op) {
            case '!': two = *q == '=' ? 0x85 : *q == ':' ? 0x89 : *q == '?' ? 0x8a : 0; break;
            case '%': two = *q == '%' ? '%' : 0; break;
            case '&': two = *q == '&' ? 0x86 : 0; break;
            case '*': two = *q == '*' ? 0x86 : 0; break;
            case '+': two = *q == '+' ? 0x87 : 0; break;
            case '-': two = *q == '-' ? 0x88 : 0; break;
            case '/': two = *q == '/' ? '%' : 0; break;
            case '<': two = *q == '<' ? 0x80 : *q == '=' ? 0x82 : *q == '>' ? 0x85 : 0; break;
            case '=': two = *q == '=' ? 0x84 : 0; break;
            case '>': two = *q == '>' ? 0x81 : *q == '=' ? 0x83 : 0; break;
            case '|': two = *q == '|' ? 0x87 : 0; break;
            }
            if (two) {
                op = two;
                p += 2;
            } else if (*p) {
                p++;
            }
        }
        int prec = t[op];
        if (prec < 2) return 0;   /* (error 7) */
        reduce(&in, prec);
        push_op(&in, (uint8_t)op, prec);
    }
}

int32_t FertExpr_Eval(const char *text)
{
    static char buf[0x200];   /* (the original's 512-byte copy, 0x52dcb0) */
    if (!FertExpr_Expand(text, buf, sizeof buf)) return 0;
    for (char *p = buf; *p; p++)
        if (*p == '.') *p = 'X';
    return interpret(buf);
}
