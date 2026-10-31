/* The track's scene elements (the scene module, 0x469000-0x471000): the .scn files next to the track
   (<track>_cameras.scn, <track>_audio.scn, the start/end lines in <track>_FStart.scn / _BStart.scn, the
   skill tests <track>_ST<n>.scn) parsed into one list of elements (0x628168) and four lists of a kind
   built from it (cameras, triggers by kind).

   A .scn is text: a keyword (AUDIO_ELEMENT, GEOM_ELEMENT, TRIGGER_ELEMENT, CAMERA_ELEMENT,
   PARTICLE_ELEMENT) with a version, then the element's fields in that version's layout, then (most
   versions) a name line. The original reads each field with fscanf straight into the C++ object, so an
   element here is the object's byte image (scene_crt.h): fields are read and written at their original
   offsets, which keeps the runtime's quirks (an int stored into a 16-bit field spills into the next one).

   Base layout (CSceneElem, vtable 0x5b4500), all classes:
     +0x00 vtable (unused here)   +0x04 u8 active (1)   +0x08 position (3 floats)
     +0x14 orientation (3x3, rows right/up/forward)     +0x38 name (64 chars)
   Class fields and sizes: see the SCENE_* offsets below. Classes are told apart by SceneElem.kind, where
   the original uses RTTI (__RTDynamicCast from CSceneElem). */
#pragma once
#include "game/scene_crt.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum { SCENE_AUDIO, SCENE_GEOM, SCENE_TRIGGER, SCENE_CAMERA, SCENE_PARTICLE } SceneKind;

enum { SCENE_MAX_SIZE = 0xf0 };
typedef struct SceneElem {
    SceneKind kind;
    uint32_t size;                 /* the object's size: 0xa4, 0xc4, 0xf0, 0xc4, 0x7c */
    uint8_t b[SCENE_MAX_SIZE];     /* the object image, offsets as in the original */
} SceneElem;

/* Field offsets (named where the port knows the meaning; others by offset). */
enum {
    SCENE_ACTIVE = 0x04, SCENE_POS = 0x08, SCENE_MAT = 0x14, SCENE_NAME = 0x38, SCENE_NAME_LEN = 0x40,
    /* CTriggerElem (0xf0, vtable 0x5b4554) */
    TRIG_TYPE = 0x7c,          /* 1 end line, 2 start line, 6 camera switch, others events (lists below) */
    TRIG_WIDTH = 0x88,         /* default 3.0 */
    TRIG_END_A = 0xa4,         /* the line's two end points (3 floats each) */
    TRIG_END_B = 0xb0,
    TRIG_ID = 0xd4,            /* s16, -1 none; Scene_FindTriggerById */
    TRIG_LINK1 = 0xd8, TRIG_LINK2 = 0xdc, TRIG_LINK3 = 0xde,   /* s16 element indices, -1 none */
    /* CCameraElem (0xc4, vtable 0x5b4228) */
    CAM_TYPE = 0xc0,           /* 1 the forward race's cameras, 2 the reverse's */
    CAM_FOV = 0x9c,            /* float, degrees (default 37.5; the data has 45) */
    CAM_TARGET = 0xb4,         /* 3 floats */
    /* CGeomElem (0xc4, vtable 0x5b4280) */
    GEOM_OBJTYPE = 0x7c,       /* the object type (a four-character code, e.g. FNSH, CONE) */
    GEOM_SCALE = 0x94,         /* 3 floats, percent (100) */
    GEOM_SCALED = 0xa0,        /* the orientation times the scale (3x3), 0x46af60 */
};

typedef struct { SceneElem **e; int n, cap; } SceneList;

static inline float scene_f32(const SceneElem *e, int off) { float v; __builtin_memcpy(&v, e->b + off, 4); return v; }
static inline int32_t scene_i32(const SceneElem *e, int off) { int32_t v; __builtin_memcpy(&v, e->b + off, 4); return v; }
static inline int16_t scene_i16(const SceneElem *e, int off) { int16_t v; __builtin_memcpy(&v, e->b + off, 2); return v; }
static inline void scene_set_i16(SceneElem *e, int off, int16_t v) { __builtin_memcpy(e->b + off, &v, 2); }
static inline const char *scene_name(const SceneElem *e) { return (const char *)e->b + SCENE_NAME; }
void scene_pos(const SceneElem *e, float out[3]);   /* CSceneElem_GetPosition 0x46bd90 */
void scene_mat(const SceneElem *e, float out[9]);   /* CSceneElem_GetMatrix 0x46bdf0 */
int32_t SceneElem_type(const SceneElem *e);         /* the int at +0x7c (trigger type) 0x469a00 */

/* The original's element constructors (default fields) and load (vtable slot 1, read mode): */
SceneElem *SceneElem_new(SceneKind kind);
/* Reads the element from the stream (its version, then that version's fields). The save mode (slot 1
   with 1, fprintf of the current version) is the scene editor's and isn't ported. */
void SceneElem_load(SceneElem *e, CrtFile *f);
/* CGeomElem's orientation times its scale (also stored at +0xa0). 0x46af60 */
void CGeomElem_ScaledMatrix(SceneElem *e, float out[9]);

/* Parses a .scn file into the list; trigger links are made relative to `base` (the list's size before:
   the elements of several files share one list). Scene_ParseElements 0x46c170 */
void Scene_ParseElements(const char *path, SceneList *list, int base);

/* The track's scenes, as Track_LoadWorld (0x487650) loads them after the world: every <track>*.scn that
   isn't a start or skill-test file (Scene_LoadStarts 0x46cc00), then the skill test `arg` if the game
   type is 4 (Scene_LoadSkillTest 0x46d280), else the start/end file of the direction `arg` (1 reverse,
   Scene_LoadStartScenes 0x46d550). `track` is the track's base name ("alps"). */
void Track_LoadScenes(const char *track, int mode, uint32_t arg);
void Scene_LoadStarts(const char *track);                  /* 0x46cc00 */
void Scene_LoadSkillTest(const char *track, int test);     /* 0x46d280 */
void Scene_LoadStartScenes(const char *track, int reverse);/* 0x46d550 */
void Scene_Clear(void);                                    /* 0x46cf60: frees every element and list */

/* The lists (0x628168 all; built by 0x46ca50 / 0x46c5d0 / 0x46c820). NULL before a load. */
SceneList *Scene_List(void);              /* 0x46d900 */
/* The odd tick's event triggers (0x470b10): the first race car through a sound trigger (type 4) sets its
   sound element's one-shot going; animation triggers (7) are pending. */
void Scene_Tick(void);
SceneList *Scene_EventTriggers(void);     /* 0x46d7c0: triggers of other types than 0, 1, 2, 6 (0x62816c) */
SceneList *Scene_CameraTriggers(void);    /* 0x46d7d0: triggers of type 6 (0x628174) */
SceneList *Scene_LineTriggers(void);      /* triggers of type 0, 1, 2 (0x628170) */
SceneList *Scene_Cameras(void);           /* 0x628178 */
int Scene_CountGeom(void);                /* 0x46d910 */
int Scene_CameraCount(void);              /* 0x46d8b0 */
/* The index (in the whole list) of the first camera of the race's direction (type 1 forward, 2 when the
   reverse flag 0x657430 is set), else of the first camera, else 0. 0x46d7e0 */
int Scene_FirstCamera(bool reverse);
SceneElem *Scene_Camera(int index);       /* 0x46d8d0: the element at index if it's a camera, else NULL */
SceneElem *Scene_FindTriggerByType(int type);   /* 0x46d1e0: in the line triggers */
SceneElem *Scene_FindTriggerById(int id);       /* 0x46d230: in the line triggers, by +0xd4 */
/* Places the geometry elements into the world's scene bundle (0x46d120). */
void Scene_PlaceGeometry(void);

/* Port addition: the start line (the type-2 trigger, Scene_FindTriggerByType(2), as the race code finds
   it): its centre and orientation. False if the scene has none. */
bool Scene_StartLine(float pos[3], float mat[9]);
