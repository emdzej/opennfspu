/* The SND library's stream players (0x566320 .. 0x566d40, 16 of them at 0x6b7680): each plays a queue of
   EA streams ("SCHl" header, "SCCl" count, "SCDl" data blocks, "SCEl" end) one after another without a
   gap, through one voice whose sources pull the data blocks from a ring (the library's stream banks,
   0x560fb0 .. 0x5616a0 at 0x6b7c00). A stream whose header differs from the voice's waits until the
   ring has drained, then starts a new voice. Each entry counts its samples played against its length;
   one played out leaves the queue (0x5662c0, 0x566200).

   The original reads the files on its file thread (STREAM_create / STREAM_queuefile / STREAM_get, the
   0x591000 module) into the player's buffer, in chunks the player takes 10 at a time on each pass of
   the device (0x5666f0); a queued stream also waits until a second's worth (its prebuffer) is read. The
   port reads the chunks itself from the data files when the player takes them, so nothing ever waits
   for the file thread, and holds them until their samples are played. */
#include "snd/snd.h"
#include "snd/snd_mix.h"
#include "vfs.h"
#include <stdlib.h>
#include <string.h>

enum { PLAYERS = 0x10, QUEUE = 8, MAXCH = 4 };

/* A ring slot (0x18 bytes): the block's serial, its samples, each channel's data; the chunk it is in. */
typedef struct {
    int32_t serial;
    int32_t len;
    const uint8_t *ch[MAXCH];
    uint8_t *chunk;           /* (the STREAM buffer's chunk, released with the slot: 0x566290) */
} RingSlot;

/* A stream bank (0x6b7c00): the ring and its reading positions. */
typedef struct {
    int32_t voice;            /* +0x00 */
    int32_t serial;           /* +0x04 */
    int16_t rd[MAXCH];        /* +0x08: each channel's last slot taken */
    int16_t avail[MAXCH];     /* +0x10: each channel's slots not taken */
    int16_t cap;              /* +0x18 */
    int16_t lead;             /* +0x1a: the channel whose progress counts */
    int16_t released;         /* +0x1c: the lead's slot before the current one */
    int16_t wr;               /* +0x1e */
    int32_t queued;           /* +0x20: the samples in slots not taken */
    int32_t decoding;         /* +0x24: those taken, not played yet */
    uint8_t channels;         /* +0x36 */
    RingSlot *slot;
} Ring;

/* A queued stream (0x2c bytes). */
typedef struct {
    VfsFile *f;               /* +0x00 (STREAM's) */
    int32_t id;               /* +0x04: the player's number and a serial */
    int32_t first, last;      /* +0x08, +0x0c: its first and last slots, -1 none */
    uint32_t rate;            /* +0x10 */
    int32_t played;           /* +0x14 */
    int32_t total;            /* +0x18: the header's samples */
    int32_t buffered;         /* +0x1c: the samples read, not played */
    int32_t blocks;           /* +0x20 */
    int32_t count;            /* +0x24: SCCl's */
    int16_t prebuffer;        /* +0x28: ms (the port's reads don't wait for it) */
    uint64_t pos;             /* (the file's read position) */
} Entry;

typedef struct {
    Entry q[QUEUE];           /* +0x00 */
    int32_t voice;            /* +0x08 */
    int32_t serial;           /* +0x10 */
    uint8_t state;            /* +0x14: 0 idle, 1 playing, 2 a new header waits for the ring to drain */
    int8_t max;               /* +0x15 */
    int8_t n;                 /* +0x16 */
    int8_t reading;           /* +0x17: the entry being read */
    bool has_format;          /* (+0x2c non-zero) */
    SndPatch hdr, next;       /* +0x34, +0x88 */
    SndParams params;         /* +0xdc */
    int8_t vol;               /* +0xe4 */
    Ring ring;
} Player;

static Player *s_player[PLAYERS];   /* 0x6b7680 */

/* ---- the ring (stream bank) ---- */

static void ring_free_slot(Ring *r, int i)   /* 0x566290: the chunk back to the stream */
{
    free(r->slot[i].chunk);
    r->slot[i].chunk = NULL;
}

/* The next block for the channel (0x5615b0); the lead's previous slot released. */
static const uint8_t *ring_pull(void *ctx, int ch, int *count)
{
    Player *pl = ctx;
    Ring *r = &pl->ring;
    if (ch < 0 || MAXCH <= ch || r->avail[ch] == 0) return NULL;
    if (r->cap <= ++r->rd[ch]) r->rd[ch] = 0;
    RingSlot *s = &r->slot[r->rd[ch]];
    *count = s->len;
    if (ch == r->lead) {
        if (-1 < r->released) ring_free_slot(r, r->released);
        if (r->cap <= ++r->released) r->released = 0;
        r->queued -= s->len;
        r->decoding += s->len;
    }
    r->avail[ch]--;
    return s->ch[ch];
}

static void entry_remove(Player *pl, int32_t id);

/* The samples the lead channel played (0x561670 -> 0x5662c0): the head entries' counts, an entry played
   out leaving the queue. */
static void ring_done(void *ctx, int ch, int n)
{
    Player *pl = ctx;
    Ring *r = &pl->ring;
    if (ch != r->lead) return;
    r->decoding -= n;
    while (n != 0 && 0 < pl->n) {
        Entry *e = &pl->q[0];
        int c = n, rest = 0;
        if ((uint32_t)e->buffered < (uint32_t)n) {
            rest = n - e->buffered;
            c = n - rest;
        }
        n = rest;
        e->played += c;
        e->buffered -= c;
        if ((uint32_t)e->total <= (uint32_t)e->played) entry_remove(pl, e->id);
    }
}

static int ring_room(const Ring *r) { return r->cap - r->avail[0] - 1; }   /* 0x561430 */
static int ring_samples(const Ring *r) { return r->decoding + r->queued; } /* 0x561460 */

/* A block into the ring (0x561370): -13 when full; else its serial. */
static int ring_push(Ring *r, int len, const uint8_t **ch, uint8_t *chunk)
{
    if (r->cap - 1 <= r->avail[0]) return -13;
    RingSlot *s = &r->slot[r->wr];
    s->len = len;
    s->serial = r->serial;
    s->chunk = chunk;
    for (int c = 0; c < r->channels && c < MAXCH; c++) {
        s->ch[c] = ch[c];
        r->avail[c]++;
    }
    r->queued += len;
    int serial = r->serial++;
    if (r->cap <= ++r->wr) r->wr = 0;
    return serial;
}

/* The ring emptied and its voice stopped (0x5614e0). */
static void ring_stop(Ring *r)
{
    if (r->voice != -1) SND_Stop(r->voice);
    for (int i = 0; i < r->cap; i++) ring_free_slot(r, i);
    r->voice = -1;
    for (int c = 0; c < MAXCH; c++) {
        r->avail[c] = 0;
        r->rd[c] = -1;
    }
    r->queued = r->decoding = 0;
    r->wr = 0;
    r->released = -1;
}

/* The voice for the header (0x561050): the ring's positions restarted. */
static int ring_start(Player *pl)
{
    Ring *r = &pl->ring;
    r->channels = pl->hdr.channels;
    r->serial = 0;
    r->queued = r->decoding = 0;
    r->wr = 0;
    r->released = -1;
    r->lead = 0;
    for (int c = 0; c < MAXCH; c++) {
        r->avail[c] = 0;
        r->rd[c] = -1;
    }
    SndPull pull = {ring_pull, ring_done, pl, 0};
    r->voice = SND_PlayStream(&pl->hdr, &pl->params, &pull);
    return r->voice;
}

/* ---- the players ---- */

static Player *player(int h) { return 0 <= h && h < PLAYERS ? s_player[h] : NULL; }   /* 0x5661e0 */

static Entry *reading(Player *pl) { return pl->reading < pl->n ? &pl->q[(int)pl->reading] : NULL; }

/* An entry out of the queue by its id (0x566200), the one being read moving down with it. */
static void entry_remove(Player *pl, int32_t id)
{
    int k = 0;
    for (int i = 0; i < pl->n; i++) {
        if (pl->q[i].id == id) {
            if (pl->q[i].f) vfs_close(pl->q[i].f);
            continue;
        }
        if (pl->reading == i) pl->reading = (int8_t)k;
        pl->q[k++] = pl->q[i];
    }
    pl->n--;
    if (pl->n < pl->reading) pl->reading--;
}

int SND_StreamBufferSize(int queue, int kb) { return ((kb * 3 + 0xc) * 8) + 0xf8 + queue * 0x2c + ((queue + 1) * 0x100); }   /* 0x566d40 */

int SND_StreamOpen(const SndParams *params, int queue, int kb, int bytes)   /* 0x566ae0 -> 0x566850 */
{
    (void)bytes;
    for (int i = 0; i < PLAYERS; i++) {
        if (s_player[i]) continue;
        Player *pl = calloc(1, sizeof *pl);
        if (!pl) return -6;
        pl->ring.cap = (int16_t)kb;   /* ((kb*3 + 12)*8 - 0x60) / 0x18 */
        pl->ring.slot = calloc((size_t)(kb < 1 ? 1 : kb), sizeof *pl->ring.slot);
        pl->ring.voice = -1;
        pl->voice = -1;
        pl->max = (int8_t)(queue < QUEUE ? queue : QUEUE);
        pl->params = *params;
        s_player[i] = pl;
        SND_StreamStop(i);
        return i;
    }
    return -9;
}

int SND_StreamStop(int h)   /* 0x566bc0 */
{
    Player *pl = player(h);
    if (!pl) return -8;
    if (-1 < pl->voice) ring_stop(&pl->ring);
    pl->voice = -1;
    for (int i = 0; i < pl->n; i++)
        if (pl->q[i].f) vfs_close(pl->q[i].f);
    pl->n = pl->reading = 0;
    pl->state = 0;
    pl->has_format = false;
    memset(&pl->hdr, 0, sizeof pl->hdr);
    memset(&pl->next, 0, sizeof pl->next);
    return 0;
}

int SND_StreamClose(int h)   /* 0x566b10 */
{
    Player *pl = player(h);
    if (!pl) return -8;
    SND_StreamStop(h);
    ring_stop(&pl->ring);
    free(pl->ring.slot);
    free(pl);
    s_player[h] = NULL;
    return 0;
}

/* The file's stream from the offset queued (0x566ba0 -> 0x5669d0): its id, negative when the queue is
   full or the file missing. */
int SND_StreamQueueFile(int h, int prebuffer, const char *path, uint32_t offset)
{
    Player *pl = player(h);
    if (!pl) return -8;
    if (pl->max <= pl->n) return -13;
    VfsFile *f = vfs_open(path);
    if (!f) return -1;
    Entry *e = &pl->q[(int)pl->n];
    memset(e, 0, sizeof *e);
    e->f = f;
    e->pos = offset;
    pl->serial += 0x100;
    if (pl->serial < 0) pl->serial = 0;
    e->id = pl->serial | h;
    e->prebuffer = (int16_t)prebuffer;
    pl->n++;
    e->first = e->last = -1;
    return e->id;
}

int SND_StreamFade(int h, int ms, int vol)   /* 0x565f60 */
{
    Player *pl = player(h);
    if (!pl) return -8;
    pl->vol = (int8_t)vol;
    SND_Fade(pl->voice, ms / 10, vol);
    return 0;
}

int SND_StreamSetVolume(int h, int vol)   /* 0x566c90 */
{
    Player *pl = player(h);
    if (!pl) return -8;
    pl->vol = (int8_t)vol;
    SND_SetVolume(pl->voice, vol);
    return 0;
}

int SND_StreamVolume(int h)   /* 0x566cd0: the voice's, else the one set */
{
    Player *pl = player(h);
    if (!pl) return -8;
    int v = SND_Heard(pl->voice);
    return v < 0 ? pl->vol : v;
}

void SND_StreamStatus(int h, int32_t out[3])   /* 0x565ec0: entries, the head's id, ms in the ring */
{
    out[0] = out[1] = out[2] = 0;
    Player *pl = player(h);
    if (!pl) return;
    out[0] = pl->n;
    if (pl->n) {
        out[1] = pl->q[0].id;
        if (pl->hdr.rate) out[2] = (int32_t)((uint32_t)(ring_samples(&pl->ring) * 1000) / pl->hdr.rate);
    }
}

/* An entry's state (0x565d90): 0 nothing read yet, 2 the head, 1 queued behind it, 3 gone; then ms
   played, ms left and ms read ahead. */
void SND_StreamEntry(int32_t id, int32_t out[4])
{
    out[0] = out[1] = out[2] = out[3] = 0;
    if (id < 0) return;
    Player *pl = player(id & 0xff);
    if (!pl) return;
    for (int i = 0; i < pl->n; i++) {
        Entry *e = &pl->q[i];
        if (e->id != id) continue;
        if (e->first < 0) return;
        out[0] = i == 0 ? 2 : 1;
        uint32_t rate = i == 0 ? pl->hdr.rate : pl->next.rate;
        if (!rate) rate = 1;
        out[1] = (int32_t)((uint64_t)(uint32_t)e->played * 1000 / rate);
        out[2] = (int32_t)((uint64_t)(uint32_t)(e->total - e->played) * 1000 / rate);
        out[3] = (int32_t)((uint32_t)(e->buffered * 1000) / rate);
        return;
    }
    out[0] = 3;
}

/* ---- the chunks ---- */

static bool same_format(const SndPatch *a, const SndPatch *b)
{
    return a->channels == b->channels && a->rate == b->rate && a->codec == b->codec && a->t06 == b->t06;
}

/* The header (0x566320): the entry's length and rate; the voice started for it, or, when the format
   differs from the voice's, the player waits (state 2) for the ring to drain. */
static bool chunk_header(Player *pl, const uint8_t *body, uint32_t len)
{
    Entry *e = reading(pl);
    SndPatch h;
    const uint8_t *p = body + 4;   /* "PT\0\0" */
    if (len < 4) return true;
    SND_PatchParse(&p, &h);
    pl->next = h;
    e->total = h.nsamples;
    e->rate = h.rate;
    if (!(pl->has_format && same_format(&pl->hdr, &pl->next))) {
        if (pl->has_format) {
            pl->state = 2;
            return false;
        }
        pl->hdr = pl->next;
        pl->has_format = true;
    }
    if (pl->state != 1) {
        pl->voice = ring_start(pl);
        pl->state = 1;
    }
    return true;
}

/* A data block (0x5664e0): its sample count, then each channel's offset past the table (the streams
   here have one), the channel's data: two words of predictor samples, then EA-XA frames. */
static bool chunk_data(Player *pl, uint8_t *chunk, uint32_t len)
{
    Entry *e = reading(pl);
    int nch = pl->hdr.channels;
    if (len < 0xc + 4u * (uint32_t)nch || MAXCH < nch) {
        free(chunk);
        return true;
    }
    int32_t count;
    memcpy(&count, chunk + 8, 4);
    const uint8_t *ch[MAXCH];
    for (int c = 0; c < nch; c++) {
        int32_t off;
        memcpy(&off, chunk + 0xc + 4 * c, 4);
        ch[c] = chunk + 0xc + nch * 4 + off;
    }
    e->buffered += count;
    e->blocks++;
    int slot = ring_push(&pl->ring, count, ch, chunk);
    if (slot < 0) {
        free(chunk);
        return true;
    }
    e->last = slot;
    if (e->first < 0) e->first = slot;
    return true;
}

/* The entry being read's next chunk taken (0x5665f0 with STREAM_get); false when the player stops
   taking chunks for now. */
static bool take_chunk(Player *pl)
{
    Entry *e = reading(pl);
    if (!e || !e->f) return false;
    uint8_t h[8];
    if (vfs_read_at(e->f, e->pos, h, 8) != 8) {
        pl->reading++;   /* (a stream cut short ends as at its SCEl) */
        return false;
    }
    uint32_t len;
    memcpy(&len, h + 4, 4);
    if (len < 8) len = 8;
    uint32_t tag;
    memcpy(&tag, h, 4);
    uint8_t *chunk = malloc(len);
    if (!chunk || vfs_read_at(e->f, e->pos, chunk, len) != (int64_t)len) {
        free(chunk);
        return false;
    }
    e->pos += len;
    switch (tag) {
    case 0x6c484353:   /* SCHl */
    {
        bool go = chunk_header(pl, chunk + 8, len - 8);
        free(chunk);
        return go;
    }
    case 0x6c434353:   /* SCCl (0x5664a0) */
        if (12 <= len) memcpy(&e->count, chunk + 8, 4);
        free(chunk);
        return true;
    case 0x6c444353:   /* SCDl */
        return chunk_data(pl, chunk, len);
    case 0x6c454353:   /* SCEl (0x5665c0): the next entry is read next */
        free(chunk);
        pl->reading++;
        return true;
    default:
        free(chunk);
        return true;
    }
}

/* Each pass (0x5666f0, on the device's service): a waiting header takes over once the ring is empty
   (the voice restarted with it); then up to the ring's room (10 while idle) chunks are taken. */
void SndStream_Service(void)
{
    for (int i = 0; i < PLAYERS; i++) {
        Player *pl = s_player[i];
        if (!pl || pl->n == 0) continue;
        if (pl->state == 2) {
            if (0 < ring_samples(&pl->ring)) continue;
            pl->hdr = pl->next;
            ring_stop(&pl->ring);
            pl->voice = ring_start(pl);
            pl->state = 1;
        }
        int k = 10;
        if (pl->state == 1) {
            k = ring_room(&pl->ring);
            if (k == 0) continue;
        }
        while (0 < k-- && take_chunk(pl)) {}
    }
}

void SndStream_Shutdown(void)   /* 0x5661c0 */
{
    for (int i = 0; i < PLAYERS; i++) SND_StreamClose(i);
}
