/* The race cameras ([camera], 0x460f50-0x465d00): one per player (0x627e80, 0x10c bytes each), placed
   by its mode each frame (Camera_Update 0x463af0) and turned into the race view's frame (0x464e40, by
   the view's update Camera_Track 0x461060). The modes (the table at 0x5cf610: an offset in the car's
   frame and six flags): 0 on the hood (forward of the car's centre), 1 the driver's seat (its offset and tilt the car's
   drivercampos and drivercamrot), 2 .. 4 the near chase cameras, 5 .. 7 the far
   ones, 8, 9 and 15 fixed on the car, 13 overhead; the replay's: 10 circling the car, 11 a TV camera
   following the road, 12 a fixed one rising with the distance (the track's scene cameras, cut to as
   the cars cross the camera triggers: 0x413ba0), 14 the race's fly-in, 17 .. 19 on two cars (the
   followed one and the one looked at). A car that leaves the ground with a mode flagged so gets the crash camera (0x461450). */
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
    float f30;                /* +0x30: 3; the TV cameras' height over the road */
    float f34, f38, f3c, f40; /* +0x34: 0, 37.5, 0, 37.5; the TV cameras' zoom: to a field of view (+0x34
                                 on, +0x38 the current), or by the distance (+0x3c on, +0x40 from) */
    float f44, f48;           /* +0x44: 1, 0.02: the zoom's ease and its rate */
    float tilt[3];            /* +0x4c: the hood camera's turns about x, y and z (degrees) */
    float fov;                /* +0x58: the half field of view (degrees), 37.5 */
    float znear;              /* +0x5c: 0.5 */
    float f60[3];             /* +0x60: the TV camera's look ahead (across, up, along the road) */
    float campos[3];          /* +0x6c: the scene camera's position */
    float f78;                /* +0x78: 37.5: the field of view zoomed to */
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
    uint8_t padca[2];
    float tvspeed;            /* +0xcc: the TV camera's follow along the road (0 .. 1) */
    uint8_t padd0[0xc];
    WSurface surf;            /* +0xdc: the eye's road node */
} RaceCamera;

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(RaceCamera) == 0x10c, "the original's layout");
#endif

extern RaceCamera g_racecam[2];   /* 0x627e80 */
extern int32_t g_look[4];         /* 0x5e99a8 .. 0x5e99b4: the look keys: forward (halve), right (+90), back, left (-90) */
extern int32_t g_camera_car;      /* 0x65741c: the car the camera goes back to (an index of the race's cars) */

/* The camera on the car with its mode. 0x465650 (the race's set-up also points +0 and +4 at the car) */
void Camera_Init(int p, Car *car, int mode);
void Camera_SetMode(int p, int mode);        /* 0x465650 */
void Camera_NextMode(int p);                 /* 0x465920: the car's camera list's next */
void Camera_Update(void);                    /* 0x463af0 */
/* Camera p put at once at its mode's offset in the car's frame (a car put back on the road: 0x461770,
   the fixed cameras' placing). */
void Camera_Snap(int p);
/* The sound's listener: the first view's eye and orientation as 0x465360 makes them. */
void Camera_ListenerFrame(int p, float eye[3], float orient[9]);
/* The race view's frame, field of view and near plane from camera p (type: the view's, 1 the car's own
   eye). 0x464e40 */
void Camera_ToView(int p, RCamera *c, int type);

/* The replay's cameras at its start (0x413ec0's replay part): mode 10 without scene cameras, else every
   human's camera the race direction's first and the TV direction on. */
void Camera_ReplayStart(void);
/* The camera triggers the humans cross (0x413ba0): their scene camera; the viewed one's camera cut to it
   when cut. */
void Camera_ReplayCuts(bool cut);
/* Whether the replay's cameras follow the scene's cameras (0x606850 is 0x14). */
bool Camera_ReplayDirected(void);
/* The replay's controls (0x414870, Ghidra's Race_SetupSkidpad): 0 the replay again, 1 restart the race,
   2 quit, 3 pause / play, 4 .. 7 the speeds 2 (normal), 3 (double), 1 (a quarter), 0 (a half), 8 the
   next camera kind, 9 / 10 the next / previous human viewed. Returns 1 for the replay again, 2 for a
   restart (the caller's to act on), else 0. */
int Replay_Control(int what);
int Replay_Speed(void);          /* 0x606864 */
bool Replay_PauseWanted(void);   /* 0x606860 */
bool Replay_ExitWanted(void);    /* 0x606868 */
/* The game's pause in the replay (0x606a98): the cameras still, the simulation stopped. */
void Replay_SetPaused(bool on);
bool Replay_Paused(void);
/* The cameras held this tick (0x606ad4: a slow replay's skipped tick). */
void Camera_Hold(bool on);
