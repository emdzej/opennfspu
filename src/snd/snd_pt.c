/* The banks' patches (see snd.h). */
#include "snd/snd.h"
#include "exe.h"
#include <string.h>

static int32_t le32(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

const uint8_t *SND_BankPatch(const uint8_t *bank, int i)
{
    if (le16(bank + 6) <= i) return NULL;
    int base = bank[4] == 4 ? 0x14 : 0xc;
    int32_t o = le32(bank + base + 4 * i);
    return o ? bank + o + base + 4 * i : NULL;
}

/* n big-endian bytes (0x59bb40). */
static int32_t be(const uint8_t *p, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v = v << 8 | p[i];
    return (int32_t)v;
}

bool SND_TagNext(SndTags *t)
{
    while (*t->p == 0xfc) t->p++;
    t->tag = *t->p;
    if (t->tag == 0xff) return false;
    t->p++;
    if (t->tag == 0xfd || t->tag == 0xfe) return true;
    t->len = *t->p;
    if (t->len == 0xff) {
        t->len = be(t->p + 1, 4);
        t->p += 4;
    }
    t->p++;
    t->data = t->p;
    if ((uint32_t)t->len < 5) t->value = be(t->p, t->len);   /* (a longer tag keeps the last value) */
    t->p += t->len;
    return true;
}

bool SND_PatchParse(const uint8_t **p, SndPatch *out)
{
    /* The values by tag (0x00 .. 0x25, 0x80 .. 0xa1) with their defaults, and their data. */
    int32_t lo[0x26], hi[0x22];
    const uint8_t *lod[0x26], *hid[0x22];
    memset(lo, 0, sizeof lo);
    memset(hi, 0, sizeof hi);
    memset(lod, 0, sizeof lod);
    memset(hid, 0, sizeof hid);
    lo[2] = lo[4] = lo[0xe] = lo[0x1c] = 0x7f;
    lo[7] = 0x3c;
    lo[8] = -1;
    lo[9] = lo[0x25] = 1;
    lo[0xc] = 0x40;
    hi[0x82 - 0x80] = 1;
    hi[0x84 - 0x80] = 0x5622;
    hi[0x86 - 0x80] = hi[0x87 - 0x80] = -1;
    hi[0xa0 - 0x80] = 10;
    memset(out, 0, sizeof *out);
    out->pt = *p;
    SndTags t = {*p, 0, 0, NULL, 0};
    bool more = false;
    while (SND_TagNext(&t)) {
        int k = t.tag;
        if (k < 0x26) {
            lo[k] = t.value;
            lod[k] = t.data;
            if (k == 0x14 && out->nparts < 4) {
                out->part[out->nparts] = t.data;
                out->partlen[out->nparts] = t.len;
                out->nparts++;
            }
        } else if (0x80 <= k && k <= 0xa1) {
            hi[k - 0x80] = t.value;
            hid[k - 0x80] = t.data;
            if (0x98 <= k && k <= 0x9b) {
                out->chdata[k - 0x98] = t.data;
                out->chlen[k - 0x98] = (int16_t)t.len;
            }
        } else if (k == 0xfe) {
            more = true;
            break;
        }
    }
    out->keylo = (uint8_t)lo[1];
    out->keyhi = (uint8_t)lo[2];
    out->velhi = (uint8_t)lo[4];
    out->t06 = (uint8_t)lo[6];
    out->t08 = (uint8_t)lo[8];
    out->t09 = (uint8_t)lo[9];
    out->vello = (uint8_t)lo[3];
    out->pan = (uint8_t)lo[0xc];
    out->t0d = (uint8_t)lo[0xd];
    out->rootkey = (uint8_t)lo[7];
    out->t0f = (uint8_t)lo[0xf];
    out->t10 = (int16_t)lo[0x10];
    out->t0a = (uint8_t)lo[0xa];
    out->vol = (uint8_t)lo[0xe];
    out->p12 = lod[0x12] + lo[0x12];
    out->t11 = (int16_t)lo[0x11];
    out->t13 = (uint8_t)lo[0x13];
    out->p17 = lod[0x17] + lo[0x17];
    out->envstart = (uint8_t)lo[0x1c];
    /* The envelope: the exe's default (0x5e3498) when there is none (data plus value otherwise). */
    out->env = lod[0x19] ? (const int32_t *)(const void *)(lod[0x19] + lo[0x19])
                         : (const int32_t *)(const void *)exe_data(0x5e3498, 8);
    out->t1e = (uint8_t)lo[0x1e];
    out->p1d = lod[0x1d] + lo[0x1d];
    out->t1f = (uint8_t)lo[0x1f];
    out->p20 = lod[0x20] + lo[0x20];
    out->t21 = (uint8_t)lo[0x21];
    out->t22 = (int16_t)lo[0x22];
    out->t23 = (uint8_t)lo[0x23];
    out->t24 = (int16_t)lo[0x24];
    out->t25 = (uint8_t)lo[0x25];
    out->t80 = (uint8_t)hi[0];
    out->channels = (uint8_t)hi[2];
    out->codec = (uint8_t)hi[0xa0 - 0x80];
    out->rate = (uint16_t)hi[4];
    out->nsamples = hi[5];
    out->loopstart = hi[6];
    out->looplen = hi[7];
    out->p8a = hid[0xa];
    out->dataofs[0] = hi[8];
    out->dataofs[1] = hi[9];
    out->dataofs[2] = hi[0x14];
    out->dataofs[3] = hi[0x15];
    out->chextra[0] = hi[0x1c];
    out->chextra[1] = hi[0x1d];
    out->chextra[2] = hi[0x1e];
    out->chextra[3] = hi[0x1f];
    out->flags = (uint8_t)hi[0xc];
    uint32_t f = (uint32_t)hi[0x21];
    if (f && !out->flags) {
        out->flags = (uint8_t)((f & 1 ? 1 : 0) | (f & 2 ? 2 : 0) | (f & 0x80 ? 0x20 : 0) | (f & 0x40 ? 0x40 : 0));
    }
    *p = t.p;
    return more;
}
