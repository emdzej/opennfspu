/* The front end's 3D: its objects (FE3DObject, vtable 0x5b6808: an R3DCar with its own car state, a car
   of GameData\CarModel or a room model of FEData\Models) and the 3D rooms behind the screens (FE3DRoom,
   vtable 0x5b6eac, made by name from the screen's "room" attribute through the factory map 0x6909c0,
   0x4e6f10; the application holds the current one at +0xe8 and draws it first, in the 3D camera +0xfc,
   0x4d2980).

   Ported so far: the garage (FEGarage3DRoom, ID_GARAGE: a spinning room, FESpinning3DRoom 0x4e9570, of
   the model "garage"), its car (set by the screen's FECar3DControl of position type 0, 0x4bf9a0), their
   placement, spin and light (0x4e9690, 0x4e7b60), the car's floor shadow (FEGarage3DRoom_v15 0x4e9380);
   the dealership, factory, track select and victory rooms. The car control's doors, lids and roof are in
   fecar3d.c.

   docs/re/frontend.md has the rooms' notes and the port's one deviation in their draw. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "render/r3dcar.h"

typedef struct FE3DObject {
    R3DCarState state;        /* +0x08: the car state (the original's is a whole car object, 0x1254 bytes) */
    R3DCarSpec spec;          /* +0x0c */
    R3DCar car;               /* +0x10 */
    int kind;                 /* +0x14: 0 a car, 2 a room model */
    char name[64];
    float damage[10];         /* (the car object's +0x4e4: the ten damage zones the state points at) */
} FE3DObject;

/* 0x4d0bb0: the model name of its kind's folder; the four colours (body, stripe, interior, wheels), the
   colour package, the race number (< 0 none), the body style; options NULL = the style's. */
FE3DObject *FE3DObject_New(int kind, const char *name, const uint32_t colours[4], uint32_t scheme,
                           int racenumber, int style, const int *options);
void FE3DObject_Free(FE3DObject *o);            /* FE3DObject_v0 0x4d0fb0 */
void FE3DObject_SetPos(FE3DObject *o, float x, float y, float z);   /* 0x4d1150 */
void FE3DObject_RotY(FE3DObject *o, float turns);                   /* 0x4d1050: the rotation set */
void FE3DObject_RotX(FE3DObject *o, float turns);                   /* 0x4d1070: and turned further */
void FE3DObject_RotZ(FE3DObject *o, float turns);                   /* 0x4d10e0 */
void FE3DObject_Draw(FE3DObject *o, EaglMatStack *model);           /* 0x4d1030 */

/* The room of a screen's "room" attribute, made when it changes (NULL / "ID_NONE" / unknown: none). */
void FE3D_SetRoom(const char *id);
/* The car a screen's 3D car control shows in the room (its car file; NULL: none) in its colours (the
   description's order, +0x218: body, slots 0x7c, 0x80, interior; NULL the port's silver). */
void FE3D_SetCar(const char *file, const uint32_t *colours);
/* The cars of the screen's 3D car controls of position types 1 .. 3 (their own, placed and turning
   without a room: the car compare's), named between Begin and End each frame; NULL file: none. */
void FE3D_BeginControlCars(void);
void FE3D_SetControlCar(int type, const char *file, const uint32_t *colours);
void FE3D_EndControlCars(void);
/* The dealer car shown and the list's cars (the dealership's segments follow the list's position, +0x30c,
   and slide when it moves). */
void FE3D_SetIndex(int index, int count);
/* The room's tick and draw in the front end's 3D camera (0x4d2980's first view); whether a room drew. */
bool FE3D_Draw(void);
/* The control cars in the same camera, the frame's second 3D view (0x4d2980: the screens' slot 39,
   0x4e0840, after the base screen's 2D and a z-buffer clear); whether any drew. */
bool FE3D_DrawControlCars(void);
void FE3D_Free(void);

/* For the screen's 3D car control (FECar3DControl): the room's car's yaw in turns (slot 45, 0x4e7f30:
   +0x288; the dealership's 0x4e8300), the room camera's zoom towards its close-up place (slot 46,
   0x4e6ea0: +0x2e8, 0 .. 1), the room car (NULL none) and its damage zones (0x4d1450). */
float FE3D_CarYaw(void);
void FE3D_SetZoom(float f);
R3DCar *FE3D_Car(void);
void FE3D_SetCarDamage(const float d[10]);
/* A hinge of the room car (0x443e60). */
void FE3D_CarHinge(int kind, float x, float y, float z);
