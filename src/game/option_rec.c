/* The option records (see option_rec.h). */
#include "game/option_rec.h"
#include "exe.h"
#include "game/car.h"
#include "game/car_drive.h"
#include "game/net.h"
#include "game/race_progress.h"
#include "game/replay_file.h"
#include "support/crt.h"
#include <stdlib.h>
#include <string.h>

/* The options' table (0x5d1e40, 12 bytes an entry: id, address, name) as Options_Parse leaves it: its
   static entries with the immediate moves (C7 05 address value) between 0x4b0db0 and 0x4b4950 over them,
   up to the zero id. The address of an id, 0 for none (the last entry with it, as 0x4b4950 looks). */
enum { TABLE = 0x5d1e40, TABLE_MAX = 0x300 };
static uint32_t s_addr[0x300];
static void table(void)
{
    static bool done;
    if (done) return;
    done = true;
    static uint32_t ids[TABLE_MAX], addrs[TABLE_MAX];
    for (int i = 0; i < TABLE_MAX; i++) {
        ids[i] = exe_u32(TABLE + 12u * (uint32_t)i);
        addrs[i] = exe_u32(TABLE + 12u * (uint32_t)i + 4);
    }
    for (uint32_t a = 0x4b0db0; a + 10 <= 0x4b4950; a++) {
        if ((exe_u32(a) & 0xffff) != 0x05c7) continue;
        uint32_t at = exe_u32(a + 2), v = exe_u32(a + 6);
        if (at < TABLE || at >= TABLE + 12 * TABLE_MAX) continue;
        uint32_t k = (at - TABLE) / 12, f = (at - TABLE) % 12;
        if (f == 0) ids[k] = v;
        else if (f == 4) addrs[k] = v;
        else continue;
        a += 9;
    }
    for (int i = 0; i < TABLE_MAX && ids[i]; i++)
        if (ids[i] < 0x300) s_addr[ids[i]] = addrs[i];
}

static struct { uint32_t va; void *p; int size; } s_bound[8];
static int s_nbound;

void Option_Bind(uint32_t va, void *p, int size)
{
    if (s_nbound < 8) s_bound[s_nbound].va = va, s_bound[s_nbound].p = p, s_bound[s_nbound++].size = size;
}

static void *field(uint32_t va, int *size)
{
    for (int i = 0; i < s_nbound; i++)
        if (s_bound[i].va == va) {
            *size = s_bound[i].size;
            return s_bound[i].p;
        }
    return Replay_Field(va, size);
}

int Option_Size(const int32_t *rec)   /* 0x4b4aa0: in words */
{
    int32_t id = rec[0];
    if (0x21d < id) return 3;
    if (id >= 0x14e) return 2;
    const int32_t *r = rec;
    if (id < 0x142) {
        if (0x10d < id) {
            /* a text from the second word (third above 0x139), its end's word included */
            if (0x139 < id) r++;
            const uint8_t *t = (const uint8_t *)(r + 1);
            size_t n = strlen((const char *)t);
            r += (n + 1 + 3) / 4 - 1;
        }
        return (int)(r - rec) + 2;
    }
    if (0x146 < id) r++;
    return (int)(r - rec) + 2 + r[1];
}

void Option_Apply(const int32_t *rec)
{
    table();
    int32_t id = rec[0];
    if (id < 0 || id >= 0x300) return;
    if (0x14e <= id && id < 0x21e) {
        /* (a binding: 0x5e9130's word and its type's handler, 0x5cc5d0. The port's bindings are fe.txt's,
           read by controls.c; no record the port makes or receives carries one) */
        return;
    }
    uint32_t va = s_addr[id];
    if (!va) return;
    int size = 0;
    if (0x21d < id) {   /* a car's word: the index's description */
        if (rec[1] < 0 || rec[1] >= CAR_MAX) return;
        void *p = field(va + (uint32_t)rec[1] * CARDESC_SIZE, &size);
        if (p && size >= 4) memcpy(p, &rec[2], 4);
        return;
    }
    if (id < 0x142) {
        if (id < 0x10e) {
            void *p = field(va, &size);
            if (p && size >= 4) memcpy(p, &rec[1], 4);
            return;
        }
        if (0x139 < id) {
            if (rec[1] < 0 || rec[1] >= CAR_MAX) return;
            va += (uint32_t)rec[1] * CARDESC_SIZE;
            rec++;
        }
        char *p = field(va, &size);
        if (!p || size < 1) return;
        const char *t = (const char *)(rec + 1);
        int n = 0;
        while (n < size - 1 && t[n]) n++;
        memcpy(p, t, (size_t)n);
        p[n] = 0;
        return;
    }
    if (0x146 < id) {
        if (rec[1] < 0 || rec[1] >= CAR_MAX) return;
        va += (uint32_t)rec[1] * CARDESC_SIZE;
        rec++;
    }
    int32_t *p = field(va, &size);
    for (int i = 0; p && i < rec[1] && 4 * (i + 1) <= size; i++) p[i] = rec[2 + i];
}

void Option_PutInt(int32_t **p, int id, int32_t v)
{
    *(*p)++ = id;
    *(*p)++ = v;
}

void Option_PutCar(int32_t **p, int id, int car, int32_t v)
{
    *(*p)++ = id;
    *(*p)++ = car;
    *(*p)++ = v;
}

void Option_PutCarText(int32_t **p, int id, int car, const char *text)
{
    *(*p)++ = id;
    *(*p)++ = car;
    size_t n = strlen(text) + 1, w = (n + 3) / 4;
    memset(*p, 0, w * 4);
    memcpy(*p, text, n);
    *p += w;
}

void Option_PutCarWords(int32_t **p, int id, int car, int n, const int32_t *v)
{
    *(*p)++ = id;
    *(*p)++ = car;
    *(*p)++ = n;
    for (int i = 0; i < n; i++) *(*p)++ = v[i];
}

/* A car description's records as the front end writes a racer's (0x4ee430, 0x4eb230): its texts, its
   words and its lists; not GEOMCONFIG (0x14a), which no writer gives, nor the lobby's FEMULTIPLAYERCARID,
   READY and LOADINGAMOUNT (0x226, 0x255, 0x256), which the screens send themselves. The port reads the
   values back from the description its set-up filled. */
static int32_t *car_records(int32_t *p, int car, const uint8_t *d)
{
    table();
    for (int id = 0x13b; id <= 0x141; id++)
        if (s_addr[id]) Option_PutCarText(&p, id, car, (const char *)d + (s_addr[id] - 0x65808cu));
    static const struct { int16_t id, n; } lists[] = {{0x148, 4}, {0x149, 4}, {0x14b, 0x14}, {0x14c, 0x14}, {0x14d, 10}};
    for (size_t k = 0; k < sizeof lists / sizeof *lists; k++) {
        int32_t v[0x14];
        memcpy(v, d + (s_addr[lists[k].id] - 0x65808cu), 4u * (size_t)lists[k].n);
        Option_PutCarWords(&p, lists[k].id, car, lists[k].n, v);
    }
    for (int id = 0x21f; id <= 0x254; id++) {
        if (!s_addr[id] || id == 0x226) continue;
        int32_t v;
        memcpy(&v, d + (s_addr[id] - 0x65808cu), 4);
        Option_PutCar(&p, id, car, v);
    }
    return p;
}

/* The race's own records the front end writes (0x4eab10, 0x4ec480, 0x4ec590, 0x4ecc60): the words below
   0x2f and the texts 0x10e .. 0x125 the port keeps, from its fields. */
static int32_t *race_records(int32_t *p)
{
    table();
    for (int id = 1; id < 0x2f; id++) {
        int size = 0;
        int32_t *f = s_addr[id] ? Replay_Field(s_addr[id], &size) : NULL;
        if (f && size >= 4) Option_PutInt(&p, id, *f);
    }
    for (int id = 0x10e; id < 0x126; id++) {
        int size = 0;
        const char *f = s_addr[id] ? Replay_Field(s_addr[id], &size) : NULL;
        if (!f || size < 1) continue;
        *p++ = id;
        size_t n = strnlen(f, (size_t)size - 1) + 1, w = (n + 3) / 4;
        memset(p, 0, w * 4);
        memcpy(p, f, n - 1);
        p += w;
    }
    return p;
}

void RaceSetup_Network(void)
{
    if (!Net_InGame()) return;
    /* The front end's records of a network race (0x4eab10, 0x4ec590): LOCAL_CAR my number, COMM_MODE 4,
       the eight seats and no opponents (NUMCARS 8 + 0); the local car's description with my number. */
    g_race_players = 4;
    g_race_inputs = 8;
    g_nracecars = 8;
    g_ntrafficcars = 0;
    int32_t *list = calloc(0x2000, sizeof *list), *p = list;
    if (!list) return;
    p = race_records(p);
    p = car_records(p, g_net->me, g_cardesc[0]);
    *p = 0;
    /* (Race_Configure: the block cleared, the records applied, the own car's at my number) */
    memset(g_cardesc, 0, sizeof g_cardesc);
    for (const int32_t *r = list; *r; r += Option_Size(r)) Option_Apply(r);
    g_race_local = g_net->me;

    Net_StartRace();   /* 0x48d300 */
    int seats = 0, want = 8, mine = 0;
    while (Net_InGame() && want != seats) {
        uint8_t racing = g_net->racing;
        int me = g_net->me;
        seats = 0;
        mine = 0;
        /* The seats: the racing players' in the list's order */
        NETLST_EACH(&g_net->plist, n) {
            NetPlayer *pl = n->p;
            if (!(racing & (1u << (pl->number & 0x1f)))) {
                pl->car = -1;
            } else {
                pl->car = seats;
                if (pl->number == me) mine = seats;
                seats++;
            }
        }
        for (const int32_t *r = list; *r; r += Option_Size(r)) {
            int32_t id = r[0];
            bool car = !(id < 0x21e && (0x14d < id || (id < 0x147 && (0x141 < id || id < 0x13a))));
            if (!car) {
                /* the race's: the host's sent (its words below 0x2f, texts 0x10e .. 0x125, lists 0x142 ..
                   0x145), the others the player's own */
                if (id < 0x146 && (0x141 < id || (id < 0x126 && (0x10d < id || id < 0x2f)))) {
                    if (Net_IsFirst()) Net_AddOption(r);
                } else {
                    Option_Apply(r);
                }
            } else if (id != 0x247 && (r[1] == me || (Net_IsFirst() && 7 < r[1]))) {
                /* a car's: mine to my seat, the host's opponents after the seats */
                int32_t c[0x60];
                int n = Option_Size(r);
                if (n > 0x60) continue;
                memcpy(c, r, 4u * (size_t)n);
                c[1] = c[1] == me ? mine : c[1] - 8 + seats;
                Net_AddOption(c);
            }
        }
        if (Net_IsFirst()) {
            /* The grid: each seat's STARTINGPOS, a random place of those left */
            int8_t left[8] = {0, 1, 2, 3, 4, 5, 6, 7};
            int k = seats;
            for (int i = 0; i < seats; i++) {
                int j = CRT_rand() % k;
                int32_t c[3] = {0x247, i, left[j]};
                Net_AddOption(c);
                k--;
                for (; j < k; j++) left[j] = left[j + 1];
            }
        }
        Net_FlushOptions();   /* 0x48d370 */
        Net_Sync();           /* 0x48d5d0 */
        g_race_local = mine;
        g_nracecars += seats - 8;
        g_race_inputs = seats;
        want = 0;
        uint8_t in = Net_InGame() ? g_net->racing : 0;
        for (int i = 0; i < 8; i++)
            if (in & (1u << i)) want++;
    }
    free(list);
}
