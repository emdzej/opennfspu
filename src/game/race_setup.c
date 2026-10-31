/* The race's car descriptions (see race_setup.h). */
#include "game/race_setup.h"
#include "game/car.h"
#include "game/car_damage.h"
#include "game/hud_config.h"
#include "game/race_progress.h"
#include "vfs.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { FECAR_SIZE = 0x670 };

static void put_str(uint8_t *desc, int off, const char *s)
{
    memset(desc + off, 0, 0x40);
    strncpy((char *)desc + off, s, 0x3f);
}

static void put_i(uint8_t *desc, int off, int32_t v) { memcpy(desc + off, &v, 4); }

bool RaceSetup_SingleCar(const char *name)
{
    size_t n = 0;
    uint8_t *db = vfs_read_all("FEData\\Data\\nfs5.car", &n);
    if (!db) return false;
    const uint8_t *rec = NULL;
    for (size_t i = 0; i + FECAR_SIZE <= n; i += FECAR_SIZE) {
        const char *file = (const char *)db + i + 0x7f, *spec = (const char *)db + i + 0xb1;
        if (!strcasecmp(file, name) || !strcasecmp(spec, name)) {
            rec = db + i;
            break;
        }
    }
    if (!rec) {
        free(db);
        return false;
    }
    uint8_t *d = g_cardesc[0];
    memset(d, 0, CARDESC_SIZE);
    /* As 0x4ee430 writes them (the front end's choices it takes from its screens are their defaults). */
    put_i(d, 0x150, 0);                                   /* DRIVER: rand() & 7 in the original */
    put_str(d, 0x17c, "");                                /* DRIVERNAME */
    put_str(d, 0x534, (const char *)rec + 0x34);          /* FEMULTIPLAYERCARNAME */
    put_str(d, 0x1c0, "");                                /* LICENSE */
    put_str(d, 0x228, (const char *)rec + 0x7f);          /* FILENAME */
    put_str(d, 0x268, (const char *)rec + 0xb1);          /* SPECNAME */
    put_str(d, 0x2a8, (const char *)rec + 0x115);         /* AISPECNAME */
    put_str(d, 0x2e8, (const char *)rec + 0x179);         /* SOUNDNAME */
    int16_t body;
    memcpy(&body, rec + 0x1c0, 2);
    put_i(d, 0x328, body);                                /* BODYSTYLE */
    put_i(d, 0x0, 0xb);                                   /* TYPE */
    put_i(d, 0x8, 1);                                     /* CLASS: the human player */
    put_i(d, 0x200, 0xe);                                 /* SPEECHCOLOUR */
    for (int k = 0; k < 20; k++) put_i(d, 0x28 + 4 * k, 0);   /* PARTINDEX: the front end's owned-car parts */
    /* The setup screen's sliders (FECarSetup 0x4f06f0) at their middle, as 1/65536ths (the brake bias
       times 100); the gear and final ratio offsets and the toe at none. A stand-in until the front end's
       defaults are ported. */
    put_i(d, 0x114, 0x8000);                              /* FRONTDOWNFORCE */
    put_i(d, 0x118, 0x8000);                              /* REARDOWNFORCE */
    put_i(d, 0x11c, 0x8000 * 100);                        /* BRAKEBIAS */
    put_i(d, 0x120, 0x8000);                              /* RIDEHEIGHT */
    put_i(d, 0x124, 0x8000);                              /* SUSPENSIONTRAVEL */
    put_i(d, 0x130, 0x8000);                              /* SUSPENSIONSTIFFNESS */
    put_i(d, 0x134, 0x8000);                              /* SHOCKRATE */
    put_i(d, 0x13c, 0x8000);                              /* FRONTTIREPRESSURE */
    put_i(d, 0x140, 0x8000);                              /* REARTIREPRESSURE */
    put_i(d, 0xc, 1);                                     /* TRANSMISSION: automatic */
    /* The colours (+0x218 COLORCONFIG: body, stripe, interior, wheels; the car viewer's silver), no race
       number, colour package 0. */
    static const uint32_t colours[4] = {0xffb8bcc0u, 0xff202020u, 0xff303030u, 0xffc0c0c0u};
    memcpy(d + 0x218, colours, sizeof colours);
    put_i(d, 0x510, -1);                                  /* RACENUMBER */
    put_i(d, 0x514, 0);                                   /* STRIPEPACKAGE: the colour scheme */
    /* The HUD's dials (0x4eadb0, the units option km/h). */
    int16_t g[0x12];
    memcpy(g, rec + 0x300, sizeof g);
    HudDials *h = &g_hud_dials;
    if (!g_hud_imperial) h->speedo_kmh = g[0], h->speedo_start = g[3], h->speedo_deg = g[4], h->speedo_units = g[1];
    else h->speedo_mph = g[6], h->speedo_start = g[9], h->speedo_deg = g[10], h->speedo_units = g[7];
    h->tach = g[12], h->tach_start = g[15], h->tach_deg = g[16], h->tach_units = g[13];
    h->needle = g[14], h->pivot = g[17];
    free(db);
    /* Damage on (0x657a2c, the front end's damage option; a stand-in until the options are ported). */
    g_race_damage = 1;
    g_race_laps = 2;   /* 0x6573f0: two laps (the front end's choice; a stand-in) */
    g_nracecars = 1;
    g_ntrafficcars = 0;
    return true;
}

bool RaceSetup_Opponents(int n)
{
    size_t size = 0;
    uint8_t *per = vfs_read_all("FEData\\Data\\nfs5.per", &size);
    if (!per) return false;
    int nper = (int)(size / 0x2c);
    if (CAR_MAX <= n) n = CAR_MAX - 1;
    if (nper < n) n = nper;
    for (int i = 1; i <= n; i++) {
        uint8_t *d = g_cardesc[i];
        memcpy(d, g_cardesc[0], CARDESC_SIZE);
        const uint8_t *r = per + 0x2c * (i - 1);
        int16_t id;
        memcpy(&id, r, 2);
        float k = 1.0f;   /* the event's difficulty (its record's +0x3cc, the front end's) */
        float sk[8];
        memcpy(sk, r + 0xc, sizeof sk);
        put_i(d, 0x8, 2);                                  /* CLASS: an opponent */
        put_i(d, 0x14c, i);                                /* PERSONALITY */
        put_i(d, 0x150, i);                                /* DRIVER */
        float v[8] = {k * sk[0], k * sk[2], k * sk[4], k * sk[6], sk[1], sk[3], sk[5], sk[7]};
        memcpy(d + 0x154, v, sizeof v);                    /* SKILLSLOW .. SKILLRANGEDEFAULT */
        put_i(d, 0x174, n - i);                            /* STARTINGPOS */
        put_str(d, 0x17c, "");                             /* DRIVERNAME (the record's string) */
        (void)id;
    }
    put_i(g_cardesc[0], 0x174, n);
    free(per);
    g_nracecars = n + 1;
    return true;
}
