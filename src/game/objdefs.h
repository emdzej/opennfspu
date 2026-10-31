/* The track's object definitions (WObjectDefs, from GameData\Track\animdefs.txt): per tagged object type
   its collision kind (cylinder, box, smackable), mass, radius, sound kind, its animation's pieces and
   speeds. Read by WMidGround_ParseObjectDefs (0x47ea60) into the object at 0x628ba0 (0x47ed00): +0 the
   definitions (a vector of 0x8c-byte records), +4 the audio sections'. The text is read
   lowercased with everything but letters, digits and ";{}=:" dropped, and "//" comments cut. */
#pragma once
#include <stdint.h>

enum { WOBJ_MAXPIECES = 16, WOBJ_MAXSPEEDS = 4 };

/* A piece of an animation (0x14 bytes, WMidGround_ParsePiece 0x47e680). */
typedef struct {
    uint32_t basegroup;   /* +0x00: a four-character tag (upper case) */
    uint32_t quatgroup;   /* +0x04 */
    uint32_t vertgroup;   /* +0x08 */
    uint32_t flags;       /* +0x0c: 1 LOOP, 0x10 LANDFLAT, 8 DIRECTED (0x5b45a4, 0x5b45b0, 0x5b45ac) */
    float playbackrate;   /* +0x10: 1 */
} WObjectPiece;

/* An object definition (0x8c bytes, WMidGround_ParseAnim 0x47dbb0). */
typedef struct {
    uint32_t tag;                         /* +0x00: the type (four characters, upper case) */
    float mass;                           /* +0x04 */
    float radius;                         /* +0x08: 0.5 */
    int32_t audiotype;                    /* +0x0c: kAudSign 0 .. kAudRock 12 */
    int32_t collidetype;                  /* +0x10: 1 kCylinder, 2 kBox, 3 kSmackable, 0 none */
    uint8_t drivethru;                    /* +0x14 */
    uint8_t triggerable;                  /* +0x15 */
    uint8_t dynamiclight;                 /* +0x16 */
    int32_t npieces;                      /* +0x18 */
    WObjectPiece *piece[WOBJ_MAXPIECES];  /* +0x1c */
    int32_t nspeeds;                      /* +0x5c */
    float speed[WOBJ_MAXSPEEDS];          /* +0x60 */
    int32_t speedpieces[WOBJ_MAXSPEEDS];  /* +0x70: the pieces after each speed (the first's before any) */
    float velocityfactor;                 /* +0x80: the file's value / 1000 */
    int32_t frametickoffset;              /* +0x84 */
    int32_t tickspervertframe;            /* +0x88: 8 */
} WObjectDef;

/* A sound of an object type (0x18 bytes, WMidGround_ParseSound 0x47d840): the track.viv entry, its
   range, then the values the type's sound records take as their inner radius (FadeIn), period
   (Trigger), the period's random part (FadeOut), repeats and their random part, volume (percent). */
typedef struct {
    int16_t patch;        /* +0x00: PatchID */
    float range;          /* +0x04 */
    float fadein;         /* +0x08 */
    int16_t trigger;      /* +0x0c */
    int16_t fadeout;      /* +0x0e */
    int16_t minrepeat;    /* +0x10 */
    int16_t randomrepeat; /* +0x12 */
    int16_t volume;       /* +0x14 */
} WSoundDef;

enum { WAUD_MAXSOUNDS = 8 };
/* An object type's sounds (0x28 bytes, WMidGround_ParseAudio 0x47d570). */
typedef struct {
    uint32_t tag;         /* +0x00 */
    int32_t n;            /* +0x04 */
    WSoundDef sound[WAUD_MAXSOUNDS];   /* (the original keeps pointers at +0x08) */
} WAudioDef;

/* The type's sounds, NULL for none. 0x47ecb0 */
const WAudioDef *WObjectDefs_findaudio(uint32_t type);

/* Reads the definitions from path (0x47ed00 makes the holder, 0x47ea60 parses). */
void WMidGround_LoadObjectDefs(const char *path);
/* The definition of a type; the first definition for an unknown type (as the original), NULL for type
   0 or with none loaded. 0x47ec50 */
const WObjectDef *WObjectDefs_find(uint32_t type);
int WObjectDefs_count(void);
const WObjectDef *WObjectDefs_at(int i);   /* port: the i-th, for the tests */
void WObjectDefs_free(void);
