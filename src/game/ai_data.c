/* The AI's data (see ai_data.h). */
#include "game/ai_data.h"
#include "exe.h"
#include "game/race_car.h"
#include "game/ai_drive.h"
#include "pending.h"
#include "vfs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
#define K(va) exe_f32(va)

AiPersonality g_ai_pers[AI_NPERS];
uint8_t g_ai_scripts[AI_NPERS][7][8];
float g_ai_glue[21];
int32_t g_traf_count[3];
float g_traf_f[2], g_traf_set[2][11];
static uint8_t s_squares[CAR_MAX][0x84];   /* (the cars' +0x1014 tables) */
static bool s_has_squares[CAR_MAX];

/* A Udff stream (0x4a5340 ..): the file's 32-bit words in turn (0x4a53a0), zero past its end. */
typedef struct {
    const uint8_t *p;
    size_t n, at;
} Udff;
static int32_t next(Udff *u)   /* 0x4a53a0 */
{
    int32_t v = 0;
    if (u->p && u->at + 4 <= u->n) memcpy(&v, u->p + u->at, 4);
    u->at += 4;
    return v;
}
static float fixed(int32_t v) { return (float)v * K(0x5b253c); }   /* 0x4a51c0: 16.16 fixed */

/* The traffic's configuration (0x406c00), from the copy in the exe (0x5cc0e0: Traffic_LoadConfig opens
   the stream on it, not on the file it names). */
static void traffic_config(void)
{
    uint8_t buf[27 * 4];
    for (int i = 0; i < 27; i++) {
        uint32_t w = exe_u32(0x5cc0e0 + 4u * (uint32_t)i);
        memcpy(buf + 4 * i, &w, 4);
    }
    Udff u = {buf, sizeof buf, 0};
    for (int i = 0; i < 3; i++) g_traf_count[i] = next(&u);
    g_traf_f[0] = fixed(next(&u));
    g_traf_f[1] = fixed(next(&u));
    for (int s = 0; s < 2; s++) {
        float *f = g_traf_set[s];
        for (int k = 0; k < 5; k++) f[k] = fixed(next(&u));
        f[5] = (float)next(&u) * K(0x5b25b8);
        f[6] = fixed(next(&u));
        f[7] = (float)next(&u) * K(0x5b25b8);
        f[8] = fixed(next(&u));
        f[9] = fixed(next(&u));
        f[10] = (float)next(&u) * K(0x5b25b8);
    }
}

static uint32_t tab(uint32_t va, int32_t i) { return exe_u32(va + 4u * (uint32_t)i); }

/* The personalities (0x407d90): per record fourteen levels, each picking its trait's value from the
   exe's tables (the eighth unused, the last kept as it is). */
static void personalities(Udff *u)
{
    next(u);
    for (int r = 0; r < AI_NPERS; r++) {
        int32_t l[14];
        for (int k = 0; k < 14; k++) l[k] = next(u);
        uint32_t *v = g_ai_pers[r].v;
        v[0] = tab(0x5cc1a4, l[0]);
        v[1] = tab(0x5cc1b4, l[1]);
        v[2] = tab(0x5cc1c4, l[2]);
        for (int k = 0; k < 4; k++) v[3 + k] = tab(0x5cc1d4 + 4u * (uint32_t)k, 4 * l[3]);
        v[7] = tab(0x5cc214, l[3]);
        v[8] = tab(0x5cc224, l[4]);
        v[9] = tab(0x5cc234, l[5]);
        v[10] = tab(0x5cc244, l[5]);
        double sc;
        uint32_t lo = exe_u32(0x5b25d8), hi = exe_u32(0x5b25dc);
        uint64_t bits = (uint64_t)hi << 32 | lo;
        memcpy(&sc, &bits, 8);
        float f = (float)((float)(int32_t)tab(0x5cc254, l[6]) * sc);
        memcpy(&v[11], &f, 4);
        /* (0x657a24, the track's weather: only a track spec file sets it (World_LoadSpecFile 0x475c10,
           GameData\Track\Tr<nn>.bin's +2), and the game ships none: always 0, dry) */
        int32_t wet = 0;
        v[12] = tab(0x5cc294, wet + l[8]);
        v[13] = tab(0x5cc2a8, wet + l[8]);
        v[14] = tab(0x5cc264, l[9]);
        v[15] = tab(0x5cc274, l[9]);
        v[16] = tab(0x5cc284, l[9]);
        v[17] = tab(0x5cc2dc, l[10]);
        v[18] = tab(0x5cc2bc, l[11]);
        v[19] = tab(0x5cc2cc, l[12]);
        v[20] = (uint32_t)l[13];
    }
}

static uint8_t *open_ai(const char *fmt, size_t *n)
{
    char path[128];
    snprintf(path, sizeof path, fmt, "GameData\\Simulation\\AI\\");   /* (0x65b2a8) */
    return vfs_read_all(path, n);
}

/* AI_LoadPersonalities 0x407f50 */
static void load_personalities(void)
{
    for (int i = 0; i < g_ncars; i++) *CAR_I(g_cars[i], 0xe34) = DESC_I(g_cars[i], 0x14c);
    size_t n = 0;
    uint8_t *f = open_ai(exe_str(0x5cc318), &n);   /* "%sprsonal.bin" */
    Udff u = {f, n, 0};
    personalities(&u);
    free(f);
    f = open_ai(exe_str(0x5cc308), &n);   /* "%sscripts.bin" */
    u = (Udff){f, n, 0};
    next(&u);
    for (int r = 0; r < AI_NPERS; r++)
        for (int e = 0; e < 7; e++)
            for (int k = 0; k < 4; k++) {
                g_ai_scripts[r][e][k] = (uint8_t)next(&u);
                g_ai_scripts[r][e][4 + k] = (uint8_t)next(&u);
            }
    free(f);
    /* The head-to-head race's glue for a race or a quick race against one rival. */
    bool h2h = (g_race_type == 0 || g_race_type == 1) && g_rivals.n == 1;
    f = open_ai(h2h ? exe_str(0x5cc2f8) : exe_str(0x5cc2eb + 1), &n);   /* "%shhglue.bin", "%sglue.bin" */
    u = (Udff){f, n, 0};
    next(&u);
    memset(g_ai_glue, 0, sizeof g_ai_glue);
    if (f && u.at + sizeof g_ai_glue <= n) memcpy(g_ai_glue, f + u.at, sizeof g_ai_glue);   /* 0x4a53b0 */
    free(f);
    if (g_chasers.n != 0)
        for (int i = 0; i < 21; i++) {
            if (g_ai_glue[i] > K(0x5b24a8)) g_ai_glue[i] *= K(0x5b25e4);
            else if (g_ai_glue[i] < K(0x5b24a8)) g_ai_glue[i] *= K(0x5b25e0);
        }
    for (int i = 0; i < g_ncars; i++) {
        Car *c = g_cars[i];
        *CAR_I(c, 0xe34) %= 5;
        AI_MoodsInit(c, *CAR_I(c, 0xe34));   /* 0x40ab30: the personality's script */
    }
}

/* The opponents' squares table (0x40aa10): i * i / k / 2, halved again as a byte, for i up to 127; k the
   personality's +0x20 times 11 (opponents and police) or 12 (traffic). */
static void squares(Car *c)
{
    uint32_t f = (uint32_t)*CAR_I(c, CAR_FLAGS);
    int idx = *CAR_I(c, CAR_INDEX);
    if (!(f & 2) || idx < 0 || idx >= CAR_MAX) return;
    float k = ((f & 8) || (f & 0x20) ? K(0x5b263c) : K(0x5b240c)) * AI_PersF(c, 0x20);
    if (f & 8) k = K(0x5b24a8) * k;   /* (0x40b4a0: 1) */
    uint8_t *t = s_squares[idx];
    memcpy(t + 0x80, &k, 4);
    float inv = K(0x5b24a8) / k;
    for (int i = 0; i < 0x80; i++) {
        float x = (float)i;
        float v = (float)((double)x * x * inv * K(0x5b23f0));   /* (the x87's product, stored once) */
        int at = (int)lrintf(x);
        if (at < 0) at = -at;
        if (at >= 0x80) at = 0x80;
        t[at] = (uint8_t)((uint8_t)(int32_t)v >> 1);
    }
    s_has_squares[idx] = true;
}

void AI_LoadData(void)
{
    /* AI_LoadTuning 0x40ad40 reads Simulation\AI\tuning.bin (140 16.16 words: the six "caravan"
       slots' spacing, the car types' skill multipliers into 0x657410, the track multipliers it skips,
       the weather's curve table 0x5e6e30, the upgrades' multipliers 0x5e6e00 .. 0x5e6e2f) and sets
       0x5cc358 to 1. Nothing in the game reads any of what it stores, and 0x5cc358 is 1 already: the
       port reads nothing. */
    traffic_config();
    for (int i = 0; i < g_ncars; i++) AI_MoodsReset(g_cars[i]);   /* 0x40ab60 */
    load_personalities();
    PENDING_NOTE(0x4032b0, "the cops' objects' reset (0x5e66bc ..)");
    memset(s_has_squares, 0, sizeof s_has_squares);
    for (int i = 0; i < g_ncars; i++) {
        Car *c = g_cars[i];
        squares(c);
        *CAR_F(c, 0x10dc) = K(0x5b24a8);   /* 0x40b4a0 */
        *CAR_F(c, 0x10d8) = K(0x5b24a8);
    }
}

const AiPersonality *AI_Personality(Car *c)
{
    int i = *CAR_I(c, 0xe34);
    return &g_ai_pers[0 <= i && i < AI_NPERS ? i : 0];
}

float AI_PersF(Car *c, int off)
{
    float f;
    memcpy(&f, (const uint8_t *)AI_Personality(c)->v + off, 4);
    return f;
}

uint32_t AI_PersU(Car *c, int off)
{
    uint32_t v;
    memcpy(&v, (const uint8_t *)AI_Personality(c)->v + off, 4);
    return v;
}

const uint8_t *AI_SquaresTable(Car *c)
{
    int idx = *CAR_I(c, CAR_INDEX);
    return 0 <= idx && idx < CAR_MAX && s_has_squares[idx] ? s_squares[idx] : NULL;
}
