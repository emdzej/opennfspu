/* The front end's transitions: see fetrans.h. */
#include "front/fetrans.h"
#include "app.h"
#include "exe.h"
#include "game/options.h"
#include "render/render.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define TRANS_LENGTH 0x200   /* 0x5b6b2c */
#define TIMER_RATE 128       /* 0x5deb48 */

/* TIMER_ticks (0x53c050), 128 a second, on the port's clock. */
static uint32_t timer_ticks(void) { return (uint32_t)((uint64_t)app_ticks_ms() * 128u / 1000u); }

void FETrans_On(FertTrans *t)
{
    t->delay = 3;
    t->phase = 1;
    t->live = true;
}

void FETrans_Off(FertTrans *t)
{
    t->delay = 3;
    t->phase = 3;
    t->live = true;
}

/* 0x4e0580: three frames' wait, then the time since (in 0x200ths of half a second): coming on the counter
   0x200 less it down to 0, then on (and the mouse's move posted again, 0x4d5c10: the port works out the
   hover each frame); going off it up to 0x200, then gone. */
bool FETrans_Tick(FertTrans *t)
{
    int e = 0;
    if (t->delay < 1) {
        if (t->delay == 0) {
            t->start = timer_ticks();
            t->delay = -1;
        }
        e = (int)((timer_ticks() - t->start) * TRANS_LENGTH) / (TIMER_RATE / 2);
    } else {
        t->delay--;
    }
    if (t->phase == 1) {
        t->counter = TRANS_LENGTH - e < 0 ? 0 : TRANS_LENGTH - e;
        if (t->counter == 0) {
            t->phase = 2;
            return true;
        }
    } else if (t->phase == 3) {
        t->counter = e <= TRANS_LENGTH ? e : TRANS_LENGTH;
        if (t->counter == TRANS_LENGTH) t->phase = 0;
    }
    return false;
}

static const FertTrans *s_base, *s_window;

void FETrans_SetBase(const FertTrans *base) { s_base = base; }
void FETrans_SetWindow(const FertTrans *t) { s_window = t; }
const FertTrans *FETrans_Window(void) { return s_window; }

int FETrans_Counter(const FertTrans *t)
{
    int b = s_base ? s_base->counter : 0;   /* 0x4d5dd0 */
    return t->counter < b ? b : t->counter;
}

/* 0x4c8490: same 1, left 2, right 4, top 8, bottom 0x10, fade 0x20 (any case); else none. */
static uint32_t trans_flag(const char *s)
{
    char b[52];
    size_t n = 0;
    for (; s && s[n] && n < sizeof b - 1; n++) b[n] = (char)tolower((unsigned char)s[n]);
    b[n] = 0;
    static const struct { uint32_t va, flag; } names[6] = {{0x5d8400, 1}, {0x5d83f8, 2}, {0x5d83f0, 4},
                                                          {0x5d83ec, 8}, {0x5d83e4, 0x10}, {0x5d83dc, 0x20}};
    for (int i = 0; i < 6; i++)
        if (!strcmp(b, exe_str(names[i].va))) return names[i].flag;
    return 0;
}

static const char *data_str(const FertControl *c, const char *name)
{
    for (int k = 0; k < c->nparts; k++) {
        FertAttr *a = FertAttrList_find(&c->parts[k]->attrs, name);
        if (a) return FertAttr_str(a);
    }
    return NULL;
}

/* (FEControl_v20's reading of them, 0x4c7110: one "same" takes the other's, both none) */
static void read_flags(FertControl *c)
{
    if (c->trans_read) return;
    c->trans_read = true;
    const char *on = data_str(c, "transon"), *off = data_str(c, "transoff");
    if (!on || !off) return;
    c->transon = trans_flag(on);
    c->transoff = trans_flag(off);
    bool s_on = c->transon & 1, s_off = c->transoff & 1;
    if (s_on && s_off) c->transon = c->transoff = 0;
    else if (s_on) c->transon = c->transoff;
    else if (s_off) c->transoff = c->transon;
}

/* The flags by the phase: its own, or the first screen's while on (0x4d5e90: on without one); off
   and gone the off ones, coming on the on ones. */
static bool flags_of(FertControl *c, uint32_t *flags)
{
    const FertTrans *t = s_window;
    if (!t) return false;
    read_flags(c);
    int phase = t->phase;
    if (phase == 2) phase = s_base ? s_base->phase : 2;
    *flags = phase == 3 || phase == 0 ? c->transoff : phase == 1 ? c->transon : 0;
    return true;
}

/* FEFADESENABLED (0x7d, 0x5d5dac) and FETRANSITIONSENABLED (0x7c, 0x5d5dbc): fe.txt's, on at 0. */
static bool fades(void) { return Options_Int(0x5d5dac, 0) == 0; }
static bool slides(void) { return Options_Int(0x5d5dbc, 0) == 0; }

int FETrans_Fade(FertControl *c)
{
    uint32_t f;
    if (!fades() || !flags_of(c, &f) || !(f & 0x20)) return 0xff;
    return 0xff - FETrans_Counter(s_window) * 0xff / TRANS_LENGTH;
}

void FETrans_Offset(FertControl *c, int32_t *x, int32_t *y)
{
    uint32_t f;
    if (!flags_of(c, &f) || !f || !slides()) return;
    int k = FETrans_Counter(s_window);
    int w = g_display.width, h = g_display.height;   /* 0x5deac8, 0x5deacc */
    if (f & 2) *x -= w * k / TRANS_LENGTH;
    if (f & 4) *x += w * k / TRANS_LENGTH;
    if (f & 8) *y -= h * k / TRANS_LENGTH;
    if (f & 0x10) *y += h * k / TRANS_LENGTH;
}
