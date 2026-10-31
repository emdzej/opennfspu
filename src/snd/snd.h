/* EA's SND sound library, version 4.1g (Porsche.exe 0x55e000-0x569000 for its interface, 0x588000-0x591000
   and 0x595000-0x59d000 for its voices, software mixer and DirectSound device). The port keeps the
   library's own structures and arithmetic; its device (DirectSound buffers filled by a thread) is the
   mixer pulled by the platform's audio (app_audio). See docs/re/sound.md. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* ---- banks and patches ---- */

/* A bank ("BNKl"): +4 its version (4: the patches' offsets from +0x14), +6 its patch count, +8 the size of
   its header part. The offsets are from their own word. */
const uint8_t *SND_BankPatch(const uint8_t *bank, int i);   /* 0x58ff20: NULL for none */

/* A patch's layer ("PT" tags; 0x598eb0), 0xa4 bytes in the original. Tags 0x00..0x25 and 0x80..0xa1
   carry a value (big-endian, up to four bytes) and their data; the fields below are those the library
   reads, with the defaults the parser starts from. */
typedef struct {
    int16_t t24;              /* +0x00: tag 0x24 */
    uint8_t t80;              /* +0x02: tag 0x80 */
    uint8_t channels;         /* +0x03: tag 0x82 (1) */
    int16_t t10;              /* +0x04: tag 0x10 */
    int16_t t11;              /* +0x06: tag 0x11 */
    uint8_t keylo, keyhi;     /* +0x08, +0x09: tags 0x01, 0x02 (0, 0x7f): the key range */
    uint8_t vello, velhi;     /* +0x0a, +0x0b: tags 0x03, 0x04 (0, 0x7f): the velocity range */
    uint8_t t06;              /* +0x0c: tag 0x06 */
    uint8_t rootkey;          /* +0x0d: tag 0x07 (60) */
    uint8_t t08;              /* +0x0e: tag 0x08 (0xff) */
    uint8_t t09;              /* +0x0f: tag 0x09 (1) */
    uint8_t t0a;              /* +0x10: tag 0x0a */
    uint8_t pan;              /* +0x11: tag 0x0c (0x40) */
    uint8_t t0d;              /* +0x12: tag 0x0d */
    uint8_t codec;            /* +0x13: tag 0xa0 (10, EA-XA ADPCM) */
    uint8_t vol;              /* +0x14: tag 0x0e (0x7f) */
    uint8_t t0f;              /* +0x15: tag 0x0f */
    uint8_t t13;              /* +0x16: tag 0x13 */
    int8_t nparts;            /* +0x17: the tag 0x14 entries */
    uint8_t flags;            /* +0x18: tag 0x8c, or from tag 0xa1's bits */
    uint8_t envstart;         /* +0x19: tag 0x1c (0x7f) */
    uint8_t t1e, t1f;         /* +0x1a, +0x1b: tags 0x1e, 0x1f */
    uint8_t t21, t23;         /* +0x1c, +0x1d: tags 0x21, 0x23 */
    uint8_t t25;              /* +0x1e: tag 0x25 (1) */
    uint8_t pad1f;
    const uint8_t *p12;       /* +0x20: tag 0x12: its data plus its value */
    const uint8_t *p17;       /* +0x24: tag 0x17 */
    const int32_t *env;       /* +0x28: tag 0x19: the envelope ({ticks, level} pairs; the exe's
                                 default 0x5e3498: full level for ever) */
    const uint8_t *p1d;       /* +0x2c: tag 0x1d */
    const uint8_t *p20;       /* +0x30: tag 0x20 */
    const uint8_t *part[4];   /* +0x34: the tag 0x14 entries' data */
    int32_t partlen[4];       /* +0x44 */
    int16_t t22;              /* +0x54: tag 0x22 */
    uint16_t rate;            /* +0x56: tag 0x84 (22050) */
    int32_t nsamples;         /* +0x58: tag 0x85 */
    int32_t loopstart;        /* +0x5c: tag 0x86 (-1) */
    int32_t looplen;          /* +0x60: tag 0x87 (-1) */
    const uint8_t *p8a;       /* +0x64: tag 0x8a's data */
    int32_t dataofs[4];       /* +0x68: tags 0x88, 0x89, 0x94, 0x95: each channel's data from the bank */
    const uint8_t *chdata[4]; /* +0x78: tags 0x98 .. 0x9b's data */
    int16_t chlen[4];         /* +0x88 */
    int32_t chextra[4];       /* +0x90: tags 0x9c .. 0x9f */
    const uint8_t *pt;        /* +0xa0: where the layer's tags start */
} SndPatch;

/* The tag reader's cursor (0x599510): the position, the tag, its value, its data and its length. */
typedef struct {
    const uint8_t *p;
    int32_t tag;
    int32_t value;
    const uint8_t *data;
    int32_t len;
} SndTags;
/* The next tag (0xfc bytes skipped); false at 0xff. 0xfd and 0xfe carry nothing. */
bool SND_TagNext(SndTags *t);
/* One layer of the patch from *p on (*p moves past it); true when another layer follows (0xfe). */
bool SND_PatchParse(const uint8_t **p, SndPatch *out);

/* ---- the library ---- */

/* The configuration (0x6b4800, 0x7c bytes; the fields the port uses). */
extern uint8_t g_snd_quality;     /* 0x6b4825: the resampler's four taps from 50 */

/* The play parameters (0x561710 sets their defaults; 0x1c bytes). */
typedef struct {
    int32_t patch;            /* +0x00: -1 */
    int8_t bank;              /* +0x04: 0xff */
    int8_t key;               /* +0x05: 60 */
    int8_t velocity;          /* +0x06: 127 */
    int8_t pan;               /* +0x07: 64 */
    int8_t vol;               /* +0x08: 127 */
    int8_t f09;               /* +0x09: 64 */
    int8_t f0a;               /* +0x0a: 127 */
    int8_t f0b;               /* +0x0b: 0 (non-zero: the pan words at +0x18, +0x1a) */
    uint16_t pitch;           /* +0x0c: 0x1000 (4.12) */
    uint16_t f0e, f10;        /* +0x0e, +0x10: 0x1000 */
    uint16_t f12;             /* +0x12: 0xffff (the lowpass) */
    uint16_t f14;             /* +0x14: 0 */
    uint8_t pad16[2];
    int16_t pan18, pan1a;     /* +0x18, +0x1a */
} SndParams;
void SND_ParamsDefault(SndParams *p);                       /* 0x561710 */

/* The library's start with the device's rate, channels and voices (SNDAUTHOR 0x564ba0 with the
   defaults 0x58a2a0, the game's settings 0x4a6480). */
bool SND_Init(int rate, int channels, int voices, int quality);
void SND_Shutdown(void);
/* A bank in memory: its slot (SNDbankadd 0x564fe0), -9 for none free; and its removal (0x564e70). */
int SND_BankAdd(const uint8_t *bank);
void SND_BankRemove(int slot);
/* Plays the bank's patch (SNDplay 0x5652f0): the voice's handle, negative for none. */
int SND_Play(int bank, int patch, SndParams *p);
int SND_Stop(int handle);                                   /* 0x5651e0 */
bool SND_Playing(int handle);                               /* 0x565800 (the original returns 1 when not) */
int SND_SetVolume(int handle, int vol);                     /* 0x5657c0 */
int SND_SetPan(int handle, int pan, int spread);            /* 0x565670 */
int SND_SetPitch(int handle, int pitch);                    /* 0x565400 */
int SND_TimeLeft(int handle);                               /* 0x5658b0: hundredths of a second */
/* A function the library's 100 Hz tick calls (0x560e90; at most eight), and its removal (0x560ec0). */
void SND_AddTick(void (*fn)(void));
void SND_RemoveTick(void (*fn)(void));
/* Fills n frames of interleaved 16-bit samples, the library's ticks at 100 Hz among them (0x58f970). */
void SND_Fill(int16_t *out, int n);
int SND_Rate(void);
int SND_Channels(void);
