/* The SND library's voices, banks and device (see snd.h): the voice table (0x6b4940), the play path
   (0x590560 .. 0x589d90 for the software mixer's voices), the library's 100 Hz tick (0x5892d0) and the
   device's fill (0x58f970). */
#include "snd/snd.h"
#include "exe.h"
#include "pending.h"
#include "snd/snd_mix.h"
#include <stdlib.h>
#include <string.h>

uint8_t g_snd_quality;

/* A voice (0x70 bytes). A sample of n channels takes n voices: the first holds the handle and lists
   them all (+0x04); the others have the handle -1 and their first's index at +0x1c. */
typedef struct {
    int32_t handle;           /* +0x00: the index in the low byte, a serial above */
    int16_t chv[4];           /* +0x04: the voices of each channel */
    int16_t patch;            /* +0x0c */
    uint8_t bank;             /* +0x0e */
    uint8_t state;            /* +0x0f: 0 free, 1 playing, 2 the other channels still playing */
    int32_t nsamples;         /* +0x10 */
    uint16_t pan;             /* +0x14: an angle, 0x100 steps */
    int16_t pan2;             /* +0x16 */
    uint16_t rate;            /* +0x18 */
    uint8_t prio;             /* +0x1a: 0x65 and above can't be taken */
    uint8_t lastlayer;        /* +0x1b */
    int16_t first;            /* +0x1c */
    uint8_t group;            /* +0x1e: the layers of one play */
    int8_t panbyte;           /* +0x1f */
    uint32_t age;             /* +0x20: the tick it started or ended */
    int32_t vstep, vtarget, vcur;     /* +0x24, +0x28, +0x2c: the volume ramp, 16.16 */
    int32_t eslope, elevel, eticks;   /* +0x30, +0x34, +0x38: the envelope */
    int8_t vol;               /* +0x3c */
    int8_t out;               /* +0x3d: the volume heard, 0 .. 127 */
    int8_t layerpan;          /* +0x3e */
    int8_t f3f;               /* +0x3f: the play's +0x09 */
    uint8_t nseg;             /* +0x40: the envelope's segments */
    int8_t seg;               /* +0x41 */
    uint8_t f42;              /* +0x42 */
    int8_t spread;            /* +0x43 */
    int8_t send[2];           /* +0x44: the patch's tag 0x13, the play's +0x0a */
    int8_t grp[2];            /* +0x46: by the groups' volumes (0x589b20) */
    uint8_t lfo1len, lfo2len; /* +0x48, +0x49 */
    uint8_t lfo1ph, lfo2ph;   /* +0x4a, +0x4b */
    int16_t f4c;              /* +0x4c: the play's +0x0e */
    const int32_t *env;       /* +0x50 */
    const int8_t *volcurve;   /* +0x54: tag 0x12 */
    const int8_t *bendcurve;  /* +0x58: tag 0x17 */
    const int8_t *lfo1;       /* +0x5c: tag 0x1d, on the volume */
    const int8_t *lfo2;       /* +0x60: tag 0x20, on the pitch */
    int16_t lfo2depth;        /* +0x64 */
    int16_t benddepth;        /* +0x66 */
    int16_t cents;            /* +0x68 */
    uint16_t pitchmul;        /* +0x6a: 4.12, 0 recomputes it */
    uint16_t pitch;           /* +0x6c: the play's, 4.12 */
    uint16_t pitchout;        /* +0x6e */
} SndVoice;

/* A voice's device side (0xf8 bytes at 0x6b47dc): for the software mixer its sources (one per channel,
   the voices' own indices), its rate against the device's, its pan levels per output channel. */
typedef struct {
    uint32_t flags;           /* +0x00: the output's (4 the software mixer) */
    float levels[4];          /* +0x04 */
    int8_t channels;          /* +0x14 */
    uint8_t n;                /* +0x2c: the sources */
    int16_t src[4];           /* +0x30 */
    int32_t extra[4];         /* +0x38 */
    float ratio;              /* +0x48: 16 x the sample's rate over the device's */
} SndDev;

static SndVoice *s_voice;       /* 0x6b4940 */
static SndDev *s_dev;           /* 0x6b47dc */
static int s_nvoices;           /* 0x6b48e4 */
static int s_nsw;               /* 0x6b481d: the software voices */
static int s_rate = 32000, s_channels = 2;   /* 0x6b481a, 0x6b481c */
static uint8_t s_master = 0x7f; /* 0x6b48dd */
static uint32_t s_ticks;        /* 0x6b48e8 */
static int32_t s_serial;        /* 0x6ad68c */
static uint8_t s_groupid;       /* 0x6aef30 */
static int8_t s_groupvol[32][2];/* 0x6b494c, 0x6b494d (0x10 bytes apart) */
static uint8_t s_pan[256][4];   /* 0x6b1760: each pan angle's levels per output channel */
static const uint8_t **s_bank;  /* 0x6b4944 (the slots' first words) */
static int s_nbanks = 0x40;
static bool s_on;               /* 0x6b48dc */
static uint32_t s_rand[6];      /* 0x5e347c .. 0x5e3490: the library's own generator */
static bool s_rand_loaded;

int SND_Rate(void) { return s_rate; }
int SND_Channels(void) { return s_channels; }

/* The library's random numbers (0x598da0): the real library's generator on its own words. */
static uint32_t snd_rand(void)
{
    if (!s_rand_loaded) {
        for (int i = 0; i < 6; i++) s_rand[i] = exe_u32(0x5e347c + 4 * (uint32_t)i);
        s_rand_loaded = exe_loaded();
    }
    uint32_t *w = s_rand, s = w[4] + w[5], c = s < w[5] || s < w[4];
    w[4] = s;
    for (int i = 3; i >= 0; i--) {
        uint32_t t = s + c + w[i];
        c = t < w[i];
        w[i] = t;
        s = t;
    }
    if (++w[5] == 0 && ++w[4] == 0 && ++w[3] == 0 && ++w[2] == 0 && ++w[1] == 0) w[0]++;
    return w[0];
}

/* A random amount within +- range (0 .. 0x10000). 0x598e70 */
static int snd_random(int range)
{
    if (0x10000 < range) range = 0x10000;
    else if (range < 0) range = 0;
    return (int)(((snd_rand() & 0x7fff) - 0x4000) * (uint32_t)range) >> 0xe;
}

void SND_ParamsDefault(SndParams *p)
{
    memset(p, 0, sizeof *p);
    p->pitch = p->f0e = p->f10 = 0x1000;
    p->patch = -1;
    p->bank = (int8_t)0xff;
    p->key = 0x3c;
    p->f12 = 0xffff;
    p->velocity = 0x7f;
    p->pan = 0x40;
    p->vol = 0x7f;
    p->f09 = 0x40;
    p->f0a = 0x7f;
}

/* ---- the levels ---- */

/* 2x - x^2 / 127. 0x58de60 */
static int shape(int x) { return x * 2 - (x * x) / 0x7f; }

/* The pan angles' levels between the speakers' angles (0x58de90; the configuration's +0x2c: stereo at
   0x4000 and 0xc000). */
static void pan_table(void)
{
    uint16_t sp[4];
    int n = s_channels;
    if (n == 1) {
        for (int i = 0; i < 0x100; i++) s_pan[i][0] = 0x7f;
        return;
    }
    if (n == 2) {
        sp[0] = 0x4000;
        sp[1] = 0xc000;
    } else {
        for (int k = 0; k < n; k++) sp[k] = (uint16_t)(0x2000 + k * 0x4000);
    }
    for (int i = 0; i < 0x100; i++) {
        int a = i * 0x100;
        memset(s_pan[i], 0, sizeof s_pan[i]);
        if (sp[0] < a) {
            if (a < sp[n - 1]) {
                for (int j = 0; j < n; j++)
                    if (sp[j] <= a && a <= sp[j + 1 < n ? j + 1 : j]) {
                        int up = sp[j + 1] - a, down = a - sp[j], t = up + down;
                        s_pan[i][j] = (uint8_t)shape((up * 0x7f) / t);
                        s_pan[i][j + 1] = (uint8_t)shape((down * 0x7f) / t);
                    }
            } else {
                int d = a - sp[n - 1], e = sp[0] - a + 0x10000, t = d + e;
                s_pan[i][0] = (uint8_t)shape((d * 0x7f) / t);
                s_pan[i][n - 1] = (uint8_t)shape((e * 0x7f) / t);
            }
        } else {
            int d = sp[0] - a, e = (a - sp[n - 1]) + 0x10000, t = e + d;
            s_pan[i][0] = (uint8_t)shape((e * 0x7f) / t);
            s_pan[i][n - 1] = (uint8_t)shape((d * 0x7f) / t);
        }
    }
}

/* Cents to a 4.12 multiplier (0x589c30): whole octaves by shifting, the rest from the exe's tables
   (0x5e1d3c up, 0x5e1e3c down; 256 steps an octave). */
static int cents_pitch(int c)
{
    int m = 0x1000;
    if (0x4af < c) {
        int o = c / 0x4b0;
        c %= 0x4b0;
        while (o--) m <<= 1;
    }
    if (c < -0x4af) {
        int o = -c / 0x4b0;
        c += o * 0x4b0;
        while (o--) m >>= 1;
    }
    int i = (c * 0x369d) >> 0x10;
    if (i < -0xff) i = -0xff;
    else if (-1 < i) return ((exe_data(0x5e1d3c + (uint32_t)i, 1)[0] + 0x100) * m) >> 8;
    return ((exe_data((uint32_t)(0x5e1e3c + i), 1)[0] + 0x100) * m) >> 9;
}

/* The pitch (0x589cf0): the cents, the bend by the play's +0x09 through the bend curve, the pitch LFO;
   times the play's pitch. */
static void voice_pitch(SndVoice *v)
{
    if (v->pitchmul == 0) {
        int c = v->cents;
        if (v->benddepth) {
            int b = v->bendcurve ? v->bendcurve[(int)v->f3f] : v->f3f;
            c += ((b - 0x40) * v->benddepth) >> 6;
        }
        if (v->lfo2) c += ((v->lfo2[v->lfo2ph] - 0x40) * v->lfo2depth) >> 6;
        v->pitchmul = (uint16_t)cents_pitch(c);
    }
    v->pitchout = (uint16_t)(((uint32_t)v->pitch * v->pitchmul) >> 0xc);
}

/* The volume (0x589ba0): envelope x ramp x volume x master over 127^3, the volume LFO, the volume curve. */
static void voice_volume(SndVoice *v)
{
    int x = (v->elevel >> 0x10) * (v->vcur >> 0x10) * v->vol * s_master;
    v->out = (int8_t)(x / 0x1f417f);
    if (v->lfo1) v->out = (int8_t)((v->lfo1[v->lfo1ph] * v->out) / 0x7f);
    if (v->volcurve) v->out = v->volcurve[(int)v->out];
}

/* The group's levels (0x589b20): the sends times the group's two volumes over 127^2. */
static void voice_group(int g, SndVoice *v)
{
    int x = v->send[1] * v->send[0];
    v->grp[0] = (int8_t)((s_groupvol[g][0] * x) / 0x3f01);
    v->grp[1] = (int8_t)((s_groupvol[g][1] * x) / 0x3f01);
}

/* ---- the software voices' device side (0x596640 .. 0x596a50) ---- */

static void dev_pitch(SndDev *d, int pitch)   /* 0x596800 */
{
    int p = (int)((float)pitch * d->ratio);   /* _ftol */
    for (int c = 0; c < d->n; c++) SndMix_SetPitch(d->src[c], p);
}

static void dev_volume(SndDev *d, int vol)   /* 0x596770 */
{
    float f = (float)vol * 0.007874016f;   /* 0x5c1788 */
    for (int c = 0; c < d->n; c++) SndMix_SetLevels(d->src[c], f, s_dev[d->src[c]].levels);
}

static void dev_gain(SndDev *d, int g)   /* 0x5967c0 */
{
    float f = (float)g * 0.007874016f;
    for (int c = 0; c < d->n; c++) SndMix_SetGain(d->src[c], f);
}

/* The pan (0x596640): a mono voice's angle; each channel of a multi-channel one at the configuration's
   angles (0x6b4954: stereo 0xc000, 0x4000) plus its extra. */
static void dev_pan(SndDev *d, int pan, int vol)
{
    for (int c = 0; c < d->n; c++) {
        SndDev *cd = &s_dev[d->src[c]];
        int a = pan;
        if (d->n != 1) {
            uint16_t base = d->n == 2 ? (c == 0 ? 0xc000 : 0x4000) : (uint16_t)(c * 0x10000 / d->n);
            if (d->n == 2 && s_channels == 2) d->extra[c] = 0;
            a = base + d->extra[c];
        }
        const uint8_t *lv = s_pan[(a >> 8) & 0xff];
        for (int k = 0; k < s_channels; k++) cd->levels[k] = (float)(int8_t)lv[k] * 0.007874016f;
        SndMix_SetLevels(d->src[c], (float)vol * 0.007874016f, cd->levels);
    }
}

static void voice_apply_volume(int i) { dev_volume(&s_dev[i], s_voice[i].out); }    /* 0x590ac0 */
static void voice_apply_pitch(int i) { dev_pitch(&s_dev[i], s_voice[i].pitchout); } /* 0x590a10 */

/* ---- voices ---- */

static int s_alloc[4];   /* 0x6b47e8 */

static bool allocated(int v, int n)   /* 0x589660 */
{
    for (int i = 0; i < n; i++)
        if (s_alloc[i] == v) return true;
    return false;
}

/* n voices in [from, to) (0x5896a0): free ones, oldest first; else of a lower priority than 0x65, the
   lowest, oldest first (they are stopped). The first's handle into *handle. */
static int voice_alloc(int n, uint8_t prio, int32_t *handle, int from, int to)
{
    for (int i = 0; i < n; i++) s_alloc[i] = -1;
    s_serial += 0x100;
    if (s_serial < 0) s_serial = 0;
    int got = 0;
    for (int k = 0; k < n; k++) {
        uint32_t best = 0xffffffffu;
        int pick = -1;
        for (int v = from; v < to; v++)
            if (s_voice[v].state == 0 && !allocated(v, got) && s_voice[v].age < best) {
                best = s_voice[v].age;
                pick = v;
            }
        if (pick >= 0) s_alloc[got++] = pick;
    }
    for (int k = got; k < n; k++) {
        int pick = -1;
        uint32_t best = 0xffffffffu;
        uint8_t low = 0x66;
        for (int v = from; v < to; v++) {
            if (allocated(v, got)) continue;
            uint8_t p = s_voice[v].prio;
            if (p >= 0x65) continue;
            if (p < low || (p == low && s_voice[v].age < best)) {
                low = p;
                best = s_voice[v].age;
                pick = v;
            }
        }
        if (pick < 0) break;
        s_alloc[got++] = pick;
        if (n <= got) break;
    }
    if (got != n) return -9;
    *handle = s_alloc[0] | s_serial;
    for (int k = 0; k < got; k++) {
        SndVoice *v = &s_voice[s_alloc[k]];
        if (v->state == 1) SND_Stop(v->handle < 0 ? s_voice[v->first].handle : v->handle);
        v->state = 1;
        v->prio = prio;
        v->age = s_ticks;
    }
    SndVoice *first = &s_voice[s_alloc[0]];
    first->handle = *handle;
    first->chv[0] = (int16_t)s_alloc[0];
    first->first = -1;
    for (int k = 1; k < got; k++) {
        first->chv[k] = (int16_t)s_alloc[k];
        s_voice[s_alloc[k]].handle = -1;
        s_voice[s_alloc[k]].first = (int16_t)s_alloc[0];
    }
    return s_alloc[0];
}

static int voice_of(int32_t handle)   /* 0x589ad0 */
{
    if (handle < 0) return -8;
    int i = handle & 0xff;
    if (s_nvoices <= i || s_voice[i].state == 0 || s_voice[i].handle != handle) return -8;
    return i;
}

/* The voice freed, or its first kept until its other channels end (0x5899b0). */
static void voice_free(int i)
{
    SndVoice *v = &s_voice[i];
    v->state = 0;
    v->group = 0;
    v->age = s_ticks;
}

static void source_end(int i) { voice_free(i); }

/* Stops the voice's sources (0x58a660 -> 0x596a50). */
static void voice_stop(int i)
{
    SndDev *d = &s_dev[i];
    for (int c = 0; c < d->n; c++) {
        SndMix_Release(d->src[c]);
        voice_free(d->src[c]);
    }
}

/* The voices of the same group as voice v's, one after another (0x5900f0). */
static bool next_of_group(int v, int *at)
{
    uint8_t g = s_voice[v].group;
    if (!g) {
        if (*at < 0) {
            *at = v;
            return true;
        }
        return false;
    }
    for (int i = *at + 1; i < s_nvoices; i++) {
        *at = i;
        if (s_voice[i].group == g && s_voice[i].state == 1 && s_voice[i].handle >= 0) return true;
    }
    return false;
}

int SND_Stop(int handle)
{
    if (!s_on) return -10;
    int v = voice_of(handle);
    if (v < 0) return v;
    int at = -1;
    while (next_of_group(v, &at)) voice_stop(at);
    return v;
}

bool SND_Playing(int handle) { return s_on && voice_of(handle) >= 0; }

/* The play's volume, 0 .. 127, at once (0x5657c0 -> 0x565730; nothing when it is already so). */
int SND_SetVolume(int handle, int vol)
{
    if (!s_on) return -10;
    int v = voice_of(handle);
    if (v < 0) return v;
    int at = -1;
    while (next_of_group(v, &at)) {
        SndVoice *x = &s_voice[at];
        if (x->vcur == vol << 0x10) return 0;
        x->vcur = vol << 0x10;
        x->vstep = 0;
        voice_volume(x);
        voice_apply_volume(at);
    }
    return v;
}

/* The pan angle and its spread (clamped to +-0x4000). 0x565670 */
int SND_SetPan(int handle, int pan, int spread)
{
    if (!s_on) return -10;
    if (0x3fff < spread) spread = 0x3fff;
    else if (spread < -0x4000) spread = -0x4000;
    int v = voice_of(handle);
    if (v < 0) return v;
    int at = -1;
    while (next_of_group(v, &at)) {
        s_voice[at].pan = (uint16_t)pan;
        s_voice[at].pan2 = (int16_t)spread;
        dev_pan(&s_dev[at], s_voice[at].pan, s_voice[at].out);   /* 0x590a70 */
    }
    return v;
}

/* The play's pitch, 4.12 (0x565400; it stops at the first voice already at it). */
int SND_SetPitch(int handle, int pitch)
{
    if (!s_on) return -10;
    int v = voice_of(handle);
    if (v < 0) return v;
    int at = -1;
    while (next_of_group(v, &at) && s_voice[at].pitch != (uint16_t)pitch) {
        s_voice[at].pitch = (uint16_t)pitch;
        voice_pitch(&s_voice[at]);
        voice_apply_pitch(at);
    }
    return v;
}

/* ---- playing ---- */

/* The software voice's sources (0x589d90 with 0x596910 and 0x598280): one per channel, then its pitch,
   volume and pan, and they start. */
static int voice_sources(int i, const SndPatch *s, const uint8_t *bank)
{
    SndVoice *v = &s_voice[i];
    SndDev *d = &s_dev[i];
    d->flags = 4;
    d->channels = (int8_t)s->channels;
    d->n = (uint8_t)s->channels;
    d->ratio = ((float)v->rate * 16.0f) / (float)s_rate;   /* 0x5b26a8 */
    for (int c = 0; c < d->n; c++) {
        d->src[c] = v->chv[c];
        d->extra[c] = s->chextra[c];
        SndMix_SetupSource(d->src[c], s->codec, s->nsamples != 0, bank + s->dataofs[c], s->chdata[c], s->nsamples,
                           s->loopstart, s->looplen);
    }
    dev_pitch(d, v->pitchout);
    dev_volume(d, v->out);
    dev_pan(d, v->pan, v->out);
    for (int c = 0; c < d->n; c++) SndMix_Start(d->src[c]);   /* 0x596a20 */
    dev_gain(d, v->grp[0]);
    return 0;
}

/* One layer's voices (0x5901d0): the pitch from the key against the root key (with the layers' shared
   and the layer's own random detune), the pan and volume from the layer's and the play's, the
   envelope, the modulators. */
static int play_layer(const uint8_t *bank, const SndParams *p, const SndPatch *s, int key, int vel, int detune)
{
    int pan = s->pan;
    if (s->t0d) pan += snd_random(s->t0d);
    pan = pan < 0x80 ? (pan < 0 ? 0 : pan) : 0x7f;
    int32_t handle = -9;
    int i = voice_alloc(s->channels, s->t06, &handle, 0, s_nsw);   /* (0x58a720, 0x58a260: the software voices) */
    if (i < 0) return -9;
    SndVoice *v = &s_voice[i];
    v->spread = (int8_t)s->t25;
    v->cents = (int16_t)((key - s->rootkey) * 100 + s->t10);
    if (detune) v->cents = (int16_t)(v->cents + detune);
    if (s->t11) v->cents = (int16_t)(v->cents + snd_random(s->t11));
    v->volcurve = (const int8_t *)s->p12;
    v->bendcurve = (const int8_t *)s->p17;
    v->vcur = p->vol << 0x10;
    v->vstep = 0;
    v->panbyte = p->pan;
    int pb = p->pan;
    if (v->spread != 1) pb = (pb - 0x40) * v->spread + 0x40;
    pb = pb - 0x40 + pan;
    pb = pb < 0 ? 0 : pb > 0x7f ? 0x7f : pb;
    int vol = s->vol;
    if (s->t0f) vol += snd_random(s->t0f);
    vol = vol < 0x80 ? (vol < 0 ? 0 : vol) : 0x7f;
    vol *= vel;
    v->layerpan = (int8_t)pan;
    v->seg = 0;
    v->vol = (int8_t)(vol / 0x7f);
    v->f3f = p->f09;
    v->benddepth = (int16_t)(s->t0a * 100);
    v->nseg = s->t09;
    v->env = s->env;
    v->f42 = s->t08;
    v->elevel = s->envstart << 0x10;
    v->eticks = v->env[0] < 0 ? 0x7fffffff : v->env[0];
    v->lfo2 = (const int8_t *)s->p20;
    v->lfo1len = s->t1e;
    v->lfo2len = s->t21;
    v->eslope = (v->env[1] * 0x10000 + s->envstart * -0x10000) / v->eticks;
    v->lfo1 = (const int8_t *)s->p1d;
    v->lfo2depth = s->t22;
    v->lfo1ph = s->t1f ? (uint8_t)(snd_rand() % (uint32_t)s->t1f) : 0;
    v->lfo2ph = s->t23 ? (uint8_t)(snd_rand() % (uint32_t)s->t21) : 0;   /* (by the length's, as the original) */
    v->pitchmul = 0;
    v->f4c = (int16_t)p->f0e;
    v->pitch = p->pitch;
    voice_pitch(v);
    v->send[0] = (int8_t)s->t13;
    v->send[1] = p->f0a;
    voice_group(0, v);
    voice_volume(v);
    v->patch = (int16_t)p->patch;
    v->bank = (uint8_t)p->bank;
    if (!p->f0b) {
        v->pan = (uint16_t)(((pb - 0x40) & 0xff) << 8);
        v->pan2 = 0;
    } else {
        v->pan = (uint16_t)p->pan18;
        v->pan2 = p->pan1a;
    }
    v->rate = s->rate;           /* 0x598cd0 */
    v->nsamples = s->nsamples;
    if (p->f12 != 0xffff && (float)p->f12 / ((float)s_rate * 0.5f) < 1.0f)
        PENDING_NOTE(0x599690, "the SND voices' lowpass");
    if (p->f14) PENDING_NOTE(0x599720, "the SND voices' filter");
    voice_sources(i, s, bank);
    return handle;
}

/* The patch's layers whose key and velocity ranges hold the play's (0x590560); the layers of one play
   share a group. */
static int play_patch(const uint8_t *bank, const uint8_t *pt, const SndParams *p)
{
    if (!pt) return -8;
    int key = p->key, vel = p->velocity;
    const uint8_t *cur = pt + ((pt[3] & 2) ? 8 : 4);
    uint8_t group = 0;
    {   /* 0x590180: a group id no voice has */
        for (;;) {
            if (++s_groupid == 0) s_groupid = 1;
            bool used = false;
            for (int i = 0; i < s_nvoices; i++)
                if (s_voice[i].state && s_voice[i].group == s_groupid) used = true;
            if (!used) break;
        }
        group = s_groupid;
    }
    int handles[8], nh = 0, last = -9, detune = 0;
    bool first = true, more;
    do {
        SndPatch s;
        more = SND_PatchParse(&cur, &s);
        if (first) {
            if (s.t24) detune = snd_random(s.t24);
            first = false;
        }
        if (s.keylo <= key && key <= s.keyhi && s.vello <= vel && vel <= s.velhi) {
            last = play_layer(bank, p, &s, key, vel, detune);
            if (last < 0) break;
            if (nh < 8) handles[nh++] = last;
        }
    } while (more);
    if (nh == 0) return last;
    for (int k = 0; k < nh; k++) {
        int i = voice_of(handles[k]);
        if (i < 0) continue;
        s_voice[i].group = nh == 1 ? 0 : group;
        s_voice[i].lastlayer = 0;
        if (nh > 1 && k == nh - 1) s_voice[i].lastlayer = 1;
    }
    return last;
}

int SND_Play(int bank, int patch, SndParams *p)
{
    if (!s_on) return -10;
    p->bank = (int8_t)bank;
    p->patch = patch;
    if (bank < 0 || s_nbanks <= bank || !s_bank[bank]) return -8;
    const uint8_t *b = s_bank[bank];
    if (patch < 0 || (b[6] | b[7] << 8) <= patch) return -8;
    return play_patch(b, SND_BankPatch(b, patch), p);
}

/* ---- banks ---- */

int SND_BankAdd(const uint8_t *bank)
{
    if (!s_on) return -10;
    for (int i = 0; i < s_nbanks; i++)
        if (!s_bank[i]) {
            s_bank[i] = bank;
            return i;
        }
    return -9;
}

void SND_BankRemove(int slot)
{
    if (!s_on || slot < 0 || s_nbanks <= slot || !s_bank[slot]) return;
    for (int i = 0; i < s_nvoices; i++)
        if (s_voice[i].state && s_voice[i].bank == slot && s_voice[i].handle >= 0) SND_Stop(s_voice[i].handle);
    s_bank[slot] = NULL;
}

/* ---- the tick ---- */

/* 100 times a second (0x5892d0): each playing voice's LFOs, its volume ramp (ending the voice below 0),
   its envelope (ending it after the last segment); then its volume and pitch to the sources. */
static void tick(void)
{
    s_ticks++;
    for (int i = 0; i < s_nvoices; i++) {
        SndVoice *v = &s_voice[i];
        if (v->state != 1 || v->handle < 0) continue;
        if (v->lfo2) {
            if (v->lfo2len <= ++v->lfo2ph) v->lfo2ph = 0;
            v->pitchmul = 0;
            voice_pitch(v);
            voice_apply_pitch(i);
        }
        bool dirty = false;
        if (v->lfo1) {
            dirty = true;
            if (v->lfo1len <= ++v->lfo1ph) v->lfo1ph = 0;
        }
        if (v->vstep) {
            dirty = true;
            v->vcur += v->vstep;
            if (v->vstep < 0 ? v->vcur <= v->vtarget : v->vtarget <= v->vcur) {
                v->vcur = v->vtarget;
                v->vstep = 0;
            }
            if (v->vcur < 0) {
                SND_Stop(v->handle);
                continue;
            }
        }
        if (v->eslope) {
            dirty = true;
            v->elevel += v->eslope;
        }
        if (v->eticks == 0) {
            if (v->nseg <= ++v->seg) {
                SND_Stop(v->handle);
                continue;
            }
            const int32_t *e = v->env + v->seg * 2;
            v->eticks = e[0] < 0 ? 0x7fffffff : e[0];
            v->eslope = (e[1] * 0x10000 - v->elevel) / v->eticks;
        }
        v->eticks--;
        if (dirty) {
            voice_volume(v);
            if (v->handle >= 0) voice_apply_volume(i);
        }
    }
}

/* ---- the device ---- */

static int32_t s_tickleft, s_tickn, s_tickdone;   /* 0x6ad6e0, 0x6ad6e4, 0x6ad6e8 */

void SND_Fill(int16_t *out, int n)
{
    if (!s_on) {
        memset(out, 0, (size_t)n * (size_t)s_channels * sizeof *out);
        return;
    }
    while (0 < n) {
        if (s_tickleft < 1) {
            s_tickn++;
            tick();
            s_tickleft = (int32_t)((((uint32_t)s_rate * (uint32_t)s_tickn) / 100 - (uint32_t)s_tickdone) & 0xffffff0u);
            s_tickdone += s_tickleft;
            if (30000 < s_tickn) s_tickn = s_tickdone = 0;
        }
        int k = n <= s_tickleft ? n : s_tickleft;
        if (k < 1) k = n;   /* (port: a rate under 1600 would never reach a 16-sample tick) */
        s_tickleft -= k;
        n -= k;
        SndMix_Mix(out, k);
        out += k * s_channels;
    }
}

bool SND_Init(int rate, int channels, int voices, int quality)
{
    SND_Shutdown();
    s_rate = rate;
    s_channels = channels;
    s_nsw = voices;
    s_nvoices = voices;
    g_snd_quality = (uint8_t)quality;
    s_voice = calloc((size_t)s_nvoices, sizeof *s_voice);
    s_dev = calloc((size_t)s_nvoices, sizeof *s_dev);
    s_bank = calloc((size_t)s_nbanks, sizeof *s_bank);
    if (!s_voice || !s_dev || !s_bank) return false;
    for (int i = 0; i < s_nvoices; i++) s_voice[i].handle = -1;
    for (int g = 0; g < 32; g++) s_groupvol[g][0] = s_groupvol[g][1] = 0x7f;
    /* (the groups' volumes are the reverb's sends, 0x565150 -> 0x590050, 0x598c60; the reverb isn't ported) */
    s_master = 0x7f;
    s_ticks = 0;
    pan_table();
    SndMix_Init(s_nsw, s_channels, s_rate);
    g_snd_source_end = source_end;
    s_tickleft = s_tickn = s_tickdone = 0;
    s_on = true;
    return true;
}

void SND_Shutdown(void)
{
    s_on = false;
    SndMix_Free();
    free(s_voice);
    free(s_dev);
    free(s_bank);
    s_voice = NULL;
    s_dev = NULL;
    s_bank = NULL;
}
