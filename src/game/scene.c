/* The scene module (see scene.h). Field reads go through crt_fscanf with the original's format strings,
   read from the exe at runtime (exe_str), and destinations at the original object offsets. */
#include "game/scene.h"
#include "exe.h"
#include "game/track.h"
#include "game/car.h"
#include "game/world.h"
#include "game/track_pending.h"
#include "pending.h"
#include "platform.h"
#include "render/camera_math.h"
#include "vfs.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Format strings (fscanf/fprintf) and constants of the scene module, by address in the exe. */
enum {
    FMT_D = 0x5cd1b0,            /* "%d" */
    FMT_S = 0x5ccb84,            /* "%s" */
    FMT_3F = 0x5cf9c8,           /* position */
    FMT_9F = 0x5cf9ac,           /* orientation */
    FMT_9F_NL = 0x5cfad8,        /* orientation, then a newline */
    FMT_9F_TABS = 0x5cfa00,      /* orientation, rows tab-separated, newline */
    VEC_ZERO = 0x5bb2b0, MAT_IDENTITY = 0x5bb2d0,
    PERCENT = 0x5b3ca0,          /* 0.01 */
};

static const char *fmt(uint32_t va) { return exe_str(va); }

/* Element keywords and default names, from the exe. */
enum {
    KW_AUDIO = 0x5cfb94, KW_GEOM = 0x5cfb1c, KW_TRIGGER = 0x5cfb84, KW_CAMERA = 0x5cfa30,
    KW_PARTICLE = 0x5cfb68,
    NAME_AUDIO = 0x5d1a30, NAME_GEOM = 0x5cfa60, NAME_CAMERA = 0x5cf944, NAME_PARTICLE = 0x5cfb4c,
};

static float f32_at(uint32_t va) { float v = 0; const uint8_t *p = exe_data(va, 4); if (p) memcpy(&v, p, 4); return v; }

static void put_f32(SceneElem *e, int off, float v) { memcpy(e->b + off, &v, 4); }
static void put_i32(SceneElem *e, int off, int32_t v) { memcpy(e->b + off, &v, 4); }
static void put_u8(SceneElem *e, int off, uint8_t v) { e->b[off] = v; }

void scene_pos(const SceneElem *e, float out[3]) { memcpy(out, e->b + SCENE_POS, 12); }   /* 0x46bd90 */
void scene_mat(const SceneElem *e, float out[9]) { memcpy(out, e->b + SCENE_MAT, 36); }   /* 0x46bdf0 */
int32_t SceneElem_type(const SceneElem *e) { return scene_i32(e, 0x7c); }                 /* 0x469a00 */

static void set_name(SceneElem *e, const char *s)   /* CSceneElem_SetName 0x46be40 (no bound, as the original) */
{
    size_t l = strlen(s);
    if (l > SCENE_NAME_LEN - 1) l = SCENE_NAME_LEN - 1;   /* the port bounds it to the field */
    memcpy(e->b + SCENE_NAME, s, l);
    e->b[SCENE_NAME + l] = 0;
}

/* CSceneElem ctor 0x46bb30: the zero position, the identity, an empty name, active. */
static void base_init(SceneElem *e)
{
    const uint8_t *z = exe_data(VEC_ZERO, 12), *m = exe_data(MAT_IDENTITY, 36);
    if (z) memcpy(e->b + SCENE_POS, z, 12);
    if (m) memcpy(e->b + SCENE_MAT, m, 36);
    e->b[SCENE_NAME] = 0;
    put_u8(e, SCENE_ACTIVE, 1);
}

/* The name line: two fgets (the rest of the numbers' line, then the name), the newline cut off. Without a
   name (an empty read) the field keeps what it had. */
static void read_name(SceneElem *e, CrtFile *f)
{
    char *n = (char *)e->b + SCENE_NAME;
    crt_fgets(n, SCENE_NAME_LEN, f);
    crt_fgets(n, SCENE_NAME_LEN, f);
    size_t l = strlen(n);
    if (l) n[l - 1] = 0;
}

static void read_pos_mat(SceneElem *e, CrtFile *f, uint32_t matfmt)
{
    uint8_t *b = e->b;
    crt_fscanf(f, fmt(FMT_3F), b + 0x08, b + 0x0c, b + 0x10);
    crt_fscanf(f, fmt(matfmt), b + 0x14, b + 0x18, b + 0x1c, b + 0x20, b + 0x24, b + 0x28, b + 0x2c, b + 0x30, b + 0x34);
}

/* ------------------------------------------------------------------------------------- CAudioElem */

/* ctor 0x4ab3c0: the defaults; 0x4abf60 also fills a table of circle points (0x6568f0) from the radius,
   the editor's display of the element, which the port doesn't draw. */
static void audio_init(SceneElem *e)
{
    base_init(e);
    const uint8_t *z = exe_data(VEC_ZERO, 12);
    if (z) memcpy(e->b + 0x7c, z, 12);
    set_name(e, exe_str(NAME_AUDIO));
    scene_set_i16(e, 0x88, 0);
    scene_set_i16(e, 0x8a, -1);
    put_f32(e, 0x8c, 100.0f);
    PENDING_NOTE(0x4abf60, "CAudioElem circle table (editor display)");
    scene_set_i16(e, 0xa0, -1);
    put_i32(e, 0x90, 0);
    scene_set_i16(e, 0x94, 0);
    scene_set_i16(e, 0x96, 0);
    scene_set_i16(e, 0x98, 0);
    scene_set_i16(e, 0x9a, 0);
    scene_set_i16(e, 0x9e, 0);
    scene_set_i16(e, 0x9c, 100);
    put_u8(e, 0xa2, 0);
    put_u8(e, 0x78, 0);
}

/* CAudioElem_v1 0x4ab4c0 (read): versions 1-4 (the data has 4). */
static void audio_load(SceneElem *e, CrtFile *f)
{
    uint8_t *b = e->b;
    int v = 0;
    crt_fscanf(f, fmt(FMT_D), &v);
    if (v < 1 || v > 4) return;
    crt_fscanf(f, fmt(FMT_3F), b + 0x08, b + 0x0c, b + 0x10);
    switch (v) {
    case 1:
    case 2:
        crt_fscanf(f, fmt(0x5d1a88), b + 0x88, b + 0x8a, b + 0x8c, b + 0x90, b + 0x94, b + 0x96, b + 0x98,
                   b + 0x9a, b + 0x9e, b + 0xa0);
        if (v == 1) return;   /* no name line */
        break;
    case 3:
        crt_fscanf(f, fmt(0x5d1a64), b + 0x88, b + 0x8a, b + 0x8c, b + 0x90, b + 0x94, b + 0x96, b + 0x98,
                   b + 0x9a, b + 0x9e, b + 0xa0, b + 0x9c);
        break;
    case 4:
        crt_fscanf(f, fmt(0x5d1a40), b + 0x88, b + 0x8a, b + 0x8c, b + 0x90, b + 0x94, b + 0x96, b + 0x98,
                   b + 0x9a, b + 0x9e, b + 0xa0, b + 0x9c, b + 0x78);
        break;
    }
    read_name(e, f);
}

/* -------------------------------------------------------------------------------------- CGeomElem */

/* 0x46af60 (and the end of the load and of the ctor): diag(scale) * 0.01 times the orientation into +0xa0. */
void CGeomElem_ScaledMatrix(SceneElem *e, float out[9])
{
    float s[9] = {0};
    float k = f32_at(PERCENT);
    s[0] = scene_f32(e, GEOM_SCALE) * k;
    s[4] = scene_f32(e, GEOM_SCALE + 4) * k;
    s[8] = scene_f32(e, GEOM_SCALE + 8) * k;
    float m[9];
    scene_mat(e, m);
    float r[9];
    Mat3_Mul(m, s, r);
    memcpy(e->b + GEOM_SCALED, r, 36);
    if (out) memcpy(out, r, 36);
}

/* ctor 0x46a020. The original first sets the scale from the (uninitialised) fields and computes the
   matrix with it (0x46aea0), then puts 100% and computes it again: only the second counts. */
static void geom_init(SceneElem *e)
{
    base_init(e);
    set_name(e, exe_str(NAME_GEOM));
    scene_set_i16(e, 0x78, 0);
    put_i32(e, 0x80, 0);
    put_i32(e, 0x84, 0);
    put_i32(e, 0x7c, -1);
    scene_set_i16(e, 0x88, -1);
    put_i32(e, 0x8c, 1);
    put_i32(e, 0x90, 1);
    put_f32(e, GEOM_SCALE, 100.0f);
    put_f32(e, GEOM_SCALE + 4, 100.0f);
    put_f32(e, GEOM_SCALE + 8, 100.0f);
    CGeomElem_ScaledMatrix(e, NULL);
}

/* CGeomElem_v1 0x46a200 (read): versions 1-5 (the data has 4 and 5). */
static void geom_load(SceneElem *e, CrtFile *f)
{
    uint8_t *b = e->b;
    int v = 0, unused = 0;
    crt_fscanf(f, fmt(FMT_D), &v);
    if (v >= 1 && v <= 5) {
        read_pos_mat(e, f, v == 1 ? FMT_9F_NL : FMT_9F_TABS);
        switch (v) {
        case 1:
            crt_fscanf(f, fmt(0x5cfac4), b + 0x7c, b + 0x80, b + 0x84, b + 0x88, b + 0x8c, b + 0x90);
            break;   /* no name line */
        case 2:
            crt_fscanf(f, fmt(0x5cfac4), b + 0x7c, b + 0x80, b + 0x84, b + 0x88, b + 0x8c, b + 0x90);
            read_name(e, f);
            break;
        case 3:
            crt_fscanf(f, fmt(0x5cfaac), b + 0x78, b + 0x7c, b + 0x80, b + 0x84, b + 0x88, b + 0x8c, b + 0x90);
            read_name(e, f);
            break;
        case 4:   /* an eighth number into a local the original never uses */
            crt_fscanf(f, fmt(0x5cfa94), b + 0x78, b + 0x7c, b + 0x80, b + 0x84, b + 0x88, b + 0x8c, b + 0x90, &unused);
            read_name(e, f);
            break;
        case 5:
            crt_fscanf(f, fmt(0x5cfa74), b + 0x78, b + 0x7c, b + 0x80, b + 0x84, b + 0x88, b + 0x8c, b + 0x90,
                       b + 0x94, b + 0x98, b + 0x9c);
            read_name(e, f);
            break;
        }
    }
    CGeomElem_ScaledMatrix(e, NULL);   /* every path, an unknown version too */
}

/* ------------------------------------------------------------------------------------ CTriggerElem */

/* ctor 0x46db40. The original computes the line's end points (+0xa4..+0xb8) from the width field before
   it sets it (heap memory: whatever was there); every load version reads both end points, so the port
   leaves them zero. */
static void trigger_init(SceneElem *e)
{
    base_init(e);
    e->b[SCENE_NAME] = 0;
    put_i32(e, 0x80, 1);
    put_i32(e, 0x7c, 0);
    put_i32(e, 0x90, 0);
    scene_set_i16(e, 0x98, 0);
    put_i32(e, 0xe0, 0);
    put_i32(e, 0x9c, 0);
    put_u8(e, 0x78, 0);
    put_i32(e, 0xe4, 0);
    put_i32(e, 0xe8, 0);
    put_i32(e, 0xa0, 0);
    put_i32(e, 0xec, 0);
    put_f32(e, 0x84, 1.0f);
    put_f32(e, 0x88, 3.0f);
    put_f32(e, 0x8c, 0.25f);
    put_f32(e, 0x94, 200.0f);
    for (int o = 0xd4; o <= 0xde; o += 2) scene_set_i16(e, o, -1);
}

/* CTriggerElem_v1 0x46dd30 (read): versions 1-5 (the data has 3, 4 and 5). Version 1 has no name; 2 jumps
   into version 4's tail for its last fields and the name. */
static void trigger_load(SceneElem *e, CrtFile *f)
{
    uint8_t *b = e->b;
    int v = 0;
    crt_fscanf(f, fmt(FMT_D), &v);
    if (v < 1 || v > 5) return;
    read_pos_mat(e, f, FMT_9F);
    switch (v) {
    case 1:
    case 2:
        crt_fscanf(f, fmt(0x5cfd60), b + 0x7c, b + 0x80, b + 0x84, b + 0x88, b + 0x8c, b + 0x90, b + 0x94,
                   b + 0xd4, b + 0xd6, b + 0xd8, b + 0xec);
        break;
    case 3:
        crt_fscanf(f, fmt(0x5cfd08), b + 0x7c, b + 0x80, b + 0x84, b + 0x88, b + 0x8c, b + 0x90, b + 0x94,
                   b + 0xd4, b + 0xd6, b + 0xd8, b + 0xda, b + 0xdc, b + 0xde, b + 0xec);
        break;
    case 4:
        crt_fscanf(f, fmt(0x5cfcd8), b + 0x7c, b + 0x80, b + 0x84, b + 0x88, b + 0x8c, b + 0x90, b + 0x94,
                   b + 0xd4, b + 0xd6, b + 0xd8, b + 0xda, b + 0xdc, b + 0xde, b + 0xec, b + 0xe0, b + 0xa0);
        break;
    case 5:
        crt_fscanf(f, fmt(0x5cfca4), b + 0x7c, b + 0x80, b + 0x84, b + 0x88, b + 0x8c, b + 0x90, b + 0x94,
                   b + 0xd4, b + 0xd6, b + 0xd8, b + 0xda, b + 0xdc, b + 0xde, b + 0xec, b + 0xe0, b + 0xa0, b + 0x9c);
        break;
    }
    crt_fscanf(f, fmt(0x5cfd4c), b + 0xa4, b + 0xa8, b + 0xac, b + 0xb0, b + 0xb4, b + 0xb8);
    crt_fscanf(f, fmt(0x5cfd34), b + 0xe4, b + 0xbc, b + 0xc0, b + 0xc4, b + 0xe8, b + 0xc8, b + 0xcc, b + 0xd0);
    if (v == 1) return;
    read_name(e, f);
}

/* ------------------------------------------------------------------------------------- CCameraElem */

/* ctor 0x469170 ("Replay Camera"). */
static void camera_init(SceneElem *e)
{
    base_init(e);
    set_name(e, exe_str(NAME_CAMERA));
    put_i32(e, 0x80, 0);
    put_i32(e, 0x84, 0);
    put_i32(e, 0x90, 0);
    put_i32(e, 0x98, 0);
    put_i32(e, 0xbc, 0);
    put_i32(e, 0xb8, 0);
    put_i32(e, 0xb4, 0);
    put_i32(e, CAM_TYPE, 0);
    put_i32(e, 0x88, 1);
    put_i32(e, 0x8c, 1);
    put_i32(e, 0x7c, 1);
    put_i32(e, 0x78, 12);
    put_f32(e, 0x94, 3.0f);
    put_f32(e, CAM_FOV, 37.5f);
    put_f32(e, 0xa0, 0.6f);
}

/* CCameraElem_v1 0x469270 (read): versions 1-4 (the data has 4). Version 2 has no break: after its own
   fields and name it goes on reading a version-3 element (a bug the data never meets). */
static void camera_load(SceneElem *e, CrtFile *f)
{
    uint8_t *b = e->b;
    int v = 0;
    float skip[3];
    crt_fscanf(f, fmt(FMT_D), &v);
    switch (v) {
    case 1:
        read_pos_mat(e, f, FMT_9F);
        crt_fscanf(f, fmt(0x5cf9a0), b + 0x80, b + 0x84, b + 0x88);
        crt_fscanf(f, fmt(0x5cf988), b + 0x8c, b + 0x7c, b + 0x78, b + 0x90, b + 0x94, b + 0x98, b + 0x9c, b + 0xa0);
        read_name(e, f);
        return;
    case 2:
        read_pos_mat(e, f, FMT_9F);
        crt_fscanf(f, fmt(0x5cf9a0), b + 0x80, b + 0x84, b + 0x88);
        crt_fscanf(f, fmt(0x5cf988), b + 0x8c, b + 0x7c, b + 0x78, b + 0x90, b + 0x94, b + 0x98, b + 0x9c, b + 0xa0);
        /* three values into locals the original drops, then the target */
        crt_fscanf(f, fmt(0x5cf970), &skip[0], &skip[1], &skip[2], b + 0xb4, b + 0xb8, b + 0xbc);
        read_name(e, f);
        /* fall through */
    case 3:
        read_pos_mat(e, f, FMT_9F);
        crt_fscanf(f, fmt(0x5cf9a0), b + 0x80, b + 0x84, b + 0x88);
        crt_fscanf(f, fmt(0x5cf988), b + 0x8c, b + 0x7c, b + 0x78, b + 0x90, b + 0x94, b + 0x98, b + 0x9c, b + 0xa0);
        crt_fscanf(f, fmt(FMT_3F), b + 0xb4, b + 0xb8, b + 0xbc);
        read_name(e, f);
        return;
    case 4:
        read_pos_mat(e, f, FMT_9F);
        crt_fscanf(f, fmt(0x5cf9a0), b + 0x80, b + 0x84, b + 0x88);
        crt_fscanf(f, fmt(0x5cf954), b + 0x8c, b + 0x7c, b + 0x78, b + 0x90, b + 0xc0, b + 0x94, b + 0x98, b + 0x9c, b + 0xa0);
        crt_fscanf(f, fmt(FMT_3F), b + 0xb4, b + 0xb8, b + 0xbc);
        read_name(e, f);
        return;
    default:
        return;
    }
}

/* ----------------------------------------------------------------------------------- CParticleElem */

static void particle_init(SceneElem *e)   /* ctor 0x46b490 */
{
    base_init(e);
    set_name(e, exe_str(NAME_PARTICLE));
    put_i32(e, 0x78, -1);
}

/* CParticleElem_v1 0x46b530 (read): version 1 only. */
static void particle_load(SceneElem *e, CrtFile *f)
{
    int v = 0;
    crt_fscanf(f, fmt(FMT_D), &v);
    if (v != 1) return;
    read_pos_mat(e, f, FMT_9F_NL);
    crt_fscanf(f, fmt(FMT_D), e->b + 0x78);
    read_name(e, f);
}

/* ---------------------------------------------------------------------------------------- elements */

SceneElem *SceneElem_new(SceneKind kind)
{
    static const uint32_t sizes[] = {0xa4, 0xc4, 0xf0, 0xc4, 0x7c};
    SceneElem *e = calloc(1, sizeof *e);
    if (!e) return NULL;
    e->kind = kind;
    e->size = sizes[kind];
    switch (kind) {
    case SCENE_AUDIO: audio_init(e); break;
    case SCENE_GEOM: geom_init(e); break;
    case SCENE_TRIGGER: trigger_init(e); break;
    case SCENE_CAMERA: camera_init(e); break;
    case SCENE_PARTICLE: particle_init(e); break;
    }
    return e;
}

void SceneElem_load(SceneElem *e, CrtFile *f)
{
    switch (e->kind) {
    case SCENE_AUDIO: audio_load(e, f); break;
    case SCENE_GEOM: geom_load(e, f); break;
    case SCENE_TRIGGER: trigger_load(e, f); break;
    case SCENE_CAMERA: camera_load(e, f); break;
    case SCENE_PARTICLE: particle_load(e, f); break;
    }
}

static void list_push(SceneList *l, SceneElem *e)
{
    if (l->n == l->cap) {
        int c = l->cap ? l->cap * 2 : 16;
        SceneElem **ne = realloc(l->e, (size_t)c * sizeof *ne);
        if (!ne) return;
        l->e = ne;
        l->cap = c;
    }
    l->e[l->n++] = e;
}

static SceneList *list_new(void) { return calloc(1, sizeof(SceneList)); }
static void list_free(SceneList *l) { if (l) { free(l->e); free(l); } }

/* The link fields hold indices into the file's own elements; with several files in one list they are
   moved by the list's size before the file (`base`). */
static void relink(SceneElem *e, int off, int base)
{
    int16_t v = scene_i16(e, off);
    if (v != -1) scene_set_i16(e, off, (int16_t)(v + base));
}

void Scene_ParseElements(const char *path, SceneList *list, int base)
{
    CrtFile *f = UTIL_fopen(path, "r");
    if (!f) return;   /* the original fopens without a check; the port's files may be missing */
    static const struct { uint32_t kw; SceneKind kind; } kinds[] = {
        {KW_AUDIO, SCENE_AUDIO}, {KW_GEOM, SCENE_GEOM}, {KW_TRIGGER, SCENE_TRIGGER},
        {KW_CAMERA, SCENE_CAMERA}, {KW_PARTICLE, SCENE_PARTICLE},
    };
    char word[256];
    while (!f->eof) {
        word[0] = 0;   /* the keyword buffer starts empty each time: at the end nothing matches */
        crt_fscanf(f, fmt(FMT_S), word);
        for (size_t k = 0; k < sizeof kinds / sizeof *kinds; k++) {
            if (strcmp(word, exe_str(kinds[k].kw)) != 0) continue;
            SceneElem *e = SceneElem_new(kinds[k].kind);
            if (!e) break;
            SceneElem_load(e, f);
            if (e->kind == SCENE_TRIGGER) {
                relink(e, TRIG_LINK1, base);
                relink(e, TRIG_LINK2, base);
                relink(e, TRIG_LINK3, base);
            }
            list_push(list, e);
            break;
        }
    }
    crt_fclose(f);
}

/* ------------------------------------------------------------------------------------------- lists */

static SceneList *g_scene;            /* 0x628168 */
static SceneList *g_event_triggers;   /* 0x62816c */
static SceneList *g_line_triggers;    /* 0x628170 */
static SceneList *g_camera_triggers;  /* 0x628174 */
static SceneList *g_cameras;          /* 0x628178 */

SceneList *Scene_List(void) { return g_scene; }
SceneList *Scene_EventTriggers(void) { return g_event_triggers; }
SceneList *Scene_CameraTriggers(void) { return g_camera_triggers; }
SceneList *Scene_LineTriggers(void) { return g_line_triggers; }
SceneList *Scene_Cameras(void) { return g_cameras; }

static SceneList *reset(SceneList **l)
{
    if (!*l) *l = list_new();
    else (*l)->n = 0;
    return *l;
}

/* 0x46ca50: the cameras, and the triggers of type 6 (the cameras' switch points). */
static void build_cameras(void)
{
    SceneList *c = reset(&g_cameras), *t = reset(&g_camera_triggers);
    for (int i = 0; i < g_scene->n; i++) {
        SceneElem *e = g_scene->e[i];
        if (e->kind == SCENE_CAMERA) list_push(c, e);
        else if (e->kind == SCENE_TRIGGER && SceneElem_type(e) == 6) list_push(t, e);
    }
}

/* 0x46c5d0: the triggers of other types than 0, 1, 2 and 6. */
static void build_event_triggers(void)
{
    SceneList *t = reset(&g_event_triggers);
    for (int i = 0; i < g_scene->n; i++) {
        SceneElem *e = g_scene->e[i];
        int ty = e->kind == SCENE_TRIGGER ? SceneElem_type(e) : -1;
        if (e->kind == SCENE_TRIGGER && ty != 0 && ty != 1 && ty != 2 && ty != 6) list_push(t, e);
    }
}

/* 0x46c820: the triggers of type 0, 1 and 2 (the start and end lines). */
static void build_line_triggers(void)
{
    SceneList *t = reset(&g_line_triggers);
    for (int i = 0; i < g_scene->n; i++) {
        SceneElem *e = g_scene->e[i];
        int ty = e->kind == SCENE_TRIGGER ? SceneElem_type(e) : -1;
        if (ty == 0 || ty == 1 || ty == 2) list_push(t, e);
    }
}

void Scene_Clear(void)
{
    if (g_scene) {
        for (int i = 0; i < g_scene->n; i++) free(g_scene->e[i]);
        list_free(g_scene);
        g_scene = NULL;
    }
    list_free(g_event_triggers);
    g_event_triggers = NULL;
    list_free(g_line_triggers);
    g_line_triggers = NULL;
    /* the original frees the camera-trigger list only together with the camera list */
    if (g_cameras) {
        list_free(g_cameras);
        g_cameras = NULL;
        list_free(g_camera_triggers);
        g_camera_triggers = NULL;
    }
}

/* The scene files of a track in the track directory, in the order the file system lists them (NTFS and
   CD file systems: by name, compared in upper case). */
typedef struct { char (*names)[64]; int n, cap; } Names;

static void collect_scn(const char *name, bool is_dir, void *user)
{
    Names *l = user;
    size_t len = strlen(name);
    if (is_dir || len < 4 || len >= 64 || strcasecmp(name + len - 4, ".scn") != 0) return;
    if (l->n == l->cap) {
        int c = l->cap ? l->cap * 2 : 32;
        char (*nn)[64] = realloc(l->names, (size_t)c * sizeof *nn);
        if (!nn) return;
        l->names = nn;
        l->cap = c;
    }
    memcpy(l->names[l->n++], name, len + 1);
}

static int cmp_upper(const void *a, const void *b)
{
    const unsigned char *p = a, *q = b;
    for (;; p++, q++) {
        int c = toupper(*p), d = toupper(*q);
        if (c != d || !c) return c - d;
    }
}

static void list_track_dir(Names *l)
{
    char dir[260];
    snprintf(dir, sizeof dir, "%s", g_track_dir);
    size_t dl = strlen(dir);
    if (dl && (dir[dl - 1] == '\\' || dir[dl - 1] == '/')) dir[dl - 1] = 0;
    vfs_list(dir, collect_scn, l);
    qsort(l->names, (size_t)l->n, sizeof *l->names, cmp_upper);
}

static void upper(char *s) { for (; *s; s++) *s = (char)toupper((unsigned char)*s); }   /* _strupr */

static void ensure_scene(void) { if (!g_scene) g_scene = list_new(); }

/* Scene_LoadStarts 0x46cc00: clears the scene, then parses every "<dir>*.scn" whose name starts with the
   track's name and doesn't go on with _FSTART, _BSTART or _ST (compared upper case), each with the links
   moved by the elements before it; then the lists. */
void Scene_LoadStarts(const char *track)
{
    Scene_Clear();
    ensure_scene();
    Names l = {0};
    list_track_dir(&l);
    char t[256];
    snprintf(t, sizeof t, "%s", track);
    upper(t);
    size_t tl = strlen(t);
    int base = 0;
    for (int i = 0; i < l.n; i++) {
        char u[256];
        snprintf(u, sizeof u, "%s", l.names[i]);
        upper(u);
        if (strncmp(u, t, tl) != 0) continue;
        if (!strncmp(u + tl, exe_str(0x5cfbd0), 7) || !strncmp(u + tl, exe_str(0x5cfbc8), 7) ||
            !strncmp(u + tl, exe_str(0x5cfbc4), 3))
            continue;
        char path[300];
        snprintf(path, sizeof path, "%s%s", g_track_dir, l.names[i]);
        Scene_ParseElements(path, g_scene, base);
        base = g_scene->n;
    }
    free(l.names);
    build_cameras();
    build_event_triggers();
    build_line_triggers();
}

/* The original's error box (MessageBoxA with the game window, caption "ERROR"): the port logs it. */
static void error_box(const char *text)
{
    char m[400];
    snprintf(m, sizeof m, "%s: %s", exe_str(0x5cfc30), text);
    plat_log(m);
}

/* Scene_LoadStartScenes 0x46d550: <dir><track>_FStart.scn (reverse: _BStart.scn) added to the scene. */
void Scene_LoadStartScenes(const char *track, int reverse)
{
    ensure_scene();
    int base = g_scene->n;
    char path[300];
    snprintf(path, sizeof path, "%s%s%s", g_track_dir, track, exe_str(reverse == 1 ? 0x5cfc94 : 0x5cfc88));
    if (vfs_exists(path)) {
        Scene_ParseElements(path, g_scene, base);
    } else {
        /* the original only formats the message (0x5cfc64) and doesn't show it */
    }
    build_line_triggers();
}

/* Scene_LoadSkillTest 0x46d280: <dir><track>_ST<test>.scn. A negative test is an error; test 0 is none:
   the original sets the game type back to 0 (0x6573ec) unless the race is linear (0x657494), which is the
   same error. The race options aren't ported, so the port takes test 0 as the non-linear case. */
void Scene_LoadSkillTest(const char *track, int test)
{
    if (test <= 0) {
        if (test < 0) error_box(exe_str(0x5cfbe0));
        return;
    }
    ensure_scene();
    int base = g_scene->n;
    char name[64], path[300];
    snprintf(name, sizeof name, exe_str(0x5cfc58), test);
    snprintf(path, sizeof path, "%s%s%s", g_track_dir, track, name);
    if (vfs_exists(path)) Scene_ParseElements(path, g_scene, base);
    else {
        char m[340];
        snprintf(m, sizeof m, exe_str(0x5cfc38), path);
        error_box(m);
    }
    build_cameras();
    build_event_triggers();
    build_line_triggers();
}

/* Track_LoadWorld 0x487650's part: the scenes after the world (mode = the game type 0x6573ec, arg = the
   skill test 0x657c80 for type 4, else the reverse flag 0x657430). */
void Track_LoadScenes(const char *track, int mode, uint32_t arg)
{
    Scene_LoadStarts(track);
    if (mode == 4) Scene_LoadSkillTest(track, (int)arg);
    else Scene_LoadStartScenes(track, (int)arg);
}

/* ----------------------------------------------------------------------------------------- queries */

int Scene_CountGeom(void)
{
    int n = 0;
    if (!g_scene) return 0;   /* the original reads the list unconditionally; before a load it's NULL */
    for (int i = 0; i < g_scene->n; i++)
        if (g_scene->e[i]->kind == SCENE_GEOM) n++;
    return n;
}

int Scene_CameraCount(void) { return g_cameras ? g_cameras->n : 0; }

int Scene_FirstCamera(bool reverse)
{
    if (!g_scene) return 0;
    for (int i = 0; i < g_scene->n; i++) {
        SceneElem *e = g_scene->e[i];
        if (e->kind != SCENE_CAMERA) continue;
        int ty = scene_i32(e, CAM_TYPE);
        if ((ty == 1 && !reverse) || (ty == 2 && reverse)) return i;
    }
    for (int i = 0; i < g_scene->n; i++)
        if (g_scene->e[i]->kind == SCENE_CAMERA) return i;
    return 0;
}

SceneElem *Scene_Camera(int index)
{
    if (!g_scene || index < 0 || index >= g_scene->n) return NULL;
    SceneElem *e = g_scene->e[(int16_t)index];
    return e->kind == SCENE_CAMERA ? e : NULL;
}

SceneElem *Scene_FindTriggerByType(int type)
{
    if (!g_line_triggers) return NULL;
    for (int i = 0; i < g_line_triggers->n; i++)
        if (SceneElem_type(g_line_triggers->e[i]) == type) return g_line_triggers->e[i];
    return NULL;
}

SceneElem *Scene_FindTriggerById(int id)
{
    if (!g_line_triggers) return NULL;
    for (int i = 0; i < g_line_triggers->n; i++)
        if (scene_i16(g_line_triggers->e[i], TRIG_ID) == id) return g_line_triggers->e[i];
    return NULL;
}

/* 0x46d120: every geometry element into the world's scene bundle (its type, position and scaled
   orientation), then the bundle's update. The bundle calls are the world's (0x482af0, 0x482f70,
   0x482fc0). */
void Scene_PlaceGeometry(void)
{
    if (!g_scene) return;
    WSceneBundle_Reset();
    int placed = 0;
    for (int i = 0; i < g_scene->n; i++) {
        SceneElem *e = g_scene->e[i];
        if (e->kind != SCENE_GEOM) continue;
        float m[9], p[3];
        CGeomElem_ScaledMatrix(e, m);
        scene_pos(e, p);
        WSceneBundle_Add((uint32_t)scene_i32(e, GEOM_OBJTYPE), p, m);
        placed++;
    }
    if (placed > 0) WSceneBundle_Update(false);
}

bool Scene_StartLine(float pos[3], float mat[9])
{
    SceneElem *t = Scene_FindTriggerByType(2);
    if (!t) return false;
    scene_pos(t, pos);
    scene_mat(t, mat);
    return true;
}

/* ---------------------------------------------------------------------------- the triggers' tick */

/* The car against an event trigger (0x470620): a line trigger (+0x80 1) is crossed when either of the
   car's diagonals (wheel 0 to wheel 3, wheel 1 to wheel 2) crosses its two ends on the ground plane,
   the first diagonal's ends within 3.5 of the line's height; a long car (its description's +0x518)
   has each diagonal extended, the far end by its length, the near one by twice that again (as the
   original's two steps do). A sphere trigger (0) holds the car's centre within its radius (+0x84);
   other shapes always hit. */
static void stretch(float *a, float *b)
{
    float t[3];
    for (int k = 0; k < 3; k++) t[k] = b[k] - a[k];
    for (int k = 0; k < 3; k++) b[k] += t[k];
    for (int k = 0; k < 3; k++) t[k] = a[k] - b[k];
    for (int k = 0; k < 3; k++) a[k] += t[k];
}

static bool trigger_hit(const SceneElem *t, Car *c)
{
    int shape = scene_i32(t, 0x80);
    if (shape == 1) {
        float q[6], p[6];
        memcpy(q, t->b + TRIG_END_A, 12);
        memcpy(q + 3, t->b + TRIG_END_B, 12);
        memcpy(p, CAR_F(c, 0x7f8), 12);
        memcpy(p + 3, CAR_F(c, 0xa44), 12);
        bool longcar = DESC_I(c, 0x518) != 0;
        if (longcar) stretch(p + 3, p);
        if (!(q[1] <= p[1] + 3.5f && q[1] <= p[4] + 3.5f && p[1] - 3.5f <= q[1] && p[4] - 3.5f <= q[1])) return false;   /* 0x5b38ec */
        q[1] = q[4] = p[1] = p[4] = 0;
        if (World_SegmentsCross(p, q, NULL)) return true;
        memcpy(p, CAR_F(c, 0x8bc), 12);
        memcpy(p + 3, CAR_F(c, 0x980), 12);
        p[1] = p[4] = 0;
        if (longcar) stretch(p + 3, p);
        return World_SegmentsCross(p, q, NULL);
    }
    if (shape != 0) return true;
    float at[3];
    scene_pos(t, at);
    const float *cp = CAR_F(c, 0x330);
    float d[3] = {cp[0] - at[0], cp[1] - at[1], cp[2] - at[2]};
    float l = 0;
    if (d[0] != 0 || d[1] != 0 || d[2] != 0) l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);   /* 0x532890 */
    return l < scene_f32(t, 0x84);
}

/* The trigger's facing (+0xc8, 0x470a30) against the car's forward axis and its direction (+0xbc,
   0x470920) against the car's way of moving (up when it stands): more than 0.5 each, a zero vector
   any; its speed window (+0x90 .. +0x94, 0x470ac0), each with 0.1 to spare. */
static bool trigger_takes(const SceneElem *t, Car *c)
{
    if (!trigger_hit(t, c)) return false;
    const float *f = (const float *)(const void *)(t->b + 0xc8);
    if (!(f[0] == 0 && f[1] == 0 && f[2] == 0)) {
        const float *a = CAR_F(c, 0x37c);
        if (!(f[0] * a[0] + f[1] * a[1] + f[2] * a[2] > 0.5f)) return false;   /* 0x5b23f0 */
    }
    const float *g = (const float *)(const void *)(t->b + 0xbc);
    if (!(g[0] == 0 && g[1] == 0 && g[2] == 0)) {
        const float *v = CAR_F(c, 0x33c);
        float u[3] = {0, 1, 0};
        if (v[0] != 0 || v[1] != 0 || v[2] != 0) {
            float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            for (int k = 0; k < 3; k++) u[k] = v[k] / l;
        }
        if (!(g[0] * u[0] + g[1] * u[1] + g[2] * u[2] > 0.5f)) return false;
    }
    float speed = *CAR_F(c, 0x35c);
    return scene_f32(t, 0x90) < speed + 0.1f && speed < scene_f32(t, 0x94) + 0.1f;   /* 0x5b2604 */
}

bool Scene_TriggerTakes(const SceneElem *t, void *car) { return trigger_takes(t, car); }

void Scene_TriggerSound(const SceneElem *t)
{
    if (scene_i32(t, 0xe0) == 1 && (scene_i32(t, TRIG_TYPE) == 0 || scene_i32(t, TRIG_TYPE) == 1)) {
        PENDING_NOTE(0x4aac00, "the gate's sound (0x4a96b0 of bank 0x6564d0, sound 2)");
        return;
    }
    /* the linked sound element (+0xd8): a one-shot (kind 1) with a sound has its trigger set (0x4abea0,
       0x4abeb0) */
    int16_t link = scene_i16(t, TRIG_LINK1);
    SceneList *all = Scene_List();
    if (-1 < link && all && link < all->n) {
        SceneElem *a = all->e[link];
        if (a->kind == SCENE_AUDIO && scene_i16(a, 0x88) == 1 && -1 < scene_i16(a, 0x8a)) a->b[0xa2] = 1;
    }
}

void Scene_TriggerShow(const SceneElem *t)
{
    /* the linked geometry (+0xde) when its +0x78 is 1 (0x46ae90): shown (0x482fa0) */
    int16_t link = scene_i16(t, TRIG_LINK3);
    SceneList *all = Scene_List();
    if (-1 < link && all && link < all->n) {
        SceneElem *g = all->e[link];
        if (g->kind == SCENE_GEOM && scene_i16(g, 0x78) == 1) {
            float p[3];
            scene_pos(g, p);
            WSceneBundle_Trigger((uint32_t)scene_i32(g, GEOM_OBJTYPE), p);
        }
    }
}

void Scene_Tick(void)
{
    SceneList *l = Scene_EventTriggers();
    for (int i = 0; l && i < l->n; i++) {
        SceneElem *t = l->e[i];
        int type = scene_i32(t, TRIG_TYPE);
        /* (type 3 tests every racer, with the race cars' list and nothing done on a hit) */
        if ((type != 4 && type != 7) || g_ncars < 1) continue;
        /* the first race car's (the original passes it to all four tests) */
        if (!trigger_takes(t, g_cars[0])) continue;
        if (t->b[0x78] && scene_i32(t, 0x9c) != 0) continue;   /* fired, and once only */
        if (type == 4) Scene_TriggerSound(t);   /* (0x4703c0; +0xe0 1 with type 0 or 1 never here) */
        else Scene_TriggerShow(t);              /* 0x470330 */
        t->b[0x78] = 1;
    }
}
