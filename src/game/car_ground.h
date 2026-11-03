/* The cars' contact with the ground (0x4999e0 and its helpers, 0x493b30-0x49bbf0): the wheels' points
   on the track's polygons (game/wtree.h), the suspension's travel and pushes, the body's pitch and roll,
   and the rate the simulation runs a car's tasks at (0x414b30).

   Per wheel (0xc4 apart, from the tracker at +0x7bc): +0x30 the point the wheel is looked for at,
   +0x3c the contact, +0x48 the ground's normal there, +0x54 the spring's push (the car's axes), +0x60 the
   contact's height over the point, +0x64 the compression, +0x68 the push's height part, +0x6c the
   camber, +0x70 +0x74 +0x78 the surface noise (from, to, its phase), +0x80 off the
   ground, +0x84 the tyre bump's phase, +0x88 the surface. */
#pragma once
#include "game/car.h"
#include <stdbool.h>

/* The rate of a car's task by how far it is (mode: the task's table row): updates per second, ticks
   between updates, the time step; true on the ticks it runs (salt * id + tick / 2 matching the level's
   mask). Every tick for the first 16. 0x414b30 */
bool Sim_TaskRate(int salt, int mode, float dist, int id, int *rate, int *ticks, float *dt, bool full);

/* 0 on the polygon's plane (within 4 of the car's height), 1 without a surface, 2 far from it. 0x4943f0 */
int CarSim_ContactMode(Car *c, WSurface *s, const float p[3]);
/* The surface's roughness under the wheel: a random height eased in over a period by the surface kind.
   0x4996a0 */
float CarSim_SurfaceNoise(Car *c, int wheel);
/* The tyres' bump amplitude from their parts and damage. 0x493b30 */
float CarSim_TyreBump(Car *c);
/* The four wheels on the ground: their contacts and pushes, the ground's normal into out, the body's
   pitch and roll; the mean contact height. 0x4999e0 */
float CarSim_Ground(Car *c, float out[3]);
/* The force task (even ticks, priority 0x1e): the ground contact, gravity, landing on the ground and
   bouncing off it. 0x49bcb0 */
void CarSim_Forces(void *car);
/* The road tracking task (even ticks, priority 6): the distance to the players, the road node, the
   body's surface tracker and the car's own ground plane, the surface, the heading against the road.
   0x49aee0 */
void CarSim_Track(void *car);
/* The road node nearest p, from the tracker's last: in its segment and in the junction's other one (the
   tracker's +0x4, Car_SetBranch), the nearer by x and z; +0x8 and +0x9 say whether it changed. 0x4743a0 */
void WSurface_TrackNode(WSurface *s, const float p[3]);

/* The ride heights over the plane through `at` of normal n: +0x424 the lowest, +0x428 the mean (the
   corners' for an upright car, the body points' otherwise). 0x49a870 */
void CarSim_RideHeights(Car *c, const float n[3], const float at[3]);
/* The car's ground plane from the body's polygon and the road's axes at its node. 0x499490 */
void CarSim_BodyPlane(Car *c);
/* The body's tracker on its road node and polygon. 0x499460 */
void CarSim_TrackBody(Car *c);
/* A traffic object's ground: the body's polygon, its normal into n, the ride heights and the corner
   points; the ground's height under the car (-2097152000 on a wall). 0x499830 */
float CarSim_TrafficGround(Car *c, float n[3]);
/* The corner points of a traffic object, its half length scaled by fwd (0x4a12f0's, 0x5b24e8). */
void CarSim_TrafficCorners(Car *c, float fwd);
