/* FERT's model: attributes, parts, states, controls and windows, the template registry and the layouts
   (fert.h). */
#include "front/fert.h"
#include "front/fert_expr.h"
#include "vfs.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copyname(char *dst, const char *src)
{
    snprintf(dst, FERT_NAME, "%s", src ? src : "");
}

static char *dupstr(const char *s)
{
    size_t n = strlen(s ? s : "");
    char *d = malloc(n + 1);
    if (d) memcpy(d, s ? s : "", n + 1);
    return d;
}

/* A key "<name><suffix>" of the current section. */
static const char *get_sub(IniData *ini, const char *name, const char *suffix)
{
    char k[2 * FERT_NAME];
    snprintf(k, sizeof k, "%s%s", name, suffix);
    return IniData_Get(ini, k);
}

/* ---------------------------------------------------------------- attributes */

/* A number as 0x523aa0 reads it: "0x" (or "0X") and hexadecimal digits (0x523ae0: at least 3
   characters, sscanf "%x"), else atoi. */
int32_t Fert_ParseInt(const char *s);
static int32_t parse_int(const char *s) { return Fert_ParseInt(s); }

int32_t Fert_ParseInt(const char *s)
{
    if (!s) return atoi("");
    unsigned v = 0;
    if (strlen(s) >= 3 && s[0] == '0' && tolower((unsigned char)s[1]) == 'x' && sscanf(s + 2, "%x", &v) == 1)
        return (int32_t)v;
    return atoi(s);
}

FertAttr *FertAttr_new(FertAttrType type, const char *name, const char *def)
{
    FertAttr *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->type = type;
    copyname(a->name, name);
    copyname(a->screenkey, name);
    a->value = dupstr(def);
    a->label = dupstr("");
    a->exposed = true;
    a->minvalue = INT32_MIN;
    a->maxvalue = INT32_MAX;
    a->maxlength = type == FATTR_PATHNAME ? 0x104 : 0;
    return a;
}

void FertAttr_free(FertAttr *a)
{
    if (!a) return;
    free(a->str);
    free(a->value);
    free(a->label);
    free(a);
}

void FertAttr_set(FertAttr *a, const char *value)
{
    char *v = dupstr(value);
    if (!v) return;
    free(a->value);
    a->value = v;
}

void FertAttr_parseTemplate(FertAttr *a, IniData *ini)
{
    const char *v = get_sub(ini, a->name, ".value");
    const char *sk = get_sub(ini, a->name, ".screenkey");
    const char *lb = get_sub(ini, a->name, ".label");
    const char *fl = get_sub(ini, a->name, ".flags");
    FertAttr_set(a, v ? v : "");
    if (sk) copyname(a->screenkey, sk);
    if (lb) {
        char *l = dupstr(lb);
        if (l) {
            free(a->label);
            a->label = l;
        }
    }
    a->exposed = true;
    if (fl) {
        /* (strcspn to the first of H, I, D, E, N, then the word compared there) */
        size_t at = strcspn(fl, "HIDDEN");
        if (strncmp(fl + at, "HIDDEN", 6) == 0) a->exposed = false;
    }
    /* the kinds' own keys (0x526f80, 0x526c10) */
    if (a->type == FATTR_INT) {
        const char *mn = get_sub(ini, a->name, ".minvalue"), *mx = get_sub(ini, a->name, ".maxvalue");
        a->minvalue = mn ? parse_int(mn) : INT32_MIN;
        a->maxvalue = mn ? parse_int(mx) : INT32_MAX;   /* (the original tests the min's key for both) */
    } else if (a->type == FATTR_STRING || a->type == FATTR_LOCSTRING) {
        const char *ml = get_sub(ini, a->name, ".maxlength");
        if (ml) a->maxlength = parse_int(ml);
    }
}

void FertAttr_parseLayout(FertAttr *a, IniData *ini)
{
    if (!a->exposed) return;
    const char *v = IniData_Get(ini, a->screenkey);
    if (v) FertAttr_set(a, v);
}

void FertAttr_export(const FertAttr *a, const char *prefix)
{
    char name[3 * FERT_NAME];
    if (a->type != FATTR_POINT) {
        snprintf(name, sizeof name, "%s%s", prefix, a->name);   /* 0x526680 */
        FertSyms_Define(name, a->value);
        return;
    }
    char b[0x200];
    snprintf(b, sizeof b, "%s", a->value ? a->value : "");
    char *c = strchr(b, ',');
    if (!c) return;
    *c = 0;
    static const char *const sub[4] = {".x", ".y", ".width", ".height"};
    for (int i = 0; i < 4; i++) {
        snprintf(name, sizeof name, "%s%s%s", prefix, a->name, sub[i]);
        FertSyms_Define(name, i & 1 ? c + 1 : b);
    }
}

/* %name% replaced by the expansion of name, each pair in turn (0x526a20 finds them, 0x526ab0 replaces) */
static char *substitute(const char *v)
{
    char *out = dupstr(v);
    for (int guard = 0; out && guard < 64; guard++) {
        char *a = strchr(out, '%'), *b = a ? strchr(a + 1, '%') : NULL;
        if (!b) break;
        char name[0x200], exp[0x200];
        snprintf(name, sizeof name, "%.*s", (int)(b - a - 1), a + 1);
        FertExpr_Expand(name, exp, sizeof exp);   /* 0x52d9f0 */
        size_t n = (size_t)(a - out), e = strlen(exp), t = strlen(b + 1);
        char *r = malloc(n + e + t + 1);
        if (!r) break;
        memcpy(r, out, n);
        memcpy(r + n, exp, e);
        memcpy(r + n + e, b + 1, t + 1);
        free(out);
        out = r;
    }
    return out;
}

void FertAttr_eval(FertAttr *a)
{
    const char *v = a->value ? a->value : "";
    switch (a->type) {
    case FATTR_INT:
        a->num = *v ? FertExpr_Eval(v) : 0;
        break;
    case FATTR_POINT: {
        char b[0x200];
        snprintf(b, sizeof b, "%s", v);
        char *c = strchr(b, ',');
        if (!c) break;
        *c = 0;
        a->pt[0] = FertExpr_Eval(b);
        a->pt[1] = FertExpr_Eval(c + 1);
        break;
    }
    case FATTR_COLOR:
        a->num = strlen(v) > 2 && v[0] == '0' && v[1] == 'x' ? parse_int(v) : FertExpr_Eval(v);
        break;
    default: {
        char *s2 = substitute(v);
        if (s2) {
            free(a->str);
            a->str = s2;
        }
        break;
    }
    }
}

int32_t FertAttr_int(const FertAttr *a) { return a->num; }

void FertAttr_point(const FertAttr *a, int32_t out[2])
{
    out[0] = a->pt[0];
    out[1] = a->pt[1];
}

uint32_t FertAttr_color(const FertAttr *a) { return (uint32_t)a->num; }

const char *FertAttr_str(const FertAttr *a) { return a->str ? a->str : a->value; }

void FertAttr_setPoint(FertAttr *a, int32_t x, int32_t y)
{
    char b[32];
    snprintf(b, sizeof b, "%d,%d", (int)x, (int)y);
    FertAttr_set(a, b);
    a->pt[0] = x;
    a->pt[1] = y;
}

void FertAttrList_add(FertAttrList *l, FertAttr *a)
{
    if (!a) return;
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 8;
        FertAttr **p = realloc(l->a, (size_t)cap * sizeof *p);
        if (!p) return;
        l->a = p;
        l->cap = cap;
    }
    l->a[l->n++] = a;
}

FertAttr *FertAttrList_find(const FertAttrList *l, const char *name)
{
    for (int i = 0; i < l->n; i++)
        if (!strcmp(l->a[i]->name, name)) return l->a[i];
    return NULL;
}

void FertAttrList_free(FertAttrList *l)
{
    for (int i = 0; i < l->n; i++) FertAttr_free(l->a[i]);
    free(l->a);
    memset(l, 0, sizeof *l);
}

/* ---------------------------------------------------------------- parts */

static void add(FertAttrList *l, FertAttrType t, const char *name, const char *def) { FertAttrList_add(l, FertAttr_new(t, name, def)); }

FertPart *FertPart_new(const char *id, const char *name)
{
    static const char *const ids[] = {"RECT", "SHAPE", "TEXT", "LOCALIZEDTEXT", "DATA", "TEXTURE"};
    int k = -1;
    for (int i = 0; i < 6; i++)
        if (!strcmp(id, ids[i])) k = i;
    if (k < 0) return NULL;
    FertPart *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    p->kind = (FertPartKind)k;
    copyname(p->name, name);
    copyname(p->type, id);
    FertAttrList *l = &p->attrs;
    switch (p->kind) {
    case FPART_RECT:      /* 0x52a3a0 */
    case FPART_TEXTURE:
        add(l, FATTR_POINT, "pos", "50,50");
        add(l, FATTR_POINT, "size", "100,100");
        add(l, FATTR_COLOR, "color", "0xffff0000");
        break;
    case FPART_SHAPE:     /* 0x52a660 */
        add(l, FATTR_POINT, "pos", "50,50");
        add(l, FATTR_POINT, "size", "50,50");
        add(l, FATTR_PATHNAME, "shapePathName", "");
        add(l, FATTR_INT, "shapeIndex", "0");
        add(l, FATTR_INT, "flip", "0");
        break;
    case FPART_TEXT:      /* 0x52ac00 */
    case FPART_LOCALIZEDTEXT:
        add(l, FATTR_POINT, "pos", "50,50");
        add(l, FATTR_POINT, "size", "50,50");
        add(l, FATTR_PATHNAME, "fontPathName", "");
        add(l, FATTR_COLOR, "foreColor", "0xffffffff");
        add(l, FATTR_COLOR, "backColor", "0xffffffff");
        add(l, FATTR_STRING, "text", "Hello, World!");
        if (p->kind == FPART_LOCALIZEDTEXT) add(l, FATTR_LOCSTRING, "stringID", "");   /* 0x4e5c00 */
        break;
    case FPART_DATA:      /* 0x52b080: its attributes come from the template */
        break;
    }
    return p;
}

void FertPart_free(FertPart *p)
{
    if (!p) return;
    FertAttrList_free(&p->attrs);
    free(p);
}

void FertPart_parseTemplate(FertPart *p, IniData *ini)
{
    if (p->kind == FPART_DATA) {
        /* attributes.value: a comma list of names, each typed by <name>.type (0x52b170) */
        const char *list = IniData_Get(ini, "attributes.value");
        char b[1024];
        snprintf(b, sizeof b, "%s", list ? list : "");
        for (char *t = strtok(b, ","); t; t = strtok(NULL, ",")) {
            while (*t == ' ') t++;
            if (!*t || FertAttrList_find(&p->attrs, t)) continue;
            const char *ty = get_sub(ini, t, ".type");
            FertAttrType k = FATTR_STRING;
            if (ty) {
                if (!strcmp(ty, "COLOR")) k = FATTR_COLOR;
                else if (!strcmp(ty, "PATHNAME")) k = FATTR_PATHNAME;
                else if (!strcmp(ty, "POINT")) k = FATTR_POINT;
                else if (!strcmp(ty, "INT")) k = FATTR_INT;
                else if (!strcmp(ty, "LOCALIZEDSTRING")) k = FATTR_LOCSTRING;
            }
            add(&p->attrs, k, t, "");
        }
    }
    for (int i = 0; i < p->attrs.n; i++) FertAttr_parseTemplate(p->attrs.a[i], ini);
}

void FertPart_parseLayout(FertPart *p, IniData *ini)
{
    for (int i = 0; i < p->attrs.n; i++) FertAttr_parseLayout(p->attrs.a[i], ini);
}

/* ---------------------------------------------------------------- controls */

FertControl *FertControl_new(bool window)
{
    FertControl *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->window = window;
    FertAttrList *l = &c->attrs;   /* 0x523f60 */
    add(l, FATTR_POINT, "pos", "0,0");
    add(l, FATTR_POINT, "size", "0,0");
    add(l, FATTR_INT, "moveable", "0");
    add(l, FATTR_INT, "resizeable", "0");
    add(l, FATTR_INT, "autosize", "0");
    add(l, FATTR_INT, "state", "0");
    return c;
}

void FertControl_free(FertControl *c)
{
    if (!c) return;
    FertAttrList_free(&c->attrs);
    for (int i = 0; i < c->nparts; i++) FertPart_free(c->parts[i]);
    free(c->parts);
    for (int i = 0; i < c->nstates; i++) free(c->states[i].parts);
    free(c->states);
    for (int i = 0; i < c->nchildren; i++) FertControl_free(c->children[i]);
    free(c->children);
    FertControl_free(c->screen);
    free(c);
}

int FertControl_findPart(const FertControl *c, const char *name)
{
    for (int i = 0; i < c->nparts; i++)
        if (!strcmp(c->parts[i]->name, name)) return i;
    return -1;
}

int FertControl_findState(const FertControl *c, const char *name)
{
    for (int i = 0; i < c->nstates; i++)
        if (!strcmp(c->states[i].name, name)) return i;
    return -1;
}

/* A comma list's items (0x523960 counts them, 0x5239c0 takes the n-th), blanks kept as they are. */
typedef void (*ItemFn)(const char *item, void *user);
static void each_item(const char *list, ItemFn fn, void *user)
{
    if (!list) return;
    const char *s = list;
    for (;;) {
        const char *e = strchr(s, ',');
        size_t n = e ? (size_t)(e - s) : strlen(s);
        char b[FERT_NAME];
        snprintf(b, sizeof b, "%.*s", (int)n, s);
        fn(b, user);
        if (!e) break;
        s = e + 1;
    }
}

typedef struct {
    FertControl *c;
    IniData *ini;
} ParseCtx;

static void parse_part(const char *name, void *user)
{
    ParseCtx *x = user;
    char sec[2 * FERT_NAME];
    snprintf(sec, sizeof sec, "part.%s", name);
    if (IniData_SelectSection(x->ini, sec) < 0) return;
    const char *id = IniData_Get(x->ini, "id");
    FertPart *p = FertPart_new(id ? id : "", name);
    if (!p) return;
    FertPart_parseTemplate(p, x->ini);
    FertPart **a = realloc(x->c->parts, (size_t)(x->c->nparts + 1) * sizeof *a);
    if (!a) {
        FertPart_free(p);
        return;
    }
    x->c->parts = a;
    a[x->c->nparts++] = p;
}

typedef struct {
    FertState *s;
    const FertControl *c;
} StateCtx;

static void state_part(const char *name, void *user)
{
    StateCtx *x = user;
    int k = FertControl_findPart(x->c, name);
    if (k < 0) return;
    int *a = realloc(x->s->parts, (size_t)(x->s->nparts + 1) * sizeof *a);
    if (!a) return;
    x->s->parts = a;
    a[x->s->nparts++] = k;
}

static void parse_state(const char *name, void *user)
{
    ParseCtx *x = user;
    char sec[2 * FERT_NAME];
    snprintf(sec, sizeof sec, "state.%s", name);
    if (IniData_SelectSection(x->ini, sec) < 0) return;
    FertState *a = realloc(x->c->states, (size_t)(x->c->nstates + 1) * sizeof *a);
    if (!a) return;
    x->c->states = a;
    FertState *s = &a[x->c->nstates++];
    memset(s, 0, sizeof *s);
    copyname(s->name, name);
    StateCtx sx = {s, x->c};   /* 0x529c50 */
    each_item(IniData_Get(x->ini, "parts"), state_part, &sx);
}

void FertControl_parseTemplate(FertControl *c, IniData *ini)
{
    IniData_SelectSection(ini, "control");
    const char *id = IniData_Get(ini, "id"), *d = IniData_Get(ini, "description");
    if (id) copyname(c->id, id);
    if (d) snprintf(c->description, sizeof c->description, "%s", d);
    for (int i = 0; i < c->attrs.n; i++) FertAttr_parseTemplate(c->attrs.a[i], ini);
    /* (the lists are copied before the sections they name are selected) */
    char parts[1024], states[1024];
    const char *p = IniData_Get(ini, "parts"), *s = IniData_Get(ini, "states");
    snprintf(parts, sizeof parts, "%s", p ? p : "");
    snprintf(states, sizeof states, "%s", s ? s : "");
    ParseCtx x = {c, ini};
    if (parts[0]) each_item(parts, parse_part, &x);
    if (states[0]) each_item(states, parse_state, &x);
}

void FertControl_parseLayout(FertControl *c, IniData *ini)
{
    const char *n = IniData_Get(ini, "name");   /* 0x5baf7c */
    copyname(c->name, n ? n : "");
    for (int i = 0; i < c->attrs.n; i++) FertAttr_parseLayout(c->attrs.a[i], ini);
    for (int i = 0; i < c->nparts; i++) FertPart_parseLayout(c->parts[i], ini);
    const char *st = IniData_Get(ini, "state");
    if (st) {
        int k = FertControl_findState(c, st);
        if (k >= 0) c->state = k;   /* 0x524e00 */
    }
    FertControl_Update(c);   /* (slot 14) */
}

bool (*Fert_MeasureText)(const FertPart *p, int32_t *w, int32_t *h);

void FertControl_Update(FertControl *c)
{
    FertSyms_Clear();
    for (int i = 0; i < c->attrs.n; i++) FertAttr_export(c->attrs.a[i], "control.");
    for (int k = 0; k < c->nparts; k++) {
        FertPart *p = c->parts[k];
        char prefix[FERT_NAME + 2];
        snprintf(prefix, sizeof prefix, "%s.", p->name);   /* 0x52a200 */
        for (int i = 0; i < p->attrs.n; i++) FertAttr_export(p->attrs.a[i], prefix);
    }
    for (int i = 0; i < c->attrs.n; i++) FertAttr_eval(c->attrs.a[i]);
    for (int k = 0; k < c->nparts; k++) {
        FertPart *p = c->parts[k];
        /* (a text part evaluates twice when the first took over 3 timer ticks: not in the port) */
        for (int i = 0; i < p->attrs.n; i++) FertAttr_eval(p->attrs.a[i]);
        int32_t w, h;
        if ((p->kind == FPART_TEXT || p->kind == FPART_LOCALIZEDTEXT) && Fert_MeasureText && Fert_MeasureText(p, &w, &h)) {
            FertAttr *sz = FertAttrList_find(&p->attrs, "size");
            if (sz) FertAttr_setPoint(sz, w, h);   /* 0x527310 */
        }
    }
}

static void add_child(FertControl *w, FertControl *c)   /* the window's slot 22 */
{
    FertControl **a = realloc(w->children, (size_t)(w->nchildren + 1) * sizeof *a);
    if (!a) return;
    w->children = a;
    a[w->nchildren++] = c;
}

/* ---------------------------------------------------------------- the registry */

typedef struct {
    FertTemplate *t;
    int n;
} TemplateList;

static TemplateList s_controls, s_screens;   /* 0x69caf4 +0, +0xc */

typedef struct {
    const char *dir;
    const char *ext;
    TemplateList *out;
    char (*names)[260];
    int n, cap;
} ListCtx;

static void on_file(const char *name, bool is_dir, void *user)
{
    ListCtx *x = user;
    size_t l = strlen(name), e = strlen(x->ext);
    if (is_dir || l < e || strcasecmp(name + l - e, x->ext)) return;
    if (x->n == x->cap) {
        int cap = x->cap ? x->cap * 2 : 64;
        char (*a)[260] = realloc(x->names, (size_t)cap * sizeof *a);
        if (!a) return;
        x->names = a;
        x->cap = cap;
    }
    snprintf(x->names[x->n++], 260, "%s", name);
}

static int cmp_names(const void *a, const void *b) { return strcasecmp(a, b); }

/* (the directory's order is FindFirstFile's; the port sorts the names as NTFS lists them) */
static void list_kind(const char *dir, const char *ext, TemplateList *out)
{
    ListCtx x = {dir, ext, out, NULL, 0, 0};
    vfs_list(dir, on_file, &x);
    qsort(x.names, (size_t)x.n, sizeof *x.names, cmp_names);
    for (int i = 0; i < x.n; i++) {
        char path[600];
        snprintf(path, sizeof path, "%s\\%s", dir, x.names[i]);
        size_t len = 0;
        uint8_t *text = vfs_read_all(path, &len);
        if (!text) continue;
        IniFile f = {0};
        bool ok = IniFile_Parse(&f, (const char *)text, (uint32_t)len);
        const char *id = NULL, *d = NULL;
        if (ok) {
            IniData_LoadSection(f.ini, 0);
            id = IniData_Get(f.ini, "id");
            d = IniData_Get(f.ini, "description");
        }
        if (id && d) {
            FertTemplate *a = realloc(out->t, (size_t)(out->n + 1) * sizeof *a);
            if (a) {
                out->t = a;
                FertTemplate *t = &a[out->n++];
                memset(t, 0, sizeof *t);
                copyname(t->id, id);
                snprintf(t->description, sizeof t->description, "%s", d);
                t->text = text;
                t->len = (uint32_t)len;
                text = NULL;
            }
        }
        IniFile_Free(&f);
        free(text);
    }
    free(x.names);
}

int Fert_ListControls(const char *dir)
{
    Fert_FreeTemplates();
    list_kind(dir, ".ctr", &s_controls);
    list_kind(dir, ".scr", &s_screens);
    return s_controls.n + s_screens.n;
}

int Fert_ControlCount(void) { return s_controls.n; }
int Fert_ScreenCount(void) { return s_screens.n; }
const FertTemplate *Fert_Control(int i) { return i >= 0 && i < s_controls.n ? &s_controls.t[i] : NULL; }
const FertTemplate *Fert_Screen(int i) { return i >= 0 && i < s_screens.n ? &s_screens.t[i] : NULL; }

void Fert_FreeTemplates(void)
{
    TemplateList *ls[2] = {&s_controls, &s_screens};
    for (int k = 0; k < 2; k++) {
        for (int i = 0; i < ls[k]->n; i++) free(ls[k]->t[i].text);
        free(ls[k]->t);
        ls[k]->t = NULL;
        ls[k]->n = 0;
    }
}

FertControl *(*Fert_ControlFactory)(const char *id);

static const FertTemplate *find(const TemplateList *l, const char *id)
{
    for (int i = 0; i < l->n; i++)
        if (!strcmp(l->t[i].id, id)) return &l->t[i];
    return NULL;
}

static FertControl *make(const FertTemplate *t, FertControl *c)
{
    if (!c) return NULL;
    IniFile f = {0};
    if (!t->text || !IniFile_Parse(&f, (const char *)t->text, t->len)) {
        IniFile_Free(&f);
        FertControl_free(c);
        return NULL;
    }
    FertControl_parseTemplate(c, f.ini);   /* (slot 11) */
    IniFile_Free(&f);
    return c;
}

FertControl *Fert_MakeControl(const char *id)
{
    const FertTemplate *t = find(&s_controls, id);
    if (!t) return NULL;
    FertControl *c = Fert_ControlFactory ? Fert_ControlFactory(id) : NULL;   /* 0x4c6f90 */
    if (!c) c = FertControl_new(false);
    return make(t, c);
}

FertControl *Fert_MakeScreen(const char *id)
{
    const FertTemplate *t = find(&s_screens, id);
    return t ? make(t, FertControl_new(false)) : NULL;
}

/* ---------------------------------------------------------------- windows and layouts */

bool FertWindow_LoadLayout(FertControl *w, const char *path)
{
    size_t len = 0;
    uint8_t *text = vfs_read_all(path, &len);
    if (!text) return false;
    IniFile f = {0};
    if (!IniFile_Parse(&f, (const char *)text, (uint32_t)len)) {
        IniFile_Free(&f);
        free(text);
        return false;
    }
    IniData *ini = f.ini;
    IniData_LoadSection(ini, 0);
    const char *id = IniData_Get(ini, "id");
    FertControl_free(w->screen);
    w->screen = Fert_MakeScreen(id ? id : "");
    if (w->screen) FertControl_parseLayout(w->screen, ini);
    for (int i = 1; i < ini->nsections; i++) {
        IniData_LoadSection(ini, i);
        const char *cid = IniData_Get(ini, "id");
        if (!cid) continue;
        FertControl *c = Fert_MakeControl(cid);
        if (!c) continue;
        add_child(w, c);
        IniData_LoadSection(ini, i);   /* (the template's parse used its own file) */
        FertControl_parseLayout(c, ini);
    }
    IniFile_Free(&f);
    free(text);
    return true;
}
