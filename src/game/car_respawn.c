/* A race car's hits and its return to the road (see car_respawn.h). */
#include "game/car_respawn.h"
#include "exe.h"
#include "game/ai.h"
#include "game/car_drive.h"
#include "game/race_car.h"
#include "game/race_progress.h"
#include "game/road.h"
#include "game/wtree.h"
#include "game/track.h"
#include "pending.h"
#include <string.h>
#include <strings.h>

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
#define K(va) exe_f32(va)

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static int16_t car_node(Car *c)
{
    int16_t n;
    memcpy(&n, c->b + 8, 2);
    return n;
}

/* Is node n drivable (its polygon's surface) and no other active race car or traffic car on it? The
   car's own tracker follows the node. 0x40f4e0 */
static bool node_free(Car *c, int n)
{
    float p[3];
    memcpy(p, g_road->nodes + (size_t)n * ROAD_NODE_SIZE + 4, 12);
    WSurface_Update(&c->body, p, true);   /* 0x473fc0 */
    if (!(c->body.surface & 0xf)) return false;
    for (int i = 0; i < g_ncars; i++) {
        Car *o = g_cars[i];
        if (*CAR_I(o, 0) != *CAR_I(c, 0) && o->b[0x7f] && car_node(o) == n) return false;
    }
    for (int i = 0; i < g_ntraffic; i++)
        if (g_traffic[i] && car_node(g_traffic[i]) == n) return false;
    return true;
}

/* Its spins and pushes, the hit and the contact counts cleared. 0x40f460 */
static void still(Car *c)
{
    memset(CAR_F(c, 0x388), 0, 12);
    memset(CAR_F(c, 0xd64), 0, 12);
    memset(CAR_F(c, 0xd4c), 0, 12);
    *(int16_t *)(void *)(c->b + 0x418) = 0;
    *CAR_I(c, 0x42c) = 0;
    *CAR_I(c, 0x450) = 0;
    *CAR_F(c, 0x430) = 0;
    *CAR_I(c, 0x434) = 0;
    *CAR_I(c, 0x43c) = 0;
    *CAR_I(c, 0x420) = 0;
    *CAR_F(c, 0x35c) = 0;
    *CAR_I(c, 0x1104) = 0;
    *CAR_I(c, 0x40c) = 0;
}

void CarSim_Respawn(Car *c, int kind)
{
    if (g_race_type == 4 && !strcasecmp(g_track_name, exe_str(0x5cc54c))) return;   /* "skidpad" */
    float off[3] = {0, 0, 0};
    int32_t dir = *CAR_I(c, 0xeac);
    *CAR_I(c, 0x1104) = 0;
    *CAR_I(c, 0x1100) = 0;
    if (*CAR_I(c, 0x540) != 0) {
        *CAR_I(c, 0x1104) = 0;
        return;
    }
    if (kind == 0) {
        if (K(0x5b260c) < dot3(CAR_F(c, 0x3c4), CAR_F(c, 0x370)) && *CAR_F(c, 0x388) < K(0x5b24a8) && *CAR_F(c, 0x390) < K(0x5b24a8) &&
            *CAR_I(c, 0x464) != 0 && c->b[0x12] != 0 && *(uint16_t *)(void *)(c->b + 0x418) < 6 && *CAR_F(c, 0x424) < K(0x5b24a8)) {
            *CAR_I(c, 0x1104) = 0;
            return;
        }
    } else if (kind == 3) {
        PENDING_NOTE(0x40f5a0, "the respawn at a given distance (kind 3)");
    }
    int n = car_node(c);
    for (int guard = 0; !node_free(c, n) && guard < g_road->nnodes; guard++) n = Road_Step(g_road, n, g_race_reverse ? -1 : 1, -1);
    CarSim_ResetDrive(c);   /* 0x49e010 */
    if (*CAR_I(c, CAR_FLAGS) & 8) {
        float lp[2];
        AI_LinePoint(n, lp);   /* 0x403450 */
        off[0] = lp[0];
        memcpy(&off[1], g_road->nodes + (size_t)n * ROAD_NODE_SIZE + 8, 4);
        off[2] = lp[1];
    }
    CarSim_Place(c, n, off, dir);   /* 0x49b000 */
    if (g_race_players < 2 && *CAR_I(c, CAR_INDEX) == g_race_local) PENDING_NOTE(0x461770, "the player's camera after a reset");
    memset(CAR_F(c, 0x33c), 0, 12);
    still(c);
}

/* The race car's part. 0x40f7a0 */
static void race_part(Car *c)
{
    if (*CAR_I(c, CAR_FLAGS) & 0x400) return;
    int32_t *wait = CAR_I(c, 0x540);
    if (*wait != 0) {
        float v = *CAR_F(c, 0xd60), s = *CAR_F(c, 0xd58);
        if ((v < 0 ? -v : v) < K(0x5b2604) && (s < 0 ? -s : s) < K(0x5b2604)) (*wait)++;
        if (0x140 < *wait) {
            *wait = 0;
            CarSim_Respawn(c, 1);
        }
    }
    if (*wait == 0 && *CAR_I(c, 0x544) == 0 && (c->b[0xd8b] == 5 || c->b[0xd89] != 0) && *CAR_I(c, 0xb50) == 0 && 0x300 < g_sim_tick &&
        *CAR_I(c, 0xb4c) == 0 && *CAR_F(c, 0x35c) < K(0x5b2734)) {
        CarSim_Respawn(c, 2);
        *CAR_I(c, 0xb4c) = 0xa0;
        if (c->b[0xd89] != 0) c->b[0xd89] = 0;
    }
    if (0 < *CAR_I(c, 0xb4c)) (*CAR_I(c, 0xb4c))--;
    if (*CAR_I(c, 0x1104) != 0 && *(int16_t *)(void *)(c->b + 0x418) == 0) *CAR_I(c, 0x1104) = 0;
    if (*CAR_I(c, 0x1108) != 0 && c->b[0x7e] == 0) *CAR_I(c, 0x1108) = 0;
    if (*CAR_F(c, 0x430) != K(0x5b23f8)) {
        if (*CAR_I(c, 0x434) == 0 && K(0x5b2730) < *CAR_F(c, 0x430)) {
            if (*CAR_I(c, CAR_FLAGS) & 1) PENDING_NOTE(0x493d40, "the player's hit feedback");
            *CAR_I(c, 0x1104) = 1;
            for (int w = 0; w < 4; w++) *CAR_I(c, 0x870 + 0xc4 * w) = 0;
            if (*CAR_I(c, 0x1100) < 1) *CAR_I(c, 0x1100) = 0xc0;
        }
        PENDING_NOTE(0x412250, "the hits' sparks, debris (0x43f3f0) and sounds (the +0x110c events)");
        *CAR_F(c, 0x430) = 0;
        *CAR_I(c, 0x434) = 0;
        *CAR_I(c, 0x438) = 0;
    }
    /* The crash timer: three seconds, nine times faster off the road; reset while upright and going. */
    int32_t *t = CAR_I(c, 0x1100);
    if (0 < *t) {
        int was = *t;
        *t = was - 1;
        if (*CAR_I(c, 0x464) == 0) *t = was - 9;
        if (*CAR_F(c, 0x3a0) <= K(0x5b260c) || *CAR_I(c, 0xdb0) == 0) {
            if (*t < 1) CarSim_Respawn(c, 0);
        } else {
            *t = 0;
        }
    }
    if (*t < 1 && (dot3(CAR_F(c, 0x3c4), CAR_F(c, 0x370)) < K(0x5b2604) || *CAR_I(c, 0x464) == 0)) *t = 0xc0;
}

void CarSim_HitTask(void *car)
{
    Car *c = car;
    if (c->b[0x7d]) return;
    if (*CAR_F(c, 0x430) != K(0x5b23f8)) {
        *CAR_F(c, 0x458) = *CAR_F(c, 0x430);
        *CAR_I(c, 0x45c) = g_sim_tick;
        *CAR_I(c, 0x454) = *CAR_I(c, 0x434);
    }
    if (!(*CAR_I(c, CAR_FLAGS) & 0x80)) {
        PENDING_NOTE(0x40a790, "the AI's notice of a hit");
        race_part(c);   /* 0x40f7a0 */
        PENDING_NOTE(0x410050, "the car's sounds of the ground (the +0x110c events)");
        return;
    }
    PENDING_NOTE(0x4102e0, "the traffic's hit sounds");
    *CAR_F(c, 0x430) = 0;
    *CAR_I(c, 0x434) = 0;
    *CAR_I(c, 0x438) = 0;
}
