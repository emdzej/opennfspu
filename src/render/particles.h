/* The particle systems (RParticleSystem, RGenericParticleSystem, RGenericMovableParticleSystem;
   0x454100 .. 0x455f00): the parameter sets of GameData\Render\particle.dat and carpart.dat by their
   tag, and the live systems by their emitter, each stepped 64 times a second while something asks
   for it within the last 100 steps, drawn as billboards (or as streaks) of the particle.fsh art. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

struct Texture;

/* A parameter set (0x7c bytes after its tag in the .dat files). */
typedef struct {
    uint32_t flags;          /* +0x00: 1 bounce on the emitter's ground height, 2 one shot (started by a
                                reset), 4 drawn as streaks along the velocity */
    uint32_t count;          /* +0x04: the particles at most (by the special effects level) */
    float emit[3];           /* +0x08: x, z the spread of the start about the emitter; y the speed along
                                the emitter's second axis */
    float rand[3];           /* +0x14: the speed's spread along the first axis, the second, the third */
    float friction;          /* +0x20: the horizontal speed kept each step */
    uint32_t matid;          /* +0x24: the texture's name in particle.fsh (byte-swapped) */
    uint32_t blend;          /* +0x28: the transparency (render state 0x38) */
    int32_t zbias;           /* +0x2c: the depth bias at a squared distance of 1 */
    uint8_t nframes, framespeed;
    int8_t nstates;          /* +0x32: the colours used (2 .. 4 blend through them, else the first) */
    uint8_t pad;
    float overallsize, overallspeed;   /* +0x34 */
    float startsize, endsize;          /* +0x3c: (x 10) */
    uint32_t lifetime;       /* +0x44: steps */
    uint32_t periodon, periodoff, prime;   /* +0x48: the emitting and pausing steps, the steps run at once */
    float weight, randweight;          /* +0x54: the fall per step (x 0.005) and its spread */
    float startrot, endrot, randrot;   /* +0x5c: the turn's spread per life (degrees) */
    float volscale;          /* +0x68: the share of the view distance it emits fully within */
    uint32_t colours[4];     /* +0x6c: ARGB */
} PartParams;
_Static_assert(sizeof(PartParams) == 0x7c, "particle.dat record");

/* The emitter a system follows ('ef' type 3 +8, 0x30 bytes): its place (x, y, z and the ground height
   of the bouncing kinds), its first and second axes, the parameter set's tag. */
typedef struct {
    float pos[4];
    float axis1[3], axis2[3];
    uint32_t tag;
    uint32_t pad;
} PartEmitter;

/* A particle (the pool's 0x2c-byte nodes): its place, speed, age in steps, fall per step and turn. */
typedef struct Particle {
    struct Particle *next;
    float pos[4];
    float vel[3];
    float age;
    float fall;
    float spin;      /* the turn over its life */
} Particle;

typedef struct PartSystem {
    int life;                    /* +0x08: 100 when asked for, less one a step; at 0 it goes */
    uint32_t ticks;              /* +0x0c: steps into the period */
    float dist2;                 /* +0x10 */
    float emitted, due;          /* +0x14, +0x18 */
    Particle *list;              /* +0x20 */
    const PartParams *p;         /* +0x24 */
    const PartEmitter *emitter;  /* +0x28 */
    bool active;                 /* +0x2c */
    uint8_t flags;               /* +0x2e: 1 the particles about the emitter, 2 no size (not drawn) */
    float side[3];               /* +0x30: the emitter's third axis */
    struct Texture *tex;         /* +0x3c */
    float rate;                  /* +0x40: particles per step */
    float k44, k48;              /* +0x44, +0x48: 1 */
    const PartEmitter *origin;   /* +0x4c */
    int alive, max;              /* +0x50, +0x54 */
    bool movable;                /* (vtable 0x5b3e24) */
    PartEmitter own;             /* +0x58: the movable kind's own copy */
} PartSystem;

/* A system on the emitter at the squared distance d2: generic (primed at once) or movable (its own copy of
   the emitter, then primed). RGenericParticleSystem 0x454f50 / 0x454120, RGenericMovableParticleSystem
   0x454f80 */
PartSystem *PartSystem_New(float d2, const PartEmitter *e, bool movable);
void PartSystem_Free(PartSystem *s);    /* 0x454300 */
void PartSystem_Step(PartSystem *s);    /* slot 2, 0x454b80 */
void PartSystem_Draw(PartSystem *s);    /* slot 1, 0x454380 */
void PartSystem_Reset(PartSystem *s);   /* slot 3, 0x4542f0: emitting from the period's start */
/* Another parameter set (written into the emitter's tag); the particles go if its count differs.
   0x454a10 */
void PartSystem_SetTag(PartSystem *s, uint32_t tag);

extern int g_render_particles;   /* 0x6197e0: the special effects' share (0, 100, 200, 256 of 256) */

/* The render's random numbers (0x5ceae0, 0x5ceae4): seeded by the race (0x44fbd0), 16 bits each. */
void Render_SeedRand(uint32_t seed);
uint32_t Render_Rand(void);

/* The parameter sets and the systems' map (0x455180 from Render_RaceInit), all gone (0x4552c0). */
void Particles_Init(void);
void Particles_Free(void);
/* The steps due by the 128 Hz timer, one per two of its ticks, at most six, none while paused (Render_Frame
   0x468900 with Particles_Tick 0x4550b0). */
void Particles_Frame(bool paused);
/* The systems asked for since the last step (0x455130, in the world's pass). */
void Particles_Draw(void);
/* An 'ef' type 3 emitter in view at the squared distance d2 (RDrawArticleBundle_v3 0x450be0): its
   system made if new (RGenericParticleSystem, primed), kept for 100 more steps. 0x455020 */
void Particles_Ask(const PartEmitter *e, float d2);
/* The same with a movable system (its own copy of the emitter: 0x455020 with the movable creator
   0x5b3cd8), NULL with the effects off. */
PartSystem *Particles_AskMovable(const PartEmitter *e, float d2);
/* The movable system's emitter placed at pos (x, y, z, w). Slot 4, 0x455000 */
void PartSystem_Move(PartSystem *s, const float pos[4]);
