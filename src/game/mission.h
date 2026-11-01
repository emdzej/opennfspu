/* Factory Driver's mission in the race: the conditions the front end's race options give it (Options_Parse
   0x4b0db0's ids 0xf4 .. 0xff, the skill test 0x1c), the in-race cheat's forced result (0x427aa0) and the
   mission's verdict (HMissionSummary_v5 0x4293b0), which goes back to the front end (0x657834). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

extern int32_t g_mission_win[3];    /* 0x65780c: the pass messages' hud.loc strings (0xf4 .. 0xf6), 0 none */
extern int32_t g_mission_lose[3];   /* 0x657818: the fail ones (0xf7 .. 0xf9) */
extern int32_t g_mission_time;      /* 0x657824 (0xfa): the time limit in seconds, -1 none */
extern int32_t g_mission_damage;    /* 0x657828 (0xfb): the damage limit, -1 none */
extern int32_t g_mission_cones;     /* 0x65782c (0xfc): the cones that may fall, -1 none */
extern int32_t g_mission_penalty;   /* 0x657830 (0xfd): the seconds a fallen cone adds, -1 none */
extern int32_t g_mission_pass;      /* 0x657834: the verdict, 1 passed */
extern int32_t g_mission_narrator;  /* 0x657838 (0xfe): the narrator (FactoryPeople.fsh, speech) */
extern int32_t g_mission_place;     /* 0x65783c (0xff): the place to reach in a race, 0 a skill test */
extern int32_t g_skill_test;        /* 0x657c80 (0x1c): the skill test's scene number */
extern int32_t g_mission_forced;    /* 0x60c9ec: the cheat's verdict, -1 none (0x427ab0) */
extern bool g_mission_skip;         /* 0x657e2c: the cheat skipped the mission (HCheat_v9 0x41c100) */
extern int32_t g_mission_knocked;   /* HMissionSummary +0x140: the times cones fell (0x4297e0) */

/* The damage over the limit (0x434f00): the local racer's ten zones summed (each step truncated), a
   tenth of them; live: the car's own zones (+0x4e4), else those the race's end kept (+0xc98). False
   outside Factory Driver (game type 9) or without a limit. */
bool Mission_DamageOver(bool live);
/* The verdict at the race's end (HMissionSummary_v5's first part) into g_mission_pass: a race's place
   (+0xcd4 at most g_mission_place), else the time with the cones' penalty within the limit, the damage
   and the cones; the cheat's over all. */
void Mission_Judge(void);
/* A race's start (port: the summary's counts, the verdict and the cheat's skip cleared). */
void Mission_Reset(void);
