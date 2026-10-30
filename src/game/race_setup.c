/* The race's car descriptions (see race_setup.h). */
#include "game/race_setup.h"
#include "game/car.h"
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
    free(db);
    g_nracecars = 1;
    g_ntrafficcars = 0;
    return true;
}
