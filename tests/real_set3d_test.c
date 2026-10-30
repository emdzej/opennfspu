/* THRASH_opendll and the table sets (real/set3d.c) with a recording fake driver: the table gets the
   driver's entries, the aliases, the defaults for a missing getstate/tfree, the open's setstate(0x1f,
   0x6b), the 3Dfx flag from the about block; select/save switch whole tables. No game data needed. */
#include "real/set3d.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static ThrashAbout about_block;
static uint32_t last_state;
static uintptr_t last_value;
static const ThrashAbout *f_about(void) { return &about_block; }
static uintptr_t f_setstate(uint32_t s, uintptr_t v) { last_state = s; last_value = v; return 1; }
static void f_v(void) {}
static int f_i(void) { return 1; }
static bool f_b(void) { return true; }
static int f_clip(int a, int b, int c, int d) { return 0; }
static void f_2(const void *a, const void *b) {}
static void f_1(const void *a) {}
static void f_3(const void *a, const void *b, const void *c) {}
static void f_4(const void *a, const void *b, const void *c, const void *d) {}
static void f_m(int n, const void *v, const int *i) {}
static ThrashSurface *f_lock(void) { return NULL; }
static int f_rect(int x, int y, int w, int h, void *d) { return 0; }
static int f_wrect(int x, int y, int w, int h, const void *d) { return 0; }
static int f_int(int x) { return 0; }
static int f_tex(ThrashTexture t) { return 0; }
static bool f_mode(int m, int b, int p) { return true; }
static ThrashTexture f_talloc(int w, int h, int f, int c, uint32_t m) { return 1; }
static ThrashTexture f_tupdate(ThrashTexture t, const void *p, const uint32_t *c) { return t; }
static int f_unlock(ThrashSurface *s) { return 0; }
static bool f_window(int w) { return true; }

static const ThrashDll fake = {
    .name = "fake", .about = f_about, .clearwindow = f_v, .clip = f_clip, .drawline = f_2, .drawlinemesh = f_m,
    .drawlinestrip = f_m, .drawpoint = f_1, .drawpointmesh = f_m, .drawquad = f_4, .drawquadmesh = f_m,
    .drawtri = f_3, .drawtrifan = f_m, .drawtrimesh = f_m, .drawtristrip = f_m, .drawsprite = f_2,
    .drawspritemesh = f_m, .flushwindow = f_b, .init = f_i, .is = f_i, .lockwindow = f_lock, .pageflip = f_v,
    .readrect = f_rect, .restore = f_i, .selectdisplay = f_int, .setstate = f_setstate, .getstate = NULL,
    .settexture = f_tex, .setvideomode = f_mode, .sync = f_int, .talloc = f_talloc, .tfree = NULL,
    .treset = f_i, .tupdate = f_tupdate, .unlockwindow = f_unlock, .window = f_window, .writerect = f_wrect,
};

int main(void)
{
    memcpy(&about_block, "XFD3", 4);   /* stored as 0x33444658 */
    THRASH_registerdll(&fake);
    CHECK(THRASH_opendll("FAKE.DLL") == 1);
    CHECK(THRASH.fn.setstate == f_setstate && THRASH.setstate_alias == f_setstate && THRASH.setstate_alias2 == f_setstate);
    CHECK(THRASH.fn.getstate != NULL && THRASH.fn.getstate(5) == 0);   /* the default */
    CHECK(THRASH.getstate_alias == NULL);                              /* copied before the default */
    CHECK(THRASH.fn.tfree != NULL && THRASH.fn.tfree(1) == 0);
    CHECK(last_state == 0x1f && last_value == 0x6b);
    CHECK(THRASH_is3dfx);
    /* Sets: the open saved the table into set 0; a cleared table comes back with select. */
    ThrashTable saved = THRASH;
    memset(&THRASH, 0, sizeof THRASH);
    THRASH_savetable(3);
    THRASH_selecttable(0);
    CHECK(!memcmp(&THRASH, &saved, sizeof THRASH));
    THRASH_selecttable(3);
    CHECK(THRASH.fn.about == NULL);
    THRASH_selecttable(9);   /* out of range: set 0 */
    CHECK(THRASH.fn.about == f_about);
    printf("real_set3d_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
