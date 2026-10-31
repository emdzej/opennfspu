/* The SND library's software mixer (0x596910 .. 0x59d3a0): sources, their node chains (decoder,
   resampler), the mixing into the output channels and their conversion to 16-bit samples. Internal to
   src/snd/. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* A chain node: the next one, its render, its type (the chain is sorted by it: 2 resampler, 4 decoder). */
struct SndNode;
typedef int (*SndRender)(struct SndNode *n, int count, float *tmp, float *out, int ch);
typedef struct SndNode {
    struct SndNode *next;     /* +0x00 */
    SndRender render;         /* +0x04 */
    int16_t type;             /* +0x08 */
    int16_t chan;             /* +0x0a */
} SndNode;

/* A source (0x50 bytes, the table 0x6b1710). */
typedef struct {
    uint8_t state;            /* +0x00: 0 free, 1 set up, 2 playing */
    uint8_t ramp;             /* +0x01: the levels changed: ramp to them over the next 32 samples */
    float gain;               /* +0x04 */
    float level;              /* +0x08: the master level times the gain times 1.5 */
    float master, mastert;    /* +0x0c, +0x10: current and target master levels */
    float chan[4];            /* +0x14: current levels per output channel */
    float chant[4];           /* +0x24: their targets */
    SndNode *chain;           /* +0x34 */
    int (*position)(SndNode *dec);   /* +0x38 */
    SndNode *decoder;         /* +0x3c */
    SndNode *effect;          /* +0x40: (the per-channel effect of tags 0x98 ..: not ported) */
    SndNode *resampler;       /* +0x44 */
    SndNode *lowpass;         /* +0x48 */
    SndNode *filter;          /* +0x4c */
} SndSource;

/* The mixer's configuration (0x6b15e0 ..): the sources, the output channels, the device's rate. */
void SndMix_Init(int nsources, int nchannels, int rate);
void SndMix_Free(void);
SndSource *SndMix_Source(int i);

/* A source for the codec (4, 7, 8, 9 8-bit PCM, 10 EA-XA ADPCM, 0x40), with or without a length, its
   data, its length and loop (0x598280). */
void SndMix_SetupSource(int i, int codec, bool has_length, const uint8_t *data, const uint8_t *chdata,
                        int nsamples, int loopstart, int looplen);
void SndMix_Start(int i);                               /* 0x5983e0 */
void SndMix_Release(int i);                             /* 0x598440 */
/* The levels: master and per output channel (0x5984b0), the gain (0x5995b0), the pitch (0x5995e0: 16.16
   of the source's rate, at most 4). */
void SndMix_SetLevels(int i, float master, const float *chan);
void SndMix_SetGain(int i, float gain);
void SndMix_SetPitch(int i, int pitch);
/* The source's sample position (0x5997b0), -1 without a decoder. */
int SndMix_Position(int i);
/* The one-pole lowpass (0x599690): made below 1, removed from 1 on. */
void SndMix_SetLowpass(int i, float c);
/* The voice-end callback (0x6b15e8): a source's decoder ran out. */
extern void (*g_snd_source_end)(int i);

/* Mixes n frames (at most 256 a block) into dest: interleaved 16-bit samples, one per output channel.
   0x5986f0 */
void SndMix_Mix(int16_t *dest, int n);
