/* The sky (0x458000-0x460000), first part: the per-track sky configuration GameData\Track\Sky\<name>.bin
   (a 0x71dc-byte block, "SkyCnfig", version 9; older versions are brought up to 9 on load), its weather
   slots (the light environment the renderer and the cars light with) and the sky's texture file
   Sky\<name>.fsh. The sky's layers (Sky\<name>.txt, SkyLevel_* / Scroller_* / SkyMapSource_*) and its
   draw are pending (docs/re/sky.md). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { SKY_CONFIG_SIZE = 0x71dc, SKY_CONFIG_VERSION = 9, SKY_SLOT_BASE = 0xce8, SKY_SLOT_SIZE = 0x3c };

/* The configuration object (0x10c bytes; global 0x627e18). */
typedef struct {
    uint8_t flag;          /* +0x000: 0 */
    char path[0x103];      /* +0x001: the file it came from */
    uint8_t *data;         /* +0x104: the SKY_CONFIG_SIZE block */
    int32_t f108;          /* +0x108: -1 */
} SkyConfig;

/* Creates the global configuration from Sky\<name>.bin, else Sky\default.bin, else an empty block
   (0x4594d0 + Sky_LoadConfig 0x459530). */
SkyConfig *Sky_CreateConfig(const char *name);
SkyConfig *Sky_Config(void);                     /* 0x627e18 */
void Sky_FreeConfig(void);
/* Brings the block from `version` up to version 9 (0x4595d0): a new block with the old contents, the
   fields later versions added set to their defaults or copied from older fields. */
void SkyConfig_Upgrade(SkyConfig *c, int version);

/* The light environment (global 0x627e1c): the weather slot of the race (0x657d9c), or the nearest lower
   one that is set (a slot's first word non-zero), else slot 0. 0x459820 / 0x459890 */
void Sky_CreateLight(int weather);
void Sky_FreeLight(void);
const uint8_t *Sky_LightSlot(void);              /* *0x627e1c: SKY_SLOT_SIZE bytes in the configuration */
int Sky_SlotFor(int weather);                    /* the slot index the above picks (0x4598f0) */

/* Sky\<name>.fsh (else Sky\default.fsh) as a file (global 0x627e50), and the sky objects made from it
   (pending). `reload` frees the previous one first. Sky_LoadFsh 0x45da90 */
void Sky_LoadFsh(const char *name, bool reload);
const uint8_t *Sky_Fsh(void);                    /* 0x627e50 */
