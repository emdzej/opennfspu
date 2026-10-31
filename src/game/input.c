/* The race's input packets and the replay (see input.h). */
#include "game/input.h"
#include "game/ai.h"
#include "game/race_progress.h"
#include "pending.h"
#include "render/r3dcar.h"
#include <string.h>

enum { RING = 0x80, PLAYERS = 4, REPLAY_SIZE = 0x19000 };

int32_t g_replay_mode;
uint8_t g_input_packet[4];

static uint8_t ring[PLAYERS][RING][4];   /* 0x606af0 */
static int32_t taken[PLAYERS][RING];     /* 0x607af0 */
static int32_t queued[PLAYERS];          /* 0x608af4 */
static uint8_t current[PLAYERS][4];      /* 0x608b14 */
static uint8_t packet[4];                /* 0x5e9664: the packet a car's task reads */
static uint8_t batch[PLAYERS][4][8];     /* 0x606880: eight ticks of steering, throttle, brake, flags */
static int32_t batched[PLAYERS];         /* 0x5e9a00 */
static uint8_t replay[REPLAY_SIZE + 0x100];   /* 0x5ed848 */
static int32_t written, reading;         /* 0x5e99f8, 0x5e99fc */

void Input_Reset(bool keep_replay)
{
    memset(ring, 0, sizeof ring);
    memset(taken, 0, sizeof taken);
    memset(current, 0, sizeof current);
    memset(batch, 0, sizeof batch);
    memset(batched, 0, sizeof batched);
    for (int p = 0; p < PLAYERS; p++) queued[p] = g_sim_tick / 2;   /* (port: the ring follows the half-ticks) */
    reading = 0;
    if (!keep_replay) {
        memset(replay, 0, sizeof replay);
        written = 0;
    }
}

bool Input_Enqueue(int player, const uint8_t p[4])
{
    int i = queued[player] & 0x7f;
    if (taken[player][i]) return false;
    memcpy(ring[player][i], p, 4);
    taken[player][i] = 1;
    queued[player]++;
    return true;
}

void Input_QueuePacket(void)
{
    if (g_race_over) return;
    PENDING_NOTE(0x413820, "the network's and the second player's packets");
    Input_Enqueue(g_race_local, g_input_packet);
}

void Sim_HalfStep(int n)
{
    for (int p = 0; p < g_humans.n && p < PLAYERS; p++) {
        memcpy(current[p], ring[p][n & 0x7f], 4);
        taken[p][n & 0x7f] = 0;
    }
}

/* Eight bytes run-length packed: [length] then the bytes, a run of three or more as 0xff, its length,
   its byte. 0x413c70 */
static int pack(const uint8_t *src, uint8_t *out)
{
    int n = 1, i = 0;
    for (;;) {
        uint8_t c = src[i];
        int run = 0;
        do {
            run++;
            i++;
        } while (i < 8 && src[i] == c);
        if (run < 3) {
            for (int k = 0; k < run; k++) out[n++] = c;
        } else {
            out[n++] = 0xff;
            out[n++] = (uint8_t)run;
            out[n++] = c;
        }
        if (7 < i) {
            out[0] = (uint8_t)n;
            out[n] = 0;
            return n;
        }
    }
}

static int unpack(const uint8_t *in, uint8_t out[8])
{
    int n = (int8_t)in[0], o = 0;
    for (int i = 1; i < n && o < 8;) {
        if (in[i] == 0xff) {
            int run = (int8_t)in[i + 1];
            for (int k = 0; k < run && o < 8; k++) out[o++] = in[i + 2];
            i += 3;
        } else {
            out[o++] = in[i++];
        }
    }
    return n;
}

/* A block of eight ticks into the replay: the steering, then the three packed. Full: recording stops
   (mode 1). 0x414080 */
static void store(const uint8_t b[4][8])
{
    if (0x18fff < written + 0x80) {
        g_replay_mode = 1;
        return;
    }
    memcpy(replay + written, b[0], 8);
    written += 8;
    for (int k = 1; k < 4; k++) written += pack(b[k], replay + written);
}

static void load_block(uint8_t b[4][8])   /* 0x4141c0 */
{
    memcpy(b[0], replay + reading, 8);
    reading += 8;
    for (int k = 1; k < 4; k++) reading += unpack(replay + reading, b[k]);
}

/* Recording: the current packet is the car's and goes into the block, with the assists' bits (the
   description's +0x14, +0x18, +0x1c) in the top bits. 0x414410 */
static void record(int p)
{
    memcpy(packet, current[p], 4);   /* 0x413890 */
    Car *c = g_humans.c[p];
    int i = batched[p];
    batch[p][0][i] = (uint8_t)(packet[1] + 0x40);
    batch[p][1][i] = packet[2];
    batch[p][2][i] = packet[3];
    batch[p][3][i] = packet[0];
    if (DESC_I(c, 0x14)) batch[p][0][i] |= 0x80;
    if (DESC_I(c, 0x18)) batch[p][1][i] |= 0x80;
    if (DESC_I(c, 0x1c)) batch[p][2][i] |= 0x80;
    if (++batched[p] == 8) {
        batched[p] = 0;
        store((const uint8_t(*)[8])batch[p]);
    }
}

/* Replaying: the packets read back (the assists set from their bits); past the replay's end, mode 3 and
   empty packets. 0x4144d0 */
static void play(int p)
{
    memcpy(packet, current[p], 4);   /* 0x413890 */
    int32_t *i = &batched[p];
    bool have = true;
    if (*i == 0) {
        if (replay[reading] != 0) load_block(batch[p]);
        else {
            g_replay_mode = 3;
            have = false;
        }
    }
    if (have && g_replay_mode == 2) {
        Car *c = g_humans.c[p];
        uint8_t *d = CAR_DESCB(c);
        int32_t a = (batch[p][0][*i] & 0x80) != 0, t = (batch[p][1][*i] & 0x80) != 0, b = (batch[p][2][*i] & 0x80) != 0;
        memcpy(d + 0x14, &a, 4);
        memcpy(d + 0x18, &t, 4);
        memcpy(d + 0x1c, &b, 4);
        packet[3] = batch[p][2][*i] & 0x7f;
        packet[1] = (uint8_t)((batch[p][0][*i] & 0x7f) - 0x40);
        packet[2] = batch[p][1][*i] & 0x7f;
        packet[0] = batch[p][3][*i];
    } else if (g_replay_mode == 3) {
        memset(packet, 0, 4);
    }
    (*i)++;
    PENDING_NOTE(0x413ba0, "the replay's camera cuts");
    if (*i == 8) *i = 0;
}

/* The manual gearbox's command: up (0x10) or down (0x18) a gear within the specs' count. 0x4138b0 */
static int8_t shift(int8_t gear, int32_t ngears)
{
    uint8_t cmd = packet[0] & 0xf8;
    if (cmd == 0x10) {
        if (gear < ngears - 1) return (int8_t)(gear + 1);
    } else if (cmd == 0x18 && 0 < gear) {
        gear--;
    }
    return gear;
}

void CarSim_PacketTask(void *car)
{
    Car *c = car;
    uint8_t *b = c->b;
    if (*CAR_I(c, CAR_FLAGS) & 4) {
        int p = *CAR_I(c, CAR_PLAYER);   /* 0x414830 */
        if (0 <= p && p < PLAYERS) {
            if (g_replay_mode != 0 && g_replay_mode != 1) play(p);
            else record(p);
        }
    }
    b[0xd8a] = packet[0];
    if (*CAR_I(c, 0xda8) == 0) {
        if (*CAR_I(c, 0xdac) == 0) {
            b[0xd7c] = (uint8_t)(packet[2] << 1);
            b[0xd7d] = (uint8_t)(packet[3] << 1);
            *CAR_I(c, 0xd90) = g_r3d_mirror == 1 ? (int8_t)packet[1] * -2 : (int8_t)packet[1] * 2;
            b[0xd85] = packet[0] & 1;
            b[0xd86] = packet[0] >> 1 & 1;
        } else {
            PENDING_NOTE(0x499150, "a network car's controls");
        }
    } else {
        AI_Plan(c);    /* 0x40b670 */
        AI_Steer(c);   /* 0x40c000 */
        b[0xd7c] = b[0xd9c];
        *CAR_I(c, 0xd90) = *CAR_I(c, 0xda0);
        b[0xd7d] = b[0xd9d];
        b[0xd86] = 0;
    }
    uint8_t cmd = packet[0] >> 3;
    b[0xd8b] = cmd;
    uint8_t *l = &b[0xd87];
    switch (cmd) {
    case 7:
    case 8:
    case 9:
        b[0xd8c] = cmd;
        break;
    case 10:
        if ((*l & 0x10) && ((*l ^= 0x10) & 8)) return;
        *(uint16_t *)(void *)(b + 0x1232) = 0;
        b[0x1230] |= 0x80;
        *l ^= 8;
        break;
    case 0xb:
        if ((*l & 8) && ((*l ^= 8) & 0x10)) return;
        *(uint16_t *)(void *)(b + 0x1230) = 0;
        b[0x1232] |= 0x80;
        *l ^= 0x10;
        break;
    case 0xc:
        if (!(*l & 8) || !(*l & 0x10)) {
            *l |= 0x18;
            *(uint16_t *)(void *)(b + 0x1230) |= 0x80;
            *(uint16_t *)(void *)(b + 0x1232) |= 0x80;
        } else {
            *(uint16_t *)(void *)(b + 0x1230) = 0;
            *l ^= 0x18;
            *(uint16_t *)(void *)(b + 0x1232) = 0;
        }
        break;
    case 0xd: *l ^= 3; break;
    case 0xf: *l ^= 4; break;
    default: break;
    }
    int8_t g = shift((int8_t)b[0xd82], *SPEC_I(c, 0x50));
    if (*CAR_F(c, 0xdb4) <= *SPEC_F(c, 0x94) || (int8_t)b[0xd82] <= g) b[0xd7e] = (uint8_t)g;
}
