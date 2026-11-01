/* The race's traffic objects (0x4a0d60 .. 0x4a16a0): the cars of CLASS 0x80. The ones this port makes are
   the knockable objects' (game/smack.h): one traffic "car" of kind 1 per body of the instance bundle, its
   shape from the body's points and its mass, its position and rotation the ones the body follows. It
   sleeps until a race car comes near, then falls, slides and tumbles in the car simulation like any car
   (game/car_ground.h's traffic branches) until it comes to rest. The road traffic's vehicles (kind 0,
   the 0x4a12f0 branch on a road node) aren't ported. */
#pragma once
#include "game/car.h"

enum {
    TRAFFIC_MAX = 135,          /* the descriptions' room (0x628fc0 .. 0x65807c) */
    TRAFFICDESC_SIZE = 0x594,
};

/* The traffic descriptions (0x628fc0, 0x594 bytes each; the race cars' layout, then +0x580 the body's
   index and +0x590). */
extern uint8_t g_trafficdesc[TRAFFIC_MAX][TRAFFICDESC_SIZE];

/* A description per body of the instance bundle: TYPE 0xb, kind 1, CLASS 0x80, the name "356B";
   g_ntrafficcars their count. 0x4a15d0 */
void Traffic_SmackDescs(void);
/* The object's shape (mass, sound kind, body points, half box) from its body. 0x4a1060 */
void Traffic_Shape(Car *c, uint32_t id);
/* The object on its body's placing, asleep: its rotation and position followed by the body, the
   ground's plane, height and ride heights, the corner points. 0x4a12f0 */
void Traffic_Place(Car *c);
/* The traffic task (+0xe1c, even ticks): asleep until a race car is near; awake, the motion damped
   until it is still, then asleep again. 0x4a0e10 */
void Traffic_Drive(void *car);
