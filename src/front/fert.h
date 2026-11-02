/* FERT, the front end's runtime (0x524000-0x52c000, \frontend\fert\src\Window.cpp): controls made from
   templates (FEData\Controls\*.ctr, *.scr), each with typed attributes, parts and states, placed by
   layouts (FEData\Layouts\*.lay) into windows. docs/re/frontend.md.

   The original's classes are C++ with STL strings and vectors; the port keeps their fields as plain C
   (fixed name buffers, growable arrays) and their vtables where the game overrides them. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "render/car_ini.h"

/* ---- attributes (FERT::Attribute 0x5bb284 and its kinds) ---- */

typedef enum {
    FATTR_STRING,      /* StringAttribute 0x5bb07c */
    FATTR_INT,         /* IntAttribute 0x5bb094 */
    FATTR_POINT,       /* PointAttribute 0x5bb0ac */
    FATTR_COLOR,       /* ColorAttribute 0x5bb0c4 */
    FATTR_PATHNAME,    /* PathNameAttribute 0x5bb208 (a string of at most 0x104) */
    FATTR_LOCSTRING,   /* LocalizedTextAttribute 0x5b6e4c */
} FertAttrType;

enum { FERT_NAME = 64 };

typedef struct {
    FertAttrType type;
    char name[FERT_NAME];       /* +0x04 */
    char *value;                /* +0x10: the text as given */
    char screenkey[FERT_NAME];  /* +0x1c: the layout's key (the name unless .screenkey says) */
    char *label;                /* +0x28 */
    bool exposed;               /* +0x34: not HIDDEN: layouts may set it */
    int32_t maxlength;          /* string kinds' .maxlength (0x104 for path names) */
    int32_t minvalue, maxvalue; /* +0x3c, +0x40: INT's range (0x80000000, 0x7fffffff) */
    /* the last update's values (slot 4): a number (+0x38), a point's two (+0x38, +0x3c), a string's text
       with its %name% replaced */
    int32_t num;
    int32_t pt[2];
    char *str;
} FertAttr;

typedef struct {
    FertAttr **a;
    int n, cap;
} FertAttrList;

FertAttr *FertAttr_new(FertAttrType type, const char *name, const char *def);
void FertAttr_free(FertAttr *a);
void FertAttr_set(FertAttr *a, const char *value);
/* From a template's section (0x52bab0, then the kind's own keys): <name>.value, .screenkey, .label,
   .flags (HIDDEN keeps layouts off it). */
void FertAttr_parseTemplate(FertAttr *a, IniData *ini);
/* From a layout's section (0x52be20): the exposed ones take the value under their screen key. */
void FertAttr_parseLayout(FertAttr *a, IniData *ini);
/* Its text as symbols (slot 3): prefix+name = the text; a point's halves as .x and .y, and as .width
   and .height (0x527420). */
void FertAttr_export(const FertAttr *a, const char *prefix);
/* Its value from its text (slot 4): INT evaluated (0 when empty, 0x526f50); POINT's halves evaluated
   (0x5277b0; nothing without a comma); COLOR hexadecimal after "0x", else evaluated (0x527990); the
   string kinds with each %name% replaced by name's expansion (0x526790). */
void FertAttr_eval(FertAttr *a);
/* The last update's values. */
int32_t FertAttr_int(const FertAttr *a);
void FertAttr_point(const FertAttr *a, int32_t out[2]);
uint32_t FertAttr_color(const FertAttr *a);
const char *FertAttr_str(const FertAttr *a);
/* The text "x,y" and the values (0x527310: what a text part's measured size sets). */
void FertAttr_setPoint(FertAttr *a, int32_t x, int32_t y);
/* A number as the original's reader (0x523aa0): hexadecimal after "0x", else atoi. */
int32_t Fert_ParseInt(const char *s);

void FertAttrList_add(FertAttrList *l, FertAttr *a);
FertAttr *FertAttrList_find(const FertAttrList *l, const char *name);
void FertAttrList_free(FertAttrList *l);

/* ---- parts (FERT::Part 0x5bb1a0) ---- */

typedef enum {
    FPART_RECT,          /* "RECT": FERectPart 0x5b6e0c on FERT::RectPart 0x5bb1c0 */
    FPART_SHAPE,         /* "SHAPE": FEShapePart 0x5b6e64 on FERT::ShapePart 0x5bb1e0 */
    FPART_TEXT,          /* "TEXT": FETextPart 0x5b6e8c on FERT::TextPart 0x5bb220 */
    FPART_LOCALIZEDTEXT, /* "LOCALIZEDTEXT": FELocalizedTextPart 0x5b6e2c */
    FPART_DATA,          /* "DATA": FERT::DataPart 0x5bb240 */
    FPART_TEXTURE,       /* "TEXTURE" */
} FertPartKind;

typedef struct {
    FertPartKind kind;
    FertAttrList attrs;      /* +0x04 */
    char name[FERT_NAME];    /* +0x08 */
    char type[FERT_NAME];    /* +0x14: the id it was made from */
    /* a shape part's image (+0x198) and the bank it came from (+0x19c), the front end's */
    void *image;
    char bank[260];
    int32_t bankindex;
    bool hidden;             /* +0x1ac */
} FertPart;

/* A part of an id (the factory 0x4e5830), NULL for an unknown one. */
FertPart *FertPart_new(const char *id, const char *name);
void FertPart_free(FertPart *p);
/* Its section of the template (part.<name> selected): its attributes; a DATA part first makes the
   ones its attributes.value lists, by their .type. */
void FertPart_parseTemplate(FertPart *p, IniData *ini);
void FertPart_parseLayout(FertPart *p, IniData *ini);

/* ---- states (0x529ab0, 0x14 bytes) ---- */

typedef struct {
    char name[FERT_NAME];
    int *parts;             /* indices into the control's parts */
    int nparts;
} FertState;

/* ---- controls (FERT::Target 0x5bb028, FERT::Control 0x5bafd4, FERT::Window 0x5bb0e0) ---- */

/* A window's transition (FEMenu +0x124 .. +0x130: front/fetrans.c), and a control's transition flags
   (FEControl +0x118, +0x11c). */
typedef struct {
    int delay;                 /* +0x124: frames before it starts */
    uint32_t start;            /* +0x128: TIMER_ticks */
    int phase;                 /* +0x12c: 0 gone, 1 coming on, 2 on, 3 going off */
    int counter;               /* +0x130: 0x200 .. 0 (on) */
    bool live;                 /* (the port's: started at least once) */
} FertTrans;

typedef struct FertControl {
    char id[FERT_NAME];        /* the template's id */
    char name[FERT_NAME];      /* Target's name */
    char description[128];
    FertAttrList attrs;        /* +0xdc: pos, size, moveable, resizeable, autosize, state, then the template's */
    FertPart **parts;          /* +0xd8 */
    int nparts;
    FertState *states;         /* +0xd4 */
    int nstates;
    int state;                 /* the current state */
    struct FertControl **children;   /* a window's controls */
    int nchildren;
    struct FertControl *screen;      /* +0x11c: a window's screen (its layout's first section) */
    bool window;
    /* the front end's (FEControl): its flags (+0x10c: 2 keep, 4 hover, 8 selected, 0x10 disabled,
       0x20 hidden, 0x40 focused), the states it has (+0x110) */
    uint32_t feflags, fecaps;
    struct FertControl *hover;       /* +0x110 of a window: the control under the mouse */
    struct FertControl *focus;       /* +0x120 of a window */
    int32_t mouse[2];                /* +0x114 */
    struct FertControl *sub;         /* a panel's screen (FEPanel +0x14c: its layout "filename", open) */
    FertTrans trans;                 /* a window's */
    uint32_t transon, transoff;      /* a control's (+0x118, +0x11c) */
    bool trans_read;
} FertControl;

FertControl *FertControl_new(bool window);   /* 0x523f60 (Control), 0x527bc0 (Window) */
void FertControl_free(FertControl *c);
/* Its template (FERT_Control_Parse 0x5246a0): [control]'s attributes, then each part of parts= from
   [part.<name>], then each state of states= from [state.<name>]. */
void FertControl_parseTemplate(FertControl *c, IniData *ini);
/* Its layout section (0x524a90): name, the attributes' and the parts' layout values, the state named;
   then an update. */
void FertControl_parseLayout(FertControl *c, IniData *ini);
/* Its attributes' values (FERT_Control_v14 0x524f70): the symbols emptied; the control's attributes
   defined as control.<name>, each part's as <part>.<name>; then the control's attributes evaluated, then
   each part's (a text part then measures its text: Fert_MeasureText). */
void FertControl_Update(FertControl *c);
/* The front end's text measuring for a text part (FETextPart_v4 0x4e6800): the size of its text in its
   font; false without a font. */
extern bool (*Fert_MeasureText)(const FertPart *p, int32_t *w, int32_t *h);
/* The front end's part classes after their attributes' evaluation (a shape part loads its image,
   FEShapePart_v8 0x4e6140) and as they go. */
extern void (*Fert_PartEvalHook)(FertPart *p);
extern void (*Fert_PartFreeHook)(FertPart *p);
int FertControl_findPart(const FertControl *c, const char *name);
/* A part's rectangle (slot 2: its pos to pos + size; none for DATA) and the control's (0x524c40: (0, 0)
   to its size, or with autosize the union of its state's parts' with (0, 0); then at its pos). */
void FertPart_rect(const FertPart *p, int32_t r[4]);
void FertControl_rect(const FertControl *c, int32_t r[4]);
/* Whether (x, y) is in it, edges included, a rectangle of no width never (0x525310). */
bool FertControl_contains(const FertControl *c, int32_t x, int32_t y);
/* The window's first control at (x, y) (0x528830; the window's pos taken off). */
FertControl *FertWindow_hit(FertControl *w, int32_t x, int32_t y);
/* The state by name, if it has one (0x524e00 by its index). */
bool FertControl_setState(FertControl *c, const char *name);
int FertControl_findState(const FertControl *c, const char *name);

/* ---- the template registry (FERT_ListControls 0x52c140; the global 0x69caf4) ---- */

/* A template (0x1c bytes): its [control] id and description, and the file, read whole. */
typedef struct {
    char id[FERT_NAME];
    char description[128];
    uint8_t *text;              /* +0x18 */
    uint32_t len;
} FertTemplate;

/* Every *.ctr (the controls) and *.scr (the screens) of the directory, by their [control] id and
   description; the count of both. */
int Fert_ListControls(const char *dir);
int Fert_ControlCount(void);    /* 0x52c830 */
int Fert_ScreenCount(void);     /* 0x52c960 */
const FertTemplate *Fert_Control(int i);
const FertTemplate *Fert_Screen(int i);
void Fert_FreeTemplates(void);

/* The front end's control classes by template id (0x4c6f90); NULL for a plain FERT::Control. Set by
   the front end as its classes are ported. */
extern FertControl *(*Fert_ControlFactory)(const char *id);
/* A control of a template id, made and parsed from its template (0x52c860); NULL if the id is unknown
   (ids compare exactly). */
FertControl *Fert_MakeControl(const char *id);
/* A screen of a .scr id (0x52c990): a plain control. */
FertControl *Fert_MakeScreen(const char *id);

/* ---- layouts (FERT_ListLayouts 0x528900; windows from .lay files) ---- */

/* A window's layout (FERT_Window_v21 0x5280d0): the first section's id makes its screen (with that
   section's values), every later section with an id a control of that template, added to the window,
   then given the section's values. False if the file can't be read (the original aborts). */
bool FertWindow_LoadLayout(FertControl *w, const char *path);
