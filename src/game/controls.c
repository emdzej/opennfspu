/* The race's controls (controls.h). */
#include "game/controls.h"
#include "exe.h"
#include "game/input.h"
#include "game/options.h"
#include "game/race_progress.h"
#include "pending.h"
#include "platform.h"
#include <ctype.h>
#include <string.h>
#include <strings.h>

enum {
    FIRST = 0x14e, NBIND = 0xd0,     /* ids 0x14e..0x21d */
    PLAYER = 80,                     /* a player's slots (C1_COMMAND1 .. C1_COMMAND2_TOGGLEHUD) */
    IFACE = 2 * PLAYER, NIFACE = 32, /* I_* (MAINEXIT ..) */
    PSX = IFACE + NIFACE, NPSX = 16, /* PSX_* */
    NCMD = 18,                       /* the commands: LEAVERACE .. TOGGLEHUD */
    HELD = 0x40,
    NDEV = 8,
};

/* The slots of a player (from 0x5e9668): the two modifiers, left, right, gas, brake, the handbrake and
   the horn (the packet's flag bits 0, 1), then the commands four times: long-pressed, plain, and with
   the first or the second modifier held. */
enum { S_COMMAND1, S_LEFT = 2, S_RIGHT, S_GAS, S_BRAKE, S_FLAGS, S_LONG = 8, S_PLAIN = 26, S_MOD = 44 };

static uint32_t s_bind[NBIND];       /* 0x5e9668 (0x5e9130 + 4 id) */
static int32_t s_count[2][NCMD];     /* 0x5e95d4: the commands' samples held */
static uint32_t s_latch[2];          /* 0x5e99bc: the commands taken (until let go) */
static int32_t s_modifier[2];        /* 0x5e95cc: the modifier held (1, 2), 0 none */
static uint32_t s_ring[32];          /* 0x5e954c */
uint16_t g_input_psx;                /* 0x5e9548 */
static uint8_t s_packet1[4];         /* 0x5e99e0: the second player's */
static uint8_t s_key[256];           /* 0x5de028 */
static uint32_t s_joyflags;          /* 0x5e8e58: the joysticks' directions and button while paused */
int32_t g_input_samples, g_input_cursor;

/* The joysticks (0x5e8e80, 10 words each: 8 axes, the hat, the state, whose buttons are at +0x34),
   registered by the types' init functions (0x411ba0, 0x411a40, 0x411ae0) when a binding names one. */
typedef struct {
    const int32_t *axis[8];
    const int32_t *pov;
    const uint8_t *state;
} JoyDev;
static JoyDev s_dev[NDEV];

static int read_null(uint32_t p) { (void)p; return 0; }   /* 0x508530 */

/* 0x411be0: the axis (0..0xffff) between low << 8 and high << 8 as 0..255 (either way round). */
static int read_analog(uint32_t p)
{
    uint32_t dev = p >> 20;
    if (dev >= NDEV || !s_dev[dev].state || !s_dev[dev].axis[(p >> 16) & 7]) return 0;
    int32_t v = *s_dev[dev].axis[(p >> 16) & 7];
    int32_t lo = (int32_t)(p & 0xff00), hi = (int32_t)((p & 0xff) << 8);
    if (lo < hi) {
        if (lo <= v) return hi < v ? 0xff : (v - lo) * 0xff / (hi - lo);
    } else if (hi < lo) {
        if (v < hi) return 0xff;
        if (v <= lo) return (lo - v) * 0xff / (lo - hi);
    }
    return 0;
}

static int read_button(uint32_t p)   /* 0x411ab0 */
{
    uint32_t dev = p >> 16;
    if (dev >= NDEV || !s_dev[dev].state) return 0;
    return s_dev[dev].state[0x34 + (p & 0x1f)] ? 0xff : 0;
}

static int read_key(uint32_t p) { return s_key[p & 0xff] ? 0xff : 0; }   /* 0x411c80 */

/* 0x411ca0: code 0 the joystick's button; a key held; else the arrows' and Enter's codes (the extended
   ones, which the key table never has) the joystick's directions. */
static int read_intface(uint32_t p)
{
    uint32_t k = p & 0xff;
    if (!k) return s_joyflags & 0x10 ? 0xff : 0;
    if (s_key[k]) return 0xff;
    uint32_t bit = k == 0xcb ? 4 : k == 0xcd ? 8 : k == 0xc8 ? 1 : k == 0xd0 ? 2 : k == 0x1c ? 0x10 : 0;
    return s_joyflags & bit ? 0xff : 0;
}

/* 0x411b20: the hat (hundredths of a degree) within an eighth's 45 degrees either side of direction
   p & 7 (north's range wrapping). */
static int read_pov(uint32_t p)
{
    uint32_t dev = p >> 16;
    uint32_t hi = ((p + 1) & 7) * 4500, lo = ((p - 1) & 7) * 4500;
    if (dev >= NDEV || !s_dev[dev].state || !s_dev[dev].pov) return 0;
    int32_t v = *s_dev[dev].pov;
    if (v < 0) return 0;
    if (hi < lo) {
        if ((int32_t)hi < v) hi = lo + 9000;
        else lo = (uint32_t)-(int32_t)(((p + 1) & 7) * 4500);
    }
    return v <= (int32_t)hi && (int32_t)lo <= v ? 0xff : 0;
}

static int (*const s_readers[6])(uint32_t) = {read_null, read_analog, read_button, read_key, read_intface,
                                              read_pov};   /* 0x5cc5cc */

static int rd(uint32_t b)
{
    uint32_t t = b & 0xff;
    return t < 6 ? s_readers[t]((uint32_t)((int32_t)b >> 8)) : 0;
}

static bool held(uint32_t b) { return b && rd(b) > HELD; }

/* ---- the option values ---- */

/* 0x4b5220: the type's index by its name (0x5cc5c8, 12 bytes apart), 0 unknown. */
static uint32_t type_index(const char *name)
{
    for (uint32_t i = 0; i < 16; i++) {
        uint32_t n = exe_u32(0x5cc5c8 + 12 * i);
        if (!n) return 0;
        if (!strcasecmp(name, exe_str(n))) return i;
    }
    return 0;
}

/* 0x4b5040: numbers (decimal, '-', "0x" hex), names (the symbol table 0x5d63a0: SC_* and the game's,
   the rest of the text as one, unknown 0), the fields between ',' packed: the first two four bits
   apart above bit 16, the third from bit 8, the last added. Spaces start a number over. */
static int32_t parse_value(const char *s)
{
    uint32_t v = 0, acc = 0;
    int field = 0, base = 10, sign = 1;
    if (!*s) return 0;
    for (;;) {
        char c = *s;
        switch (c) {
        case ',':
            if (field < 2) acc = (v << 12 | acc) << 4;
            else acc |= v << 8;
            base = 10;
            field++;
            /* fall through */
        case ' ':
            v = 0;
            sign = 1;
            break;
        case '-':
            sign = -1;
            break;
        case 'X':
        case 'x':
            if (!v) base = 16;
            break;
        default:
            if (c >= '0' && c <= '9') v = (uint32_t)((c - '0') * sign) + (uint32_t)base * v;
            else if (base == 16 && c >= 'A' && c <= 'F') v = v * 16 + (uint32_t)((c - 0x37) * sign);
            else if (base == 16 && c >= 'a' && c <= 'f') v = v * 16 + (uint32_t)((c - 0x57) * sign);
            else if (!v && isalpha((unsigned char)c)) {
                uint32_t e = 0x5d63a0;
                for (; exe_u32(e) && strcasecmp(s, exe_str(exe_u32(e))); e += 8) {}
                v = exe_u32(e + 4);
                s += strlen(s) - 1;
            }
            break;
        }
        if (!s[1]) return (int32_t)(v + acc);
        s++;
    }
}

/* Options_Parse's binding case (0x4b0db0, ids above 0x14d): the type up to ',', the value after it,
   spaces, tabs and returns dropped, 63 characters each at most. */
uint32_t Controls_Parse(const char *text)
{
    char type[64], val[64];
    int n = 0;
    for (; *text && *text != ',' && n < 0x3f; text++)
        if (*text != ' ' && *text != '\t' && *text != '\r') type[n++] = *text;
    type[n] = 0;
    if (*text) text++;
    n = 0;
    for (; *text && *text != '\n' && n < 0x3f; text++)
        if (*text != ' ' && *text != '\t' && *text != '\r') val[n++] = *text;
    val[n] = 0;
    return type_index(type) | (uint32_t)parse_value(val) << 8;
}

/* The names of the bindings: Options_Parse's records (id, target, name) at 0x5d2d7c, 12 bytes per id,
   which it fills with immediate moves (C7 05 address value) between 0x4b0db0 and 0x4b4950. */
static uint32_t s_names[NBIND];
static void find_names(void)
{
    static bool done;
    if (done) return;
    done = true;
    for (uint32_t a = 0x4b0db0; a + 10 <= 0x4b4950; a++) {
        if ((exe_u32(a) & 0xffff) != 0x05c7) continue;
        uint32_t at = exe_u32(a + 2), v = exe_u32(a + 6);
        if (at < 0x5d2d7c || at >= 0x5d2d7c + 12 * NBIND || (at - 0x5d2d7c) % 12 != 8) continue;
        s_names[(at - 0x5d2d7c) / 12] = v;
        a += 9;
    }
}

uint32_t Controls_Binding(int id) { return id >= FIRST && id < FIRST + NBIND ? s_bind[id - FIRST] : 0; }

void Controls_Load(void)
{
    /* 0x412e20 (with INPUT_init and the devices' reset, 0x4116e0) */
    memset(s_bind, 0, sizeof s_bind);
    memset(s_count, 0, sizeof s_count);
    memset(s_modifier, 0, sizeof s_modifier);
    memset(s_latch, 0, sizeof s_latch);
    memset(s_ring, 0, sizeof s_ring);
    g_input_psx = 0;
    find_names();
    for (int i = 0; i < NBIND; i++) {
        const char *v = s_names[i] ? Options_Str(exe_str(s_names[i])) : NULL;
        if (v) s_bind[i] = Controls_Parse(v);
    }
    for (int i = 0; i < NBIND; i++)
        if ((s_bind[i] & 0xff) == 1 || (s_bind[i] & 0xff) == 2 || (s_bind[i] & 0xff) == 5)
            PENDING_NOTE(0x411ba0, "the joysticks' registration (Analog, Button, Pov bindings: 0x411ba0, 0x411a40, 0x411ae0)");
    /* 0x4117c0: the interface's own keys over the file's */
    static const struct { uint8_t slot; uint32_t code; } defs[] = {
        {8, 0x103}, {9, 0}, {1, 0xc804}, {2, 0xd004}, {3, 0xcb04}, {4, 0xcd04}, {5, 0x1c04},
        {14, 0x3b03}, {12, 0x3c03}, {11, 0x3d03}, {10, 0x3e03}, {24, 0x3f03}, {25, 0x203}, {26, 0x4003},
        {27, 0x4103}, {31, 0x4203}, {17, 0x4403}, {15, 0x5703}, {16, 0x5803}, {29, 0x4a03}, {30, 0x4e03},
        {28, 0xf03}, {13, 0x4001903}, {0, 0},
    };
    for (size_t i = 0; i < sizeof defs / sizeof defs[0]; i++) s_bind[IFACE + defs[i].slot] = defs[i].code;
    s_bind[S_PLAIN + 15] = 0x4303;   /* C1_SWITCHMUSIC (0x5e970c): F9 */
}

/* 0x411970: the joysticks polled (0x4118c0); while paused their stick's directions and a button. */
static void poll_devices(void)
{
    s_joyflags = 0;
}

/* The key table as the window's key messages leave it (WndProc 0x53b1c0 / 0x53b420): the scan code
   without its extended bit. */
static void read_keys(void)
{
    uint8_t keys[KEY_COUNT] = {0};
    memset(s_key, 0, sizeof s_key);
    if (!plat_keys(keys)) return;
    for (int k = 0; k < KEY_COUNT; k++)
        if (keys[k]) s_key[k & 0x7f] = 1;
}

static void latch_on(int p, int i, int *cmd)
{
    if (!(s_latch[p] & 1u << i)) {
        *cmd = i + 1;
        s_latch[p] |= 1u << i;
    }
}

/* The long-pressed commands: a command held past five samples; a plain command with the same binding
   waits for the release (and counts with it). */
static void long_presses(int p, const uint32_t *S, const bool *ok, bool plain_ok[NCMD], int *cmd)
{
    for (int i = 0; i < NCMD; i++) {
        uint32_t b = S[S_LONG + i];
        if (!b) continue;
        if (!ok[i] || rd(b) <= HELD) {
            s_latch[p] &= ~(1u << i);
            s_count[p][i] = 0;
            continue;
        }
        if (++s_count[p][i] > 5) latch_on(p, i, cmd);
        for (int j = 0; j < NCMD; j++)
            if (S[S_PLAIN + j] == b) {
                plain_ok[j] = false;
                s_count[p][j] = s_count[p][i];
            }
    }
}

/* The plain commands: on the press, or on a release within five samples when a long press shares it. */
static void plain_presses(int p, const uint32_t *S, const bool *ok, const bool plain_ok[NCMD], int *cmd)
{
    for (int i = 0; i < NCMD; i++) {
        uint32_t b = S[S_PLAIN + i];
        if (!b) continue;
        if (!ok[i] || rd(b) <= HELD) {
            if (ok[i] && 0 < s_count[p][i] && s_count[p][i] < 6) {
                latch_on(p, i, cmd);
                s_count[p][i] = 0;
            } else {
                s_latch[p] &= ~(1u << i);
            }
        } else if (plain_ok[i]) {
            latch_on(p, i, cmd);
        }
    }
}

static void flags(uint8_t *pk, const uint32_t *S, const bool *ok)
{
    for (int i = 0; i < 2; i++) {
        if (!ok[i]) continue;
        if (held(S[S_FLAGS + i])) pk[0] |= (uint8_t)(1 << i);
        else pk[0] &= (uint8_t)~(1 << i);
    }
    pk[0] &= 7;
}

void Input_Sample(void)
{
    poll_devices();
    read_keys();
    bool iface_ok[NIFACE], psx_ok[NPSX];
    memset(iface_ok, 1, sizeof iface_ok);
    memset(psx_ok, 1, sizeof psx_ok);
    uint32_t iface = 0;
    for (int p = 0; p < 2; p++) {
        if (p == 1 && g_race_inputs == 1) break;
        const uint32_t *S = s_bind + PLAYER * p;
        uint8_t *pk = p ? s_packet1 : g_input_packet;
        int mod = 0;
        for (int i = 0; i < 2; i++)
            if (held(S[S_COMMAND1 + i])) mod = i + 1;
        s_modifier[p] = mod;
        int cmd = 0;
        bool plain_ok[NCMD];
        memset(plain_ok, 1, sizeof plain_ok);
        if (!mod) {
            static const bool all[NCMD] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
            pk[1] = (uint8_t)((rd(S[S_RIGHT]) - rd(S[S_LEFT])) / 4);
            pk[2] = (uint8_t)(rd(S[S_GAS]) / 2);
            pk[3] = (uint8_t)(rd(S[S_BRAKE]) / 2);
            if (pk[2] >= 0x7f) pk[2] = 0x7e;
            if (pk[3] >= 0x7f) pk[3] = 0x7e;
            flags(pk, S, all);
            long_presses(p, S, all, plain_ok, &cmd);
            plain_presses(p, S, all, plain_ok, &cmd);
            /* (the modified commands only follow their keys here) */
            for (int i = 0; i < 2 * NCMD; i++) {
                uint32_t b = S[S_MOD + i];
                if (!b) continue;
                if (rd(b) <= HELD) s_latch[p] &= ~(1u << (i % NCMD));
                else s_latch[p] |= 1u << (i % NCMD);
            }
        } else {
            /* The modifier's commands win: whatever shares one of their bindings is left alone (the
               driving controls, the interface's and the debug keys too). */
            const uint32_t *M = S + S_MOD + NCMD * (mod - 1);
            bool ok[42];   /* slots 2 .. 43 */
            memset(ok, 1, sizeof ok);
            for (int k = 0; k < 42; k++)
                for (int j = 0; j < NCMD; j++)
                    if (S[S_LEFT + k] == M[j]) ok[k] = false;
            for (int k = 0; k < NIFACE; k++)
                for (int j = 0; j < NCMD; j++)
                    if (s_bind[IFACE + k] == M[j]) iface_ok[k] = false;
            for (int k = 0; k < NPSX; k++)
                for (int j = 0; j < NCMD; j++)
                    if (s_bind[PSX + k] == M[j]) psx_ok[k] = false;
            int l = rd(S[S_LEFT]), r = rd(S[S_RIGHT]);
            if (ok[0] && ok[1]) pk[1] = (uint8_t)((r - l) / 2);   /* (half, not a quarter as above) */
            if (ok[2]) pk[2] = (uint8_t)rd(S[S_GAS]);             /* (whole and not capped) */
            if (ok[3]) pk[3] = (uint8_t)rd(S[S_BRAKE]);
            flags(pk, S, ok + 4);
            long_presses(p, S, ok + 6, plain_ok, &cmd);
            plain_presses(p, S, ok + 24, plain_ok, &cmd);
            for (int i = 0; i < NCMD; i++) {
                if (!M[i]) continue;
                if (rd(M[i]) <= HELD) s_latch[p] &= ~(1u << i);
                else latch_on(p, i, &cmd);
            }
        }
        pk[0] |= (uint8_t)(cmd << 3);
        /* SWITCHMUSIC, CYCLECAMERA, TOGGLEHUD: the interface's MUSIC, CYCLECAMERA, TOGGLEHUD */
        int c = pk[0] >> 3;
        if (c == 0x10 || c == 0x11 || c == 0x12) {
            pk[0] &= 7;
            iface |= c == 0x10 ? 0x40000u : c == 0x11 ? 0x800000u : 0x1000000u;
        }
    }
    g_input_samples++;
    for (int i = 0; i < NIFACE; i++)
        if (iface_ok[i] && held(s_bind[IFACE + i])) iface |= 1u << i;
    s_ring[g_input_samples & 0x1f] = iface;
    uint32_t psx = 0;
    for (int i = 0; i < NPSX; i++)
        if (psx_ok[i] && held(s_bind[PSX + i])) psx |= 1u << i;
    g_input_psx = (uint16_t)psx;
}

bool Input_Action(int n, bool edge)
{
    uint32_t bit = 1u << (n & 0x1f), now = s_ring[g_input_cursor & 0x1f];
    if (!edge) return (now & bit) != 0;
    return (bit & ~s_ring[(g_input_cursor - 1) & 0x1f] & now) != 0;
}
