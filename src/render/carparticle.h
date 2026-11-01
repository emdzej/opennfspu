/* A car's particles (R3DCarParticle, 0x43ed40 .. 0x43f9b0): eleven systems on emitter records of their
   own, tagged SMRT x 4 (skid smoke, a wheel each), CSPK (the hits' sparks, about the car), WTSP x 2 (the
   rear wheels' spray on a wet road), RDDT x 4 (dirt by the wheel's surface: dirt, grass, snow). The cars
   drawn in a frame are queued (0x6115c0); the queue's systems step with the simulation's odd ticks and
   draw after the world's particles. */
#pragma once
#include "game/car.h"
#include "render/particles.h"

typedef struct CarParticles {
    PartEmitter rec[11];    /* +0x04: 0x210 bytes */
    PartSystem *sys[11];    /* +0x08 */
    Car *car;               /* +0x34 */
} CarParticles;

/* R3DCar_ctor's (0x43ed40): the systems primed at the world's origin, then off; the sparks about the
   car's place. */
CarParticles *CarParticles_New(Car *car);
void CarParticles_Free(CarParticles *cp);   /* 0x43ee50 */
/* The car drawn this frame (0x43eeb0 from R3DCar_v7): each system on or off by its wheel, the queue. */
void CarParticles_Update(CarParticles *cp, bool on);
/* Sparks at a hit (0x43f3f0): from p relative to the car, along the car's velocity v. */
void CarParticles_Sparks(CarParticles *cp, const float p[3], const float v[3]);
/* The queue's systems drawn (0x43f610); stepped, the queue emptied (R3DCarParticle_Tick 0x43f590). */
void CarParticles_DrawAll(void);
void CarParticles_TickAll(void);
/* The queue emptied (0x43ec30 / 0x43ecd0, with the race). */
void CarParticles_Clear(void);
