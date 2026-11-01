/* Factory Driver's mission in the race (mission.h). */
#include "game/mission.h"
#include "game/car.h"
#include "game/car_drive.h"
#include "game/race_progress.h"
#include "exe.h"
#include "pending.h"
#include <string.h>

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

int32_t g_mission_win[3], g_mission_lose[3];
int32_t g_mission_time = -1, g_mission_damage = -1, g_mission_cones = -1, g_mission_penalty = -1;
int32_t g_mission_pass, g_mission_narrator, g_mission_place, g_skill_test;
int32_t g_mission_forced = -1;
bool g_mission_skip;
int32_t g_mission_knocked;

static Car *local_racer(void)
{
    return g_race_local >= 0 && g_race_local < g_racers.n ? g_racers.c[g_race_local] : NULL;
}

bool Mission_DamageOver(bool live)
{
    if (g_race_mode != 9) return false;
    /* (live: the local race car's own zones, 0x5e8b34; else the local human's kept ones, 0x5e8c00) */
    Car *c = live ? (g_race_local < g_ncars ? g_cars[g_race_local] : NULL) : (g_race_local < g_humans.n ? g_humans.c[g_race_local] : NULL);
    if (!c) return false;
    int32_t sum = 0;
    for (int off = 0xc98; off < 0xcc0; off += 4) sum = (int32_t)((float)sum + *CAR_F(c, live ? off - 0x7b4 : off));
    return g_mission_damage != -1 && sum / 10 > g_mission_damage;
}

void Mission_Judge(void)
{
    PENDING_NOTE(0x42a310, "the network game's summary (0x42a310: no verdict)");
    int32_t pass = 1;
    Car *c = local_racer();
    if (g_mission_place != 0) {
        if (c && g_mission_place < *CAR_I(c, 0xcd4)) pass = 0;
    } else {
        if (g_mission_time != -1 && c) {
            int32_t t = *CAR_I(c, 0xcd8);
            if (t == 0) {
                pass = 1;
            } else {
                float k = exe_f32(0x5b24e4);
                if (g_mission_penalty != -1) t = (int32_t)((float)(g_mission_knocked * g_mission_penalty) * k + (float)t);
                if ((float)g_mission_time * k < (float)t) pass = 0;
            }
        }
        if (Mission_DamageOver(false)) pass = 0;
        if (g_mission_cones != -1 && g_mission_cones < g_mission_knocked) pass = 0;
    }
    if (g_mission_forced != -1) pass = g_mission_forced;
    g_mission_pass = pass;
}

void Mission_Reset(void)
{
    g_mission_pass = 0;
    g_mission_skip = false;
    g_mission_knocked = 0;
}
