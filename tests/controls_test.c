/* The race's controls (game/controls.c): fe.txt's bindings parsed as Options_Parse does (the type's
   name, the value's numbers and SC_* names), the interface's defaults over them, and the keyboard's
   reading. Skips (exit 0) without data. */
#include "exe.h"
#include "game/controls.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    char err[256];
    if (!vfs_mount_default() || !vfs_exists("fe.txt") || !exe_init(err, sizeof err)) {
        printf("controls_test: no game data, skipped\n");
        return 0;
    }
    CHECK(Controls_Parse("key,SC_UP") == 0x4803);          /* the arrows' scan codes without 0x80 */
    CHECK(Controls_Parse(" Key , sc_esc ") == 0x103);      /* names any case, spaces dropped */
    CHECK(Controls_Parse("Intface,0xc8") == 0xc804);
    CHECK(Controls_Parse("analog,1,2,0,255") == ((1u << 20 | 2u << 16 | 0u << 8 | 255u) << 8 | 1));
    CHECK(Controls_Parse("button,0,5") == (5u << 8 | 2));
    CHECK(Controls_Parse("pov,0,2") == (2u << 8 | 5));
    CHECK(Controls_Parse("nonsense,12") == 12u << 8);      /* an unknown type is Null's */
    CHECK(Controls_Parse("key,SC_NOSUCH") == 3);           /* an unknown name 0 */
    {
        char t[96];   /* Options_Dump's text back through the parser */
        static const uint32_t b[] = {0x4803, 0x103, (5u << 8 | 2), ((1u << 20 | 2u << 16 | 0x80u << 8 | 0xffu) << 8 | 1), (2u << 8 | 5)};
        for (size_t i = 0; i < sizeof b / sizeof b[0]; i++) {
            Controls_Format(b[i], t, sizeof t);
            CHECK(Controls_Parse(t) == b[i]);
        }
        Controls_Format(0x4803, t, sizeof t);
        CHECK(!strcmp(t, "Key,SC_UP"));
    }
    Controls_Load();
    uint32_t gas = Controls_Binding(0x152);                /* C1_GAS */
    printf("C1_GAS %#x C1_LEFT %#x C1_HANDBRAKE %#x C1_SHIFTUP %#x I_CYCLECAMERA %#x\n", gas,
           Controls_Binding(0x150), Controls_Binding(0x154), Controls_Binding(0x169), Controls_Binding(0x205));
    CHECK(Controls_Binding(0x1f6) == 0x103);               /* I_PAUSE: Escape over the file's P */
    CHECK(Controls_Binding(0x1ef) == 0xc804);              /* I_UP */
    CHECK(Controls_Binding(0x177) == 0x4303);              /* C1_SWITCHMUSIC: F9 */
    CHECK(Controls_Binding(0x21e) == 0);
    if (fails) return 1;
    printf("controls_test: ok\n");
    return 0;
}
