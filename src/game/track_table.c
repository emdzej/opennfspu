/* The track's AI tuning, run from the original's code (Track_Table 0x4a2ba0, 0x4a29a0, 0x4a2730).

   The three functions compare the track (0x657444) and the car's AI spec name with _stricmp (0x5ae170),
   test the direction (0x657430), store immediates into the zones and return a constant through the x87
   stack. The interpreter below knows exactly the instruction forms they use (moves, pushes and pops,
   compares and tests, conditional jumps, calls, fld of a float, rep stosd) and stops on anything else, so
   a different build or a slip fails loudly (the factors stay 1 and the zones empty) instead of guessing. */
#include "game/track_table.h"
#include "game/race_car.h"
#include "game/track.h"
#include "exe.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

float g_track_road_width = 12.0f;
int32_t g_track_zone[TRACK_NZONES][2];
float g_track_zone_f[TRACK_NZONES] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static bool s_ok = true;

enum { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI };
enum {
    STACK_VA = 0x7f000000u, STACK_SIZE = 0x400u,   /* the interpreter's stack */
    ARG_VA = 0x7e000000u,                          /* the AI spec name passed */
    RET_VA = 0x7d000000u,                          /* the return address that ends a run */
    STRICMP_VA = 0x5ae170u,
    TRACK_VA = 0x657444u, REVERSE_VA = 0x657430u,
    WIDTH_VA = 0x5e6e34u, ZONES_VA = 0x5e74acu, FACTORS_VA = 0x5e74fcu,
};

typedef struct {
    uint32_t r[8], eip;
    bool zf, sf, of, cf;
    float st;
    uint8_t stack[STACK_SIZE];
    const char *arg;
    bool fail;
} Cpu;

static bool in_stack(uint32_t va) { return STACK_VA <= va && va + 4 <= STACK_VA + STACK_SIZE; }

static float u2f(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

/* The globals the functions write: the width, the zones' pairs and their factors. */
static uint32_t *global(uint32_t va)
{
    if (va == WIDTH_VA) return (uint32_t *)(void *)&g_track_road_width;
    if (ZONES_VA <= va && va < ZONES_VA + 8 * TRACK_NZONES && !(va & 3))
        return (uint32_t *)(void *)&g_track_zone[(va - ZONES_VA) / 8][(va - ZONES_VA) / 4 % 2];
    if (FACTORS_VA <= va && va < FACTORS_VA + 4 * TRACK_NZONES && !(va & 3))
        return (uint32_t *)(void *)&g_track_zone_f[(va - FACTORS_VA) / 4];
    return NULL;
}

static uint32_t rd(Cpu *c, uint32_t va)
{
    if (in_stack(va)) {
        uint32_t v;
        memcpy(&v, c->stack + (va - STACK_VA), 4);
        return v;
    }
    uint32_t *g = global(va);
    if (g) return *g;
    if (va == REVERSE_VA) return (uint32_t)g_race_reverse;
    return exe_u32(va);   /* (the constants the functions return) */
}

static void wr(Cpu *c, uint32_t va, uint32_t v)
{
    if (in_stack(va)) {
        memcpy(c->stack + (va - STACK_VA), &v, 4);
        return;
    }
    uint32_t *g = global(va);
    if (g) *g = v;
    else c->fail = true;
}

static const char *str_at(Cpu *c, uint32_t va)
{
    if (va == ARG_VA) return c->arg;
    if (va == TRACK_VA) return g_track_name;
    return exe_str(va);
}

static void push(Cpu *c, uint32_t v)
{
    c->r[ESP] -= 4;
    wr(c, c->r[ESP], v);
}
static uint32_t pop(Cpu *c)
{
    uint32_t v = rd(c, c->r[ESP]);
    c->r[ESP] += 4;
    return v;
}

static const uint8_t *code(uint32_t va) { return exe_data(va, 16); }

/* A ModRM operand: a register (*reg_out >= 0) or a memory address. */
static uint32_t modrm(Cpu *c, const uint8_t *p, int *len, int *reg, int *rm_reg)
{
    int mod = p[0] >> 6, rm = p[0] & 7;
    *reg = p[0] >> 3 & 7;
    *rm_reg = -1;
    *len = 1;
    if (mod == 3) {
        *rm_reg = rm;
        return 0;
    }
    uint32_t base = 0;
    if (rm == 4) {   /* SIB: a base without index */
        uint8_t sib = p[1];
        (*len)++;
        if ((sib >> 3 & 7) != 4 || (sib & 7) == 5) c->fail = true;
        base = c->r[sib & 7];
    } else if (mod == 0 && rm == 5) {
        uint32_t d;
        memcpy(&d, p + 1, 4);
        *len += 4;
        return d;
    } else {
        base = c->r[rm];
    }
    if (mod == 1) {
        base += (uint32_t)(int32_t)(int8_t)p[*len];
        *len += 1;
    } else if (mod == 2) {
        uint32_t d;
        memcpy(&d, p + *len, 4);
        base += d;
        *len += 4;
    }
    return base;
}

static void flags_sub(Cpu *c, uint32_t a, uint32_t b)
{
    uint32_t r = a - b;
    c->zf = r == 0;
    c->sf = (r >> 31) != 0;
    c->cf = a < b;
    c->of = (((a ^ b) & (a ^ r)) >> 31) != 0;
}
static void flags_logic(Cpu *c, uint32_t r)
{
    c->zf = r == 0;
    c->sf = (r >> 31) != 0;
    c->cf = c->of = false;
}

static bool cond(Cpu *c, int cc)
{
    switch (cc) {
    case 0x4: return c->zf;                          /* je */
    case 0x5: return !c->zf;                         /* jne */
    case 0xc: return c->sf != c->of;                 /* jl */
    case 0xd: return c->sf == c->of;                 /* jge */
    case 0xe: return c->zf || c->sf != c->of;        /* jle */
    case 0xf: return !c->zf && c->sf == c->of;       /* jg */
    default: return false;
    }
}

/* Runs the function at va with one argument; its x87 result, or 1 when it can't. */
static float run(uint32_t va, const char *arg)
{
    static Cpu cpu;
    Cpu *c = &cpu;
    memset(c, 0, sizeof *c);
    c->arg = arg;
    c->st = 1.0f;
    c->r[ESP] = STACK_VA + STACK_SIZE;
    push(c, ARG_VA);
    push(c, RET_VA);
    c->eip = va;
    for (int n = 0; n < 100000 && !c->fail; n++) {
        if (c->eip == RET_VA) {
            s_ok = true;
            return c->st;
        }
        if (c->eip == STRICMP_VA) {   /* _stricmp(a, b), cdecl */
            const char *a = str_at(c, rd(c, c->r[ESP] + 4)), *b = str_at(c, rd(c, c->r[ESP] + 8));
            c->r[EAX] = (uint32_t)strcasecmp(a, b);
            c->eip = pop(c);
            continue;
        }
        const uint8_t *p = code(c->eip);
        if (!p) break;
        uint8_t op = p[0];
        int len, reg, rmr;
        uint32_t a;
        if (0x50 <= op && op <= 0x57) {
            push(c, c->r[op - 0x50]);
            c->eip += 1;
        } else if (0x58 <= op && op <= 0x5f) {
            c->r[op - 0x58] = pop(c);
            c->eip += 1;
        } else if (op == 0x68) {
            uint32_t v;
            memcpy(&v, p + 1, 4);
            push(c, v);
            c->eip += 5;
        } else if (op == 0x6a) {
            push(c, (uint32_t)(int32_t)(int8_t)p[1]);
            c->eip += 2;
        } else if (0xb8 <= op && op <= 0xbf) {
            memcpy(&c->r[op - 0xb8], p + 1, 4);
            c->eip += 5;
        } else if (op == 0xa1 || op == 0xa3) {
            memcpy(&a, p + 1, 4);
            if (op == 0xa1) c->r[EAX] = rd(c, a);
            else wr(c, a, c->r[EAX]);
            c->eip += 5;
        } else if (op == 0x89 || op == 0x8b || op == 0x85 || op == 0x39 || op == 0x3b || op == 0x33 || op == 0x8d) {
            a = modrm(c, p + 1, &len, &reg, &rmr);
            uint32_t m = rmr >= 0 ? c->r[rmr] : (op == 0x8d ? 0 : rd(c, a));
            switch (op) {
            case 0x89:
                if (rmr >= 0) c->r[rmr] = c->r[reg];
                else wr(c, a, c->r[reg]);
                break;
            case 0x8b: c->r[reg] = m; break;
            case 0x8d:
                if (rmr >= 0) c->fail = true;
                c->r[reg] = a;
                break;
            case 0x85: flags_logic(c, m & c->r[reg]); break;
            case 0x39: flags_sub(c, m, c->r[reg]); break;
            case 0x3b: flags_sub(c, c->r[reg], m); break;
            case 0x33:
                c->r[reg] ^= m;
                flags_logic(c, c->r[reg]);
                break;
            }
            c->eip += 1 + (uint32_t)len;
        } else if (op == 0x83 || op == 0x81) {
            a = modrm(c, p + 1, &len, &reg, &rmr);
            uint32_t imm;
            if (op == 0x83) imm = (uint32_t)(int32_t)(int8_t)p[1 + len];
            else memcpy(&imm, p + 1 + len, 4);
            uint32_t m = rmr >= 0 ? c->r[rmr] : rd(c, a), r = m;
            if (reg == 0) {
                r = m + imm;
                flags_logic(c, r);
                c->cf = r < m;
            } else if (reg == 5) {
                flags_sub(c, m, imm);
                r = m - imm;
            } else if (reg == 7) {
                flags_sub(c, m, imm);
            } else {
                c->fail = true;
            }
            if (reg != 7) {
                if (rmr >= 0) c->r[rmr] = r;
                else wr(c, a, r);
            }
            c->eip += 1 + (uint32_t)len + (op == 0x83 ? 1 : 4);
        } else if (op == 0xc7) {
            a = modrm(c, p + 1, &len, &reg, &rmr);
            uint32_t imm;
            memcpy(&imm, p + 1 + len, 4);
            if (reg != 0) c->fail = true;
            if (rmr >= 0) c->r[rmr] = imm;
            else wr(c, a, imm);
            c->eip += 1 + (uint32_t)len + 4;
        } else if (op == 0xd9) {   /* fld m32 */
            a = modrm(c, p + 1, &len, &reg, &rmr);
            if (reg != 0 || rmr >= 0) c->fail = true;
            c->st = u2f(rd(c, a));
            c->eip += 1 + (uint32_t)len;
        } else if ((0x70 <= op && op <= 0x7f) || op == 0xeb) {
            uint32_t next = c->eip + 2;
            bool take = op == 0xeb || cond(c, op & 0xf);
            if (op != 0xeb && (op & 0xf) < 4) c->fail = true;
            c->eip = take ? next + (uint32_t)(int32_t)(int8_t)p[1] : next;
        } else if (op == 0x0f && 0x80 <= p[1] && p[1] <= 0x8f) {
            int32_t d;
            memcpy(&d, p + 2, 4);
            if ((p[1] & 0xf) < 4) c->fail = true;
            c->eip = cond(c, p[1] & 0xf) ? c->eip + 6 + (uint32_t)d : c->eip + 6;
        } else if (op == 0xe9 || op == 0xe8) {
            int32_t d;
            memcpy(&d, p + 1, 4);
            uint32_t next = c->eip + 5;
            if (op == 0xe8) push(c, next);
            c->eip = next + (uint32_t)d;
        } else if (op == 0xc3) {
            c->eip = pop(c);
        } else if (op == 0x90) {
            c->eip += 1;
        } else if (op == 0xf3 && p[1] == 0xab) {   /* rep stosd */
            for (; c->r[ECX]; c->r[ECX]--, c->r[EDI] += 4) wr(c, c->r[EDI], c->r[EAX]);
            c->eip += 2;
        } else {
            c->fail = true;
        }
    }
    char m[80];
    snprintf(m, sizeof m, "track tuning: can't run 0x%08x (at 0x%08x)", (unsigned)va, (unsigned)c->eip);
    plat_log(m);
    s_ok = false;
    return 1.0f;
}

float Track_Table(const char *aispec) { return run(0x4a2ba0, aispec); }
float Track_CarFactor(const char *aispec) { return run(0x4a2730, aispec); }
bool Track_TableOk(void) { return s_ok; }
