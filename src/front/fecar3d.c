/* The screens' 3D car control's interactive part: see fecar3d.h. */
#include "front/fecar3d.h"
#include "app.h"
#include "exe.h"
#include "front/fe3d.h"
#include "front/feaccess.h"
#include "game/audio.h"
#include "render/r3dcar.h"
#include <string.h>

/* The parts: 0 the roof, 1 and 2 the doors, 3 the hood, 4 the trunk. */
enum { ROOF, DOOR1, DOOR2, HOOD, TRUNK, NPARTS };

static struct {
    FertControl *c;
    FertControl *window;     /* (the window it holds the mouse of) */
    int32_t roof;            /* +0x140: Car.Roof's position last seen */
    uint8_t armed[2];        /* +0x1ae, +0x1af: a door opened by a click (the next one goes inside) */
    uint8_t inside;          /* +0x1b0: the camera in the car */
    int32_t inside_at;       /* +0x1b4 */
    uint8_t moving[NPARTS];  /* +0x39c */
    int32_t open[NPARTS];    /* +0x3a4: 0 shut .. 255 open */
    int32_t dir[NPARTS];     /* +0x3b8: 1 opening, -1 closing, 0 never moved */
    int32_t start[NPARTS];   /* +0x3cc */
    int part_over;           /* +0x3e0: the part under the mouse (v8) */
    int32_t mouse[2];        /* +0x3e4, +0x3e8 */
} s;

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static int32_t timer_ticks(void) { return (int32_t)((uint64_t)app_ticks_ms() * 128u / 1000u); }   /* TIMER_ticks 0x53c050 */

static bool interactive(const FertControl *c)
{
    if (!c || strcmp(c->id, "NFS5.3DCAR")) return false;
    for (int k = 0; k < c->nparts; k++) {   /* (position type 0: the room's car) */
        FertAttr *a = FertAttrList_find(&c->parts[k]->attrs, "positiontype");
        if (a && FertAttr_int(a) != 0) return false;
    }
    return true;
}

/* A new control (v20 0x4bf870): every part shut, the camera outside. */
static void attach(FertControl *c)
{
    if (s.c == c) return;
    memset(&s, 0, sizeof s);
    s.c = c;
}

static void rect(const FertControl *c, int32_t r[4])   /* 0x4c8240 */
{
    int32_t pos[2] = {0, 0}, size[2] = {0, 0};
    FertAttr *a = FertAttrList_find(&c->attrs, "pos");
    if (a) FertAttr_point(a, pos);
    a = FertAttrList_find(&c->attrs, "size");
    if (a) FertAttr_point(a, size);
    r[0] = pos[0], r[1] = pos[1], r[2] = pos[0] + size[0], r[3] = pos[1] + size[1];
}

/* The part under (x, y) (0x4c08f0): with a roof to fold, the control's top 5/16 is the roof; else by the
   car's turn (its fraction past the whole turns, as the original's x87 minus its truncation: negative
   for a negative turn), the trunk facing away (above 0.9 or to 0.1), the hood facing the camera (0.4 ..
   0.6), side on the ends' sixths of the control the hood and the trunk and the middle a door (the
   first door's side 0.6 .. 0.9, the second's 0.1 .. 0.4). No room car: 0. */
static int pick(const FertControl *c, int32_t x, int32_t y)
{
    int32_t r[4];
    rect(c, r);
    R3DCar *car = FE3D_Car();
    if (car && car->anim[1].frames != 0 && y <= (r[3] - r[1]) * 5 / 16 + r[1]) return ROOF;
    if (!car) return ROOF;
    float yaw = FE3D_CarYaw();   /* (slot 45) */
    float f = yaw - (float)(int32_t)yaw;
    float hi = exe_f32(0x5b25e8), lo = exe_f32(0x5b2604), mid0 = exe_f32(0x5b261c), mid1 = exe_f32(0x5b2678);
    int part = ROOF;
    if (hi < f || f <= lo) part = TRUNK;
    if (mid0 < f && f <= mid1) part = HOOD;
    if (mid1 < f && f <= hi) {
        int32_t six = (r[2] - r[0]) / 6;
        if (x < six + r[0]) part = HOOD;
        else part = x <= r[2] - six ? DOOR1 : TRUNK;
    }
    if (lo < f && f <= mid0) {
        int32_t six = (r[2] - r[0]) / 6;
        if (x < six + r[0]) return TRUNK;
        return r[2] - six < x ? HOOD : DOOR2;
    }
    return part;
}

/* The window holds the mouse for it (0x4c7940, its focused state's) or lets go (0x4c7970). */
static void capture(bool on)
{
    if (!s.window) return;
    if (on && FertControl_findState(s.c, "focused") < 0) return;   /* (+0x110 & 0x40) */
    s.window->focus = on ? s.c : NULL;
}

bool FECar3D_Captured(void) { return s.c && s.window && s.window->focus == s.c; }

/* The part starts moving: open when shut, else shut. */
static void start(int part, int32_t now)
{
    s.moving[part] = 1;
    s.start[part] = now;
    s.dir[part] = s.open[part] == 0 ? 1 : -1;
}

bool FECar3D_Click(FertControl *c, int32_t x, int32_t y)
{
    (void)y;
    if (!interactive(c)) return false;
    attach(c);
    R3DCar *car = FE3D_Car();
    if (!car || !car->loaded) return false;   /* (+0x1ac and the car's file there, 0x4f0030) */
    int part = pick(c, x, y);
    int32_t now = timer_ticks();
    if (FECar3D_Captured()) {   /* out of the car */
        if (now - s.inside_at < 0x41) return true;
        s.inside = 0;
        s.inside_at = now;
        capture(false);
        s.armed[part != DOOR1] = 0;
        return true;
    }
    if (s.moving[part]) return true;
    if (part == ROOF) {   /* Car.Roof put the other way (v0x2c), its new place kept */
        FEAccessor *roof = FEAccess_Find("Car.Roof");
        if (roof) {
            FEAccess_SetPos(roof, s.open[ROOF] == 0 ? 1 : 0);
            s.roof = FEAccess_Pos(roof);
        }
    } else if (part == DOOR1 || part == DOOR2) {
        int d = part != DOOR1;
        if (!s.armed[d]) {
            if (s.dir[part] != 1) s.armed[d] = 1;
        } else if (0x40 < now - s.inside_at) {   /* the open door clicked again: inside */
            s.inside = 1;
            s.inside_at = now;
            capture(true);
        }
    }
    if (s.inside) return true;
    start(part, now);
    switch (part) {
    case ROOF: Audio_UI(0xd); break;   /* 0x4aab80 */
    case DOOR1:
    case DOOR2:
        if (0 < s.dir[part]) Audio_UI(0x30);
        break;
    case HOOD:
        R3DCar_Hood(car, 0 < s.dir[HOOD]);
        if (0 < s.dir[HOOD]) Audio_UI(0x32);
        break;
    case TRUNK:
        R3DCar_Trunk(car, 0 < s.dir[TRUNK]);
        if (0 < s.dir[TRUNK]) Audio_UI(0x34);
        break;
    }
    return true;
}

void FECar3D_Move(FertControl *c, int32_t x, int32_t y)
{
    if (!interactive(c) || s.c != c) return;
    s.mouse[0] = x, s.mouse[1] = y;
    s.part_over = pick(c, x, y);
}

/* A part's ticks into its 0 .. 255 (v1's): the roof at two thirds, the doors at 1 / 0.302 opening and
   1 / 0.389 shutting, the hood at 2.87, the trunk at 1 / 0.56 and 1 / 0.4; each truncated. */
static int32_t amount(int part, int32_t dt)
{
    switch (part) {
    case ROOF: return dt * 2 / 3;
    case DOOR1:
    case DOOR2: return (int32_t)((float)dt / exe_f32(0 < s.dir[part] ? 0x5b58f8 : 0x5b58f4));
    case HOOD: return (int32_t)((float)dt * exe_f32(0x5b58f0));
    default: return (int32_t)((float)dt / exe_f32(0 < s.dir[TRUNK] ? 0x5b58ec : 0x5b261c));
    }
}

void FECar3D_Tick(FertControl *c, const float *damage)
{
    if (!interactive(c)) {
        if (!c && s.c) {
            capture(false);
            memset(&s, 0, sizeof s);
        }
        return;
    }
    attach(c);
    R3DCar *car = FE3D_Car();
    int32_t now = timer_ticks();
    if (car) {
        /* the camera into the car or out of it over 64 ticks (slot 46) */
        float f = (float)(now - s.inside_at) * exe_f32(0x5b2828);
        f = f < 0 ? 0 : 1.0f < f ? 1.0f : f;
        if (!s.inside) f = 1.0f - f;
        FE3D_SetZoom(f);
    }
    FEAccessor *roof = FEAccess_Find("Car.Roof");
    if (roof && FEAccess_Pos(roof) != s.roof) {   /* the roof put the other way elsewhere */
        s.roof = FEAccess_Pos(roof);
        start(ROOF, now);
        Audio_UI(0xd);
    }
    float d[10] = {0};
    for (int i = 0; damage && i < 10; i++) d[i] = damage[i];   /* (0x4d1450 x 10: "damage" and a car) */
    FE3D_SetCarDamage(d);
    for (int i = 0; i < NPARTS; i++) {
        if (!s.moving[i]) continue;
        int32_t e = amount(i, now - s.start[i]);
        if (e < 0) e = 0;
        s.open[i] = s.dir[i] < 1 ? 0xff - e : e;
        if (s.open[i] < 1 && s.dir[i] < 0) {   /* shut: its sound */
            s.open[i] = 0;
            s.moving[i] = 0;
            if ((i == DOOR1 || i == DOOR2) && s.dir[i] < 0) Audio_UI(0x31);
            else if (i == HOOD && s.dir[HOOD] < 0) Audio_UI(0x33);
            else if (i == TRUNK && s.dir[TRUNK] < 0) Audio_UI(0x35);
        }
        if (0xfe < s.open[i] && 0 < s.dir[i]) {
            s.open[i] = 0xff;
            s.moving[i] = 0;
        }
    }
    if (!car) return;
    /* the roof's slot by its amount (0x4d13e0), the doors' hinges */
    R3DCar_AnimReset(car, 1, car->anim[1].frames * (float)s.open[ROOF] * exe_f32(0x5b3888));
    FE3D_CarHinge(6, 0, (float)s.open[DOOR1] * exe_f32(0x5b58e8), 0);
    FE3D_CarHinge(7, 0, (float)s.open[DOOR2] * exe_f32(0x5b58e4), 0);
}

void FECar3D_SetWindow(FertControl *w) { s.window = w; }
