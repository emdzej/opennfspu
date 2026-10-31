/* The game's audio (see audio.h). */
#include "game/audio.h"
#include "exe.h"
#include "game/camera.h"
#include "game/car_drive.h"
#include "game/road.h"
#include "game/scene.h"
#include "game/track.h"
#include "game/input.h"
#include "pending.h"
#include "render/r3dcar.h"
#include "real/locatbig.h"
#include "real/random.h"
#include "snd/snd.h"
#include "vfs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int32_t g_sound_quality;
int32_t g_audio_fx = 0x7f;   /* 0x5d1744: the effects' volume (the start ramps it up) */

static const char *sound_dir(void) { return "GameData\\Sounds\\"; }   /* 0x65b318 (the install's) */
static int32_t opt(uint32_t va) { return (int32_t)exe_u32(va); }      /* the options' starting values */

/* 0x5d1764: a 129-step fade in; read from its end, 0x5d17e4 - i, the fade out. */
static int fade_in(int i) { return (int8_t)exe_data(0x5d1764 + (uint32_t)i, 1)[0]; }
static int fade_out(int i) { return (int8_t)exe_data((uint32_t)(0x5d17e4 - i), 1)[0]; }

/* ---- the start (0x4a6610, 0x4a6480) and the banks (0x4a6990) ---- */

static bool s_on;   /* 0x5d1734 */

/* a / b in 16.16 (0x4a5f30). */
static int fixdiv(int a, int b) { return (int)((((int64_t)(a % b)) << 16) / b) + (a / b) * 0x10000; }
static void engines_free(void);
static void stream_close(void);
static int stream_bank(int type, int idx, bool look);
static int32_t s_nearvol = 0x80;   /* 0x655f34: 0x80 less the cars close to the listener */
typedef struct {
    int32_t slot;      /* 0x6564a0: the SND bank */
    uint8_t *data;     /* 0x6564a8 */
    int32_t size;      /* 0x6564ac */
} AudioBank;
static AudioBank s_banks[8];

bool Audio_Init(void)
{
    g_audio_fx = opt(0x5d1744);   /* (the options' starting value) */
    if (g_sound_quality == 3) return false;
    for (int i = 0; i < 8; i++) s_banks[i].slot = -1;
    int q = g_sound_quality;
    /* The device's rate (at most 0x5b4ae4's), its voices (0x5b4b08), the resampler (0x5b4b20). */
    int rate = (int32_t)exe_u32(0x5b4ae4 + 4u * (uint32_t)q);
    int voices = (int32_t)exe_u32(0x5b4b08 + 4u * (uint32_t)q);
    int quality = (int32_t)exe_u32(0x5b4b20 + 4u * (uint32_t)q);
    s_on = SND_Init(rate, 2, voices, quality);
    PENDING_NOTE(0x4ae080, "music, the speech and the track's sounds (Music_Init 0x4aded0, 0x4ab280)");
    return s_on;
}

void Audio_Shutdown(void)
{
    stream_close();
    SND_Shutdown();
    engines_free();
    for (int i = 0; i < 8; i++) {
        free(s_banks[i].data);
        s_banks[i].data = NULL;
        s_banks[i].slot = -1;
    }
    s_on = false;
}

/* A bank into the library and the game's list at k (0x4a6990; the original keeps only the header part
   when the library has the samples elsewhere, status 7, which its software voices never do). */
static int add_bank(uint8_t *data, int size, int k)
{
    int slot = SND_BankAdd(data);
    if (slot < 0) printf("AudioCmn_AddBank -- DID NOT ADD BANK\n");
    s_banks[k].slot = slot;
    s_banks[k].data = data;
    s_banks[k].size = size;
    return size;
}

/* <name>.bnk into the list at k (AudioCmn_LoadBank 0x4a6a60). */
static int load_bank(const char *name, int k)
{
    char path[160];
    snprintf(path, sizeof path, "%s%s", name, exe_str(0x5d146c));
    size_t size = 0;
    uint8_t *b = vfs_read_all(path, &size);
    if (!b) {
        printf("AudioCmn_LoadBank -- DID NOT LOAD %s\n", path);
        return 0;
    }
    return add_bank(b, (int)size, k);
}

/* ---- one-shots and the sound slots (0x4a96b0, 0x4a99e0, 0x4a8c30) ---- */

static int32_t s_slot[0x50][2];   /* 0x656180: each slot's play and patch */
static int32_t s_level;           /* 0x656860: the last impact's level, a sixteenth, falling */
static int8_t s_slotvel[0x50];    /* 0x5d17e8 */
static uint32_t s_lastpick[17];   /* 0x65644c ..: the patch picked last in each set */

/* A pan angle to a byte: the front half's (0x4a96b0, 0x4a99e0). */
static int pan_byte(int a) { return (a < 0x4000 || 0xbfff < a) ? (int)((uint32_t)(a + 0x4000) >> 8 & 0xff) : (0xbfff - a) >> 8; }

/* The bank's volume for the patch (by the options' volumes and their mixes 0x5d14bc ..). */
static int category_volume(int bank, int patch, int vol)
{
    if (bank == s_banks[2].slot) return (opt(0x5d1748) * opt(0x5d14d8) * vol * 0x55) >> 0x15;
    if (bank == s_banks[5].slot) return (opt(0x5d1750) * opt(0x5d14bc) * vol * 0x55) >> 0x15;
    if (patch == 0xc || patch == 0xd) return (g_audio_fx * vol) >> 7;
    if ((bank == s_banks[0].slot || bank == s_banks[1].slot) && patch == 2) {
        int v = g_audio_fx * vol;
        return 0x7e < v ? 0x7f : v;
    }
    if (3 <= patch && patch <= 5 && bank == s_banks[0].slot) return (opt(0x5d14d0) * g_audio_fx * vol * 0x55) >> 0x15;
    if (0x11 <= patch && patch <= 0x1c) return (opt(0x5d14cc) * g_audio_fx * vol * 0x55) >> 0x15;
    if (patch == 10 || patch == 0xb) return (opt(0x5d14e0) * opt(0x5d1750) * vol * 0x55) >> 0x15;
    if (patch == 0xf) return (opt(0x5d14e8) * opt(0x5d1748) * vol * 0x55) >> 0x15;
    return -1;   /* the rest's (below) */
}

/* A one-shot (0x4a96b0): the bank's patch at the volume by its kind, the pan from the angle, the pitch
   (16.16), the velocity; outside sounds through a 3000 Hz lowpass from the views in the car. */
static int play_once(int bank, int patch, int angle, int vol, int bend, int pitch, int vel)
{
    SndParams p;
    SND_ParamsDefault(&p);
    int v;
    if (bank == -4) v = (opt(0x5d174c) * opt(0x5d14c0) * 0x55) >> 0xe;
    else {
        v = category_volume(bank, patch, vol);
        if (v < 0) {
            v = (int8_t)((opt(0x5d14d4) * g_audio_fx * vol * 0x55) >> 0x15);
            if (0x2f < patch) {
                p.f0a = (int8_t)(0x7f - vel);
                PENDING_NOTE(0x656869, "the reverb's level under roofs (0x656869)");
            }
        }
    }
    p.vol = (int8_t)v;
    p.pitch = (uint16_t)(pitch >> 4);
    p.f0b = 0;   /* (0x655d40: no 3D) */
    p.f09 = (int8_t)bend;
    p.patch = patch;
    if (bank != s_banks[0].slot && bank != s_banks[1].slot && g_sound_quality < 2 && (g_racecam[0].flags2 & 8) &&
        g_racecam[0].mode != 0 && g_racecam[0].mode != 0xf)
        p.f12 = 3000;   /* (0x443e10, the car's interior, taken as no) */
    p.pan = opt(0x5d1760) ? (int8_t)pan_byte(angle) : 0x40;
    p.velocity = (int8_t)vel;
    if (bank < -1) {   /* the streamed ones: the cached bank's patch 0, else asked for (and silent now) */
        bank = stream_bank(bank == -4 ? 2 : bank == -3 ? 1 : 0, patch, false);
        patch = 0;
    }
    if (bank < 0) return -1;
    return SND_Play(bank, patch, &p);
}

/* A sound in a slot (0x4a99e0): its bank by the slot (0x5d14ec) or the patch; the impacts' slot 0x33
   plays and forgets; another slot's same patch has its volume, pan, velocity, pitch and lowpass set,
   a new patch ends the last (a quarter second's fade) first. */
static int play_slot(int slot, int patch, int vel, int pitch, int vol, int angle, int velocity)
{
    int b = vel < 0 ? 0 : vel;
    if (0x7f < b) b = 0x7f;
    vol = vol < 0 ? 0 : vol;
    if (0x7f < vol) vol = 0x7f;
    int bank = s_banks[5].slot;
    bool streamed = (0x1e <= slot && slot <= 0x21) || (0x34 <= slot && slot <= 0x37) || slot == 0x26 || slot == 0x38 ||
                    (0x39 <= slot && slot <= 0x48);
    if (!streamed) {
        bank = s_banks[2].slot;
        if (!((10 <= slot && slot <= 0x11) || (0x1c <= slot && slot <= 0x1d))) {
            if (patch == 6 || (9 < patch && patch < 0xc)) bank = s_banks[3].slot;
            else bank = s_banks[(int8_t)exe_data(0x5d14ec + 4u * (uint32_t)slot, 1)[0]].slot;
        }
    }
    if (slot == 0x33) return play_once(bank, patch, angle, vol, b, pitch, velocity);
    int32_t *h = s_slot[slot];
    if (h[1] == patch && h[0] != -1 && !SND_Playing(h[0])) h[0] = h[1] = -1;
    if (h[1] == patch && h[0] != -1) {
        int v = category_volume(bank, patch, vol);
        if (v < 0) v = (opt(0x5d14d4) * g_audio_fx * vol * 0x55) >> 0x15;
        if (patch == 0x10) v = (opt(0x5d14dc) * opt(0x5d1750) * vol * 0x55) >> 0x15;
        SND_SetVolume(h[0], v);
        if (opt(0x5d1760)) SND_SetPanByte(h[0], pan_byte(angle));
        SND_SetVelocity(h[0], b);
        SND_SetPitch(h[0], pitch >> 4);
        if (bank != s_banks[0].slot && bank != s_banks[1].slot && g_sound_quality < 2) {
            bool inside = (g_racecam[0].flags2 & 8) && g_racecam[0].mode != 0 && g_racecam[0].mode != 0xf;
            SND_SetLowpass(h[0], inside ? 3000 : 0xffff);
        }
        return h[0];
    }
    if (h[0] != -1) SND_Fade(h[0], 0x19, -1);
    h[0] = play_once(bank, patch, angle, vol, b, pitch, 0x7f);
    h[1] = h[0] != -1 ? patch : -1;
    return h[0];
}

static void stop_slot(int slot)
{
    if (s_slot[slot][0] != -1) SND_Fade(s_slot[slot][0], 5, -1);
    s_slot[slot][0] = s_slot[slot][1] = -1;
}

/* One of n patches from base, not the one picked last in the set (0x4a9150; the real library's
   generator). */
static int pick(int base, uint32_t n, int set)
{
    uint32_t r = REAL_rand() % n;
    if (s_lastpick[set] == r && ++r == n) r = 0;
    s_lastpick[set] = r;
    return base + (int)r;
}
static int pick3(int base, int set) { return pick(base, 3, set); }

/* The impact's patch by its kind and the surface (0x4a8c30). */
static int impact_patch(int kind, int surf)
{
    if (kind == 0x1e) return 0x13;
    if (kind == 4) return 0x20;
    if (kind == 0) {
        switch (surf) {
        case 5: case 8: case 9: return pick3(0x40, 0);
        case 10: return pick3(0x43, 8);
        case 0xb: return pick3(0x50, 9);
        case 7: return pick3(0x46, 3);
        case 0xc: return pick3(0x4c, 10);
        case 0x12: return pick3(0x73, 5);
        case 0x11: return pick3(0x76, 13);
        case 0x14: return pick3(0x79, 15);
        case 0x10: return pick3(0x56, 14);
        case 3: return pick3(0x30, 1);
        case 0: return pick3(0x40, 2);
        default: return 0;
        }
    }
    if (kind == 3) {
        switch (surf) {
        case 0xd: case 5: case 7: return pick3(0x3c, 12);
        case 10: case 0xb: return pick3(0x36, 8);
        case 0xc: return (int)(REAL_rand() % 3) + 0x39;
        case 9: return pick3(0x33, 11);
        case 8: return pick3(0x30, 1);
        case 3: return pick(0x30, 3, 1);
        case 0: return pick(0x40, 3, 2);
        case 0x17: case 0x18: return pick(0x66, 3, 0x10);
        case 0x19: return pick(0x69, 3, 0x10);
        case 0x1a: return pick(0x6c, 3, 0x10);
        case 0x13: return pick3(0x70, 5);
        case 0x12: return pick3(0x73, 5);
        case 0x11: return pick3(0x76, 13);
        case 0x14: return pick3(0x79, 15);
        case 0x10: return pick3(0x56, 14);
        case 0xe: return pick3(0x53, 7);
        case 0xf: return pick3(0x7c, 6);
        case 0x15: return pick3(0x60, 16);
        case 0x16: return pick3(99, 16);
        default: return 0;
        }
    }
    return surf == 0x1d ? 0xd : 0;
}

/* A car's sound event (0x4a9190): an impact (id -1) by its strength (2 .. 127, a tenth of the levels
   faster than the last one's), at the volume of its distance; the looped ids' sounds are pending. */
static void event_sound(int id, int kind, int surf, int strength, int dist, int angle)
{
    int att = fixdiv(g_racecam[0].mode == 0xc ? 0x8000000 : 0x2000000, dist + (g_racecam[0].mode == 0xc ? 0x10000 : 0x20000));
    if (g_racecam[0].mode == 0xc) att <<= 2;
    int vol = (att + ((att >> 31) & 0xffff)) >> 0x10;
    if (-1 < id) {
        /* The looped ones: the skids and scrapes (0x12 .. 0x15, the players' fronts and rears), by
           the surface and the tyres; the fronts with a squeal (id + 4) while the car turns hard. */
        Car *car = 0x12 <= id && id <= 0x15 ? g_cars[(id - 0x12) & 1] : NULL;
        if (strength < 1) {
            stop_slot(id);
            if (id == 0x12 || id == 0x13) {
                stop_slot(id + 4);
                if (car) *CAR_I(car, 0x1108) = 0;
            }
            return;
        }
        int patch;
        switch (surf) {
        case 5: case 8: case 9: case 0xd: patch = kind == 3 ? 0x28 : kind == 1 ? 0x2e : kind == 2 ? 0x2f : 0x2a; break;
        case 6: patch = kind == 3 ? 0x28 : 0x2d; break;
        case 7: patch = 0x29; break;
        case 10: case 0xb: patch = 0x2b; break;
        default: patch = 0x2c; break;
        }
        int vel;
        if (car || s_slot[id][1] != patch) {
            vel = (strength * 0x7f) / 0xa0000;
            if (0x7f < vel) vel = 0x7f;
            if (!car) s_slotvel[id] = (int8_t)vel;
        } else {
            vel = s_slotvel[id];
        }
        if (car) {
            int k = (strength * 0x7f + ((strength * 0x7f >> 31) & 0xffff)) >> 0x10;
            if (0x7f < k) k = 0x7f;
            vol = vol * k >> 7;
            if (id == 0x12 || id == 0x13) {
                if (vol < 0x1f) {
                    stop_slot(id + 4);
                    *CAR_I(car, 0x1108) = 0;
                } else if ((0 < *CAR_F(car, 0x11cc) && 30.0f < *CAR_F(car, 0x500)) || (*CAR_F(car, 0x11cc) < 0 && 30.0f < *CAR_F(car, 0x4f0))) {
                    play_slot(id + 4, 0x28, vel + 10, 0x10000, vol * 3, angle, 0x7f);   /* 0x5b2408 */
                    *CAR_I(car, 0x1108) = vol;
                }
            }
        }
        play_slot(id, patch, vel, 0x10000, 0x7e < vol ? 0x7f : vol, angle, 0x7f);
        return;
    }
    int lv;
    if (0x14 < surf && surf < 0x1b) {
        lv = (strength * 0x7f) / 0x1aaaa;
        lv = lv < 0x80 ? (lv < 2 ? 2 : lv) : 0x7f;
    } else {
        lv = (strength * 0x3f) / 0xa0000;
        lv = lv < 0x40 ? (lv < 2 ? 2 : lv) : 0x3f;
        lv += 0x40;
    }
    if (lv >> 4 <= s_level) return;
    s_level = lv >> 4;
    play_slot(0x33, impact_patch(kind, surf), 0x40, 0x10000, vol, angle, lv);
}

/* ---- the streamed one-shots (0x4afda0, 0x4a8340, 0x4b0370, 0x4b07f0) ----
   Small banks read from three archives when asked for: 0 the track's (track.viv, by the number in each
   entry's name), 1 the cars' (zzzwzzz.viv: the rain, then each race car's lden and ldex banks), 2 the
   speech's (zzzx<language>.viv, as listed in its oneshots.txt; the countdown its first four). Each
   kept in a cache of 32 (by its type and number, the least recently used given up); the requests in
   a ring of 64, played one after another. The original reads them on its file thread into a 32 KB
   buffer and plays them once read; the port's reads finish at once, in the tick they are asked for. */

typedef struct {
    VfsFile *f;               /* 0x65730c + 0x10 x type */
    uint32_t (*index)[2];     /* 0x657310: offset, size */
    int count;                /* 0x657314 */
} StreamSource;
static StreamSource s_stream[3];

typedef struct {
    int32_t type, idx, slot;  /* 0x656528: 32 x 0x18 */
    int32_t used;             /* the tick it was last asked for */
    uint8_t *data;
} StreamBank;
static StreamBank s_cache[0x20];

typedef struct { int32_t idx; uint8_t loadonly; int8_t type; } StreamReq;   /* 0x656b00: 64 x 0x20 */
static StreamReq s_req[0x40];
static int s_req_rd, s_req_wr;   /* 0x656ae4, 0x657300 */
static int32_t s_pair[2] = {-1, -1};   /* 0x6573a8, 0x6573ac: the one-shot playing (twice) */

static void stream_close(void)
{
    for (int t = 0; t < 3; t++) {
        if (s_stream[t].f) vfs_close(s_stream[t].f);
        free(s_stream[t].index);
        s_stream[t].f = NULL;
        s_stream[t].index = NULL;
        s_stream[t].count = 0;
    }
    for (int i = 0; i < 0x20; i++) {
        free(s_cache[i].data);
        s_cache[i] = (StreamBank){-1, -1, -1, 0, NULL};
    }
    s_req_rd = s_req_wr = 0;
    s_pair[0] = s_pair[1] = -1;
}

/* The archive kept open with an index of count entries. */
static bool stream_open(int t, const char *path, int count)
{
    StreamSource *src = &s_stream[t];
    src->f = vfs_open(path);
    if (!src->f) return false;
    src->count = count;
    src->index = calloc((size_t)count, sizeof *src->index);
    return src->index != NULL;
}

static void stream_set(int t, int i, const uint8_t *viv, size_t size, const char *name)
{
    uint32_t off = 0, len = 0;
    if (!viv || !BIG_locateentry(viv, name, 0, &off, &len) || size < off) off = len = 0;
    if (0 <= i && i < s_stream[t].count) {
        s_stream[t].index[i][0] = off;
        s_stream[t].index[i][1] = len;
    }
}

/* The archives' directories (Audio_LoadTrackSounds 0x4afda0). */
void Audio_LoadTrackSounds(void)
{
    stream_close();
    if (!s_on) return;
    char path[160];
    size_t size = 0;
    /* zzzwzzz.viv: the rain, the race cars' banks (<sound>lden, <sound>ldex, else sd6b's). */
    snprintf(path, sizeof path, exe_str(0x5d1e1c), sound_dir());
    uint8_t *viv = vfs_read_all(path, &size);
    if (viv && stream_open(1, path, g_ncars * 2 + 1)) {
        stream_set(1, 0, viv, size, exe_str(0x5d1e04));
        for (int k = 0; k < g_ncars * 2; k++) {
            const char *suffix = exe_str(k & 1 ? 0x5d1df4 : 0x5d1dfc);
            char name[64];
            snprintf(name, sizeof name, exe_str(0x5d1de8), (const char *)CAR_DESCB(g_cars[k / 2]) + 0x2e8, suffix);
            uint32_t off, len;
            if (!BIG_locateentry(viv, name, 0, &off, &len)) snprintf(name, sizeof name, exe_str(0x5d1dd8), suffix);
            stream_set(1, k + 1, viv, size, name);
        }
    }
    free(viv);
    /* track.viv: each entry by the number before its extension. */
    snprintf(path, sizeof path, exe_str(0x5d1dcc), sound_dir());
    viv = vfs_read_all(path, &size);
    if (viv && stream_open(0, path, 1000)) {
        uint32_t n = BIG_count(viv);
        for (uint32_t i = 0; i < n; i++) {
            uint32_t off = 0, len = 0;
            const char *name = BIG_locateentry(viv, NULL, (int)i, &off, &len);
            size_t l = name ? strlen(name) : 0;
            if (l < 7) continue;
            int num = atoi(name + l - 7);
            if (0 <= num && num < 1000) {
                s_stream[0].index[num][0] = off;
                s_stream[0].index[num][1] = len;
            }
        }
    }
    free(viv);
    /* The speech's: oneshots.txt's quoted names in order. */
    snprintf(path, sizeof path, exe_str(0x5d1dac), "GameData\\Speech\\", exe_str(0x5d1dbc));   /* 0x65b31c (the install's) */
    viv = vfs_read_all(path, &size);
    uint32_t toff = 0, tlen = 0;
    if (viv && BIG_locateentry(viv, exe_str(0x5d1d9c), 0, &toff, &tlen) && tlen && toff + tlen <= size) {
        char *txt = malloc(tlen + 1);
        if (txt) {
            memcpy(txt, viv + toff, tlen);
            txt[tlen] = 0;
            int n = 0;
            bool in = false;
            for (uint32_t i = 0; i < tlen; i++)
                if (txt[i] == '"' && !(in = !in)) n++;
            if (stream_open(2, path, n)) {
                int k = 0;
                char *from = txt;
                in = false;
                for (uint32_t i = 0; i < tlen; i++) {
                    if (txt[i] != '"') continue;
                    if (in) {
                        txt[i] = 0;
                        uint32_t off, len;
                        if (!BIG_locateentry(viv, from, 0, &off, &len)) printf("can't find \"%s\"\n", from);
                        stream_set(2, k++, viv, size, from);
                    } else {
                        from = txt + i + 1;
                    }
                    in = !in;
                }
            }
            free(txt);
        }
    }
    free(viv);
}

static void stream_request(int type, int idx, bool loadonly);

/* The cache's entry to take for a bank of the type: a free one, else the least recently used past 16
   ticks, else the last of a lower type (0x4a81b0); its bank given up. */
static int stream_evict(int type)
{
    for (int i = 0; i < 0x20; i++)
        if (s_cache[i].idx == -1) return i;
    int e = -1;
    int32_t oldest = g_sim_tick - 0x10;
    for (int i = 0; i < 0x20; i++)
        if (s_cache[i].idx != -1 && s_cache[i].used < oldest) {
            oldest = s_cache[i].used;
            e = i;
        }
    if (e < 0)
        for (int i = 0; i < 0x20; i++)
            if (s_cache[i].idx != -1 && s_cache[i].type < type) e = i;
    if (e < 0) return -1;
    if (s_cache[e].slot != -1) SND_BankRemove(s_cache[e].slot);
    free(s_cache[e].data);
    s_cache[e] = (StreamBank){-1, -1, -1, 0, NULL};
    return e;
}

/* The cached bank of (type, idx): its library slot, -1 when it isn't there (or not read yet); asked
   for (and not just looked at), a missing one gets an entry and a load-only request, unless more than
   five are waiting (0x4a8340, 0x4b0ac0). */
static int stream_bank(int type, int idx, bool look)
{
    for (int i = 0; i < 0x20; i++)
        if (s_cache[i].idx == idx && s_cache[i].type == type) {
            s_cache[i].used = g_sim_tick;
            return s_cache[i].slot;
        }
    if (look) return -1;
    int waiting = 0;
    for (int r = s_req_rd; r != s_req_wr; r = (r + 1) & 0x3f)
        if (0 <= s_req[r].type && s_req[r].loadonly) waiting++;
    if (5 < waiting) return -1;
    int e = stream_evict(type);
    if (e < 0) return -1;
    int wr = s_req_wr;
    stream_request(type, idx, true);
    if (wr == s_req_wr) return -1;
    s_cache[e] = (StreamBank){type, idx, -1, g_sim_tick, NULL};
    return -1;
}

/* Reads (type, idx) into its cache entry (the one asked for, else a new one) (the file thread's). */
static int stream_load(int type, int idx)
{
    int e = -1;
    for (int i = 0; i < 0x20 && e < 0; i++)
        if (s_cache[i].idx == idx && s_cache[i].type == type) e = i;
    if (0 <= e && s_cache[e].slot != -1) {
        s_cache[e].used = g_sim_tick;
        return s_cache[e].slot;
    }
    StreamSource *src = type >= 0 && type < 3 ? &s_stream[type] : NULL;
    if (!src || !src->f || idx < 0 || src->count <= idx || src->index[idx][1] == 0) return -1;
    if (e < 0) e = stream_evict(type);
    if (e < 0) return -1;
    uint32_t len = src->index[idx][1];
    uint8_t *d = malloc(len);
    if (!d || vfs_read_at(src->f, src->index[idx][0], d, len) != (int64_t)len) {
        free(d);
        s_cache[e] = (StreamBank){-1, -1, -1, 0, NULL};
        return -1;
    }
    s_cache[e] = (StreamBank){type, idx, SND_BankAdd(d), g_sim_tick, d};
    return s_cache[e].slot;
}

/* A one-shot request (0x4af6a0's defaults: type 2; 0x4b0370). */
static void stream_request(int type, int idx, bool loadonly)
{
    if (0x20 < ((s_req_wr - s_req_rd) & 0x3f)) return;
    if (((s_req_wr + 1) & 0x3f) == s_req_rd) return;
    if (type < 0 || 3 < type) return;
    s_req[s_req_wr] = (StreamReq){idx, (uint8_t)loadonly, (int8_t)type};
    s_req_wr = (s_req_wr + 1) & 0x3f;
}

/* A one-shot's play (0x4b02b0): the bank's patch 0 (type 3: the collisions' bank's patch idx), at the
   speech's volume (the countdown's four by 0x4a80f0, the rest by 0x4a80d0) or the track's. */
static int stream_play(const StreamReq *r, int slot)
{
    SndParams p;
    SND_ParamsDefault(&p);
    int bank = slot, patch = 0;
    if (r->type == 3) {
        patch = r->idx;
        bank = s_banks[3].slot;
    }
    if (r->type == 2) p.vol = (int8_t)(r->idx < 4 ? (opt(0x5d174c) * opt(0x5d14c0) * 0x55) >> 0xe : (opt(0x5d174c) * opt(0x5d14b4) * 0x55) >> 0xe);
    else p.vol = (int8_t)((opt(0x5d174c) * 0x3a74) >> 0xe);
    p.velocity = 0x40;
    p.pan = 0x40;
    return SND_Play(bank, patch, &p);
}

/* Each odd tick (0x4b07f0, 0x4b06c0, 0x4b0450): the one-shot playing done when both its plays have
   less than 0.05 s left; then the next request loaded and, unless it only loads, played twice. */
static void stream_tick(void)
{
    if (s_pair[0] != -1 && s_pair[1] != -1) {
        int a = SND_TimeLeft(s_pair[0]), b = SND_TimeLeft(s_pair[1]);
        if (a < 5 && b < 5) s_pair[0] = s_pair[1] = -1;
    }
    while (s_req_rd != s_req_wr) {
        StreamReq *r = &s_req[s_req_rd];
        if (!r->loadonly && s_pair[0] != -1) break;
        int slot = r->type == 3 ? s_banks[3].slot : stream_load(r->type, r->idx);
        if (!r->loadonly && (0 <= slot || r->type == 3)) {
            s_pair[0] = stream_play(r, slot);
            s_pair[1] = stream_play(r, slot);
        }
        s_req_rd = (s_req_rd + 1) & 0x3f;
    }
}

void Audio_Countdown(int step) { stream_request(2, step, false); }   /* 0x4a6010 */

/* ---- the engines (0x4ac9e0, 0x4ac550, 0x4ac060) ---- */

typedef struct {
    int16_t start, end;       /* the curve's non-zero part of the 512 rpm steps */
    const int8_t *curve;
    int8_t patch;             /* -1 none; below 0x40 on the throttle, from 0x40 off it */
} EngineLayer;

typedef struct {
    uint16_t vol, pitch, pan, spread, rpm, xfade, throttle, view;
} EngineSnap;

typedef struct {
    int8_t vol;
    int16_t pitch, pan;
    int32_t handle;
} EngineVoice;

/* The engine's state (0x758 bytes, 0x656aa8[slot]). */
typedef struct {
    int32_t k[13];            /* +0x00: 0x32, 0, 0x46, 0x14, 0x40, 0x80, 0x80, 0xaa, 0x50, 0x68, 0x19, 0xdc, 0x1e */
    int32_t countdown;        /* +0x34 */
    int32_t start_h, stop_h;  /* +0x38, +0x3c: the engine's start and stop sounds */
    uint8_t started;          /* +0x40 */
    int32_t vol[32];          /* +0x44: each layer's, eased */
    int32_t pan, spread, pitch;   /* +0xc4, +0xc8, +0xcc */
    int8_t bank;              /* +0xd0 */
    EngineSnap snap[32];      /* +0xd2 */
    uint8_t wr, rd;           /* +0x2d2, +0x2d3 */
    EngineLayer layer[32];    /* +0x2d4: the four tables' eight layers */
    EngineVoice v[32], v2[32];/* +0x454, +0x5d4 */
    int8_t *tables;           /* +0x754 */
} Engine;

static Engine *s_engine[2];   /* 0x656aa8 */
static bool s_tick_added;     /* 0x656ab0 */
static void engine_tick(void);

static void engines_free(void)
{
    s_tick_added = false;
    for (int i = 0; i < 2; i++) {
        if (s_engine[i]) free(s_engine[i]->tables);
        free(s_engine[i]);
        s_engine[i] = NULL;
    }
}

/* A CRDl table (version 2): +0x20 the layers' patches, +0x128 their curves' offsets from their own
   words. */
static void take_table(Engine *e, int t, const uint8_t *crd)
{
    for (int i = 0; i < 8; i++) {
        EngineLayer *l = &e->layer[t * 8 + i];
        int8_t patch = (int8_t)crd[0x20 + i];
        if (patch < 0) continue;
        int32_t off;
        memcpy(&off, crd + 0x128 + 4 * i, 4);
        const uint8_t *c = crd + 0x128 + 4 * i + off;
        l->patch = patch;
        int s = 0;
        while (s < 0x200 && c[s] == 0) s++;
        int en = s;
        while (en < 0x200 && c[en] != 0) en++;
        l->start = (int16_t)s;
        l->end = (int16_t)en;
        l->curve = (const int8_t *)c;   /* (copied below into the engine's own block) */
    }
}

bool Audio_LoadEngine(int slot, const char *sound)
{
    if (slot < 0 || 1 < slot || s_engine[slot] || !s_on) return false;
    Engine *e = calloc(1, sizeof *e);
    if (!e) return false;
    s_engine[slot] = e;
    e->start_h = -1;
    e->stop_h = -1;
    for (int i = 0; i < 32; i++) {
        e->v[i].handle = e->v2[i].handle = -1;
        e->layer[i].patch = -1;
        e->layer[i].start = e->layer[i].end = 0x200;
    }
    static const int32_t k[13] = {0x32, 0, 0x46, 0x14, 0x40, 0x80, 0x80, 0xaa, 0x50, 0x68, 0x19, 0xdc, 0x1e};
    memcpy(e->k, k, sizeof k);
    char path[160];
    size_t size = 0;
    uint8_t *viv = NULL;
    if (1 < g_sound_quality) {
        snprintf(path, sizeof path, exe_str(0x5d1b88), sound_dir(), sound);
        viv = vfs_read_all(path, &size);
    }
    if (!viv) {
        snprintf(path, sizeof path, exe_str(0x5d1b7c), sound_dir(), sound);
        viv = vfs_read_all(path, &size);
    }
    if (!viv) {
        snprintf(path, sizeof path, exe_str(1 < g_sound_quality ? 0x5d1b6c : 0x5d1b60), sound_dir());
        viv = vfs_read_all(path, &size);
    }
    if (!viv) return false;
    /* The entries by their extension: the bank, then .ect, .elt, .cct, .clt (0x5d146c, 0x5d1b3c ..). */
    static const uint32_t ext[4] = {0x5d1b3c, 0x5d1b34, 0x5d1b2c, 0x5d1b24};
    uint8_t *crd[4] = {0};
    bool bank = false;
    uint32_t n = BIG_count(viv);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t off = 0, len = 0;
        const char *name = BIG_locateentry(viv, NULL, (int)i, &off, &len);
        if (!name || size < off + len) continue;
        size_t nl = strlen(name);
        const char *x = nl >= 4 ? name + nl - 4 : name;
        if (!strcmp(x, exe_str(0x5d146c)) && !bank) {
            uint8_t *b = malloc(len);
            if (!b) continue;
            memcpy(b, viv + off, len);
            add_bank(b, (int)len, slot);
            e->bank = (int8_t)s_banks[slot].slot;
            bank = true;
            continue;
        }
        for (int t = 0; t < 4; t++)
            if (!crd[t] && !strcmp(x, exe_str(ext[t]))) {
                crd[t] = malloc(len);
                if (crd[t]) memcpy(crd[t], viv + off, len);
                if (crd[t] && memcmp(crd[t], "CRDl", 4)) printf("%s %s - file id incorrect\n", path, name);
                if (crd[t] && crd[t][4] != 2) printf("%s %s - file version incorrect\n", path, name);
            }
    }
    free(viv);
    if (bank && crd[0] && crd[1] && crd[3]) {
        int total = 0;
        for (int t = 0; t < 4; t++)
            if (crd[t]) take_table(e, t, crd[t]);
        for (int i = 0; i < 32; i++)
            if (e->layer[i].patch >= 0) total += e->layer[i].end - e->layer[i].start;
        e->tables = malloc((size_t)(total ? total : 1));
        int8_t *at = e->tables;
        for (int i = 0; i < 32 && at; i++) {
            EngineLayer *l = &e->layer[i];
            int len = l->end - l->start;
            if (l->patch < 0 || len == 0) continue;
            memcpy(at, l->curve + l->start, (size_t)len);
            l->curve = at;
            at += len;
        }
    }
    for (int t = 0; t < 4; t++) free(crd[t]);
    if (slot == 0) {   /* 0x4ad2a0 (Audio_LoadCollisionSounds 0x4a8aa0, after the engine) */
        if (!s_tick_added) SND_AddTick(engine_tick);
        s_tick_added = true;
    }
    return bank;
}

/* Every third snapshot (0x4ac060): each layer's volume from its curve at the rpm, the throttle's fade
   (out for the first and third tables, in for the others), the off-throttle crossfade (fade in on the
   throttle's patches, out off it), the volume; eased 8 a time. Then each layer's voices: off at 0;
   started or set to the volume, the pitch and the pan (the off-throttle patches two voices, the pan
   less and plus the spread). */
static void engine_play(Engine *e);
static void engine_tick(void) { if (s_engine[0]) engine_play(s_engine[0]); }   /* 0x4ac060 on the library's tick */

static void engine_play(Engine *e)
{
    if (e->rd == e->wr) return;
    if (0 < --e->countdown) return;
    e->countdown = 3;
    const EngineSnap *s = &e->snap[e->rd];
    for (int i = 0; i < 32; i++) {
        const EngineLayer *l = &e->layer[i];
        if (l->patch < 0) continue;
        int v = 0;
        if ((s->view != 0 && i < 0x10) || (s->view == 0 && 0xf < i)) {
            int r = s->rpm;
            if (l->start <= r && r < l->end) {
                int c = l->curve[r - l->start];
                if (c) {
                    int f = (i < 8 || (0xf < i && i < 0x18)) ? fade_out(s->throttle) : fade_in(s->throttle);
                    c *= f;
                    if (l->patch < 0x40) v = g_sound_quality < 2 ? fade_in(s->xfade) * c : 0;
                    else if (g_sound_quality < 2) v = s->spread == 0 ? fade_out(s->xfade) * c : ((fade_out(s->xfade) * 0x5e) >> 7) * c;
                    else v = c * 0x7f;
                }
            }
        }
        v = (int)((uint32_t)((v >> 3) * (int)s->vol)) >> 0x12;
        if (0x7f < v) v = 0x7f;
        int *cur = &e->vol[i];
        if (*cur + 8 < v) *cur += 8;
        else if (v < *cur - 8) *cur -= 8;
        else *cur = v;
    }
    e->pan = s->pan;
    e->spread = s->spread;
    e->pitch = s->pitch;
    e->rd = (uint8_t)((e->rd + 1) & 0x1f);
    for (int i = 0; i < 32; i++) {
        const EngineLayer *l = &e->layer[i];
        if (l->patch < 0) continue;
        EngineVoice *a = &e->v[i], *b = &e->v2[i];
        int vol = e->vol[i];
        if (vol == 0) {
            if (a->handle != -1) {
                SND_Stop(a->handle);
                if (b->handle != -1) SND_Stop(b->handle);
                a->handle = b->handle = -1;
            }
            continue;
        }
        int16_t p1, p2;
        if (l->patch < 0x40) {
            if (e->spread < 1) {
                if (a->pan == -0x8000 && 0 < a->vol) {
                    vol = 0;
                    p1 = p2 = -0x8000;
                } else {
                    p1 = p2 = (int16_t)e->pan;
                }
            } else {
                p1 = p2 = -0x8000;
            }
        } else {
            p1 = (int16_t)(e->pan - e->spread);
            p2 = (int16_t)(e->spread + e->pan);
        }
        SndParams p;
        SND_ParamsDefault(&p);
        p.pitch = (uint16_t)e->pitch;
        p.f0b = 1;
        p.pan1a = 0;
        p.vol = (int8_t)vol;
        if (a->handle == -1) {
            p.pan18 = p1;
            a->handle = SND_Play(e->bank, l->patch, &p);
            a->vol = (int8_t)vol;
            a->pan = p1;
            a->pitch = (int16_t)e->pitch;
            continue;
        }
        if (a->vol != vol) {
            SND_SetVolume(a->handle, vol);
            a->vol = (int8_t)vol;
        }
        if (a->pan != p1) {
            SND_SetPan(a->handle, (uint16_t)p1, 0);
            a->pan = p1;
        }
        if ((uint16_t)a->pitch != (uint32_t)e->pitch) {
            SND_SetPitch(a->handle, e->pitch);
            a->pitch = (int16_t)e->pitch;
        }
        if (l->patch < 0x40 || e->spread == 0) {
            if (b->handle != -1) {
                SND_Stop(b->handle);
                b->handle = -1;
            }
        } else if (b->handle == -1) {
            p.pan18 = p2;
            b->handle = SND_Play(e->bank, l->patch, &p);
            b->vol = (int8_t)vol;
            b->pan = p2;
            b->pitch = (int16_t)e->pitch;
        } else {
            if (b->vol != vol) {
                SND_SetVolume(b->handle, vol);
                b->vol = (int8_t)vol;
            }
            if (b->pan != p2) {
                SND_SetPan(b->handle, (uint16_t)p2, 0);
                b->pan = p2;
            }
            if ((uint16_t)b->pitch != (uint32_t)e->pitch) {
                SND_SetPitch(b->handle, e->pitch);
                b->pitch = (int16_t)e->pitch;
            }
        }
    }
}

/* The engine's snapshot (0x4ac550): its rpm step (rpm over the redline, 16.16, times k9), its pitch
   (k8 x the rpm plus 0.2 times the Doppler), its throttle, its view; the volume and crossfade by the
   view (in the car or chase: the throttle's; else the front-back mix by the car's heading to the
   listener). The engine's start (patches 6 .. 8) and stop (9 .. 11) sounds, the loops quiet under the
   start's. */
static void engine_snap(int slot, int vol, int rpm16, int throttle, int view, int pitch, int pan, int facing)
{
    Engine *e = slot >= 0 && slot < 2 ? s_engine[slot] : NULL;
    if (!e || ((e->wr + 1) & 0x1f) == e->rd) return;
    EngineSnap *s = &e->snap[e->wr];
    int r = e->k[9] * rpm16;
    s->rpm = (r & (int)0xffffc000u) < 0x7fc000 ? (uint16_t)(r >> 0xe) : 0x1ff;
    int64_t pp = (int64_t)(rpm16 + 0x3333) * pitch;
    uint32_t lo = (uint32_t)pp;
    s->pitch = (uint16_t)((e->k[8] * (int)((lo >> 0x10) + (int32_t)(pp >> 32) * 0x10000 + ((lo >> 0xf & 1) != 0))) >> 10);
    int t = throttle + (throttle >> 6) + (throttle >> 5);
    t = t < 0x80 ? (t < 0 ? 0 : t) : 0x80;
    s->throttle = (uint16_t)t;
    s->view = (uint16_t)view;
    int m;
    if (view < 2) {
        s->xfade = (uint16_t)(view == 0 ? e->k[3] : e->k[1]);
        s->pan = 0;
        s->spread = g_race_players == 1 || 1 < g_sound_quality ? 0 : (uint16_t)(s_nearvol * 0x3fff >> 7);
        m = view == 0 ? e->k[2] : e->k[0];
    } else {
        int base = e->k[4];
        int w = facing < 0 ? e->k[5] * base : (0x80 - base) * e->k[6];
        s->spread = 0;
        s->pan = (uint16_t)pan;
        s->xfade = (uint16_t)((int16_t)((((int64_t)w * facing >> 7) + ((w * facing >> 0x1f) & 0xffff)) >> 0x10) + base);
        m = e->k[7];
    }
    int v = m * vol >> 6;
    s->vol = (uint16_t)(0x7fe < v ? 0x7ff : v);
    int gain = 0x200;
    int cap = opt(0x5d1748);
    if (throttle < 0) {
        if (-1 < e->start_h) {
            SND_Stop(e->start_h);
            e->start_h = -1;
            e->started = 1;
        }
        if (e->started) {
            if (e->stop_h == -1) {
                SndParams p;
                SND_ParamsDefault(&p);
                p.vol = (int8_t)(s->vol < cap ? s->vol : cap);
                p.pan18 = (int16_t)pan;
                e->stop_h = SND_Play(e->bank, (view < 2 ? view : 2) + 9, &p);
            } else if (SND_Playing(e->stop_h)) {
                SND_SetVolume(e->stop_h, s->vol < cap ? s->vol : cap);
                SND_SetPan(e->stop_h, pan, 0);
            } else {
                e->started = 0;
                e->stop_h = -1;
            }
        }
    } else {
        if (-1 < e->stop_h) {
            SND_Stop(e->stop_h);
            e->stop_h = -1;
            e->started = 0;
        }
        if (!e->started) {
            if (e->start_h == -1) {
                SndParams p;
                SND_ParamsDefault(&p);
                p.vol = (int8_t)(s->vol < cap ? s->vol : cap);
                p.pan18 = (int16_t)pan;
                e->start_h = SND_Play(e->bank, (view < 2 ? view : 2) + 6, &p);
                gain = 0;
            } else if (!SND_Playing(e->start_h)) {
                e->started = 1;
                e->start_h = -1;
            } else {
                SND_SetVolume(e->start_h, s->vol < cap ? s->vol : cap);
                SND_SetPan(e->start_h, pan, 0);
                int left = SND_TimeLeft(e->start_h);
                gain = -1 < 0x200 - left ? 0x200 - left : 0;
            }
        }
    }
    s->vol = (uint16_t)((s->vol * gain) >> 9);
    e->wr = (uint8_t)((e->wr + 1) & 0x1f);
    engine_play(e);
}

/* The player's engine and the collisions' bank, Lil'd at the low qualities (0x4a8aa0); the speech's
   and the track's banks marked as streamed (-3, -2). */
void Audio_LoadCollisionSounds(const char *engine)
{
    if (!s_on || 2 < g_sound_quality) return;
    Audio_LoadEngine(0, engine);
    char name[160];
    snprintf(name, sizeof name, "%s%s%s", sound_dir(), 1 < g_sound_quality ? exe_str(0x5d1a1c) : "", exe_str(0x5d1a20));
    load_bank(name, 3);
    s_banks[5].slot = -2;
    s_banks[2].slot = -3;
    for (int i = 0; i < 0x50; i++) s_slot[i][0] = s_slot[i][1] = -1;
}

/* ---- the cars' sounds (0x4a9e20, 0x4a7430) ---- */

static uint8_t s_throttle[CAR_MAX];   /* 0x6563c8: each car's eased throttle */


/* The engine's part of the car's sounds (0x4a9e20): the volume by the distance (2048 / (d + 2)), the
   rpm's lift, the gear changes; the throttle eased (up by half the gap, down by an eighth); the view
   (1 in the car, 2 chase, 3 others). The wind, rain, gearbox whine and the rest are pending. */
static void car_sounds(Car *c, int dist, int rpmvol, int pitch, int pan, int heading, int speedv, int facing)
{
    (void)heading;
    if (0 < s_level) s_level--;
    if (g_racecam[0].mode == 0xc && 1 < g_replay_mode) PENDING_NOTE(0x4a9e20, "the TV camera's distances for the car's sounds");
    int att = fixdiv(0x8000000, dist + 0x20000);
    int a = (att + ((att >> 31) & 0xffff)) >> 0x10;
    /* The speed's term: |speed|, three times it for the others than the type 1 (+0x464). */
    float sp = *CAR_F(c, 0xd60);
    int type = *CAR_I(c, 0x464);
    int speed = (int)(type == 1 ? fabsf(sp) : fabsf(sp * 3.0f));   /* 0x5b2600; _ftol */
    int half = speed >> 1;
    int windvel = half + 0x40 < 0x7f ? half + 0x40 : 0x7f;
    int lift = rpmvol < 0x97 ? (rpmvol < 0 ? 0 : rpmvol) : 0x96;
    PENDING_NOTE(0x565150, "the reverb's sends under roofs (the car's 0x473290 test)");
    if (*CAR_I(c, 0x540) != 0) type = 4;
    int view, wind = half;
    switch (g_racecam[0].mode) {
    case 0:
    case 0xf: view = 1; wind = speed; break;
    case 2:
    case 3:
    case 4: view = 2; break;
    default:
        if (!(g_racecam[0].flags2 & 8)) view = 3;
        else {
            view = 0;   /* (0x443e10, the car's interior, taken as no) */
            wind = speed;
        }
        break;
    }
    int idx = *CAR_I(c, 0x520);
    if (idx < 0 || CAR_MAX <= idx) idx = 0;
    int thr = *CAR_I(c, 0xd98) == 0 ? c->b[0xd7f] : c->b[0xd80];
    int e = s_throttle[idx];
    if (e < thr) {
        int d = (thr - e) / 2;
        s_throttle[idx] = (uint8_t)(e + (d < 2 ? 1 : d));
    } else if (thr < e) {
        int d = (thr - e) / 8;   /* ((d + (d >> 31 & 7)) >> 3: towards zero */
        s_throttle[idx] = (uint8_t)(e + (-2 < d ? -1 : d));
    }
    int eased = s_throttle[idx] < 0xff ? s_throttle[idx] : 0xff;
    int up = ((lift + 0x28) * a + (((lift + 0x28) * a >> 31) & 0x7f)) >> 7;
    int v = up <= a ? a : up;
    int vol = (v * 0x7f) >> 7;
    int vol0 = vol;
    wind = wind - 0xf < 1 ? 0 : wind - 0xf;
    int windvol = (v * wind) >> 7;
    if (0x7a < windvol) windvol = 0x7a;
    if (speedv != 0 || g_racecam[0].mode == 0xb) vol = (vol0 << 0x10) / pitch;
    /* The wind by the car's type (patch 0x11 on), at the speed's velocity. */
    if (windvol == 0) stop_slot(0x18);
    else play_slot(0x18, type + 0x11, windvel, pitch, windvol, pan, 0x7f);
    stop_slot(0x1c);
    PENDING_NOTE(0x443e10, "the road's noise inside the car (slot 0x1a, the car's interior)");
    stop_slot(0x1a);
    int8_t shift = (int8_t)c->b[0xd84];
    if (shift && (int8_t)c->b[0xd81] < (int8_t)c->b[0xd82] && (int8_t)c->b[0xd81] != 1 && eased) {
        vol = ((shift * vol) / *SPEC_I(c, 0x54) + vol) >> 1;
        int q = eased >> 2;
        if (shift == 5) eased -= q;
        else if (shift == 4) eased >>= 1;
        else if (shift == 3) eased = q;
        else if (shift < 3) eased = 0;
        s_throttle[idx] = (uint8_t)eased;
    }
    /* The gear change's clunk in the views inside (the engine bank's patch 2). */
    if ((g_racecam[0].flags2 & 8) && g_racecam[0].mode != 0 && g_racecam[0].mode != 0xf && shift == 1)
        play_once(s_banks[0].slot, 2, 0, 0x7f, 0x40, 0x10000, 0x7f);
    /* The gearbox's whine (slot 0, patch 0xf): by the view's volume, the rpm against half the redline,
       the throttle and the car's whine (+0x54c); pitched by the rpm. */
    Engine *en = s_engine[0];
    int vv = en ? (((view == 0 ? en->k[11] << 4 : view == 1 ? en->k[10] : en->k[12]) * vol) >> 7) : 0;   /* 0x4ad410 */
    if (0x7e < vv) vv = 0x7f;
    float whine = *CAR_F(c, 0x54c);
    if (whine <= 0.1f) {   /* 0x5b2604 */
        stop_slot(0);
    } else {
        float rpm = *CAR_F(c, 0xdb4), red = *SPEC_F(c, 0x94), f2, f3;
        if (rpm <= red * 0.5f) {
            f2 = red * 0.25f;
            f3 = rpm * 0.5f - f2;
        } else {
            f2 = red * 0.9f;   /* 0x5b25e8 */
            f3 = f2 - rpm;
        }
        if (f3 < 0) f3 = 0;
        float pf = ((rpm * 2.5f) / red + 0.5f) * (float)pitch;   /* 0x5b2464 */
        int wv = (int)lrintf((float)vv * (f3 / f2) * (float)c->b[0xd7f] * whine * 0.007843138f);   /* 0x5b4b50 */
        if (0x7e < wv) wv = 0x7f;
        if (wv < 1) wv = 0;
        play_slot(0, 0xf, 0x40, (int)lrintf(pf), wv, pan, 0x7f);
    }
    int rpm16 = (int)(*CAR_F(c, 0xdb4) / *SPEC_F(c, 0x94) * 65536.0f);   /* 0x5b3e68 */
    int throttle = *CAR_I(c, 0xdb0) == 0 ? -1 : *CAR_I(c, 0x548) == 0 ? eased >> 1 : 0x7f;
    int engvol = ((opt(0x5d1748) * opt(0x5d14c8) * 0x55 >> 7) * vol) >> 0xe;
    engine_snap(0, engvol, rpm16, throttle, view, DESC_I(c, 0x518) ? pitch * 2 : pitch, pan, facing);
}

static float s_listener[3], s_lastlistener[3];   /* 0x655e50, 0x655f44 */
static float s_lright[3], s_lfwd[3];              /* 0x655e5c, 0x655e74 */
static const float *s_lvel;                       /* 0x655e44: the listener's speed (the camera's car's), NULL from the TV cameras */

/* An angle from (x, y) in 1024 steps (0x565030: by octant from the exe's arctangent bytes 0x5e24ac). */
static int angle(int x, int y)
{
    int base = 0;
    int neg = y < 0;
    if (neg) {
        y = -y;
        base = 0x200;
    }
    if (x < 0) {
        neg = !neg;
        x = -x;
        base = -base;
    }
    int lo = x, hi = y;
    if (y < x) {
        base += neg ? -0x100 : 0x100;
        neg = !neg;
        lo = y;
        hi = x;
    }
    int r;
    if (x == y) r = 0x80;
    else {
        uint32_t q;
        if (((uint32_t)lo & 0xff800000u) == 0) {
            q = ((uint32_t)(hi >> 1) + (uint32_t)lo * 0x100) / (uint32_t)hi;
        } else {   /* 0x58dd60, 0x58db30: the 32-bit fraction, rounded to 8 bits */
            uint32_t f = (uint32_t)(((uint64_t)(uint32_t)lo << 32) / (uint32_t)hi);
            q = (f >> 24) + ((f & 0x800000) != 0);
        }
        r = exe_data(0x5e24ac + q, 1)[0];
    }
    return neg ? base - r : r + base;
}

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/* The track's sound elements (CAudioElem): each keeps at +0x7c a record of its position (copied from
   the element's, 0x4abf40), +0x88 its kind, +0x8a its sound (an entry of track.viv), +0x8c its radius,
   +0x90 its inner radius, +0x94 its period and +0x96 its random part (in eighths of ticks), +0x98 and
   +0x9a its repeats and their random part, +0x9c its volume (percent), +0x9e the time it plays next,
   +0xa0 its voice (-1 none), +0xa2 its one-shot's trigger (kind 1, set by the scene's triggers). Up to
   16 play at once, each voice in slot 0x39 + its number (0x656ac0: 16 x 0x10 bytes). */
enum { AE_POS = 0x7c, AE_KIND = 0x88, AE_SOUND = 0x8a, AE_RADIUS = 0x8c, AE_INNER = 0x90, AE_PERIOD = 0x94,
       AE_RANDOM = 0x96, AE_REPEAT = 0x98, AE_REPRND = 0x9a, AE_VOLUME = 0x9c, AE_NEXT = 0x9e, AE_VOICE = 0xa0,
       AE_TRIGGER = 0xa2 };
typedef struct {
    SceneElem *e;             /* +0x0 */
    int32_t h;                /* +0x4: its play, -1 none */
    int16_t sound;            /* +0xc */
    int16_t repeats;          /* +0xe: plays left */
} ElemVoice;
static ElemVoice s_ev[16];
static bool s_ev_on;          /* (0x656ac0 allocated) */

static float ae_f(const SceneElem *e, int off) { return scene_f32(e, off); }
static bool is_audio(const SceneElem *e) { return e && e->kind == SCENE_AUDIO; }
static float *ae_pos(SceneElem *e)   /* 0x4abf40 */
{
    memcpy(e->b + AE_POS, e->b + SCENE_POS, 12);
    return (float *)(void *)(e->b + AE_POS);
}
static float dist3(const float *a, const float *b)   /* 0x532110 */
{
    float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return sqrtf(dot3(d, d));
}
/* The ground-plane distance's estimate: the larger difference plus a quarter of the smaller. */
static float rough_dist(const float *p, float x, float z)
{
    float a = fabsf(p[0] - x), b = fabsf(p[2] - z);
    return a <= b ? a * 0.25f + b : b * 0.25f + a;   /* 0x5b2468 */
}

static void voice_free(int v)
{
    ElemVoice *ev = &s_ev[v];
    if (ev->h != -1) {
        stop_slot(v + 0x39);
        ev->h = -1;
    }
    if (ev->e) scene_set_i16(ev->e, AE_VOICE, -1);
    ev->e = NULL;
    ev->sound = -1;
}

/* The voices stopped, the elements' times and voices cleared (0x4ae7a0). */
static void elems_reset(void)
{
    if (s_ev_on)
        for (int v = 0; v < 16; v++) {
            if (s_ev[v].h != -1) {
                stop_slot(v + 0x39);
                s_ev[v].h = -1;
            }
            s_ev[v].e = NULL;
            s_ev[v].sound = -1;
        }
    SceneList *l = Scene_List();
    for (int i = 0; l && i < l->n; i++)
        if (is_audio(l->e[i])) {
            ae_pos(l->e[i]);
            scene_set_i16(l->e[i], AE_NEXT, 0);
            scene_set_i16(l->e[i], AE_VOICE, -1);
        }
}

static void voice_take(int v, SceneElem *e)
{
    ElemVoice *ev = &s_ev[v];
    ev->e = e;
    int r = scene_i16(e, AE_REPRND);
    ev->repeats = (int16_t)(scene_i16(e, AE_REPEAT) + (0 < r ? (int)(REAL_rand() % (uint32_t)(r + 1)) : 0));
    scene_set_i16(e, AE_VOICE, (int16_t)v);
}

/* An element within its radius (+100) of the listener (0x4ae890): a voice for it (a free one, else the
   farthest one whose sound is over, if farther than it), its sound played over its repeats, its kind's
   pan (1, 5 .. by its direction; 2 the middle; 3 and 4 the car's left and right) and volume (1, 6, 7:
   by the squared distance over the radius; the others full inside the inner radius, falling to the
   radius). Kind 1 plays once its trigger is set, its sound asked for until then. */
static void elem_play(SceneElem *e, int tick8, const float *vel, int vol, int heading, const float *at)
{
    if (!s_ev_on) return;
    float *rec = ae_pos(e);
    float d = dist3(rec, at);
    int kind = scene_i16(e, AE_KIND);
    if (!(d < ae_f(e, AE_RADIUS) + 100.0f)) {   /* 0x5b4bb0 */
        int v = scene_i16(e, AE_VOICE);
        if (-1 < v) voice_free(v);
        return;
    }
    int snd = scene_i16(e, AE_SOUND);
    if (!(snd < s_stream[0].count && -1 < snd && s_stream[0].index && s_stream[0].index[snd][1] != 0)) {
        int v = scene_i16(e, AE_VOICE);
        if (-1 < v) voice_free(v);
        return;
    }
    int v = scene_i16(e, AE_VOICE);
    if (v < 0) {
        int next = scene_i16(e, AE_NEXT);
        if (0 < next && next != tick8) return;
        for (int k = 0; k < 16 && v < 0; k++)
            if (!s_ev[k].e) {
                voice_take(k, e);
                v = k;
            }
        if (v < 0) {
            int far = 0;
            float fd = 0;
            for (int k = 0; k < 16; k++) {
                float dk = dist3(ae_pos(s_ev[k].e), at);
                if (s_ev[k].h != -1 && !SND_Playing(s_ev[k].h) && fd < dk) {
                    far = k;
                    fd = dk;
                }
            }
            if (!(d < fd)) return;
            voice_take(far, e);   /* (the element it had keeps its number: 0x4aed00 drops it) */
            v = far;
        }
    }
    ElemVoice *ev = &s_ev[v];
    if (ev->sound != snd) {
        if (ev->h != -1) {
            stop_slot(v + 0x39);
            ev->h = -1;
        }
        ev->sound = (int16_t)snd;
    }
    bool again = false;
    if (ev->h != -1 && !SND_Playing(ev->h)) {
        if (ev->repeats != 0) {
            ev->repeats--;
            again = true;
            ev->h = -1;
        } else if (kind != 1 || ae_f(e, AE_RADIUS) < d) {
            stop_slot(v + 0x39);
            ev->h = -1;
            ev->sound = -1;
            scene_set_i16(ev->e, AE_VOICE, -1);
            ev->e = NULL;
        }
    }
    if (!ev->e) return;
    int pitch = 0x10000, level = 0, pan = 0;
    if (kind == 1 && !again) {
        if (ev->h != -1 && !SND_Playing(ev->h)) return;
        if (!e->b[AE_TRIGGER]) {
            stream_bank(0, ev->sound, false);
            return;
        }
        e->b[AE_TRIGGER] = 0;
    }
    float r = ae_f(e, AE_RADIUS);
    if (d < r) {
        if (kind == 3) pan = heading - 0x4000;
        else if (kind == 4) pan = heading + 0x3fff;
        else if (kind != 2) {
            if (opt(0x5d1760)) {   /* 0x4a6db0 */
                float q[3] = {rec[0] - s_listener[0], rec[1] - s_listener[1], rec[2] - s_listener[2]};
                int x = (int)(dot3(q, s_lright) * 65536.0f), y = (int)(dot3(q, s_lfwd) * 65536.0f);
                if (g_r3d_mirror) x = -x;
                pan = (angle(x >> 8, y >> 8) & 0x3ff) << 6;
            }
            /* 0x4a6ce0: the Doppler of the element's speed less the listener's */
            float closing = 0;
            float q[3] = {rec[0] - s_listener[0], rec[1] - s_listener[1], rec[2] - s_listener[2]};
            if (q[0] != 0 || q[1] != 0 || q[2] != 0) {
                float l = sqrtf(dot3(q, q)), u[3] = {q[0] / l, q[1] / l, q[2] / l};
                if (vel) closing = dot3(u, vel);
                if (s_lvel) closing -= dot3(u, s_lvel);
            }
            pitch = (int)(340.0 / ((double)closing + 340.0) * 65536.0);
        }
        if ((6 <= kind && kind <= 7) || kind == 1) {
            float r2 = r * r;
            level = (int)((double)((r2 - d * d) / r2 * (float)vol * 127.0f) * 0.0078125);   /* 0x5b4b4c, 0x5b4ba8 */
        } else {
            float in = ae_f(e, AE_INNER);
            if (d < in) level = 0x7f;
            else {
                float w = (r - in) * (r - in), x = d - in;
                level = (int)((w - x * x) / w * 127.0f);
            }
        }
    }
    if (0xa0000 < pitch) pitch = 0xa0000;
    if (pitch < 2) pitch = 1;
    ev->h = play_slot(v + 0x39, ev->sound, 0x40, pitch, scene_i16(e, AE_VOLUME) * level / 100, pan & 0xffff, 0x7f);
}

/* Each odd tick for the player's car (0x4aed00): a quarter of the scene's elements a tick (by the tick's
   bits 1-2), each one's next time set from its period once past (with the voice it has, if still its
   own), those near (the rough distance under the radius + 100) or with a voice played. Not in split
   screen. */
static void elems_tick(Car *c, int heading)
{
    if (g_race_players == 1 || !s_on || !opt(0x5d1750) || !s_ev_on) return;
    SceneList *l = Scene_List();
    if (!l) return;
    int n = l->n, part = (n >> 2) + 1;
    int tick8 = g_sim_tick >> 3;
    int q = (g_sim_tick >> 1) & 3;
    int from = q * part, to = part + from;
    if (n < to) to = n;
    for (int i = from; i < to; i++) {
        SceneElem *e = l->e[i];
        if (!is_audio(e)) continue;
        float *rec = ae_pos(e);
        int v = scene_i16(e, AE_VOICE);
        if (v != -1 && s_ev[v].e != e) scene_set_i16(e, AE_VOICE, -1);
        int period = scene_i16(e, AE_PERIOD), rnd = scene_i16(e, AE_RANDOM);
        if ((period != 0 || rnd != 0) && scene_i16(e, AE_NEXT) < tick8 && scene_i16(e, AE_VOICE) == -1) {
            int add = rnd < 2 ? 0 : (int)(REAL_rand() % (uint32_t)rnd);
            scene_set_i16(e, AE_NEXT, (int16_t)(period + tick8 + add));
        }
        if (rough_dist(rec, s_listener[0], s_listener[2]) < ae_f(e, AE_RADIUS) + 100.0f || -1 < scene_i16(e, AE_VOICE))
            elem_play(e, tick8, NULL, 0x80, heading, s_listener);
    }
    (void)c;
    if (g_world && g_world->anim) PENDING_NOTE(0x4aed00, "the animated objects' sounds (the world's bundle +0x34 lists)");
}

/* The track's sounds near the start asked for and read before the race (Audio_LoadTrackBanks
   0x4af390; the original waits up to 0x280 timer ticks, the port's reads are immediate). */
static void track_banks_load(void)
{
    if (!s_on || !opt(0x5d1750) || !g_road || g_road->nnodes < 1) return;
    float x, z;
    memcpy(&x, g_road->nodes + 4, 4);
    memcpy(&z, g_road->nodes + 0xc, 4);
    printf("Loading track sounds... ");
    SceneList *l = Scene_List();
    bool done = false;
    for (int pass = 0; pass < 0x40 && !done; pass++) {
        done = true;
        for (int i = 0; l && i < l->n; i++) {
            SceneElem *e = l->e[i];
            if (!is_audio(e)) continue;
            float *rec = ae_pos(e);
            int snd = scene_i16(e, AE_SOUND);
            if (rough_dist(rec, x, z) < ae_f(e, AE_RADIUS) + 100.0f && -1 < snd && snd < s_stream[0].count && s_stream[0].index &&
                0 < (int)s_stream[0].index[snd][1] && stream_bank(0, snd, false) == -1)
                done = false;
        }
        stream_tick();
    }
    printf(done ? "\n" : "FAILED\n");
}

void Audio_StartRace(void)
{
    /* 0x4a8430 -> 0x4ae850: the elements' voices */
    for (int v = 0; v < 16; v++) s_ev[v] = (ElemVoice){NULL, -1, -1, 0};
    s_ev_on = true;
    elems_reset();
    if (!s_on || opt(0x5d1750) <= 0) return;
    /* 0x4a8670: the opponents' engine banks (and the rain's) read before the start */
    PENDING_NOTE(0x657a24, "the rain's bank before the start (the weather)");
    for (int pass = 0; pass < 0x40; pass++) {
        stream_tick();
        bool done = true;
        for (int i = 0; i < g_ncars; i++) {
            if (!(*CAR_I(g_cars[i], CAR_FLAGS) & 2)) continue;
            int idx = *CAR_I(g_cars[i], 0x520);
            if (stream_bank(1, idx * 2 + 1, false) == -1) done = false;
            if (stream_bank(1, idx * 2 + 2, false) == -1) done = false;
        }
        if (done) break;
    }
    track_banks_load();
}

/* The player's car (0x4a7430): its distance from the listener, the Doppler (340 / (340 + the closing
   speed)), its pan, how it faces the listener, its rpm's volume lift. */
static void player_tick(Car *c)
{
    const float *p = CAR_F(c, 0x330);
    float d[3] = {p[0] - s_listener[0], p[1] - s_listener[1], p[2] - s_listener[2]};
    float closing = 0;
    if (d[0] != 0 || d[1] != 0 || d[2] != 0) {
        float u[3];
        float l = sqrtf(dot3(d, d));
        for (int k = 0; k < 3; k++) u[k] = d[k] / l;
        closing = dot3(u, CAR_F(c, 0x33c));
        if (s_lvel) closing -= dot3(u, s_lvel);
    }
    int pitch = (int)(340.0 / ((double)closing + 340.0) * 65536.0);   /* 0x5b4b40, 0x5b4b38 */
    int pan = 0;
    if (opt(0x5d1760) && g_race_players == 1) pan = 0xc000;   /* (the first viewport's side) */
    else if (opt(0x5d1760) && g_racecam[0].mode != 0) {
        int x = (int)(dot3(d, s_lright) * 65536.0f), y = (int)(dot3(d, s_lfwd) * 65536.0f);
        if (g_r3d_mirror) x = -x;
        pan = (angle(x >> 8, y >> 8) & 0x3ff) << 6;
    }
    int dist = (int)(sqrtf(dot3(d, d)) * 65536.0f);   /* 0x532110 */
    int facing = 0;
    if (dist != 0) {
        int f = fixdiv((int)(dot3(d, CAR_F(c, 0x37c)) * 65536.0f), dist);
        facing = f < 0x10000 ? (f <= -0x10000 ? -0x10000 : f) : 0x10000;
    }
    float red = *SPEC_F(c, 0x94) + (DESC_I(c, 0xc) == 1 ? 1000.0f : 2000.0f);   /* (+0x558: the description) */
    if (5 < *(uint16_t *)(void *)(c->b + 0x418)) red -= 500.0f;
    int rpmvol = (int)(*CAR_F(c, 0xdb4) * 127.0f / red);
    int speedv = s_lvel ? 0 : abs((int)(*CAR_F(c, 0xec0) * 65536.0f));   /* 0x655f5c */
    int heading = 0;
    if (opt(0x5d1760)) {
        int x = (int)(dot3(CAR_F(c, 0x3d0), s_lright) * 65536.0f), y = (int)(dot3(CAR_F(c, 0x3d0), s_lfwd) * 65536.0f);
        if (g_r3d_mirror) x = -x;
        heading = (angle(x >> 8, y >> 8) & 0x3ff) << 6;
    }
    elems_tick(c, heading);   /* 0x4aed00 */
    car_sounds(c, dist, rpmvol, pitch, pan, heading, speedv, facing);
    /* The car's sound events of this tick (the last first): the ends of looped ones, the rest played,
       the impacts from the contact point (+0x440). */
    if (!g_audio_fx) return;
    for (int k = *CAR_I(c, 0x110c) - 1; 0 <= k; k--) {
        const uint8_t *r = c->b + 0x1110 + k * 0x18;
        int32_t type, id, kind, surf;
        float strength;
        memcpy(&type, r, 4);
        memcpy(&id, r + 4, 4);
        memcpy(&kind, r + 8, 4);
        memcpy(&surf, r + 0xc, 4);
        memcpy(&strength, r + 0x10, 4);
        if ((type == 5 || type == 3) && -1 < id) {
            stop_slot(id);
            if (id == 0x12 || id == 0x13) stop_slot(id + 4);
            continue;
        }
        int a = pan;
        if (id < 0 && kind != 0x1c && kind != 0x1e) {
            a = 0;
            if (opt(0x5d1760)) {
                const float *at = CAR_F(c, 0x440);
                float e[3] = {at[0] - s_listener[0], at[1] - s_listener[1], at[2] - s_listener[2]};
                int x = (int)(dot3(e, s_lright) * 65536.0f), y = (int)(dot3(e, s_lfwd) * 65536.0f);
                if (g_r3d_mirror) x = -x;
                a = (angle(x >> 8, y >> 8) & 0x3ff) << 6;
            }
        }
        event_sound(id, kind, surf, (int)(strength * 65535.0f), dist, a);   /* 0x5b3ed0 */
    }
}

/* The cars near the listener (0x655d60: 4 x 0x38 bytes) whose sounds play in their slots: the engine's
   road noise i + 6, its lden and ldex banks i + 10 and i + 14, its events i + 0x22, the horn i + 0x27. */
typedef struct {
    Car *car;                 /* +0x00 (NEAR_GONE: no longer near) */
    int32_t speed;            /* +0x0c: its speed (16.16) */
    int32_t dist2;            /* +0x14: its distance squared, at most 0x1324 */
    int32_t horn, horn_on, horn_off, horn_n;   /* +0x20 .. +0x2c: the horn's countdowns */
} NearCar;
static NearCar s_near[4];
#define NEAR_GONE ((Car *)(intptr_t)-1)

static void near_reset(NearCar *n, Car *c) { *n = (NearCar){.car = c, .dist2 = 0x1324}; }

/* The race cars nearest the listener, up to n of them within 300 (50 for flag 0x10), the viewport's
   own car left out (split screen: the humans'); those within 400 have their lden bank asked for. The
   records of cars no longer near are let go (their sounds stopped), new cars take free ones (0x4a7b90). */
static void near_update(int n)
{
    Car *list[4] = {0};
    int ld[4];
    for (int k = 0; k < n; k++) ld[k] = 0x12c0000;
    for (int i = 0; i < g_ncars; i++) {
        Car *c = g_cars[i];
        if (g_race_players == 1 ? (*CAR_I(c, CAR_FLAGS) & 4) != 0 : (g_humans.n > 0 && c == g_humans.c[0])) continue;
        if (!c->b[0x7f]) continue;
        uint32_t flags = (uint32_t)*CAR_I(c, CAR_FLAGS);
        const float *p = CAR_F(c, 0x330);
        float d[3] = {p[0] - s_listener[0], p[1] - s_listener[1], p[2] - s_listener[2]};
        int dist = (int)(sqrtf(dot3(d, d)) * 65536.0f);   /* 0x532110 */
        if (dist < 0x1900000) stream_bank(1, *CAR_I(c, 0x520) * 2 + 1, false);   /* 0x4b02a0 */
        if (((flags & 0x10) ? 50 : 300) * 0x10000 <= dist) continue;
        for (int k = 0; k < n; k++) {
            if (ld[k] <= dist) continue;
            for (int j = n - 1; k < j; j--) {
                list[j] = list[j - 1];
                ld[j] = ld[j - 1];
            }
            list[k] = c;
            ld[k] = dist;
            break;
        }
    }
    for (int r = 0; r < n; r++) {
        if (!s_near[r].car) continue;
        int k = 0;
        for (; k < n; k++)
            if (list[k] == s_near[r].car) {
                list[k] = NULL;
                break;
            }
        if (k == n) s_near[r].car = NEAR_GONE;
    }
    for (int k = 0; k < n; k++) {
        if (!list[k]) continue;
        for (int r = 0; r < n; r++) {
            if (s_near[r].car && s_near[r].car != NEAR_GONE) continue;
            if (s_near[r].car) {
                /* (the engine's slots are stopped only for a car of none: never) */
                PENDING_NOTE(0x4aad20, "the sirens' state (0x65686c)");
                stop_slot(r + 0x22);
                stop_slot(r + 0x27);
            }
            near_reset(&s_near[r], list[k]);
            break;
        }
    }
    for (int r = 0; r < n; r++) {
        if (s_near[r].car != NEAR_GONE) continue;
        stop_slot(r + 6);
        stop_slot(r + 10);
        stop_slot(r + 14);
        stop_slot(r + 0x22);
        stop_slot(r + 0x27);
        near_reset(&s_near[r], NULL);
    }
}

/* A near car's engine (0x4aa740): its lden and ldex banks mixed by how it faces the listener (the
   fade tables 0x5d17a4 .. at the low qualities only lden's), the pitch by the rpm and the Doppler; the
   road's noise (patch 0x10) by its speed up to 40, lowered by the cars close by; all by the distance
   (128 over 2 + the distance; the camera 0xb's 256 over 1 + it). */
static void near_engine(int slot, int idx, int rpm16, int pitch, int dist, int pan, int speed, int facing)
{
    int att = g_racecam[0].mode == 0xb ? fixdiv(0x10000000, dist + 0x10000) : fixdiv(0x8000000, dist + 0x20000);
    att = (att + ((att >> 31) & 0xffff)) >> 0x10;
    if (att == 0) {
        stop_slot(slot);
        stop_slot(slot + 4);
        stop_slot(slot + 8);
        return;
    }
    int64_t pp = (int64_t)(rpm16 + 0x3333) * pitch;
    uint32_t lo = (uint32_t)pp;
    int mix = 0x3f;
    if (g_sound_quality < 2) {
        mix = ((facing >> 1) + facing) >> 10;
        mix = mix < 0x40 ? (mix < -0x3f ? -0x40 : mix) : 0x40;
    }
    int p = ((int)(((lo >> 0x10) + (int32_t)(pp >> 32) * 0x10000 + ((lo >> 0xf & 1) != 0)) * 0x50) >> 10) << 4;
    play_slot(slot + 4, idx * 2 + 1, 0x40, p, (int8_t)exe_data((uint32_t)(0x5d17a4 - mix), 1)[0] * att >> 7, pan, 0x7f);
    play_slot(slot + 8, idx * 2 + 2, 0x40, p, (int8_t)exe_data((uint32_t)(0x5d17a4 + mix), 1)[0] * att >> 7, pan, 0x7f);
    int sp = 0x280000 < speed ? 0x280000 : speed;
    /* (the rpm as the road noise's velocity, as the original passes it) */
    play_slot(slot, 0x10, rpm16, pitch, ((att * sp) / 0x280000) * s_nearvol >> 7, pan, 0x7f);
}

/* A near car's sounds (0x4a6e50): the Doppler, distance, facing and pan as the player's, its engine,
   its events (those of a slot in the record's i + 0x22), a human's horn. */
static void near_tick(int vp, int i)
{
    NearCar *n = &s_near[i];
    Car *c = n->car;
    if (!c || c == NEAR_GONE) return;
    const float *p = CAR_F(c, 0x330);
    float d[3] = {p[0] - s_listener[0], p[1] - s_listener[1], p[2] - s_listener[2]};
    float closing = 0;
    if (d[0] != 0 || d[1] != 0 || d[2] != 0) {
        float l = sqrtf(dot3(d, d)), u[3] = {d[0] / l, d[1] / l, d[2] / l};
        closing = dot3(u, CAR_F(c, 0x33c));
        if (s_lvel) closing -= dot3(u, s_lvel);
    }
    int pitch = (int)(340.0 / ((double)closing + 340.0) * 65536.0);   /* 0x5b4b40, 0x5b4b38 */
    if (pitch < 0) return;
    int dist = (int)(sqrtf(dot3(d, d)) * 65536.0f);
    int facing = 0;
    if (dist != 0) {
        int f = fixdiv((int)(dot3(d, CAR_F(c, 0x37c)) * 65536.0f), dist);
        facing = f < 0x10000 ? (f <= -0x10000 ? -0x10000 : f) : 0x10000;
    }
    int pan = 0;
    if (opt(0x5d1760)) {
        if (g_race_players == 1) pan = vp ? 0x4000 : 0xc000;
        else {
            int x = (int)(dot3(d, s_lright) * 65536.0f), y = (int)(dot3(d, s_lfwd) * 65536.0f);
            if (g_r3d_mirror) x = -x;
            pan = (angle(x >> 8, y >> 8) & 0x3ff) << 6;
        }
    }
    int du = (dist + ((dist >> 31) & 0xffff)) >> 0x10;
    n->dist2 = du * du < 0x1324 ? du * du : 0x1324;
    float sp = *CAR_F(c, 0xec0);
    if (s_lvel && g_humans.n > 0) sp -= *CAR_F(g_humans.c[0], 0xec0);   /* (the viewport's car, 0x655f50) */
    n->speed = abs((int)(sp * 65536.0f));
    int rpm16 = (int)(*CAR_F(c, 0xdb4) / *SPEC_F(c, 0x94) * 65536.0f);
    rpm16 = rpm16 < 0 ? 0 : 0x20000 < rpm16 ? 0x20000 : rpm16;
    int speed = (int)(*CAR_F(c, 0xd60) * 65536.0f);
    if (speed < 1) speed = (int)(*CAR_F(c, 0xd60) * -65536.0f);   /* 0x5b4b48 */
    near_engine(i + 6, *CAR_I(c, 0x520), rpm16, pitch, dist, pan, speed, facing);
    for (int k = *CAR_I(c, 0x110c) - 1; 0 <= k; k--) {
        const uint8_t *r = c->b + 0x1110 + k * 0x18;
        int32_t id, kind, surf;
        float strength;
        memcpy(&id, r + 4, 4);
        memcpy(&kind, r + 8, 4);
        memcpy(&surf, r + 0xc, 4);
        memcpy(&strength, r + 0x10, 4);
        if (s_on) event_sound(id < 0 ? -1 : i + 0x22, kind, surf, (int)(strength * 65536.0f), dist, pan);   /* 0x4aa9a0 */
    }
    int hvol = ((0x1324 - n->dist2) * 0x7f) / 0x1324;
    int type = DESC_I(c, 0);
    int hpatch = (type == 0x30) + 10, hvel = (int8_t)exe_data(0x5d1478 + (uint32_t)type, 1)[0];
    if (!(*CAR_I(c, CAR_FLAGS) & 4)) {
        /* The others' horn by its countdowns (nothing in the exe sets them). */
        if (0 < n->horn) {
            if (0 < n->horn_on) n->horn--;
            if (n->horn == 0) {
                c->b[0xd86] = 0;
                stop_slot(i + 0x27);
                if (--n->horn_n != 0) n->horn = n->horn_off;
            } else {
                c->b[0xd86] = 1;
                play_slot(i + 0x27, hpatch, hvel, pitch, hvol, pan, 0x7f);
            }
        } else if (n->horn < 0 && ++n->horn == 0) {
            n->horn = n->horn_on;
        }
    } else if (c->b[0xd86]) {
        play_slot(i + 0x27, hpatch, hvel, pitch, hvol, pan, 0x7f);
        n->horn = 1;
    } else if (n->horn != 0) {
        stop_slot(i + 0x27);
        n->horn = 0;
    }
    if ((*CAR_I(c, CAR_FLAGS) & 0x20) && (*CAR_I(c, 0xecc) & 2)) PENDING_NOTE(0x4aac60, "the cop cars' sirens (0x4aac60, 0x4aad80)");
}

void Audio_Tick(void)
{
    if (!s_on) return;
    /* 0x465360: the listener at the first camera's eye, its axes (the view's adjustments of the eye,
       0x464e40's, are left out). */
    const RaceCamera *cam = &g_racecam[0];
    memcpy(s_listener, cam->eye, sizeof s_listener);
    memcpy(s_lright, cam->orient, sizeof s_lright);
    memcpy(s_lfwd, cam->orient + 6, sizeof s_lfwd);
    s_lvel = cam->mode != 0xb && (cam->mode == 0xc || cam->mode == 0xe) ? NULL : cam->car ? CAR_F(cam->car, 0x33c) : NULL;
    stream_tick();   /* 0x4b07f0 */
    PENDING_NOTE(0x4a8150, "the thunder (0x4a8150)");
    int n = g_sound_quality == 0 ? 4 : g_sound_quality == 1 ? 2 : 1;
    near_update(n);
    if (g_race_players == 1) s_nearvol = 0x80;
    else {
        /* 0xc0 less, for each car within 128, its distance short of 128 */
        int v = 0xc00000;
        for (int r = 0; r < 4; r++) {
            if (!s_near[r].car) continue;
            const float *p = CAR_F(s_near[r].car, 0x330);
            float d[3] = {p[0] - s_listener[0], p[1] - s_listener[1], p[2] - s_listener[2]};
            int dist = (int)(sqrtf(dot3(d, d)) * 65536.0f);
            if (dist < 0x800000) v += dist - 0x800000;
        }
        v = (v + ((v >> 31) & 0xffff)) >> 0x10;
        s_nearvol = 0x7f < v ? 0x80 : v < 1 ? 0 : v;
    }
    if (g_humans.n > 0) player_tick(g_humans.c[0]);
    if (0 < opt(0x5d1750)) {
        near_tick(0, 0);
        if (g_sound_quality < 2) {
            near_tick(0, 1);
            if (g_sound_quality < 1) {
                near_tick(0, 2);
                near_tick(0, 3);
            }
        }
    }
    memcpy(s_lastlistener, s_listener, sizeof s_lastlistener);
}
