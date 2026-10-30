/* The sky's layer description, GameData\Track\Sky\<name>.txt (else default.txt): Sky_LoadText 0x45fa40.
   The file is lowercased and cut into tokens, one per line (leading blanks skipped; a line ends at CR, LF
   or a tab), comments being tokens that start with '#'. It is a list of BEGIN .. END blocks; a block
   counts when its first token (the computer capability) contains the name of the sky level the game runs
   at ("low", "medium", "high", "amazing": 0x5cf3a0 + 0x14 * level) or "all". The next token is the
   layer type; the block's record keeps the type's flags and a cursor at the token after it, which the
   layer's constructor reads on (SkyRecord_next). */
#pragma once
#include <stdint.h>

enum {
    SKYREC_CONFIG = 0x1, SKYREC_BASIC = 0x2, SKYREC_MULTINORMAL = 0x4, SKYREC_MULTIADDITIVE = 0x8,
    SKYREC_MULTIATTENUATE = 0x10, SKYREC_MULTIMODULATE = 0x20, SKYREC_FREECLOUD = 0x40,
    SKYREC_ADDITIVE = 0x100,   /* "additive": no longer supported (prints so) */
    SKYREC_WATER = 0x200,      /* the "...water" types: their type's flag with this */
    SKYREC_UNKNOWN = 0x800,
    SKY_MAXRECORDS = 20,
};

/* A block (12 bytes). */
typedef struct {
    uint32_t flags;      /* +0x0 */
    char *start;         /* +0x4: the first token after the type */
    char *cur;           /* +0x8: the reading cursor */
} SkyRecord;

/* The loaded description (0x64 bytes). */
typedef struct {
    SkyRecord *rec[SKY_MAXRECORDS]; /* +0x00 */
    int32_t count;                  /* +0x50 */
    char *buf;                      /* +0x54: the file */
    char *end;                      /* +0x58 */
    char *cur;                      /* +0x5c */
    int32_t len;                    /* +0x60: the tokens' length */
} SkyText;

SkyText *Sky_LoadText(SkyText *t, const char *name, int level);   /* 0x45fa40 */
void SkyText_free(SkyText *t);
static inline SkyRecord *SkyText_record(SkyText *t, int i) { return t->rec[i]; }   /* 0x45f9c0 */
/* The cursor moved to the next token that isn't a comment. 0x45f9a0 */
void SkyRecord_next(SkyRecord *r);
