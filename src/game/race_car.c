/* A race car's set-up at the race's start (see race_car.h). */
#include "game/race_car.h"
#include "exe.h"
#include "game/car_drive.h"
#include "game/car_ground.h"
#include "game/car_physics.h"
#include "game/road.h"
#include "game/scene.h"
#include "game/sim_math.h"
#include "game/wtree.h"
#include "pending.h"
#include <string.h>

int32_t g_race_reverse;
int32_t g_race_linear;
int32_t g_race_type;
int32_t g_race_knockout;
int32_t g_race_side;
int32_t g_hud_metric;

static float ground_normal[3];   /* 0x628f70: the ground's normal under the wheels */

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

#define K(va) exe_f32(va)

static void set_f(Car *c, int off, float v) { memcpy(c->b + off, &v, 4); }
static void set_i(Car *c, int off, int32_t v) { memcpy(c->b + off, &v, 4); }
static void set_bits(Car *c, int off, uint32_t v) { memcpy(c->b + off, &v, 4); }
static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }   /* Vec3_Dot */
static const float *node_vec(int n, int off) { return (const float *)(const void *)(g_road->nodes + n * ROAD_NODE_SIZE + off); }
static int32_t node_i(int n, int off) { int32_t v; memcpy(&v, g_road->nodes + n * ROAD_NODE_SIZE + off, 4); return v; }
static float node_f(int n, int off) { float v; memcpy(&v, g_road->nodes + n * ROAD_NODE_SIZE + off, 4); return v; }

/* The body tracker's fields the car's bytes mirror (+0x8 node, +0x11 changed, +0x12 none, +0x2c
   surface; in the original the tracker is the car's +0x8). */
static void mirror_body(Car *c)
{
    memcpy(c->b + 0x8, &c->body.node, 2);
    c->b[0x11] = c->body.changed;
    c->b[0x12] = c->body.none;
    memcpy(c->b + 0x2c, &c->body.surface, 2);
}

float Race_Progress(int n)
{
    float start, end;
    int count, first = 0;
    Road_SegmentRange(g_road, n, &start, &end, &count, &first);
    float p = (end - start) * ((float)(n - first) / (float)count) + start;
    if (g_race_reverse) {
        p = K(0x5b24a8) - p;
        if (!g_race_linear && p == K(0x5b24a8)) p = K(0x5b23f8);
    }
    return p;
}

void Race_GridSlot(Car *c, int start, int *node, float out[3])
{
    int dir = 1;
    if (g_race_reverse) {
        if (!g_race_linear) start = Road_Step(g_road, 0, -start, -1);
        dir = -1;
    }
    uint32_t slot = (uint32_t)DESC_I(c, 0x174);   /* the grid position (the description's +0x174) */
    int by = dir;
    if (g_race_mode == 3 && g_race_knockout)
        by = (((int)(g_ncars - slot) - 1) * (0xea / g_nracecars) + 1) * dir;
    else if (g_ncars > 2)
        by = ((int)(g_ncars - slot) * 3 + 1) * dir;
    *node = Road_Step(g_road, start, by, -1);
    uint32_t side = slot & 1;
    if (*CAR_I(c, 0xeac) * g_race_side == -1) side = 1 - side;
    int n = *node;
    float x;
    if (node_i(1, 0x38) != 0) {   /* 0x40e6b0: the road has no lane counts */
        x = side == 0 ? node_f(n, 0x48) * K(0x5b2448) : node_f(n, 0x4c) * K(0x5b23f0);
    } else {
        /* Between the lanes: the road's middle (by the lanes' widths times their counts) and a lane
           either side of it. With no lanes the division is 0 / 0, as in the original. */
        float wl = node_f(n, 0x48) * (float)node_i(n, 0x38), all = node_f(n, 0x4c) * (float)node_i(n, 0x3c) + wl;
        float mid = all * K(0x5b23f0) - wl;
        float lane = all / (float)(node_i(n, 0x3c) + node_i(n, 0x38));
        x = side == 0 ? mid - lane : lane + mid;
    }
    out[0] = x;
    out[1] = 0.5f;
    out[2] = 0.0f;
}

/* The car's orientation (+0x364) from the road's axes (+0x3b8 right, +0x3c4 up, +0x3d0 forward), turned
   round (right and forward negated) for a car going against the road. 0x417280 */
static void set_frame(Car *c, int at, bool back)
{
    const float *r = CAR_F(c, 0x3b8);
    float *m = CAR_F(c, at);
    for (int k = 0; k < 9; k++) m[k] = back && (k < 3 || k >= 6) ? -r[k] : r[k];
}

void CarSim_Place(Car *c, int n, const float off[3], int dir)
{
    int node = n < 1 ? 1 : n;
    bool back = dir != 1;
    WSurface *b = &c->body;
    WSurface_Reset(b, (int16_t)node);   /* 0x473170 */
    const float *np = node_vec(node, 4);
    float at[3] = {np[0], np[1], np[2]};
    bool zero = off[0] == K(0x5b23f8) && off[1] == K(0x5b23f8) && off[2] == K(0x5b23f8);
    /* An opponent put back on the road past the start is placed at off itself. */
    bool absolute = (*CAR_I(c, CAR_FLAGS) & 8) && g_sim_tick > 0x1c0 && !zero;
    if (absolute) memcpy(at, off, sizeof at);
    WSurface_Update(b, at, true);
    /* The polygon's mean vertex; the wheels' trackers reset to the node on the way. */
    float *mean = CAR_F(c, 0x400);
    mean[0] = mean[1] = mean[2] = 0;
    for (int i = 0; i < 4; i++) {
        WSurface_Reset(&c->surf[i], (int16_t)node);
        const float *v = WSurface_Vertex(b, i);
        mean[0] = v[0] + mean[0];
        mean[1] = v[1] + mean[1];
        mean[2] = v[2] + mean[2];
    }
    for (int k = 0; k < 3; k++) mean[k] *= K(0x5b2468);
    /* The road's axes at the node: forward along it, up the polygon's normal, right = up x forward. */
    float *right = CAR_F(c, 0x3b8), *up = CAR_F(c, 0x3c4), *fwd = CAR_F(c, 0x3d0);
    memcpy(fwd, node_vec(b->node, 0x1c), 12);
    memcpy(up, WSurface_Normal(b), 12);
    Vec3_Cross(up, fwd, right);
    Vec3_Unit(right, right);
    Vec3_Cross(right, up, fwd);
    Vec3_Unit(fwd, fwd);
    float t[9];
    Mat3_Transpose(right, t);
    set_frame(c, 0x364, back);
    set_frame(c, 0x490, back);   /* the ground frame, 0x417330 */
    memcpy(CAR_F(c, 0x3e8), WSurface_Normal(b), 12);
    memcpy(CAR_F(c, 0x3f4), WSurface_Slope(b), 12);
    Vec3_Cross(CAR_F(c, 0x3e8), CAR_F(c, 0x3f4), CAR_F(c, 0x3dc));
    float d[3] = {dot3(off, t), dot3(off, t + 3), dot3(off, t + 6)};
    float *pos = CAR_F(c, 0x330);
    if (absolute) {
        memcpy(pos, at, sizeof at);
    } else {
        const float *p = node_vec(n, 4);   /* (n itself, not the clamped node) */
        pos[0] = p[0] + d[0];
        pos[1] = p[1] + d[1];
        pos[2] = p[2] + d[2];
    }
    (void)WSurface_Vertex(b, 0);
    mirror_body(c);
    set_f(c, 0x35c, 0);
    c->b[0x7e] = 0;
    CarSim_Track(c);   /* 0x49aee0 */
    set_frame(c, 0x364, back);
    set_frame(c, 0x490, back);
    CarSim_BodyPoints(c);   /* 0x49ada0 */
    if (!(*CAR_I(c, CAR_FLAGS) & 0x80)) {
        /* The wheels' points (+0x34, +0x40 of each) at the body's corner points. */
        for (int w = 0; w < 4; w++) {
            memcpy(CAR_F(c, 0x7f8 + 0xc4 * w), CAR_F(c, 0x1dc + 12 * w), 12);
            memcpy(CAR_F(c, 0x7ec + 0xc4 * w), CAR_F(c, 0x1dc + 12 * w), 12);
        }
    }
    if (c->b[0x7f]) {
        /* On the ground: down by the suspension's settling, the contacts again, the wheels at rest. */
        set_f(c, 0x41c, CarSim_Ground(c, ground_normal));
        float settle = *CAR_F(c, 0x424);
        set_f(c, 0x424, 0);
        pos[1] = pos[1] - settle;
        set_f(c, 0x41c, CarSim_Ground(c, ground_normal));
        for (int w = 0; w < 4; w++) {
            set_f(c, 0x83c + 0xc4 * w, 0);
            set_f(c, 0x820 + 0xc4 * w, 0);
            set_f(c, 0x828 + 0xc4 * w, 0);
        }
    }
    CarSim_BodyPoints(c);
    uint16_t s;
    memcpy(&s, c->b + 0x2c, 2);
    set_i(c, 0x460, s);
    set_i(c, 0x464, s & 0xf);
}

/* The race's per-car counters and checkpoints cleared (+0xb24 the countdown's end tick). 0x40f110 */
static void clear_race(Car *c)
{
    set_i(c, 0xaf8, 0);
    set_i(c, 0xaf4, 0);
    memset(c->b + 0xb10, 0, 0x14);
    set_i(c, 0xb24, 0x1c0);
    memset(c->b + 0xad4, 0, 0x20);
    memset(c->b + 0xb28, 0, 0x20);
    memset(c->b + 0xb48, 0, 0x24);
    memset(c->b + 0xd00, 0, 0x2c);
}

/* The parts' wear (+0x164) moved by the damage of the zones each part sits in (+0x48: ten flags, the
   zones at the car's +0x4e4), scaled by 1.5 - the part's +0x60 / 200; negated when applying. Without
   apply the wear stops at 100 and is kept at the car's +0xc48. 0x493c60 */
static void parts_wear(Car *c, bool apply)
{
    for (int i = 0; i < CAR_NPARTS; i++) {
        uint8_t *p = c->p[CARP_PARTS + i];
        int16_t kind;
        memcpy(&kind, p + 0x26, 2);
        if (kind <= 0 || kind >= 0xb) continue;
        float f60, wear;
        memcpy(&f60, p + 0x60, 4);
        float k = K(0x5b246c) - f60 * K(0x5b3284);
        if (apply) k = -k;
        int nz = 0;
        float z = K(0x5b23f8);
        for (int j = 0; j < 10; j++)
            if (p[0x48 + j]) {
                z = z + *CAR_F(c, 0x4e4 + 4 * j);
                nz++;
            }
        if (nz > 1) z = z / (float)nz;
        memcpy(&wear, p + 0x164, 4);
        wear = z * k + wear;
        memcpy(p + 0x164, &wear, 4);
        if (!apply) {
            if (K(0x5b242c) < wear) {
                wear = 100.0f;
                memcpy(p + 0x164, &wear, 4);
            }
            memcpy(c->b + 0xc48 + 4 * i, p + 0x164, 4);
        }
    }
}

/* The AI's state for the race (its direction along the road, its targets and rates). 0x406db0 */
static void ai_reset(Car *c)
{
    uint32_t flags = (uint32_t)*CAR_I(c, CAR_FLAGS);
    int dir = g_race_reverse ? -1 : 1;
    set_i(c, 0xeb0, dir);
    set_i(c, 0xeac, dir);
    set_f(c, 0x1098, 1.0f);
    memset(c->b + 0x1064, 0, 0x10);   /* +0x1064 .. +0x1070 */
    set_i(c, 0x1078, 0);
    set_i(c, 0x107c, 0);
    set_i(c, 0xea8, 0);
    set_i(c, 0x104c, 1);
    set_i(c, 0x1050, 0);
    set_i(c, 0x1048, 0);
    set_i(c, 0x101c, 0);
    memset(c->b + 0xeb4, 0, 0x20);    /* +0xeb4 .. +0xed0 */
    set_i(c, 0x538, 0);
    /* +0xed4 holds the car's own address in the original (a 32-bit slot; the AI isn't ported). */
    set_i(c, 0xedc, 0);
    memset(c->b + 0xee4, 0, 0x10);    /* +0xee4 .. +0xef0 */
    set_i(c, 0x1080, 0);
    set_i(c, 0x1074, 0);
    set_f(c, 0x109c, 1.0f);
    set_i(c, 0xe7c, 0);
    set_i(c, 0x10a4, 0);
    set_i(c, 0x10ac, 0);
    set_f(c, 0x10a8, 1.0f);
    set_f(c, 0x1024, 100.0f);
    set_f(c, 0x1028, 1.0f);
    set_i(c, 0x1020, 7);
    set_i(c, 0x10b0, 0);
    set_i(c, 0x10b4, 0);
    set_i(c, 0x10b8, flags & 1);
    set_i(c, 0x10a0, 0);
    if (!(flags & 8) && (flags & 2)) c->b[0x7f] = 0;
    set_f(c, 0x10bc, 1.0f);
    set_f(c, 0x10d4, 1.0f);
    set_f(c, 0xea0, 1.0f);
    memset(c->b + 0x10c0, 0, 12);
    set_i(c, 0x10cc, -1);
    set_i(c, 0x10d0, 0);
    set_i(c, 0x10e0, 0);
    memset(c->b + 0xe94, 0, 12);
    set_i(c, 0xea4, 0);
}

void Car_RaceInit(Car *c, uint32_t id)
{
    clear_race(c);
    WSurface_Reset(&c->body, 0);
    mirror_body(c);
    set_i(c, 0x550, 0);
    set_i(c, 0x554, 0);
    set_bits(c, 0xafc, 0x424c0000u);   /* 50 */
    set_bits(c, 0xb00, 0x3ca3d70au);   /* 0.02 */
    set_bits(c, 0xb04, 0x3f7ae148u);   /* 0.98 */
    set_bits(c, 0xb08, 0x3c23d70au);   /* 0.01 */
    set_i(c, 0xb0c, 0);
    int dir = g_race_reverse ? -1 : 1;
    set_i(c, 0xeb0, dir);
    set_i(c, 0xeac, dir);
    if (!(*CAR_I(c, CAR_FLAGS) & 0x80)) {
        float mass = *SPEC_F(c, 0x40) * K(0x5b273c);
        CarSim_Init(c, (int32_t)(id | 0x100), mass, *CAR_F(c, 0x3a8), *CAR_F(c, 0x3ac), *CAR_F(c, 0x3b0));
        /* The damage carried into the race: the zones and the parts' wear. */
        memcpy(c->b + 0x4e4, CAR_DESCB(c) + 0xc8, 0x28);
        for (int i = 0; i < CAR_NPARTS; i++) memcpy((uint8_t *)c->p[CARP_PARTS + i] + 0x164, CAR_DESCB(c) + 0x78 + 4 * i, 4);
        parts_wear(c, true);
        CarSim_ResetDrive(c);   /* 0x49e010 */
        SceneElem *start = Scene_FindTriggerByType(2);   /* the start line */
        if (!start || g_race_type != 4) {
            int from = 1;
            if (!start) {
                if (g_race_linear) from = g_road->length > 99 ? 50 : 1;
            } else {
                float p[3];
                WSurface_Reset(&c->body, 1);
                scene_pos(start, p);
                WSurface_TrackNode(&c->body, p);
                from = c->body.node;
            }
            int node;
            float off[3];
            Race_GridSlot(c, from, &node, off);   /* 0x4105e0 */
            CarSim_Place(c, node, off, g_race_reverse ? -1 : 1);
            int at = c->body.node;
            set_f(c, 0xafc, (float)at);
            float p = Race_Progress(at);
            if (!g_race_linear) {
                set_f(c, 0xb00, 0);
                set_f(c, 0xb04, 1.0f);
                set_f(c, 0xb08, -1.0f);
            } else {
                set_f(c, 0xb00, p);
                set_f(c, 0xb08, p - K(0x5b2738));
            }
            set_f(c, 0xb0c, p);
            SceneElem *finish = Scene_FindTriggerByType(1);
            if (finish) {
                WSurface t = c->body;
                float q[3];
                scene_pos(finish, q);
                WSurface_TrackNode(&t, q);
                set_f(c, 0xb04, Race_Progress(t.node));
            }
        } else {
            PENDING_NOTE(0x410490, "the skill test's start (0x410490, 0x470330)");
            set_i(c, 0xb58, 1);
        }
        PENDING_NOTE(0x478e90, "the wheels' skid trails reset (the wheel objects' +0)");
    } else {
        PENDING_NOTE(0x4a1060, "the traffic car's set-up (0x4a1060, 0x4a12f0)");
        CarSim_Init(c, (int32_t)(id | 0x100), *CAR_F(c, 0x354), *CAR_F(c, 0x3a8), *CAR_F(c, 0x3ac), *CAR_F(c, 0x3b0));
        CarSim_ResetDrive(c);
    }
    set_i(c, 0x530, 0);
    set_i(c, 0x534, 0);
    set_i(c, 0x560, 0);
    set_i(c, 0x55c, 0);
    set_i(c, 0x564, 0);
    set_i(c, 0xdac, 0);
    set_i(c, 0xda8, DESC_I(c, 0x144));   /* driven by the AI */
    set_i(c, 0xda0, 0);
    c->b[0xd9c] = 0;
    c->b[0xd9d] = 0;
    set_i(c, 0xda4, 0);
    set_f(c, 0x53c, 1.0f);
    set_f(c, 0x108c, 1.0f);
    set_i(c, 0x1090, 0);
    set_i(c, 0xd2c, 0);
    set_i(c, 0x540, 0);
    set_i(c, 0x544, 0);
    set_i(c, 0x54c, 0);
    memset(c->b + 0xd34, 0, 0x48);   /* +0xd34 .. +0xd78 */
    CarSim_Track(c);
    for (int w = 0; w < 4; w++) {
        c->surf[w] = c->body;   /* 0x4730f0 */
        int o = 0x7ec + 0xc4 * w;
        memset(c->b + o, 0, 0x58);
        set_f(c, o + 0x1c, 1.0f);
        set_i(c, o + 0x58, 1);
    }
    for (int w = 0; w < 4; w++) {
        set_i(c, 0x86c + 0xc4 * w, 0);
        set_i(c, 0x878 + 0xc4 * w, w < 2);   /* the front wheels */
    }
    set_i(c, 0xacc, 0);
    set_i(c, 0xdb0, 1);
    set_i(c, 0xac8, 0);
    set_i(c, 0xe08, 0);
    set_i(c, 0xe04, 0);
    memset(c->b + 0xdb4, 0, 0x18);
    for (int w = 0; w < 4; w++) set_i(c, 0x870 + 0xc4 * w, 0);
    c->b[0xd7c] = 0;
    c->b[0xd7d] = 0;
    c->b[0xd7e] = 1;
    set_i(c, 0xd90, 0);
    c->b[0xd83] = 0;
    c->b[0xd84] = 0;
    c->b[0xd7f] = 0;
    c->b[0xd80] = 0;
    set_i(c, 0xd94, 0);
    c->b[0xd82] = 1;
    c->b[0xd81] = 1;
    c->b[0xd85] = 0;
    c->b[0xd86] = 0;
    c->b[0xd87] = g_hud_metric ? 3 : 0;
    c->b[0xd89] = 0;
    c->b[0xd88] = 0;
    set_i(c, 0xd98, 0);
    c->b[0xd8b] = 0;
    c->b[0xd8c] = 0;
    set_i(c, 0xdfc, 0);
    set_i(c, 0xe00, 0);
    ai_reset(c);
    set_i(c, 0x10e4, 0);
    set_i(c, 0x110c, 0);
    set_i(c, 0x1108, 0);
    set_i(c, 0x1100, 0);
    set_i(c, 0x1104, 0);
    memset(c->b + 0x418, 0, 2);
    set_i(c, 0x42c, 0);
    set_i(c, 0x450, 0);
    set_i(c, 0x430, 0);
    set_i(c, 0x434, 0);
    set_i(c, 0x43c, 0);
    set_i(c, 0x420, 0);
    set_i(c, 0xad0, 0);
    set_i(c, 0x40c, 0);
    set_i(c, 0x35c, 0);
    memset(c->b + 0x11c0, 0, 0x1c);
}
