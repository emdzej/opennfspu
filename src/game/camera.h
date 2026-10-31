/* The race cameras ([camera], 0x460f50-0x465d00): one per player (0x627e80, 0x10c bytes each), placed
   by its mode each frame (Camera_Update 0x463af0) and turned into the race view's frame (0x464e40, by
   the view's update Camera_Track 0x461060). The modes (the table at 0x5cf610: an offset in the car's
   frame and six flags): 0 in the car, 1 on the hood, 2 .. 4 the near chase cameras, 5 .. 7 the far
   ones, 8, 9 and 15 fixed on the car, 13 overhead; the replay's TV cameras (10 .. 12, 14, 17 .. 19) are
   pending. A car that leaves the ground with a mode flagged so gets the crash camera (0x461450). */
#pragma once
#include "game/car.h"
#include "game/wtree.h"
#include "render/render.h"
#include <stdint.h>

typedef struct {
    Car *car;                 /* +0x00: the car followed */
    Car *base;                /* +0x04: the car looked at */
    float offset[3];          /* +0x08: the eye from the car */
    float lookat[3];          /* +0x14 */
    void *f20;                /* +0x20 */
    float eye[3];             /* +0x24 */
    float f30;                /* +0x30: 3 */
    float f34, f38, f3c, f40; /* +0x34: 0, 37.5, 0, 37.5 */
    float f44, f48;           /* +0x44: 1, 0.02 */
    float tilt[3];            /* +0x4c: the hood camera's turns about x, y and z (degrees) */
    float fov;                /* +0x58: the half field of view (degrees), 37.5 */
    float znear;              /* +0x5c: 0.5 */
    float f60[3];             /* +0x60 */
    uint8_t pad6c[0xc];
    float f78;                /* +0x78: 37.5 */
    float orient[9];          /* +0x7c: rows left, up, forward */
    float shake[3];           /* +0xa0: the replay's up vector */
    uint8_t padac[0x10];
    int16_t mode;             /* +0xbc */
    int16_t cycle;            /* +0xbe: the place in the car's camera list */
    uint8_t flags;            /* +0xc0: 1 reload the mode's flags, 2 look higher, 4 roll with the car,
                                 8 look at the car, 0x10 kept out of walls and under ceilings, 0x40 the
                                 crash camera may take over, 0x80 */
    int8_t ease;              /* +0xc1: frames of quick easing after a change (0x32) */
    int8_t crash;             /* +0xc2: the crash camera's frames left */
    uint8_t flags2;           /* +0xc3: 1 the crash camera in front, 8 a view inside the car, 0x10 the
                                 mirror's view */
    uint8_t padc4[2];
    int16_t look;             /* +0xc6: the turn about the car (degrees) */
    int16_t lookspeed;        /* +0xc8 */
    uint8_t padca[0x12];
    WSurface surf;            /* +0xdc: the eye's road node */
} RaceCamera;

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(RaceCamera) == 0x10c, "the original's layout");
#endif

extern RaceCamera g_racecam[2];   /* 0x627e80 */
extern int32_t g_look[4];         /* 0x5e99a8 .. 0x5e99b4: the look keys: halve, left (+90), back, right (-90) */
extern int32_t g_camera_car;      /* 0x65741c: the car the camera goes back to (an index of the race's cars) */

/* The camera on the car with its mode. 0x465650 (the race's set-up also points +0 and +4 at the car) */
void Camera_Init(int p, Car *car, int mode);
void Camera_SetMode(int p, int mode);        /* 0x465650 */
void Camera_NextMode(int p);                 /* 0x465920: the car's camera list's next */
void Camera_Update(void);                    /* 0x463af0 */
/* The race view's frame, field of view and near plane from camera p (type: the view's, 1 the car's own
   eye). 0x464e40 */
void Camera_ToView(int p, RCamera *c, int type);
