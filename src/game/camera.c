/* The race cameras (see camera.h). */
#include "game/camera.h"
#include "game/race_progress.h"
#include "exe.h"
#include "game/car_drive.h"
#include "game/car_ground.h"
#include "game/hud.h"
#include "game/input.h"
#include "game/race.h"
#include "game/race_car.h"
#include "game/road.h"
#include "game/sim_math.h"
#include "game/world.h"
#include "pending.h"
#include "game/anim.h"
#include "game/scene.h"
#include "game/track.h"
#include "render/camera_math.h"
#include "render/drive_selftest.h"
#include "render/particles.h"
#include "render/r3dcar.h"
#include <math.h>
#include <string.h>
#include <strings.h>

RaceCamera g_racecam[2];
int32_t g_look[4];

/* 0x606a98, 0x606870, 0x606ad4: the game's pause and its single step, and a hold of the cameras (none
   of them set by the port yet). */
static int32_t s_paused, s_step, s_hold;

static float K(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float len3(const float *v) { return sqrtf(dot3(v, v)); }   /* 0x532890 */
static void unit3(float *v)                                       /* Vec3_Normalize */
{
    float l = len3(v);
    if (l > 0) {
        v[0] /= l;
        v[1] /= l;
        v[2] /= l;
    }
}
/* out = v times the matrix's rows (0x5327b0, one row vector). */
static void vec_mat(const float v[3], const float *m, float out[3])
{
    float r[3];
    for (int j = 0; j < 3; j++) r[j] = v[0] * m[j] + v[1] * m[3 + j] + v[2] * m[6 + j];
    memcpy(out, r, sizeof r);
}

static const float *car_pos(const Car *c) { return CAR_F((Car *)c, 0x330); }
static const float *car_rows(const Car *c) { return CAR_F((Car *)c, 0x364); }   /* right, up, forward */

/* The mode's entry: its offset and its flags word. */
/* Mode 1's offset is the camera's car's driver's eye once a race sets the cameras up (0x463f70 writes
   it into the table, 0x5cf620). */
static float s_driver_eye[3];
static bool s_driver_set;

static void mode_offset(int m, float out[3])
{
    if (m == 1 && s_driver_set) {
        memcpy(out, s_driver_eye, sizeof s_driver_eye);
        return;
    }
    for (int k = 0; k < 3; k++) out[k] = K(0x5cf610 + 0x10 * (uint32_t)m + 4 * (uint32_t)k);
}
static uint32_t mode_flags(int m) { return exe_u32(0x5cf61c + 0x10 * (uint32_t)m); }

static bool can_move(void) { return (!s_paused || s_step) && !s_hold; }

/* The flags from the mode's word (its bits 0 .. 5 into bits 1 .. 6), and whether the view is inside. */
static void reload_flags(RaceCamera *c)
{
    uint32_t w = mode_flags(c->mode);
    c->flags = (uint8_t)((c->flags & 0x81) | (w & 0x3f) << 1);
    c->flags2 = (uint8_t)((c->flags2 & 0xf7) | (c->mode < 2) << 3);
}

/* ---- the collision of the eye ---- */

/* Under a ceiling (0x461230): the polygons over the point (at the car's height plus *up) flagged as
   ceilings; for one not wholly below both that height and p, and whose centre is more than 2 off the
   car's height, its plane over p, less 1: when that is under the top, *up becomes the room left (at
   least 1). */
static bool under_ceiling(RaceCamera *c, const float p[3], float *up)
{
    const float *cp = car_pos(c->car);
    float q[3] = {p[0], cp[1], p[2]};
    float top = cp[1] + *up;
    bool hit = false;
    WTreeElems *v = WTree_CeilingsAt(q, 1.0f);
    for (uint32_t i = 0; v && i < v->n; i++) {
        WTreeElem *e = v->e[i];
        bool below = true;
        for (int k = 0; k < e->vt->nvertices(e); k++) {
            float y = e->vt->vertex(e, k)[1] - 1.0f;
            if (top < y || p[1] < y) below = false;
        }
        if (below) continue;
        float ctr[3];
        e->vt->centre(e, ctr);
        if (!(ctr[1] < cp[1] - 2.0f) && !(cp[1] + 2.0f < ctr[1])) continue;   /* 0x5b2580 */
        float n[3];
        e->vt->normal(e, n);
        double h = WTree_PlaneHeight(n, e->vt->vertex(e, 0), p);
        if (!(h - 1.0 < (double)top)) continue;
        float room = (float)(h - (double)q[1] - 1.0);
        if (!(room > 1.0f)) room = 1.0f;
        *up = room;
        hit = true;
    }
    WTreeElems_free(v);
    return hit;
}

/* Over the ground (0x4641f0): a ground polygon under the eye with a vertex above it less 1: the eye at
   its plane plus 1 when that is higher, if the plane is within 3.5 of the car's road height (+0x41c; the
   test 0x5cf584 is on). */
static void over_ground(RaceCamera *c, float p[3])
{
    WTreeElems *v = WTree_GroundAt(p, 1.0f);
    for (uint32_t i = 0; v && i < v->n; i++) {
        WTreeElem *e = v->e[i];
        bool clear = true;
        for (int k = 0; k < e->vt->nvertices(e); k++)
            if (p[1] <= e->vt->vertex(e, k)[1] + 1.0f) clear = false;
        if (clear) continue;
        float n[3];
        e->vt->normal(e, n);
        double h = WTree_PlaneHeight(n, e->vt->vertex(e, 0), p);
        float top = (float)(h + 1.0);
        float road = *CAR_F(c->car, 0x41c), tol = K(0x5b38ec);
        bool near = road <= (float)h + tol && (float)h - tol <= road;
        if ((double)p[1] < h + 1.0 && (near || exe_u32(0x5cf584) == 0)) {
            if (top <= p[1]) top = p[1];
            p[1] = top;
        }
    }
    WTreeElems_free(v);
}

/* Out of the objects (0x4648a0): an object of radius 2 or more that the car doesn't drive through,
   hit on the way from the eye to the car: the eye moved to the hit, a metre on towards the car. */
static void out_of_objects(RaceCamera *c, float p[3])
{
    const float *cp = car_pos(c->car);
    float seg[6] = {p[0], p[1], p[2], cp[0], cp[1], cp[2]};
    float y = p[1];
    WTreeElems *v = WTree_ObjectsAlong(cp, seg);
    for (uint32_t i = 0; v && i < v->n; i++) {
        WTreeObject *o = (WTreeObject *)v->e[i];
        float n[3], at[3];
        if (o->ovt->radius(o) < 2.0f) continue;
        if (o->ovt->hitkind(o, CAR_F(c->car, 0x33c)) == 3) continue;
        if (!o->ovt->collide(o, seg, n, at)) continue;
        float d[3] = {cp[0] - seg[0], cp[1] - seg[1], cp[2] - seg[2]};   /* 0x5320a0 */
        unit3(d);
        p[0] = at[0] + d[0];
        p[2] = at[2] + d[2];
        p[1] = (y - p[1]) + y;   /* (as the original: the height stays; the segment isn't moved) */
    }
    WTreeElems_free(v);
}

/* An edge's height against the car's road height (+0x41c), within 3.5, the test 0x5cf584 on. */
static bool edge_level(const RaceCamera *c, const float *a, const float *b)
{
    float road = *CAR_F(c->car, 0x41c), tol = K(0x5b38ec);
    float hi = a[1] < b[1] ? b[1] : a[1], lo = b[1] < a[1] ? b[1] : a[1];
    if ((hi + tol < road || road < lo - tol) && exe_u32(0x5cf584) != 0) return false;
    return true;
}

/* Inside the road's edges (0x4643c0): an edge crossed on the way from the eye to the car puts the eye
   a metre inside it; else an edge within a metre that a step outwards would cross. True if moved. */
static bool inside_edges(RaceCamera *c, float p[3])
{
    const float *cp = car_pos(c->car);
    float y = p[1];
    float seg[6] = {p[0], p[1], p[2], cp[0], cp[1], cp[2]};
    bool moved = false;
    WTreeElems *v = WTree_EdgesAlong(seg);
    for (uint32_t i = 0; v && i < v->n; i++) {
        const float *a = WTreeEdge_point(v->e[i], 0), *b = WTreeEdge_point(v->e[i], 1);
        float edge[6] = {a[0], a[1], a[2], b[0], b[1], b[2]}, at[3];
        if (!World_SegmentsCross(seg, edge, at) || !edge_level(c, a, b)) continue;
        float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        unit3(d);
        p[0] = at[0] - d[2];
        p[2] = at[2] + d[0];
        seg[0] = p[0];
        seg[2] = p[2];
        p[1] = (y - p[1]) + y;
        seg[1] = p[1];
        moved = true;
    }
    WTreeElems_free(v);
    if (moved) return true;
    y = p[1];
    v = WTree_EdgesNear(p, 1.0f);
    for (uint32_t i = 0; v && i < v->n; i++) {
        const float *a = WTreeEdge_point(v->e[i], 0), *b = WTreeEdge_point(v->e[i], 1);
        float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        unit3(d);
        float out[3] = {-d[2] * K(0x5b2624), 0, d[0] * K(0x5b2624)};
        float step[6] = {p[0], p[1], p[2], p[0] + out[0], p[1], p[2] + out[2]};
        float edge[6] = {a[0], a[1], a[2], b[0], b[1], b[2]}, at[3];
        if (!World_SegmentsCross(step, edge, at) || !edge_level(c, a, b)) continue;
        p[0] = at[0] - d[2];
        p[2] = at[2] + d[0];
        p[1] = y;
        moved = true;
    }
    WTreeElems_free(v);
    return moved;
}

/* ---- the look-at basis (0x464ab0) ---- */

/* The replay's wander of the look-at point about the car (0x6280e0 .. 0x628100): every 3 .. 6 ticks a
   new random offset within the car's speed / 20 either way (the render's random numbers), eased to at
   the replay speed's rate (1/400, 1/200 or 1/100 a frame for 1, 0, 2) unless paused. */
static int32_t s_wander_t, s_wander_n;   /* 0x6280e0, 0x6280e4 */
static float s_wander_to[3], s_wander[3];   /* 0x6280e8, 0x6280f8 */
static int32_t s_replay_paused, s_replay_speed;   /* 0x606860, 0x606864 */
static int32_t s_replay_exit, s_camkind;          /* 0x606868, 0x60686c */

static void wander(RaceCamera *c)
{
    if (s_wander_n < g_sim_tick - s_wander_t) {
        float f = *CAR_F(c->base, 0x35c) * K(0x5b3f6c), h = f * K(0x5b23f0);
        for (int k = 0; k < 3; k++) s_wander_to[k] = (float)Render_Rand() * f * K(0x5b253c) - h;
        s_wander_t = g_sim_tick;
        s_wander_n = (int32_t)lrintf((float)Render_Rand() * K(0x5b2600) * K(0x5b253c)) + 3;
    }
    float r = s_replay_speed == 1 ? K(0x5b3f88) : s_replay_speed == 0 ? K(0x5b2738) : K(0x5b27a8);
    if (!s_replay_paused && !g_drive_paused && !s_paused)
        for (int k = 0; k < 3; k++) s_wander[k] = s_wander_to[k] * r + (K(0x5b24a8) - r) * s_wander[k];
}


/* The orientation looking from the eye at target (the car looked at when NULL; during the replay's
   cameras, a random wander about it): the up row the world's, or the previous one when looking nearly
   straight up or down. */
static void look_at(RaceCamera *c, const float *target, const float eye[3], float *orient)
{
    float up[3] = {0, 1, 0}, t[3];
    if (!target) {
        if (g_replay_mode == 2 && !((c->mode >= 2 && c->mode <= 7) || c->mode == 10 || c->mode == 11)) {
            wander(c);
            for (int k = 0; k < 3; k++) t[k] = s_wander[k] + car_pos(c->base)[k];
            target = t;
        } else {
            target = car_pos(c->base);
        }
    }
    memcpy(t, target, sizeof t);
    float *fw = orient + 6;
    for (int k = 0; k < 3; k++) fw[k] = (t[k] - eye[k]) * 0.25f;   /* 0x5b2468 */
    if (c->flags & 2) fw[1] = ((1 < c->mode && c->mode < 5) ? K(0x5b3f84) : K(0x5b261c)) + fw[1];
    if (g_replay_mode == 2) up[0] = c->shake[0];
    unit3(fw);
    float d = dot3(up, fw);
    if (d < 0) d = -d;
    if (K(0x5b3f80) < d) memcpy(up, orient + 3, sizeof up);
    Vec3_Cross(up, fw, orient);
    unit3(orient);
    Vec3_Cross(fw, orient, orient + 3);
    unit3(orient + 3);
}

/* ---- the modes ---- */

/* The look keys' turn (0x461080): -90 right, 90 left, 180 back (less half the side's), halved
   together with the fourth; the angle eases a quarter of the way each frame round the shorter side,
   and the orientation turns by it. */
static void look_turn(int p)
{
    RaceCamera *c = &g_racecam[p];
    int16_t want = 0;
    if (g_look[3]) want = -0x5a;
    if (g_look[1]) want = 0x5a;
    if (g_look[2]) want = (int16_t)(0xb4 - want / 2);
    if (g_look[0]) want = (int16_t)(want / 2);
    else if (!g_look[1] && !g_look[3] && !g_look[2]) want = 0;
    int16_t cur = c->look;
    if (want == cur) return;
    if (want < 0) want = (int16_t)(want + 0x168);
    int16_t opp = (int16_t)(want + 0xb4);
    if (0x167 < opp) opp = (int16_t)(want - 0xb4);
    int d = want - cur;
    int q = d / 4;   /* (d + (d >> 31 & 3)) >> 2: towards zero */
    if (q < 1) q = -q;
    if (want < 0xb4) {
        if (want < cur) {
            if (opp < cur) {
                d += 0x168;
                q = d / 4;
                if (q < 1) q = -q;
                cur = (int16_t)(cur + q);
            } else {
                cur = (int16_t)(cur - q);
            }
            c->look = cur;
        } else if (cur < want) {
            c->look = (int16_t)(cur + q);
        }
    } else {
        if (want < cur) {
            c->look = (int16_t)(cur - q);
        } else if (cur < want) {
            if (opp < cur) {
                c->look = (int16_t)(cur + q);
            } else {
                d -= 0x168;
                q = d / 4;
                if (q < 1) q = -q;
                c->look = (int16_t)(cur - q);
            }
        }
    }
    if (0x168 < c->look) c->look = (int16_t)(c->look - 0x168);
    else if (c->look < 0) c->look = (int16_t)(c->look + 0x168);
    float m[9];
    Mat3_RotY(m, (float)c->look * K(0x5b3e84));
    Mat3_Mul(m, c->orient, c->orient);
}

/* On the hood (mode 0, 0x461c10): the car's orientation, the eye at the offset in its frame, the look-at
   point from it. */
static void mode_incar(int p)
{
    RaceCamera *c = &g_racecam[p];
    float o[3], w[3];
    mode_offset(c->mode, o);
    if (!can_move()) return;
    memcpy(c->orient, car_rows(c->car), sizeof c->orient);
    vec_mat(o, car_rows(c->car), w);
    for (int k = 0; k < 3; k++) c->lookat[k] = car_pos(c->car)[k] + w[k];
    look_turn(p);
    vec_mat(o, car_rows(c->car), w);
    for (int k = 0; k < 3; k++) c->eye[k] = w[k] + car_pos(c->car)[k];
}

/* The driver's seat (mode 1, 0x4618f0): as on the hood, with the tilt's turns (the driver's); the offset eases (a fifth across
   and up, half along) unless looking aside. Looking back puts the eye on the car's axis. */
static void mode_hood(int p)
{
    RaceCamera *c = &g_racecam[p];
    float o[3], w[3];
    mode_offset(c->mode, o);
    if (!can_move()) return;
    memcpy(c->orient, car_rows(c->car), sizeof c->orient);
    vec_mat(o, car_rows(c->car), w);
    for (int k = 0; k < 3; k++) c->lookat[k] = car_pos(c->car)[k] + w[k];
    look_turn(p);
    bool aside = g_look[3] || g_look[1];
    if (!aside && (c->tilt[0] != 0 || c->tilt[1] != 0 || c->tilt[2] != 0)) {
        Mat3_TurnX(c->orient, c->tilt[0] * K(0x5b3e84));   /* 0x556130 */
        Mat3_TurnY(c->orient, c->tilt[1] * K(0x5b3e84));   /* 0x556200 */
        Mat3_TurnZ(c->orient, c->tilt[2] * K(0x5b3e84));   /* 0x5562c0 */
    }
    if (g_look[2]) o[0] = 0;
    vec_mat(o, car_rows(c->car), w);
    if (aside) {
        for (int k = 0; k < 3; k++) c->eye[k] = w[k] + car_pos(c->car)[k];
        return;
    }
    c->offset[0] = (w[0] - c->offset[0]) * K(0x5b23f4) + c->offset[0];
    c->offset[1] = (w[1] - c->offset[1]) * K(0x5b23f4) + c->offset[1];
    c->offset[2] = (w[2] - c->offset[2]) * K(0x5b23f0) + c->offset[2];
    for (int k = 0; k < 3; k++) c->eye[k] = car_pos(c->car)[k] + c->offset[k];
}

/* Fixed on the car (modes 8, 9, 15, 0x461770): the offset in the car's frame, its height kept under
   ceilings, the orientation the car's (left, up, back). */
static void mode_fixed(int p)
{
    RaceCamera *c = &g_racecam[p];
    float o[3], w[3];
    mode_offset(c->mode, o);
    vec_mat(o, car_rows(c->car), w);
    float a = w[1], b = w[1];
    under_ceiling(c, car_pos(c->car), &a);
    under_ceiling(c, c->eye, &b);
    w[1] = b <= a ? b : a;
    for (int k = 0; k < 3; k++) c->eye[k] = w[k] + car_pos(c->car)[k];
    memcpy(c->offset, w, sizeof w);
    const float *r = car_rows(c->car);
    for (int k = 0; k < 3; k++) {
        c->orient[k] = -r[k];
        c->orient[3 + k] = r[3 + k];
        c->orient[6 + k] = -r[6 + k];
    }
}

void Camera_Snap(int p) { if (g_racecam[p].car) mode_fixed(p); }

/* The crash camera (0x461450): 3 up and 10 behind (in front when its flag says so) in the road's frame
   at the car, under ceilings, eased at 0.02 a frame; it takes the flags 0x7a. */
static void mode_crash(int p)
{
    RaceCamera *c = &g_racecam[p];
    if (!can_move()) return;
    float o[3] = {0, 3.0f, (c->flags2 & 1) ? 10.0f : -10.0f}, w[3];
    vec_mat(o, CAR_F(c->car, 0x3b8), w);
    float a = w[1], b = w[1];
    under_ceiling(c, car_pos(c->car), &a);
    under_ceiling(c, c->eye, &b);
    w[1] = b <= a ? b : a;
    float e = K(0x5b3f50);
    for (int k = 0; k < 3; k++) {
        c->offset[k] = (w[k] - c->offset[k]) * e + c->offset[k];
        c->eye[k] = car_pos(c->car)[k] + c->offset[k];
    }
    if (c->crash == 0) c->ease = 0x32;
    c->flags = (uint8_t)((c->flags & 0xfb) | 0x7a);
}

/* The chase cameras (0x461d40 near, 0x462290 far; variant 0 .. 2): the offset eased by the car's speed
   (the larger of the forward and side speeds plus a quarter of the other, times 1/900 or so, between
   0.02 and the variant's cap); higher by the road's climb over the nodes around the car; the far ones
   further back with speed. Looking back: the offset at once, reversed along the car. */
static void mode_chase(int p, int variant, bool far)
{
    RaceCamera *c = &g_racecam[p];
    Car *car = c->car;
    float o[3], rate = 0.1f, cap = 0.1f, up_rate = 0.05f;
    mode_offset(c->mode, o);
    int step = ((0 < *CAR_I(car, 0xd30)) != (g_race_reverse != 0)) ? 3 : -3;
    if (s_paused && !s_step) return;
    if (s_hold) return;
    if (!far) o[2] -= *CAR_F(car, 0x3b0);
    if (variant != 0) {
        cap = variant == 1 ? 0.1f : 0.05f;
        float f = fabsf(*CAR_F(car, 0xd60)), s = fabsf(*CAR_F(car, 0xd58));
        rate = s <= f ? s * 0.25f + f : f * 0.25f + s;
        rate *= K(0x5b3f60);
        if (cap <= rate) rate = cap;
        if (rate < K(0x5b3f50)) rate = 0.02f;
    }
    if (far && 1 < g_replay_mode) rate = 0.01f;
    if (g_look[2]) vec_mat(o, car_rows(car), c->offset);
    if (far) {
        float f = fabsf(*CAR_F(car, 0xd60)), s = fabsf(*CAR_F(car, 0xd58));
        float v = s <= f ? s * 0.25f + f : f * 0.25f + s;
        float back = v * K(0x5b3f6c);
        if (2.0f < back) back = 2.0f;
        o[2] -= back;
    }
    int16_t node;
    memcpy(&node, car->b + 8, 2);
    int ahead = Road_Step(g_road, node, step, -1);
    float ya = *(float *)(void *)(g_road->nodes + ahead * ROAD_NODE_SIZE + 8);
    int behind = Road_Step(g_road, ahead, -step, -1);
    float climb = (ya - *(float *)(void *)(g_road->nodes + behind * ROAD_NODE_SIZE + 8)) * (far ? 0.5f : K(0x5b2680));
    float lift = 0;
    if (variant == 1) {
        float hi = far ? K(0x5b3f64) : 1.0f;
        lift = hi;
        if (climb < hi) lift = climb < 0.25f ? 0.25f : climb;
    } else if (variant == 2) {
        float hi = far ? 3.0f : 2.0f, lo = far ? K(0x5b3f68) : K(0x5b3f5c);
        lift = hi;
        if (climb < hi) lift = climb < lo ? lo : climb;
    } else if (variant != 0) {
        lift = climb;
    }
    o[1] += lift;
    float w[3];
    if (!g_look[2]) {
        vec_mat(o, car_rows(car), w);
        float a = w[1], b = w[1];
        under_ceiling(c, car_pos(car), &a);
        under_ceiling(c, c->eye, &b);
        w[1] = b <= a ? b : a;
        float ry = up_rate;
        if (0 < c->ease) {
            rate = 0.1f;
            c->ease--;
            ry = K(0x5b2604);
        }
        c->offset[0] = (w[0] - c->offset[0]) * rate + c->offset[0];
        c->offset[1] = (w[1] - c->offset[1]) * ry + c->offset[1];
        c->offset[2] = (w[2] - c->offset[2]) * rate + c->offset[2];
        float minlen = far ? K(0x5b37b0) : K(0x5b3f58);
        float l = len3(c->offset);
        if (c->ease == 0 && l < minlen) for (int k = 0; k < 3; k++) c->offset[k] *= minlen / l;
        for (int k = 0; k < 3; k++) c->eye[k] = car_pos(car)[k] + c->offset[k];
        return;
    }
    vec_mat(o, car_rows(car), w);
    for (int k = 0; k < 3; k++) c->lookat[k] = car_pos(car)[k] + w[k];
    o[2] = -o[2];
    vec_mat(o, car_rows(car), w);
    float a = w[1], b = w[1];
    under_ceiling(c, car_pos(car), &a);
    under_ceiling(c, c->eye, &b);
    w[1] = b <= a ? b : a;
    for (int k = 0; k < 3; k++) c->eye[k] = w[k] + car_pos(car)[k];
}

/* Overhead (mode 13): the eye's offset from the car eased a tenth of the way to the mode's (its height
   the lower of the ceilings' room at the car and at the eye) each frame, in the world's axes. */
static void mode_overhead(int p)
{
    RaceCamera *c = &g_racecam[p];
    float o[3];
    mode_offset(c->mode, o);
    float a = o[1], b = o[1];
    under_ceiling(c, car_pos(c->car), &a);
    under_ceiling(c, c->eye, &b);
    float y = b <= a ? b : a;
    const float *cp = car_pos(c->car);
    float d[3] = {c->eye[0] - cp[0], c->eye[1] - cp[1], c->eye[2] - cp[2]};   /* 0x5320a0 */
    float t = K(0x5b2604);
    d[0] = (o[0] - d[0]) * t + d[0];
    d[1] = (y - d[1]) * t + d[1];
    d[2] = (o[2] - d[2]) * t + d[2];
    for (int k = 0; k < 3; k++) c->eye[k] = cp[k] + d[k];   /* 0x532070 */
}

/* The orbit while a screen is up (0x461680): the stored offset (0x461600) turned about the car by the
   look angle, which the orbit keys (0x461650: +3, 0x461660: -3, 0x461670: 0) change each frame; not in
   the car, on the hood or in mode 14. */
static float s_orbit[3];   /* 0x5cf710 */
static void orbit(int p)
{
    RaceCamera *c = &g_racecam[p];
    if (c->mode == 0xe || c->mode == 0 || c->mode == 1) return;
    c->look = (int16_t)(c->look + c->lookspeed);
    float m[9], w[3];
    Mat3_RotY(m, (float)c->look * K(0x5b25b8));
    memcpy(w, s_orbit, sizeof w);
    vec_mat(c->offset, m, w);
    for (int k = 0; k < 3; k++) c->eye[k] = w[k] + car_pos(c->car)[k];
}

/* ---- the replay's cameras ---- */

static void clear_shake(void);

int32_t g_tvmode;           /* 0x606850: 10 circling the car, 0x14 the scene's cameras */
static int32_t s_viewed;    /* 0x606858: the human the replay's camera cuts with */
static float s_rise_x[2], s_rise_y[2];   /* 0x628098, 0x6280a4: mode 12's camera's x and height kept */

/* The words the scene camera's flags are copied into (+0x34, +0x3c): tested as integers. */
static bool on(const float *f) { uint32_t u; memcpy(&u, f, 4); return u != 0; }
static const float *node_vec(int n, int off) { return (const float *)(const void *)(g_road->nodes + n * ROAD_NODE_SIZE + off); }

/* The way the car goes along the road: 1 forward along the node's direction (+0x1c), -1 back (with the
   reverse race's and backing up's turns). */
static int car_way(const Car *car)
{
    int16_t node;
    memcpy(&node, car->b + 8, 2);
    float d = dot3(CAR_F((Car *)car, 0x37c), node_vec(node, 0x1c));
    if (g_race_reverse) d = -d;
    return d < 0 ? -1 : 1;
}

/* The TV camera's place ahead of the car (0x463120, 0x4632d0): up to 8 nodes the way it goes (fewer
   with the follow, short of a junction), its height the camera's (3 without one), no look ahead. */
static void tv_place(RaceCamera *c, int16_t node, int way)
{
    int n = (int)((1.0f - c->tvspeed) * K(0x5b243c));
    int m = n + 1;
    if (8 < m) m = 8;
    if (way < 0) m = -m;
    int k = m, i = 0 < m ? 0 : m;
    while (m < 1 ? i < 1 : i < m) {
        if (Road_JunctionPast(g_road, node, i)) {
            k = i;
            break;
        }
        i++;
    }
    int at = Road_Step(g_road, node, k - way, -1);
    c->surf.node = (int16_t)at;
    memcpy(c->eye, Road_NodePos(g_road, at), sizeof c->eye);
    c->eye[1] = (c->f30 != 0 ? c->f30 : K(0x5b2600)) + c->eye[1];
    memset(c->f60, 0, sizeof c->f60);
}

/* The TV camera's start (mode 11, 0x463120): placed ahead of the car unless the scene's cameras place
   it. */
static void tv_start(int p)
{
    RaceCamera *c = &g_racecam[p];
    int way = car_way(c->car);
    if (*CAR_F(c->car, 0xd60) < 0) way = -way;
    int16_t node;
    memcpy(&node, c->car->b + 8, 2);
    if (g_tvmode != 0x14) tv_place(c, node, way);
    WSurface_Update(&c->surf, c->eye, true);   /* 0x473fc0 */
}

/* The zoom (0x462a40): to the scene camera's field of view at +0x48 a frame (the near plane brought
   in for the narrow ones), or by the distance to the car past +0x40, eased in by +0x44. */
static void tv_zoom(int p, float dist)
{
    RaceCamera *c = &g_racecam[p];
    float fov = 37.5f, zn = K(0x5b23f0);
    if ((s_paused && !s_step) || s_hold) return;
    if (!on(&c->f3c)) {
        if (on(&c->f34)) {
            if (c->f44 != 0) {
                c->fov = c->f38;
                c->f44 = 0;
            }
            float v = c->fov;
            c->f38 = v;
            if (v < c->f78) c->f38 = v + c->f48;
            else if (c->f78 < v) c->f38 = v - c->f48;
            fov = c->f38;
            if (fov < K(0x5b3c9c) && (zn = (K(0x5b3c9c) - fov) * (K(0x5b24a8) / fov)) <= K(0x5b23f4)) zn = K(0x5b23f4);
        }
    } else {
        if (dist < c->f40) return;
        float t = dist * c->f38 * K(0x5b3f70);
        if (K(0x5b3c9c) < c->f38) dist += fabsf(dist - t);
        else if (c->f38 < K(0x5b3c9c)) dist -= fabsf(dist - t);
        float e = fabsf(dist - c->f40), v;
        if (c->f40 < dist) v = dist - e * c->f44;
        else if (dist < c->f40) v = e * c->f44 + dist;
        else {
            /* (the original reads its camera index's bits as the float here) */
            int32_t pi = p;
            memcpy(&v, &pi, 4);
        }
        fov = v * c->f38 / dist;
        if (fov < K(0x5b3c9c) && (zn = (K(0x5b3c9c) - fov) * (K(0x5b24a8) / fov)) <= K(0x5b23f4)) zn = K(0x5b23f4);
        uint64_t u = (uint64_t)exe_u32(0x5b3f7c) << 32 | exe_u32(0x5b3f78);
        double lo;
        memcpy(&lo, &u, 8);
        float a = (float)((1.0 - (double)c->f44) * (double)c->f48 + (double)c->f44);
        if (1.0 <= (double)a) a = 1.0f;
        else if (!((double)a > lo)) a = (float)lo;
        c->f44 = a;
    }
    c->fov = fov;
    c->znear = zn;
}

/* Circling the car (mode 10, 0x462840): 4.5 across and 6 along at 1/1024 of a turn a frame, the height
   going 1, 2, 3 by a quarter turn's ease every half turn, under ceilings. */
static void mode_circle(int p)
{
    RaceCamera *c = &g_racecam[p];
    static const float h[3] = {1.0f, 2.0f, 3.0f};
    if (!can_move() || g_drive_paused) return;
    c->look++;
    float sn, cs;
    Math_SinCosTurns((float)c->look * K(0x5b25b8), &sn, &cs);
    float o[3] = {cs * K(0x5b369c), 0, sn * K(0x5b2460)};
    int16_t *phase = (int16_t *)(void *)c->padc4;   /* +0xc4 */
    if (((c->look - 0x100) & 0x1ff) == 0) (*phase)++;
    if (((c->look - 0x80) & 0x1ff) < 0x80)
        o[1] = (float)(c->look & 0x7f) * (h[(*phase + 1) % 3] - h[*phase % 3]) * K(0x5b273c) + h[*phase % 3];
    else
        o[1] = h[*phase % 3];
    float w[3];
    vec_mat(o, car_rows(c->car), w);
    float a = w[1], b = w[1];
    under_ceiling(c, car_pos(c->car), &a);
    under_ceiling(c, c->eye, &b);
    w[1] = b <= a ? b : a;
    for (int k = 0; k < 3; k++) c->eye[k] = w[k] + car_pos(c->car)[k];
}

/* The TV camera (mode 11, 0x4632d0): placed again ahead when the car has passed it (more than the
   follow's nodes on, unless it follows fully, is within 25 or the scene places it); then moved towards
   the road a node on its way (through a junction the way it goes; the look ahead's offsets along the
   node's vectors) by the car's speed along the road times the follow; then the zoom. */
static void mode_tv(int p)
{
    RaceCamera *c = &g_racecam[p];
    Car *car = c->car;
    float dist = len3((float[3]){car_pos(c->base)[0] - c->eye[0], car_pos(c->base)[1] - c->eye[1], car_pos(c->base)[2] - c->eye[2]});
    int16_t cnode;
    memcpy(&cnode, car->b + 8, 2);
    float d = dot3(CAR_F(car, 0x37c), node_vec(cnode, 0x1c));
    if (g_race_reverse) d = -d;
    int way = d < 0 ? -1 : 1;
    if (g_race_reverse ? 0 < *CAR_F(car, 0xd60) : *CAR_F(car, 0xd60) < 0) way = -way;
    if (!can_move()) return;
    WSurface_TrackNode(&c->surf, c->eye);   /* 0x4743a0 */
    int gap = cnode - c->surf.node;
    if (gap < 0) gap = -gap;
    if (g_road->nnodes / 2 < gap) gap = g_road->nnodes - gap;
    int n = (int)((1.0f - c->tvspeed) * K(0x5b243c));
    if (gap <= n + 3 || K(0x5b24a8) <= c->tvspeed || dist <= K(0x5b24dc) || g_tvmode == 0x14) {
        WSurface_Update(&c->surf, c->eye, true);
    } else {
        tv_place(c, cnode, way);
        WSurface_Update(&c->surf, c->eye, true);
    }
    float from[3];
    if (g_tvmode == 0x14) {
        memcpy(from, Road_NodePos(g_road, c->surf.node), sizeof from);
        from[1] += c->f30 == 0 ? K(0x5b2600) : c->f30;
    } else {
        memcpy(from, c->eye, sizeof from);
    }
    int to = c->surf.node + way;
    const RoadJunction *j = Road_JunctionPast(g_road, c->surf.node, way);
    if (j)
        for (int i = 0; i < j->count; i++)
            if ((0 < way && j->end[i]->dir == 0) || (way < 0 && j->end[i]->dir == 1)) to = Road_Step(g_road, c->surf.node, way, i);
    float fn = (float)to, top = (float)(g_road->nnodes - 1);
    if (top < fn) fn = top;
    if (fn < 0) fn = 0;
    int at = (int)fn;
    float t[3];
    memcpy(t, Road_NodePos(g_road, at), sizeof t);
    t[1] += c->f30 != 0 ? c->f30 : K(0x5b2600);
    static const int vec[3] = {0x28, 0x10, 0x1c};
    for (int k = 0; k < 3; k++) {
        const float *v = node_vec(at, vec[k]);
        float s = (float)way * c->f60[k];
        for (int q = 0; q < 3; q++) t[q] += v[q] * s;
    }
    float dir[3] = {t[0] - from[0], t[1] - from[1], t[2] - from[2]};
    if (dir[0] != 0 || dir[1] != 0 || dir[2] != 0) unit3(dir);
    float sp = dot3(CAR_F(car, 0x33c), node_vec(cnode, 0x1c)) * (float)way;
    if (g_race_mode == 3 && g_race_network) sp = 0;
    for (int k = 0; k < 3; k++) c->eye[k] = dir[k] * (sp * c->tvspeed) * K(0x5b2828) + c->eye[k];
    tv_zoom(p, dist);
}

/* The rising camera (mode 12, 0x462cf0): the scene camera's place, its height over where it was put
   rising with a quarter of the distance to the car times its height setting, up to that; then the
   zoom. */
static void mode_rise(int p)
{
    RaceCamera *c = &g_racecam[p];
    const float *cp = car_pos(c->car);
    float dist = len3((float[3]){c->eye[0] - cp[0], c->eye[1] - cp[1], c->eye[2] - cp[2]});
    if (s_rise_x[p] != c->eye[0]) {
        s_rise_x[p] = c->eye[0];
        s_rise_y[p] = c->eye[1];
    }
    float h = c->f30 * K(0x5b2468) * dist - K(0x5b2468);
    if (c->f30 <= h) h = c->f30;
    if (h < 0) h = 0;
    c->eye[1] = h + s_rise_y[p];
    tv_zoom(p, dist);
}

/* The scene camera `index` on camera 0 (0x413980): a cut (its +0x7c) takes its mode, place, field of
   view and orientation (unless it looks at the car, +0x88); else only its zoom and look. Then its
   distance zoom, its height, and for the TV camera its lift, follow and look ahead. */
static void scene_camera(int index)
{
    SceneElem *e = Scene_Camera(index);
    if (!e) return;
    RaceCamera *c = &g_racecam[0];
    if (g_race_mode == 3 && g_race_network && scene_i32(e, 0x78) == 0xb) {
        if (c->mode == 10) return;
        Camera_SetMode(0, 10);
        c->flags |= 8;
        return;
    }
    if (scene_i32(e, 0x7c) == 0) {
        c->f44 = 0;
        if (scene_i32(e, 0x8c)) scene_pos(e, c->campos);
        memcpy(&c->f34, e->b + 0x98, 4);
        if (scene_i32(e, 0x98)) c->f78 = scene_f32(e, 0x9c);
        if (scene_i32(e, 0x88) == 0) c->flags &= 0xf7;
    } else {
        Camera_SetMode(0, scene_i32(e, 0x78));
        if (scene_i32(e, 0x8c)) {
            scene_pos(e, c->eye);
            memcpy(c->campos, c->eye, sizeof c->campos);
        }
        memcpy(&c->f34, e->b + 0x98, 4);
        if (scene_i32(e, 0x98)) c->f38 = c->f78 = scene_f32(e, 0x9c);
        if (scene_i32(e, 0x88) == 0) {
            scene_mat(e, c->orient);
            c->flags &= 0xf7;
        }
    }
    memcpy(&c->f3c, e->b + 0x90, 4);
    if (scene_i32(e, 0x90)) c->f40 = scene_f32(e, 0x94);
    c->f30 = scene_f32(e, 0x84);
    if (c->mode == 0xb) {
        if (scene_i32(e, 0x80)) c->eye[1] += c->f30;
        c->tvspeed = scene_f32(e, 0xa0);
        memcpy(c->f60, e->b + 0xb4, sizeof c->f60);
    }
}

void Camera_ReplayCuts(bool cut)
{
    SceneList *l = Scene_CameraTriggers();
    if (!l) return;
    for (int k = 0; k < g_humans.n; k++) {
        Car *h = g_humans.c[k];
        for (int i = 0; i < l->n; i++) {
            SceneElem *t = l->e[i];
            if (!Scene_TriggerTakes(t, h)) continue;
            int link = scene_i16(t, TRIG_LINK2);
            if (*CAR_I(h, 0x10e4) == link) continue;
            *CAR_I(h, 0x10e4) = link;
            if (cut && k == s_viewed) {
                g_racecam[0].car = g_racecam[0].base = g_humans.c[s_viewed];
                scene_camera(*CAR_I(g_racecam[0].car, 0x10e4));
            }
        }
    }
}

bool Camera_ReplayDirected(void) { return g_tvmode == 0x14; }

/* The camera's mode set afresh (0x4658a0): the wander and the hold off, unless the mode's flags are
   about to be reloaded. */
static void set_mode_fresh(int p, int mode)
{
    clear_shake();
    s_hold = 0;
    if (!(g_racecam[p].flags & 1)) Camera_SetMode(p, mode);
}

/* The camera kinds the replay's camera button goes through (0x5cc654, nine): 0x14 the scene's cameras,
   then 10, 11, 8, 9, 6, 4, 15, 0. */
static int32_t camkind(void) { return (int32_t)exe_u32(0x5cc654 + 4u * (uint32_t)s_camkind); }

/* The next camera kind (a network game skipping the TV cameras, the skidpad the TV camera), on the
   viewed human: the scene's cameras (else mode 3 and kind 3) or the kind's mode. */
static void next_kind(void)
{
    bool net = g_race_mode == 3 && g_race_network;
    if (net && (camkind() == 0xb || camkind() == 0x14)) s_camkind = (s_camkind + 1) % 9;
    g_racecam[0].car = g_racecam[0].base = g_humans.c[s_viewed];
    int mode;
    if (camkind() == 0x14) {
        g_tvmode = Scene_CameraCount() != 0 ? 0x14 : 3;
        mode = 3;
    } else {
        if (camkind() == 0xb && !strcasecmp(g_track_name, exe_str(0x5cc54c))) s_camkind = (s_camkind + 1) % 9;   /* "skidpad" */
        g_tvmode = camkind();
        mode = g_tvmode;
    }
    set_mode_fresh(0, mode);
}

int Replay_Control(int what)
{
    int r = 0;
    s_step = 0;
    switch (what) {
    case 0: r = 1; break;   /* the replay again (0x606a88, 0x5e99f4 2, 0x657424 2: the caller's) */
    case 1: s_replay_exit = 1; r = 2; break;   /* restart (0x657428) */
    case 2: s_replay_exit = 1; break;          /* quit */
    case 3: s_replay_paused ^= 1; break;
    case 4: s_replay_speed = 2; break;
    case 5: s_replay_speed = 3; break;
    case 6: s_replay_speed = 1; break;
    case 7: s_replay_speed = 0; break;
    case 8:
        s_step = 1;
        s_camkind = (s_camkind + 1) % 9;
        next_kind();
        break;
    case 9: case 10:
        s_step = 1;
        s_viewed += what == 9 ? 1 : -1;
        if (s_viewed < 0) s_viewed = g_humans.n - 1;
        else if (g_humans.n <= s_viewed) s_viewed = 0;
        next_kind();
        break;
    default: break;
    }
    if (s_paused) Camera_Update();
    return r;
}

int Replay_Speed(void) { return s_replay_speed; }
bool Replay_PauseWanted(void) { return s_replay_paused != 0; }
bool Replay_ExitWanted(void) { return s_replay_exit != 0; }
void Replay_SetPaused(bool on) { s_paused = on; }
bool Replay_Paused(void) { return s_paused != 0; }
void Camera_Hold(bool on) { s_hold = on; }

void Camera_ReplayStart(void)
{
    if (Scene_CameraCount() == 0) {
        g_tvmode = 10;
        set_mode_fresh(0, 10);
    } else if (g_race_mode == 9) {
        set_mode_fresh(0, 10);
        g_tvmode = 0x14;
    } else {
        for (int k = 0; k < g_humans.n; k++) {
            *CAR_I(g_humans.c[k], 0x10e4) = Scene_FirstCamera(g_race_reverse != 0);
            scene_camera(*CAR_I(g_humans.c[k], 0x10e4));
        }
        g_tvmode = 0x14;
    }
    s_viewed = g_race_local;
    s_replay_paused = 0;
    s_replay_speed = 2;
    s_replay_exit = 0;
    s_camkind = 0;
    s_step = 0;
}

/* ---- the race's start (mode 14) ---- */

static uint32_t s_camanim;   /* 0x62893c: the track's camera animation played last */

/* The track's next camera animation (0x4719b0 with 0: each race the next of the 'CmAn' items, round),
   started now: its keys at 1/28 s each (0x3d124925), no loop, no turn (CamAnim, vtable 0x5b4634). */
static QuatAnim *camanim_next(void)
{
    if (!g_world || g_world->ncams == 0) return NULL;
    s_camanim = (s_camanim + 1) % g_world->ncams;
    const WCamAnim *w = &g_world->cams[s_camanim];
    uint32_t u = 0x3d124925u;
    float rate;
    memcpy(&rate, &u, 4);
    QuatAnim *a = QuatAnim_new(w->b, w->keys, 0, 0, rate, 0);
    if (!a) return NULL;
    a->state = 1;
    a->start = g_sim_tick;
    return a;
}

/* The fly-in (0x462df0): the animation's frame taken in the car's (its place offset in the car's axes,
   its turn after the car's, a quarter turn about x and squared up, 0x4a45f0); played out, the camera
   goes to the player's first camera (the race car's description's +0x208), or in the replay to the
   scene cameras (mode 10 without them, or in game type 9), and the cameras are updated again at once
   (the original's recursive call). */
static float s_pair_lookat[2][3];   /* 0x627e68: mode 17's point, a camera's each */

/* Mode 17 (0x463910): the camera's two cars, the followed one and the one looked at; the point halfway
   between them looked at (0x464e40), the eye four out from the first along the way from the second, as
   far again a quarter turn round, 1.5 up. (No scene, replay or camera list asks for it.) */
static void mode_pair(int p)
{
    RaceCamera *c = &g_racecam[p];
    const float *a = car_pos(c->car), *b = car_pos(c->base);
    float d[3];
    for (int k = 0; k < 3; k++) s_pair_lookat[p][k] = (a[k] + b[k]) * K(0x5b23f0);
    for (int k = 0; k < 3; k++) d[k] = a[k] - b[k];
    unit3(d);
    for (int k = 0; k < 3; k++) d[k] *= 4.0f;
    c->eye[0] = d[2] + a[0] + d[0];
    c->eye[1] = a[1] + K(0x5b246c);
    c->eye[2] = (a[2] - d[0]) + d[2];
}

/* Modes 18 and 19 (0x463a20): the eye two behind the first car on the line from the second, 1.2 up. */
static void mode_behind_pair(int p)
{
    RaceCamera *c = &g_racecam[p];
    const float *a = car_pos(c->car), *b = car_pos(c->base);
    float d[3];
    for (int k = 0; k < 3; k++) d[k] = (b[k] - a[k]) * K(0x5b2468);
    unit3(d);
    c->eye[0] = a[0] - (d[0] + d[0]);
    c->eye[1] = a[1] + K(0x5b2480);
    c->eye[2] = a[2] - (d[2] + d[2]);
}

static void mode_flyin(int p)
{
    RaceCamera *c = &g_racecam[p];
    if (!c->f20) c->f20 = camanim_next();
    if (c->f20) {
        EaglMatrix m;
        if (QuatAnim_Matrix(c->f20, &m) != 2) {   /* (CamAnim's slot 1: QuatAnim's, started) */
            float rot[9], t[3], w[3];
            EAGL_MatToRotTrans(&m, rot, t);
            vec_mat(t, car_rows(c->car), w);
            for (int k = 0; k < 3; k++) c->eye[k] = car_pos(c->car)[k] + w[k];
            Mat3_Mul(rot, car_rows(c->car), c->orient);   /* 0x5321e0 */
            EaglMatrix x;
            EAGL_MatAxisAngle(&x, 90.0f, 1.0f, 0, 0);
            EAGL_MatToRotTrans(&x, rot, t);
            Mat3_Mul(rot, c->orient, c->orient);
            Mat3_Orthonormalize(c->orient);
            return;
        }
    }
    if (g_replay_mode == 2) {
        if (Scene_CameraCount() != 0) {
            if (g_race_mode == 9) set_mode_fresh(p, 10);
            else
                for (int k = 0; k < g_humans.n; k++) {
                    *CAR_I(g_humans.c[k], 0x10e4) = Scene_FirstCamera(g_race_reverse != 0);
                    scene_camera(*CAR_I(g_humans.c[k], 0x10e4));
                }
            g_tvmode = 0x14;
        } else {
            clear_shake();
            s_hold = 0;
            if (!(c->flags & 1)) set_mode_fresh(p, 10);
        }
    } else {
        clear_shake();
        s_hold = 0;
        if (!(c->flags & 1)) set_mode_fresh(p, p < g_ncars ? DESC_I(g_cars[p], 0x208) : 2);
    }
    if (c->f20) {
        QuatAnim_free(c->f20);
        c->f20 = NULL;
    }
    Camera_Update();
}

/* ---- the modes' state ---- */

/* The replay wander's state (0x6280e0 .. 0x628100) cleared. */
static void clear_shake(void)
{
    s_wander_t = s_wander_n = 0;
    memset(s_wander_to, 0, sizeof s_wander_to);
    memset(s_wander, 0, sizeof s_wander);
}

/* The chase cameras' first offset: 4 behind the car (0x465bb0, bit 0 camera 0, bit 1 camera 1). */
static void initial_offset(int mask)
{
    for (int p = 0; p < 2; p++)
        if (mask & (1 << p) && g_racecam[p].car)
            for (int k = 0; k < 3; k++) g_racecam[p].offset[k] = car_rows(g_racecam[p].car)[6 + k] * K(0x5b245c);
}

static void mode_changed(int p)
{
    RaceCamera *c = &g_racecam[p];
    if (c->mode == 0xb) tv_start(p);
    c->ease = 0x32;
    c->look = 0;
    c->lookspeed = 0;
    reload_flags(c);
    c->flags &= 0xfe;
    if ((c->mode < 2 || 7 < c->mode) && g_replay_mode < 2) mode_fixed(p);
}

/* The car's camera list (its description's +0x208, four words: the front end's Car%d._CAMERA). */
static int listed(int p, int i)
{
    Car *car = g_racecam[p].car;
    int m = car ? DESC_I(car, 0x208 + 4 * (i & 3)) : 2;
    return m;
}

void Camera_SetMode(int p, int mode)
{
    RaceCamera *c = &g_racecam[p];
    clear_shake();
    if (c->mode < 2 || 7 < c->mode) initial_offset(p + 1);
    c->mode = (int16_t)mode;
    if (0x14 < mode) c->mode = (int16_t)listed(p, 0);
    c->f3c = 0;
    c->f40 = 37.5f;   /* 0x42160000 */
    c->f34 = 0;
    c->f38 = 37.5f;
    c->f78 = 37.5f;
    c->f44 = 1.0f;
    c->f48 = 0.02f;   /* 0x3ca3d70a */
    c->fov = 37.5f;
    c->znear = 0.5f;
    c->f30 = 3.0f;
    memset(c->f60, 0, sizeof c->f60);
    mode_changed(p);
}

void Camera_NextMode(int p)
{
    RaceCamera *c = &g_racecam[p];
    clear_shake();
    if (c->mode == 0xe || (c->flags & 1)) return;
    if (c->mode == 0xc || c->mode == 0xb) {
        c->f38 = 37.5f;
        c->fov = 37.5f;
        c->znear = 0.5f;
    }
    if (c->mode < 2 || 7 < c->mode) initial_offset(p + 1);
    c->cycle++;
    if (!(c->flags & 0x80)) c->mode = (int16_t)listed(p, c->cycle);
    else c->mode = (int16_t)(int32_t)exe_u32(0x5cf5bc + 4 * (uint32_t)(c->cycle % 3));
    if (0x14 < c->mode) c->mode = (int16_t)listed(p, 0);
    mode_changed(p);
}

void Camera_Init(int p, Car *car, int mode)
{
    RaceCamera *c = &g_racecam[p];
    memset(c, 0, sizeof *c);
    WSurface_Init(&c->surf);
    c->car = c->base = car;
    /* 0x463f70: the driver's eye (drivercampos) as mode 1's offset and the driver's turns (drivercamrot)
       as the first camera's tilt, from the camera's car. */
    const R3DCar *r = car ? car->p[CARP_R3D] : NULL;
    if (r && r->state) {
        memcpy(s_driver_eye, r->state->drivercampos, sizeof s_driver_eye);
        s_driver_set = true;
        if (p == 0) memcpy(c->tilt, r->state->drivercamrot, sizeof c->tilt);
    }
    c->mode = (int16_t)mode;
    Camera_SetMode(p, mode);
}

/* ---- each frame ---- */

void Camera_Update(void)
{
    int ncams = 1;   /* 0x627f40's top bit: the second player's camera (split screen, not ported) */
    for (int p = 0; p < ncams; p++) {
        RaceCamera *c = &g_racecam[p];
        if (!c->car) continue;
        if (c->mode != 0xb) WSurface_TrackNode(&c->surf, c->eye);   /* 0x4743a0 */
        if (HUD_ModalShown() && g_replay_mode < 2) {
            orbit(p);
            continue;
        }
        if (c->flags & 0x40) {
            if (0.5f <= *CAR_F(c->car, 0x3a0)) {
                c->crash = 0;
            } else {
                if (c->crash == 0) c->flags2 = (uint8_t)((c->flags2 & 0xfe) | (dot3(c->orient + 6, CAR_F(c->car, 0x3d0)) < 0));
                c->crash = 'd';
            }
            if (0 < c->crash) {
                c->crash--;
                mode_crash(p);
                c->flags2 &= 0xf7;
                c->flags |= 1;
                continue;
            }
        }
        if (c->flags & 1) {
            reload_flags(c);
            c->flags &= 0xfe;
            /* No replay controller (0x628c70): the camera back on the viewed car (0x5e8c00[0x65741c]). */
            if (0 <= g_race_local && g_race_local < g_ncars) c->car = c->base = g_cars[g_race_local];
            if (1 < g_replay_mode) Camera_ReplayCuts(g_tvmode == 0x14);
        }
        switch (c->mode) {
        case 0: mode_incar(p); break;
        case 1: mode_hood(p); break;
        case 2: case 3: case 4: mode_chase(p, c->mode - 2, false); break;
        case 5: case 6: case 7: mode_chase(p, c->mode - 5, true); break;
        case 8: case 9: case 0xf: mode_fixed(p); break;
        case 0xd: mode_overhead(p); break;
        case 10: mode_circle(p); break;
        case 0xb: mode_tv(p); break;
        case 0xc: mode_rise(p); break;
        case 0xe: mode_flyin(p); break;
        case 0x11: mode_pair(p); break;
        case 0x12: case 0x13: mode_behind_pair(p); break;
        default: break;
        }
    }
    s_step = 0;
}

/* ---- the view ---- */

static bool rows_ok(const float *o)
{
    for (int r = 0; r < 3; r++) {
        float d = dot3(o + 3 * r, o + 3 * r);
        if (d < K(0x5b2610) || K(0x5b3f90) < d) return false;
    }
    return true;
}

/* The eye as the views take it (0x464e40, and 0x465360 again for the listener): out of the objects and
   under the ceilings with the flag 0x10, over the ground but in modes 0, 1 and 0xc, the orientation
   turned to look at the car (flag 8) or at the pair's point (mode 0x11). */
static void view_eye(int p, RaceCamera *c, float eye[3])
{
    memcpy(eye, c->eye, 12);
    if (c->flags & 0x10) {
        out_of_objects(c, eye);
        if (inside_edges(c, eye)) {
            float up = eye[1] - car_pos(c->car)[1];
            if (under_ceiling(c, eye, &up)) eye[1] = up + car_pos(c->car)[1];
        }
    }
    if (1 < c->mode && c->mode != 0xc) over_ground(c, eye);
    if (c->flags & 8) look_at(c, NULL, eye, c->orient);
    if (c->mode == 0x11) look_at(c, s_pair_lookat[p], eye, c->orient);
}

/* The roll with the car (flag 4): its orientation turned about z by the car's +0x11c4 and about x by
   twice its +0x11c0 (less a turn with the flags' 2). */
static void view_roll(RaceCamera *c)
{
    float z[9], x[9], m[9];
    Mat3_RotZ(z, *CAR_F(c->car, 0x11c4));
    Mat3_RotX(x, (float)-((c->flags >> 1) & 1) + *CAR_F(c->car, 0x11c0) * 2.0f);
    Mat3_Mul(z, x, m);
    Mat3_Mul(m, c->orient, c->orient);
}

/* 0x465360's listener: the roll, the view's eye and the orientation, its first row turned round when
   mirrored (0x65742c). (Its writes into the main view, the field of view and near plane, are
   Camera_ToView's too.) */
void Camera_ListenerFrame(int p, float eye[3], float orient[9])
{
    RaceCamera *c = &g_racecam[p];
    if (!c->car) {
        memcpy(eye, c->eye, 12);
        memcpy(orient, c->orient, 36);
        return;
    }
    if (c->flags & 4) view_roll(c);
    view_eye(p, c, eye);
    memcpy(orient, c->orient, 36);
    if (g_r3d_mirror) orient[0] = -orient[0], orient[1] = -orient[1], orient[2] = -orient[2];
}

void Camera_ToView(int p, RCamera *v, int type)
{
    RaceCamera *c = &g_racecam[p];
    if (!c->car) return;
    /* (Nothing but the race's reset 0x410d99 writes the roll's two angles, so both turns are whole and
       the product is the orientation itself.) */
    if (c->flags & 4) view_roll(c);
    float eye[3], o[9];
    if (type == 1) {
        /* The car's own eye: its position at the mode's height, its orientation (left, up, back). */
        const float *cp = car_pos(c->car), *r = car_rows(c->car);
        eye[0] = cp[0];
        eye[1] = K(0x5cf614 + 0x10 * (uint32_t)c->mode) + cp[1];
        eye[2] = cp[2];
        for (int k = 0; k < 3; k++) {
            o[k] = -r[k];
            o[3 + k] = r[3 + k];
            o[6 + k] = -r[6 + k];
        }
    } else {
        view_eye(p, c, eye);
        memcpy(o, c->orient, sizeof o);
    }
    if (!rows_ok(o)) Mat3_Orthonormalize(o);   /* 0x4a45f0 */
    bool mirror = g_r3d_mirror != 0;
    if (type == 1) mirror = !mirror;
    if (mirror) {
        o[0] = -o[0];
        o[1] = -o[1];
        o[2] = -o[2];
    }
    v->halffov = (g_renderopts.widescreen && c->mode != 1) ? c->fov * K(0x5b2480) : c->fov;
    v->znear = (type == 1 || c->mode == 1) ? c->znear - K(0x5b3eb0) : c->znear;
    memcpy(v->frame.orient, o, sizeof o);
    memcpy(v->frame.eye, eye, sizeof eye);
}
