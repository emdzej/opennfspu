/* The SND library's software mixer (see snd_mix.h). */
#include "snd/snd_mix.h"
#include "exe.h"
#include "pending.h"
#include "snd/snd.h"
#include <stdlib.h>
#include <string.h>

void (*g_snd_source_end)(int i);

static float K(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

enum { BLOCK = 0x100, WORK = 0x2420 / 4, SLACK = 16 };

static SndSource *s_src;          /* 0x6b1710 */
static int s_nsrc, s_nch;         /* 0x6b15e4 bytes 0, 1 */
static float *s_tmp, *s_dec;      /* 0x6b1694, 0x6b1698 (with room before them for the decoders' history) */
static float *s_acc[4];           /* 0x6b16ac: each output channel's sum */
static int16_t *s_planar;         /* 0x6b16bc: a block's channels before interleaving */
static float s_xa[13][16];        /* 0x6b0400: EA-XA's nibbles by shift (0x59b680) */
static float s_pcm8[256];         /* 0x6b0000: signed 8-bit samples (0x59bf60) */

SndSource *SndMix_Source(int i) { return i >= 0 && i < s_nsrc ? &s_src[i] : NULL; }

/* ---- the chains ---- */

static void chain_add(SndNode **head, SndNode *n)   /* 0x596a80: by type */
{
    SndNode *p = *head, *prev = NULL;
    while (p && p->type < n->type) {
        prev = p;
        p = p->next;
    }
    n->next = p;
    if (prev) prev->next = n;
    else *head = n;
}

/* ---- 8- and 16-bit PCM (codecs 7, 8, 9) ---- */

typedef struct {
    SndNode n;
    const uint8_t *data;      /* +0x0c */
    int32_t pos;              /* +0x10 */
    int32_t a, b;             /* +0x14, +0x18: one-shot: the length, 16-bit; looping: the loop's start and end */
    int32_t wide;             /* +0x1c (looping) */
} SndPcm;

static void pcm_convert(const SndPcm *p, int n, int at, int wide, float *out)
{
    if (wide) {   /* 0x6b1718 = 0x59b7a0 */
        const uint8_t *s = p->data + at * 2;
        for (int i = 0; i < n; i++) out[i] = (float)(int16_t)(s[2 * i] | s[2 * i + 1] << 8);
    } else {      /* 0x59a2d0 */
        for (int i = 0; i < n; i++) out[i] = s_pcm8[p->data[at + i]];
    }
}

static int pcm_once(SndNode *nd, int n, float *tmp, float *out, int ch)   /* 0x597910 */
{
    (void)tmp;
    (void)ch;
    SndPcm *p = (SndPcm *)nd;
    int pos = p->pos, len = p->a;
    if (len <= pos) return -1;
    p->pos = pos + n;
    if (pos + n < len) {
        pcm_convert(p, n, pos, p->b, out);
        return 1;
    }
    int k = len - pos;
    pcm_convert(p, k, pos, p->b, out);
    memset(out + k, 0, (size_t)(n - k) * sizeof *out);
    return 1;
}

static int pcm_loop(SndNode *nd, int n, float *tmp, float *out, int ch)   /* 0x59c950 */
{
    (void)tmp;
    (void)ch;
    SndPcm *p = (SndPcm *)nd;
    while (0 < n) {
        int pos = p->pos, k = p->b - pos + 1;
        if (n < k) k = n;
        pcm_convert(p, k, pos, p->wide, out);
        n -= k;
        p->pos = pos + k;
        out += k;
        if ((uint32_t)p->b < (uint32_t)(pos + k)) p->pos = p->a;
    }
    return 1;
}

static int pcm_position(SndNode *nd) { return ((SndPcm *)nd)->pos; }   /* 0x597900 */

/* ---- EA-XA ADPCM (codec 10) ---- */

/* The frame decoder's state (+0x0c of the decoders): samples to go, the two last samples, the input,
   the output. */
typedef struct {
    int32_t count;
    float prev1, prev2;
    const uint8_t *in;
    float *out;
} SndXaRun;

/* Whole 28-sample frames while count is above 0 (0x59a350): the previous two samples are written
   before the output first. */
static void xa_frames(SndXaRun *r)
{
    float *o = r->out;
    o[-1] = r->prev1;
    o[-2] = r->prev2;
    const uint8_t *in = r->in;
    while (0 < r->count) {
        r->count -= 0x1c;
        int h = in[0] >> 4, sh = in[0] & 0xf;
        float c2 = K(0x5e3b38 + 4 * (uint32_t)h), c1 = K(0x5e3b28 + 4 * (uint32_t)h);
        const float *t = s_xa[sh];
        for (int i = 0; i < 14; i++) {
            uint8_t b = in[1 + i];
            o[0] = (float)((double)o[-1] * c1 + (double)t[b >> 4] + (double)o[-2] * c2);
            o[1] = (float)((double)o[0] * c1 + (double)t[b & 0xf] + (double)o[-1] * c2);
            o += 2;
        }
        in += 0xf;
    }
    r->prev1 = o[-1];
    r->prev2 = o[-2];
    r->out = o;
    r->in = in;
}

typedef struct {
    SndNode n;
    SndXaRun run;             /* +0x0c .. +0x1c */
    const uint8_t *data;      /* +0x20 */
    int32_t len;              /* +0x24 */
    int32_t pos;              /* +0x28 */
    int32_t loopstart;        /* +0x2c (looping) */
    int32_t loopend;          /* +0x30 */
    int32_t nleft;            /* one-shot +0x2c, looping +0x34 */
    float loopprev1, loopprev2;   /* +0x3c, +0x40: the predictor at the loop's start */
    float left[0x40];         /* one-shot +0x30 (28), looping +0x4c */
} SndXa;

static int xa_once(SndNode *nd, int n, float *tmp, float *out, int ch)   /* 0x5979f0 */
{
    (void)tmp;
    (void)ch;
    SndXa *x = (SndXa *)nd;
    int pos = x->pos, len = x->len;
    if (len <= pos) return -1;
    int left = x->nleft;
    x->run.out = out;
    if (n <= left) {
        memcpy(out, x->left, (size_t)n * sizeof *out);
        memmove(x->left, x->left + n, (size_t)(left - n) * sizeof *out);
        x->nleft = left - n;
        return 1;
    }
    memcpy(out, x->left, (size_t)left * sizeof *out);
    x->run.out = out + left;
    n -= left;
    int remain = len - pos;
    if (remain < 0) remain = 0;
    int cnt = n <= remain ? n : remain;
    x->run.count = cnt;
    x->run.in = x->data + (pos * 0xf) / 0x1c;
    xa_frames(&x->run);
    int over = x->run.count;
    x->pos += cnt - over;
    if (remain == cnt) {
        n += (len - x->pos) - cnt;
        if (n < 1) return 1;
        memset(x->run.out, 0, (size_t)n * sizeof *out);
        x->run.out += n;
        return 1;
    }
    int k = 0;
    if (over < 0) {
        k = -over;
        memcpy(x->left, x->run.out + over, (size_t)k * sizeof *out);
    }
    x->nleft = k;
    return 1;
}

/* Copies up to n of the k samples in buf to *out (moving it on), the rest down to buf's start. 0x59d3a0 */
static int take(int k, float *buf, int n, float **out)
{
    int c = k < n ? k : n;
    memcpy(*out, buf, (size_t)c * sizeof **out);
    *out += c;
    if (c < k) memmove(buf, buf + c, (size_t)(k - c) * sizeof *buf);
    return c;
}

/* The loop (0x59c440): frames up to the one holding the loop's end, that one into the leftovers up to
   the end, then back to the frame of the loop's start with the predictor saved there, the part of that
   frame from the start on after the leftovers. */
static int xa_inloop(SndNode *nd, int n, float *tmp, float *out, int ch)
{
    (void)tmp;
    (void)ch;
    SndXa *x = (SndXa *)nd;
    x->run.out = out;
    while (0 < n) {
        if (x->nleft == 0) {
            int pos = x->pos;
            if (pos + 0x1b < x->loopend) {
                x->run.count = 0x1c;
                x->run.in = x->data + (pos * 0xf) / 0x1c;
                xa_frames(&x->run);
                n -= 0x1c;
                x->pos += 0x1c;
            } else {
                float *keep = x->run.out;
                x->run.count = 0x1c;
                x->run.in = x->data + (pos * 0xf) / 0x1c;
                x->run.out = x->left + 2;   /* (port: room for the decoder's two history words) */
                xa_frames(&x->run);
                memmove(x->left, x->left + 2, 0x1c * sizeof *x->left);
                x->run.prev1 = x->loopprev1;
                x->nleft = x->loopend - x->pos + 1;
                int k = x->loopstart / 0x1c;
                x->run.prev2 = x->loopprev2;
                x->pos = k * 0x1c;
                if (x->loopstart % 0x1c != 0) {
                    float frame[0x1c + 2];
                    x->run.count = 0x1c;
                    x->run.in = x->data + (k * 0x1a4) / 0x1c;
                    x->run.out = frame + 2;
                    xa_frames(&x->run);
                    x->pos += 0x1c;
                    int d = x->pos - x->loopstart;
                    for (int i = 0; i < d; i++) x->left[x->nleft++] = frame[2 + 0x1c - d + i];
                }
                x->run.out = keep;
            }
        } else {
            int c = take(x->nleft, x->left, n, &x->run.out);
            n -= c;
            x->nleft -= c;
        }
    }
    x->run.out += n;
    for (; n < 0; n++) x->left[x->nleft++] = *x->run.out++;
    return 1;
}

/* Before the loop (0x59c640): as the one-shot up to the frame of the loop's start, where the predictor
   is saved and the loop's render takes over. */
static int xa_toloop(SndNode *nd, int n, float *tmp, float *out, int ch)
{
    SndXa *x = (SndXa *)nd;
    int left = x->nleft;
    x->run.out = out;
    if (n <= left) {
        memcpy(out, x->left, (size_t)n * sizeof *out);
        memmove(x->left, x->left + n, (size_t)(left - n) * sizeof *out);
        x->nleft = left - n;
        return 1;
    }
    memcpy(out, x->left, (size_t)left * sizeof *out);
    x->run.out = out + left;
    n -= left;
    int lim = (x->loopstart - x->pos) - 0x1b;
    int cnt = n <= lim ? n : lim;
    if (cnt < 0) cnt = 0;
    x->run.count = cnt;
    x->run.in = x->data + (x->pos * 0xf) / 0x1c;
    xa_frames(&x->run);
    int over = x->run.count;
    x->pos += cnt - over;
    int k = 0;
    if (over < 0) {
        k = -over;
        memcpy(x->left, x->run.out + over, (size_t)k * sizeof *out);
    }
    x->run.out += over < 0 ? over : 0;
    x->nleft = k;
    if (x->loopstart - x->pos < 0x1c) {
        x->loopprev1 = x->run.prev1;
        x->loopprev2 = x->run.prev2;
        x->n.render = xa_inloop;
        if (0 < n - cnt) xa_inloop(nd, n - cnt, tmp, x->run.out, ch);
    }
    return 1;
}

static int xa_position(SndNode *nd) { return ((SndXa *)nd)->pos; }   /* 0x597b60 */

/* ---- the resampler (0x59be30, 0x59bbb0) ---- */

typedef struct {
    SndNode n;
    int cubic;                /* +0x0c: the kernel (0x59ca10 four taps, 0x59cd20 two) */
    int32_t used;             /* +0x10 */
    uint32_t frac;            /* +0x14 */
    uint32_t step;            /* +0x18: 16.16 */
    int32_t primed;           /* +0x1c */
    int16_t pre, hist;        /* +0x20, +0x22: 1, 4 (cubic) or 0, 2 */
    float h[4];               /* +0x24 */
} SndResampler;

/* Catmull-Rom between base[i] and base[i + 1] (0x59ca10); the position 16.16 in pos. */
static void kernel_cubic(int n, const float *b, float *out, int32_t *idx, uint32_t *pos, uint32_t step)
{
    uint32_t p = *pos >> 16;
    for (int k = 0; k < n; k++) {
        int i = *idx;
        uint32_t q = p + step;
        float t = (float)p * K(0x5c1818);
        p = q & 0xffff;
        *idx = (int)(q >> 16) + i;
        float pm = b[i - 1], p0 = b[i], p1 = b[i + 1], p2 = b[i + 2];
        out[k] = (float)((double)(p1 - pm) * 0.5 * t + ((double)(p1 + p1 + pm) - ((double)p0 * 5.0 + p2) * 0.5) * t * t +
                         (double)t * t * t * ((((double)p0 - p1) * 3.0 - pm) + p2) * 0.5 + p0);
    }
    *pos = p << 16;
}

/* Linear (0x59cd20): the fraction in 32 bits. */
static void kernel_linear(int n, const float *b, float *out, int32_t *idx, uint32_t *pos, uint32_t step)
{
    uint32_t f = *pos, sf = step << 16;
    int i = *idx, si = (int)(step >> 16);
    for (int k = 0; k < n; k++) {
        out[k] = (float)((double)f * ((double)b[i + 1] - b[i]) * (double)K(0x5e3b88) + b[i]);   /* (fild qword: f exact) */
        uint32_t g = f + sf;
        i += si + (g < f);
        f = g;
    }
    *pos = f;
    *idx = i;
}

static int resample(SndNode *nd, int n, float *tmp, float *out, int ch)
{
    SndResampler *r = (SndResampler *)nd;
    int H = r->hist, P = r->pre;
    if (r->step != 0x10000) {
        int need = (int)((r->step * (uint32_t)n + r->frac) >> 16);
        float *at;
        if (!r->primed) {
            need += H - P;
            memset(tmp, 0, (size_t)P * sizeof *tmp);
            at = tmp + P;
        } else {
            memcpy(tmp, r->h, (size_t)H * sizeof *tmp);
            at = tmp + H;
        }
        if (0 < need && r->n.next) {
            int got = r->n.next->render(r->n.next, need, out, at, ch);
            if (got < 1) {
                r->primed = 1;
                memset(r->h, 0, sizeof r->h);
                return got;
            }
        }
        if (!r->primed) r->primed = 1;
        else at += P - H;
        uint32_t pos = r->frac << 16;
        r->used = 0;
        if (r->cubic) kernel_cubic(n, at, out, &r->used, &pos, r->step);
        else kernel_linear(n, at, out, &r->used, &pos, r->step);
        r->frac = pos >> 16;
        for (int i = 0; i < H; i++) r->h[i] = at[i - P + r->used];
        return n;
    }
    float *at;
    int k;
    if (!r->primed) {
        k = (H - P) + n;
        at = out;
    } else {
        memcpy(out, r->h + P, (size_t)(H - P) * sizeof *out);
        at = out + (H - P);
        k = n;
    }
    int got = r->n.next ? r->n.next->render(r->n.next, k, tmp, at, ch) : n;
    if (0 < got) {
        r->primed = 1;
        for (int i = 0; i < H; i++) r->h[i] = at[i - H + k];
        return n;
    }
    r->primed = 1;
    memset(r->h, 0, sizeof r->h);
    return got;
}

/* ---- the lowpass (0x599f20, 0x599ea0 with 0x59bf20) ---- */

typedef struct {
    SndNode n;
    float y;                  /* +0x0c */
    float keep;               /* +0x10: 1 - c */
    float c;                  /* +0x14: the cutoff over half the device's rate */
} SndLowpass;

static int lowpass(SndNode *nd, int n, float *tmp, float *out, int ch)
{
    SndLowpass *l = (SndLowpass *)nd;
    if (l->n.next) {
        int got = l->n.next->render(l->n.next, n, out, tmp, ch);
        if (got < 1) return got;
    }
    memcpy(out, tmp, (size_t)n * sizeof *out);
    float y = l->y;
    for (int i = 0; i < n; i++) {
        y = l->c * out[i] + y * l->keep;
        out[i] = y;
    }
    l->y = y;
    return n;
}

static void chain_remove(SndNode **head, SndNode *n)   /* 0x596ac0 */
{
    for (SndNode **p = head; *p; p = &(*p)->next)
        if (*p == n) {
            *p = n->next;
            return;
        }
}

void SndMix_SetLowpass(int i, float c)   /* 0x599690 */
{
    SndSource *s = &s_src[i];
    if (c < 1.0f) {
        if (!s->lowpass) {
            SndLowpass *l = calloc(1, sizeof *l);
            if (!l) return;
            l->n.render = lowpass;
            l->n.type = 0;
            s->lowpass = &l->n;
            chain_add(&s->chain, &l->n);
        }
        SndLowpass *l = (SndLowpass *)s->lowpass;   /* 0x599f00 */
        l->c = c;
        l->keep = 1.0f - c;
        return;
    }
    if (s->lowpass) {
        chain_remove(&s->chain, s->lowpass);
        free(s->lowpass);
        s->lowpass = NULL;
    }
}

/* ---- the sources ---- */

void SndMix_SetupSource(int i, int codec, bool has_length, const uint8_t *data, const uint8_t *chdata,
                        int nsamples, int loopstart, int looplen)
{
    SndSource *s = &s_src[i];
    s->chain = NULL;
    s->decoder = s->effect = s->resampler = s->lowpass = s->filter = NULL;
    s->position = NULL;
    int wide = 1, kind;
    switch (codec) {
    case 9: kind = 0; wide = 0; break;
    case 7: case 8: kind = 3; break;
    case 10: kind = 6; break;
    case 4: kind = 9; break;
    case 0x40: kind = 12; break;
    default: kind = 0; break;
    }
    int variant = has_length ? (0 < looplen ? 1 : 0) : 2;
    SndNode *d = NULL;
    if (kind <= 3 && variant < 2) {
        SndPcm *p = calloc(1, sizeof *p);
        if (p) {
            p->data = data;
            if (variant == 0) {   /* 0x5979c0 */
                p->n.render = pcm_once;
                p->a = nsamples;
                p->b = wide;
            } else {              /* 0x59c9e0 */
                p->n.render = pcm_loop;
                p->a = loopstart;
                p->b = looplen;
                p->wide = wide;
            }
            s->position = pcm_position;
            d = &p->n;
        }
    } else if (kind == 6 && variant < 2) {
        SndXa *x = calloc(1, sizeof *x);
        if (x) {
            x->data = data;
            x->len = nsamples;
            if (variant == 0) {   /* 0x597b70 */
                x->n.render = xa_once;
            } else {              /* 0x59c7a0 */
                x->n.render = xa_toloop;
                x->loopstart = loopstart;
                x->loopend = looplen;
            }
            s->position = xa_position;
            d = &x->n;
        }
    } else {
        PENDING_NOTE(0x598280, "the SND decoders for codecs 4 and 0x40, and streamed samples");
    }
    if (d) {
        d->type = 4;
        s->decoder = d;
        chain_add(&s->chain, d);
    }
    if (chdata) PENDING_NOTE(0x599e30, "the SND per-channel effect (tags 0x98 .. 0x9b)");
    s->state = 1;
}

void SndMix_SetLevels(int i, float master, const float *chan)   /* 0x5984b0 */
{
    SndSource *s = &s_src[i];
    s->ramp = 1;
    s->mastert = master;
    for (int c = 0; c < s_nch; c++) {
        float v = master * chan[c];
        s->chant[c] = v < K(0x5b2738) ? 0.0f : v;
    }
}

static void update_level(SndSource *s) { s->level = s->master * s->gain * K(0x5b246c); }   /* 0x597e50 */

void SndMix_SetGain(int i, float gain)   /* 0x5995b0 */
{
    s_src[i].gain = gain;
    update_level(&s_src[i]);
}

void SndMix_SetPitch(int i, int pitch)   /* 0x5995e0 */
{
    SndSource *s = &s_src[i];
    if (0x40000 < pitch) pitch = 0x40000;
    if (!s->resampler) {
        SndResampler *r = calloc(1, sizeof *r);
        if (!r) return;
        /* 0x59be30: four taps from the quality 0x6b4825 of 50 on (the port's plain kernels: flags 0) */
        r->cubic = 0x32 <= g_snd_quality;
        r->n.render = resample;
        r->pre = (int16_t)(r->cubic ? 1 : 0);
        r->hist = (int16_t)(r->cubic ? 4 : 2);
        r->n.type = 2;
        s->resampler = &r->n;
        chain_add(&s->chain, &r->n);
    }
    ((SndResampler *)s->resampler)->step = (uint32_t)pitch;   /* 0x59bba0 */
}

int SndMix_Position(int i)
{
    SndSource *s = &s_src[i];
    return s->position && s->decoder ? s->position(s->decoder) : -1;
}

void SndMix_Start(int i)
{
    SndSource *s = &s_src[i];
    for (int c = 0; c < s_nch; c++) s->chan[c] = s->chant[c];
    s->ramp = 0;
    s->master = s->mastert;
    s->state = 2;
}

/* Up to 32 samples rendered with the levels ramping to their targets, into the sums (0x597e70). */
static int ramp(SndSource *s, int n)
{
    if (0x20 < n) n = 0x20;
    int got = s->chain ? s->chain->render(s->chain, n, s_tmp, s_dec, 0) : -1;
    if (0 < got) {
        float m1 = s->mastert, m0 = s->master;
        for (int c = 0; c < s_nch; c++) {
            float to = s->chant[c], from = s->chan[c];
            for (int k = 0; k < n; k++) {
                float v = (to - from) / (float)n + s->chan[c];
                s->chan[c] = v;
                s_acc[c][k] += s_dec[k] * v;   /* (*0x6b1714)(1, v, ...) */
                if (c == s_nch - 1) {
                    s->master = (m1 - m0) / (float)n + s->master;
                    update_level(s);
                }
            }
            s->chan[c] = to;
        }
        s->master = s->mastert;
        return n;
    }
    s->master = s->mastert;
    for (int c = 0; c < s_nch; c++) s->chan[c] = s->chant[c];
    return 0;
}

void SndMix_Release(int i)
{
    SndSource *s = &s_src[i];
    float zero[4] = {0, 0, 0, 0};
    SndMix_SetLevels(i, 0, zero);
    ramp(s, 0x100);
    for (SndNode *n = s->chain; n;) {
        SndNode *next = n->next;
        free(n);
        n = next;
    }
    s->chain = NULL;
    s->decoder = s->effect = s->resampler = s->lowpass = s->filter = NULL;
    s->state = 0;
}

/* A block of n samples (at most 256): each playing source into the sums by its levels, then each output
   channel to 16 bits (0x59a100: the float rounded by adding 1.5 x 2^23, its low 20 bits saturated). */
static void block(int n, int16_t **dest)
{
    for (int i = 0; i < s_nsrc; i++) {
        SndSource *s = &s_src[i];
        if (s->state != 2) continue;
        int done = 0, rest = n;
        if (s->ramp) {
            done = ramp(s, n);
            s->ramp = 0;
            rest = n - done;
        }
        if (!rest) continue;
        int got = s->chain ? s->chain->render(s->chain, rest, s_tmp, s_dec, 0) : -1;
        if (got < 0) {
            SndMix_Release(i);   /* 0x598420 */
            if (g_snd_source_end) g_snd_source_end(i);
        } else if (got) {
            for (int c = 0; c < s_nch; c++)
                if (s->chan[c] != 0.0f)
                    for (int k = 0; k < rest; k++) s_acc[c][done + k] += s_dec[k] * s->chan[c];
        }
    }
    for (int c = 0; c < s_nch; c++) {
        for (int k = 0; k < n; k++) {
            float f = s_acc[c][k] + K(0x5c1800);
            uint32_t u;
            memcpy(&u, &f, 4);
            u &= 0xfffff;
            if (0x7fff < u && u < 0xf8000) u = (u < 0x80000 ? 0xffffu : 0) - 0x8000u;
            dest[c][k] = (int16_t)u;
        }
        memset(s_acc[c], 0, (size_t)n * sizeof *s_acc[c]);
    }
}

void SndMix_Mix(int16_t *dest, int n)
{
    while (0 < n) {
        int k = n < BLOCK ? n : BLOCK;
        int16_t *planes[4];
        for (int c = 0; c < s_nch; c++) planes[c] = s_planar + c * BLOCK;
        block(k, planes);
        for (int f = 0; f < k; f++)
            for (int c = 0; c < s_nch; c++) dest[f * s_nch + c] = planes[c][f];   /* 0x59ba90 */
        dest += k * s_nch;
        n -= k;
    }
}

void SndMix_Init(int nsources, int nchannels, int rate)   /* 0x597fe0 */
{
    SndMix_Free();
    s_nsrc = nsources;
    s_nch = nchannels < 4 ? nchannels : 4;
    (void)rate;
    s_src = calloc((size_t)nsources, sizeof *s_src);
    static float tmpbuf[WORK + SLACK], decbuf[WORK + SLACK];
    s_tmp = tmpbuf + SLACK;
    s_dec = decbuf + SLACK;
    for (int c = 0; c < s_nch; c++) s_acc[c] = calloc(0x440 / 4, sizeof *s_acc[c]);
    s_planar = calloc((size_t)(BLOCK * s_nch), sizeof *s_planar);
    for (int sh = 0; sh < 13; sh++)
        for (int k = 0; k < 16; k++) s_xa[sh][k] = (float)(((int32_t)((uint32_t)k << 28) >> 16) >> sh);
    for (int v = -0x80; v < 0x80; v++) s_pcm8[v & 0xff] = (float)v * K(0x5b26b0);
}

void SndMix_Free(void)
{
    if (s_src)
        for (int i = 0; i < s_nsrc; i++)
            for (SndNode *n = s_src[i].chain; n;) {
                SndNode *next = n->next;
                free(n);
                n = next;
            }
    free(s_src);
    s_src = NULL;
    for (int c = 0; c < 4; c++) {
        free(s_acc[c]);
        s_acc[c] = NULL;
    }
    free(s_planar);
    s_planar = NULL;
    s_nsrc = 0;
}
