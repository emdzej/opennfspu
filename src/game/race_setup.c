/* The race's car descriptions (see race_setup.h). */
#include "game/race_setup.h"
#include "game/car.h"
#include "game/car_damage.h"
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
    free(db);
    /* Damage on (0x657a2c, the front end's damage option; a stand-in until the options are ported). */
    g_race_damage = 1;
    g_nracecars = 1;
    g_ntrafficcars = 0;
    return true;
}
