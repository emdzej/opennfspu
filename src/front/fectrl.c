/* The controller setup (fectrl.h). */
#include "front/fectrl.h"
#include "exe.h"
#include "front/fe2d.h"
#include "front/fe_parts.h"
#include "front/feapp.h"
#include "real/font.h"
#include "game/controls.h"
#include "game/input.h"
#include "pending.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* An FEControllerInputAccessor's input (+0x14, six words): the type (0 none, 1 an axis, 2 a button, 3 a
   key, 5 a hat), the device, the axis, button or key (the scan code) or hat (0x20 + n), the axis's way
   (+1, -1), the hat's direction (eighths), the key's event code (its name's). */
typedef struct {
    int32_t type, dev, idx, dir, pov, key;
} CtrlInput;

/* FEdaControllers' (0x51d8b0), in 0x51d540's order: the binding each commits (slot 1, 0x51c480). */
enum { NCTRL = 19 };
static const struct { const char *name; int id; } k_ctrl[NCTRL] = {
    {"Controller.Accel", 0x152},          {"Controller.Brake", 0x153},           {"Controller.HandBrake", 0x154},
    {"Controller.SteerLeft", 0x150},      {"Controller.SteerRight", 0x151},      {"Controller.Horn", 0x155},
    {"Controller.ShiftUp", 0x169},        {"Controller.ShiftDown", 0x16a},       {"Controller.LookBehind", 0x201},
    {"Controller.Lights", 0x174},         {"Controller.ResetCar", 0x16c},        {"Controller.CameraView", 0x178},
    {"Controller.Pause", 0x1f6},          {"Controller.LeftTurnSignal", 0x171},  {"Controller.RightTurnSignal", 0x172},
    {"Controller.Hazards", 0x173},        {"Controller.LookLeft", 0x202},        {"Controller.LookRight", 0x203},
    {"Controller.LookForward", 0x204},
};
static CtrlInput s_in[NCTRL];
static bool s_loaded;

/* The devices' records: as the setup was pressed (0x69c9e8) and now (0x69c9ec), 0x104 bytes each. */
enum { NDEVS = 8, REC = 0x104 };
static uint8_t s_then[NDEVS][REC], s_now[NDEVS][REC];

static int32_t rd32(const uint8_t *r, int off) { int32_t v; memcpy(&v, r + off, 4); return v; }

/* The key's event code as the window's key message gives it (0x53b2a0): the navigation keys and the
   function keys their scan code << 8 (F11 0x8700, F12 0x8800), others their character, 0 none. */
static int32_t key_event(uint8_t dik, bool shift)
{
    uint8_t s = dik & 0x7f;
    if ((dik & 0x80) && (s == 0x47 || s == 0x48 || s == 0x49 || s == 0x4b || s == 0x4d || s == 0x4f || s == 0x50 ||
                         s == 0x51 || s == 0x52 || s == 0x53))
        return s << 8;
    if (0x3b <= dik && dik <= 0x44) return dik << 8;
    if (dik == 0x57) return 0x8700;
    if (dik == 0x58) return 0x8800;
    if (dik == 0x01) return 0x1b;
    if (dik == 0x0f) return 9;
    return Input_KeyChar(dik, shift);
}

/* A key's code from its scan code alone (port: the inputs are rebuilt from fe.txt's bindings, which
   keep only the scan code): the arrows' and the others' of the navigation block as the extended keys. */
static int32_t key_of_scan(int scan)
{
    if (scan == 0x47 || scan == 0x48 || scan == 0x49 || scan == 0x4b || scan == 0x4d || scan == 0x4f ||
        scan == 0x50 || scan == 0x51 || scan == 0x52 || scan == 0x53)
        return scan << 8;
    return key_event((uint8_t)scan, false);
}

/* 0x51b8d0: a key's name and its arrow (arc 0 up, 2 right, 4 down, 6 left, -1 none); 0 when it can't be
   bound: the letters (upper case), space, a few signs, the navigation keys; not the digits or the
   function keys. */
static int key_name(int32_t k, char *out, size_t cap, int *arc)
{
    out[0] = 0;
    *arc = -1;
    int sid = 0;
    uint32_t lit = 0;
    switch (k) {
    case 0x4800: *arc = 0; return 0x4801;
    case 0x4d00: *arc = 2; return 0x4d01;
    case 0x5000: *arc = 4; return 0x5001;
    case 0x4b00: *arc = 6; return 0x4b01;
    case 0x4700: sid = 0xc9; break;
    case 0x4900: sid = 0xca; break;
    case 0x5100: sid = 0xcb; break;
    case 0x4f00: sid = 0xcc; break;
    case 0x5200: sid = 0xcd; break;
    case 0x5300: sid = 0xce; break;
    case 0x20: sid = 0xcf; break;
    case 0x2c: lit = 0x5dd470; break;
    case 0x2e: lit = 0x5cd1f4; break;
    case 0x2f: lit = 0x5ccde0; break;
    case 0x3b: lit = 0x5dd478; break;
    case 0x3d: lit = 0x5dd46c; break;
    case 0x5b: lit = 0x5d0b34; break;
    case 0x5c: lit = 0x5d11e8; break;
    case 0x5d: lit = 0x5dd474; break;
    case 0x60: lit = 0x5dd468; break;
    case 0xc4: case 0xe4: lit = 0x5dd464; break;
    case 0xc5: case 0xe5: lit = 0x5dd45c; break;
    case 0xd6: case 0xf6: lit = 0x5dd460; break;
    default:
        if (0x60 < k && k < 0x7b) k -= 0x20;
        if (0x40 < k && k < 0x5b) {
            snprintf(out, cap, "%c", (char)k);   /* ("%c" 0x5dd458) */
            return 1;
        }
        return 0;
    }
    snprintf(out, cap, "%s", sid ? FEStrings_Get(sid) : exe_str(lit));
    return 1;
}

/* The strings' own printf-style fields, %d and %s, filled in order (0x4e0270's arguments). */
static void fmt(char *out, size_t cap, const char *f, const char *s, int d)
{
    size_t n = 0;
    for (; f && *f && n + 1 < cap; f++) {
        if (f[0] == '%' && (f[1] == 'd' || f[1] == 's')) {
            char t[64];
            if (f[1] == 'd') snprintf(t, sizeof t, "%d", d);
            else snprintf(t, sizeof t, "%s", s ? s : "");
            for (const char *p = t; *p && n + 1 < cap; p++) out[n++] = *p;
            f++;
        } else {
            out[n++] = *f;
        }
    }
    out[n] = 0;
}

/* ---- the inputs and the bindings ---- */

/* A binding's input (port: the inverse of the commit, slot 1 0x51c480, as fe.txt keeps the bindings and
   not the player's save the accessors' inputs, 0x4b7dc0). */
static CtrlInput from_binding(uint32_t b)
{
    CtrlInput in = {0};
    switch (b & 0xff) {
    case 1:
        in = (CtrlInput){1, (int32_t)(b >> 28), (int32_t)(b >> 24 & 0xf), (b >> 8 & 0xff) > (b >> 16 & 0xff) ? 1 : -1, 0, 0};
        break;
    case 2: in = (CtrlInput){2, (int32_t)(b >> 24), (int32_t)(b >> 8 & 0xffff), 0, 0, 0}; break;
    case 3: in = (CtrlInput){3, 0, (int32_t)(b >> 8 & 0xff), 0, 0, key_of_scan((int)(b >> 8 & 0xff))}; break;
    case 5: in = (CtrlInput){5, (int32_t)(b >> 24), 0x20, 0, (int32_t)(b >> 8 & 0xffff), 0}; break;
    default: break;
    }
    return in;
}

/* The commit (slot 1, 0x51c480): an axis from 0x80 to 0x80 + 0x7f its way (the dead zone's accessors,
   XAxis_Min .., pend), a button, a key's scan code, a hat's direction. */
static uint32_t to_binding(const CtrlInput *in)
{
    switch (in->type) {
    case 1: {
        uint32_t lo = 0x80, hi = (uint32_t)(in->dir * 0x7f + 0x80);
        return ((((uint32_t)in->dev << 4 | (uint32_t)in->idx) << 8 | lo) << 8 | hi) << 8 | 1;
    }
    case 2: return ((uint32_t)in->dev << 16 | (uint32_t)in->idx) << 8 | 2;
    case 3: return (uint32_t)in->idx << 8 | 3;
    case 5: return ((uint32_t)in->dev << 16 | (uint32_t)in->pov) << 8 | 5;
    default: return 0;
    }
}

static void load(void)
{
    if (s_loaded) return;
    s_loaded = true;
    Controls_Load();   /* (the devices listed) */
    for (int i = 0; i < NCTRL; i++) s_in[i] = from_binding(Controls_Option(k_ctrl[i].id));
}

static void commit(void)
{
    for (int i = 0; i < NCTRL; i++)
        if (to_binding(&s_in[i]) != Controls_Option(k_ctrl[i].id)) Controls_SetOption(k_ctrl[i].id, to_binding(&s_in[i]));
    PENDING_NOTE(0x51c480, "the pedals' and the steering's analog options (0x222 .. 0x224)");
}

static int find(const char *name)
{
    for (int i = 0; name && i < NCTRL; i++)
        if (!strcasecmp(name, k_ctrl[i].name)) return i;
    return -1;
}

/* 0x51d4a0: the same input (a key by its scan code and its code, a hat by its direction too). */
static bool same(const CtrlInput *a, const CtrlInput *b)
{
    if (a->type != b->type) return false;
    if (a->type == 3) return a->dev == b->dev && a->key == b->key && a->idx == b->idx;
    if (a->type == 5) return a->dev == b->dev && a->idx == b->idx && a->dir == b->dir && a->pov == b->pov;
    return a->dev == b->dev && a->idx == b->idx && a->dir == b->dir;
}

/* The input given to accessor k: unchanged when it's its own already, else taken from any other that had
   it (0x51d540: those left without one) and set; the bindings written. */
static void assign(int k, const CtrlInput *in)
{
    if (same(in, &s_in[k])) return;
    for (int i = 0; i < NCTRL; i++)
        if (same(&s_in[i], in)) s_in[i].type = 0;
    s_in[k] = *in;
    commit();
}

/* ---- the devices ---- */

/* The joysticks in the list's order (0x51e9f0): the attached devices of type 4 before n. */
static int joy_number(int dev)
{
    int k = 0;
    for (int i = 0; i < Controls_Devices(); i++) {
        if (Controls_DeviceType(i) != 4) continue;
        if (i == dev) return k;
        k++;
    }
    return 0;
}

static bool attached(int dev) { return 0 <= dev && dev < Controls_Devices(); }   /* 0x51eb10 */

static void snapshot(uint8_t dst[NDEVS][REC])   /* 0x51e880 */
{
    for (int i = 0; i < NDEVS; i++) {
        const uint8_t *r = Controls_DeviceState(i);
        if (r) memcpy(dst[i], r, REC);
        else memset(dst[i], 0, REC);
    }
}

/* 0x51ec30: a joystick's change since the press: a button (any changed), a hat (its direction), an axis
   moved 5000 either way (its way; the sliders' by the z rotation's, as the original compares). The
   last change found in the device wins. */
static bool joy_change(CtrlInput *in)
{
    for (int d = 0; d < Controls_Devices() && d < NDEVS; d++) {
        const uint8_t *now = s_now[d], *was = s_then[d];
        if (rd32(now, 0) != 4) continue;
        in->dev = d;
        in->idx = -1;
        for (int i = 0; i < 0x20; i++)
            if (now[0x34 + i] != was[0x34 + i]) {
                in->type = 2;
                in->idx = i;
            }
        for (int i = 0; i < 4; i++)
            if (rd32(now, 0x24 + 4 * i) != rd32(was, 0x24 + 4 * i)) {
                in->type = 5;
                in->idx = i + 0x20;
                in->pov = rd32(now, 0x24 + 4 * i) / 4500;
            }
        for (int a = 0; a < 8; a++) {
            int32_t n = rd32(now, 4 + 4 * a), w = rd32(was, 4 + 4 * a);
            if (w + 5000 <= n || n <= w - 5000) {
                int32_t wn = a < 6 ? n : rd32(now, 0x18), ww = a < 6 ? w : rd32(was, 0x18);
                in->type = 1;
                in->idx = a;
                in->dir = wn < ww + 5000 ? -1 : 1;
            }
        }
        if (in->idx >= 0) return true;
    }
    return false;
}

/* ---- the control (FEControllerSetupControl, vtable 0x5bac84) ---- */

static const char *data(const FertControl *c, const char *name)
{
    for (int k = 0; k < c->nparts; k++) {
        FertAttr *a = FertAttrList_find(&c->parts[k]->attrs, name);
        if (a) return FertAttr_str(a);
    }
    return NULL;
}

static int ctrl_of(const FertControl *c)
{
    return strcmp(c->id, "NFS5.CONTROLLERSETUP") ? -1 : find(data(c, "accessor"));
}

/* Its text (slot 15 0x51bef0): while waiting, string 0x25b; else the input: an axis "joystick n" and
   the axis's name with its way's arrow, a button "joystick n" and its number, a key its name (0xd0)
   and arrow, a hat "joystick n", its number and direction's arrow. */
static void text_of(int k, char *t, size_t cap, int *arc, bool waiting)
{
    char j[96];
    *arc = -1;
    t[0] = 0;
    if (waiting) {
        snprintf(t, cap, "%s", FEStrings_Get(0x25b));
        return;
    }
    const CtrlInput *in = &s_in[k];
    switch (in->type) {
    case 1: {
        static const int sid[8] = {0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8};
        fmt(j, sizeof j, FEStrings_Get(0xdb), NULL, joy_number(in->dev) + 1);
        if (in->idx < 0 || in->idx > 7) {
            snprintf(t, cap, "%s", j);
            break;
        }
        snprintf(t, cap, "%s %s", j, FEStrings_Get(sid[in->idx]));   /* ("%s %s" 0x5da730) */
        bool pos = in->dir == 1;
        switch (in->idx) {
        case 1: case 4: *arc = pos ? 4 : 0; break;   /* (y: down, up) */
        case 2: case 5: *arc = pos ? 0 : 4; break;   /* (z: up, down) */
        default: *arc = pos ? 2 : 6; break;          /* (x, the sliders: right, left) */
        }
        break;
    }
    case 2:
        fmt(j, sizeof j, FEStrings_Get(0xdb), NULL, joy_number(in->dev) + 1);
        fmt(t, cap, FEStrings_Get(0xda), j, in->idx + 1);
        break;
    case 3: {
        char name[52];
        key_name(in->key, name, sizeof name, arc);
        fmt(t, cap, FEStrings_Get(0xd0), name, 0);
        break;
    }
    case 5:
        fmt(j, sizeof j, FEStrings_Get(0xdb), NULL, joy_number(in->dev) + 1);
        fmt(t, cap, FEStrings_Get(0xd9), j, in->idx - 0x1f);
        *arc = in->pov;
        break;
    default:
        break;
    }
}

bool FECtrl_Draw(FertControl *c, int32_t ox, int32_t oy)
{
    int k = ctrl_of(c);
    if (k < 0) return false;
    if (c->feflags & 0x20) return true;
    load();
    bool waiting = (c->feflags & 0x40) != 0;
    if (!waiting && s_in[k].type && !attached(s_in[k].dev)) {
        PENDING_NOTE(0x51ca50, "FEControllers_Map (a missing device's inputs back to the defaults)");
    }
    FEControl_Draw(c, ox, oy);
    int32_t p[2] = {0, 0}, r[4];
    FertAttr *pa = FertAttrList_find(&c->attrs, "pos");   /* 0x524b90 */
    if (pa) FertAttr_point(pa, p);
    int x = p[0] + ox, y = p[1] + oy;
    FertControl_rect(c, r);   /* 0x4c8240 */
    char t[200];
    int arc;
    text_of(k, t, sizeof t, &arc, waiting);
    int a = 0xff;   /* (0x4c8410: the control's alpha) */
    float right = (float)(r[2] - r[0] - 4 + x), shift = 0;
    if (arc != -1) {
        char path[300], nm[16];
        FE_ResolvePath("weeicons.fsh", path, sizeof path);   /* ("%sweeicons.fsh" 0x5d7bf8) */
        FEShapes_Load(path);
        snprintf(nm, sizeof nm, "arc%d", arc);   /* ("arc%d" 0x5dd4a8) */
        FeImage *im = FEShapes_Named(path, nm);
        if (im) {
            FE2D_DepthStep();
            FE2D_Image(im, right - (float)im->w, (float)(y + 2), (float)im->w, (float)im->h, (uint32_t)(a * 0xd0 / 0xff) << 24 | 0xffffffu, 0, false);
            shift = (float)(-4 - im->w);
        }
    }
    char fp[300];
    FE_ResolvePath("univers8b.ffn", fp, sizeof fp);   /* ("%sunivers8b.ffn" 0x5d81ac) */
    HudFont *f = FEFont_Get(fp);
    if (f && f->font && t[0]) {
        float fx, fy, fw, fh;
        FONT_extent(f->font, t, &fx, &fy, &fw, &fh);
        char *copy = FE2D_Alloc((int)strlen(t) + 1);
        if (copy) {
            strcpy(copy, t);
            FE2D_Text(f, right + shift - fw, (float)(y + 2), 0, (uint32_t)a << 24 | 0xffffffu, copy, 1.0f, 1.0f);   /* 0x4d9fe0, align 2 */
        }
    }
    float w = (float)(r[2] - r[0]), h = (float)(r[3] - r[1]);
    if ((waiting || (c->feflags & 4)) && !(c->feflags & 0x10)) FEStyle_Inset((float)x, (float)y, w, h, a, 3);   /* 0x5208d0 */
    else FEStyle_Shine((float)x, (float)y, w, h, a);   /* 0x521090 */
    return true;
}

/* A press (slot 9 0x51be70): the focus taken, the devices' records kept as they are. */
bool FECtrl_Click(FertControl *c)
{
    if (ctrl_of(c) < 0) return false;
    load();
    Controls_Load();   /* (port: the devices listed again, a browser's pad showing after a press) */
    c->feflags |= 0x40;   /* 0x4c7940 */
    FEApp_RefreshState(c);
    snapshot(s_now);
    memcpy(s_then, s_now, sizeof s_then);   /* 0x51eb90 */
    return true;
}

static void unfocus(FertControl *c)   /* 0x4c7970 */
{
    c->feflags &= ~0x40u;
    FEApp_RefreshState(c);
}

/* Its tick (slot 1 0x51bd60): the devices' records now; while waiting, a joystick's change is the input
   (a press of one already its own lets the wait go). */
static void tick(FertControl *c)
{
    int k = ctrl_of(c);
    if (k < 0) return;
    snapshot(s_now);
    if (!(c->feflags & 0x40)) return;
    CtrlInput in = {0};
    if (!joy_change(&in)) return;
    assign(k, &in);
    unfocus(c);
}

void FECtrl_Tick(FertControl *w)
{
    for (int i = 0; w && i < w->nchildren; i++) {
        FertControl *c = w->children[i];
        if (c->sub) FECtrl_Tick(c->sub);
        tick(c);
    }
}

FertControl *FECtrl_Waiting(FertControl *w)
{
    for (int i = 0; w && i < w->nchildren; i++) {
        FertControl *c = w->children[i];
        if (c->sub) {
            FertControl *f = FECtrl_Waiting(c->sub);
            if (f) return f;
        }
        if (ctrl_of(c) >= 0 && (c->feflags & 0x40)) return c;
    }
    return NULL;
}

/* A key to the waiting control (slot 4 0x51be90, 0x51c920): only with the one key held; one without a
   name keeps it waiting; keypad '.' (Delete) as Delete. */
void FECtrl_Keys(FertControl *c, const uint8_t *k, int nk)
{
    uint8_t held[KEY_COUNT] = {0};
    if (!c || !plat_keys(held)) return;
    int scan = -1, n = 0;
    uint8_t table[0x80] = {0};
    for (int i = 0; i < KEY_COUNT; i++)
        if (held[i]) table[i & 0x7f] = 1;
    for (int i = 0; i < 0x80; i++)
        if (table[i]) scan = i, n++;
    if (n != 1 || nk == 0) return;
    bool shift = held[0x2a] || held[0x36];
    int32_t code = key_event(k[nk - 1], shift);
    if (code == 0x2e && scan == 0x53) code = 0x5300;
    char name[52];
    int arc;
    if (!key_name(code, name, sizeof name, &arc)) return;
    CtrlInput in = {3, 0, scan, 0, 0, code};
    assign(ctrl_of(c), &in);
    unfocus(c);
}
