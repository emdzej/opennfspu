/* The replay files (see replay_file.h). */
#include "game/replay_file.h"
#include "game/ai.h"
#include "game/camera.h"
#include "game/car.h"
#include "game/car_damage.h"
#include "game/car_drive.h"
#include "game/hud_config.h"
#include "game/input.h"
#include "game/mission.h"
#include "game/music.h"
#include "game/race.h"
#include "game/race_car.h"
#include "game/race_progress.h"
#include "game/race_setup.h"
#include "game/track.h"
#include "platform.h"
#include "render/r3dcar.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    BLOCK = 0x3e24,          /* 0xf89 dwords from 0x6573e8 */
    FINAL = BLOCK,           /* 0x5ed844 */
    FILE_SIZE = 0x1ce28,     /* the block, the final tick, the replay buffer */
    DESCS = 0xca4,           /* 0x65808c: the nine car descriptions */
    NDESCS = (BLOCK - DESCS) / CARDESC_SIZE,
};

extern int32_t g_hud_opponentlist;   /* (hud.c) */

static int32_t s_mission_skip;

/* The block's fields the port has, at their offsets from 0x6573e8. */
static const struct {
    int off;
    void *p;
    int size;
} k_fields[] = {
    {0x000, &g_race_mode, 4},          /* FEGAME_TYPE */
    {0x004, &g_race_type, 4},          /* RACE_TYPE */
    {0x008, &g_race_laps, 4},          /* NUM_LAPS */
    {0x00c, &g_ai_level, 4},           /* SKILL */
    {0x010, &g_race_rally, 4},         /* RALLY */
    {0x014, &g_race_heavy, 4},         /* HEAVY */
    {0x018, &g_race_derby, 4},         /* DERBY */
    {0x01c, &g_race_players, 4},       /* COMM_MODE */
    {0x02c, &g_race_cops, 4},          /* COPS */
    {0x034, &g_race_local, 4},         /* 0x65741c */
    {0x03c, &g_race_view, 4},          /* 0x657424 */
    {0x044, &g_r3d_mirror, 4},         /* MIRROR */
    {0x048, &g_race_reverse, 4},       /* REVERSE */
    {0x05c, g_track_name, 32},         /* 0x657444 */
    {0x0ac, &g_race_linear, 4},        /* LINEAR_TRACK */
    {0x0b0, g_track_records, 32},      /* 0x657498 */
    {0x0d0, g_lap_records, 32},        /* 0x6574b8 */
    {0x0f0, g_record_names, 0x100},    /* 0x6574d8 */
    {0x1f0, g_record_cars, 0x100},     /* 0x6575d8 */
    {0x2f0, g_race_tour.race_points, 32},
    {0x310, g_race_tour.total_points, 32},
    {0x334, g_race_tour.prize, 32},
    {0x354, &g_race_tour.safe_bonus, 4},
    {0x358, g_race_tour.earnings, 32},
    {0x378, g_race_tour.bonus, 32},
    {0x398, &g_race_tour.race, 4},
    {0x39c, g_race_tour.won, 40},
    {0x3c4, g_race_tour.safe_won, 40},
    {0x3ec, &g_race_tour.runs, 4},
    {0x3f0, &g_race_tour.last_race, 4},
    {0x3f4, &g_race_tour.end_of_era, 4},
    {0x3f8, &g_race_tour.era, 4},
    {0x3fc, &g_race_tour.rally, 4},
    {0x400, g_race_tour.total_time, 32},
    {0x424, g_mission_win, 12},        /* 0x65780c */
    {0x430, g_mission_lose, 12},
    {0x43c, &g_mission_time, 4},
    {0x440, &g_mission_damage, 4},
    {0x444, &g_mission_cones, 4},
    {0x448, &g_mission_penalty, 4},
    {0x44c, &g_mission_pass, 4},
    {0x450, &g_mission_narrator, 4},
    {0x454, &g_mission_place, 4},
    {0x644, &g_race_damage, 4},        /* DAMAGE */
    {0x64c, &g_race_seed, 4},          /* RAND_SEED */
    {0x674, &g_music_shuffle, 4},      /* 0x657a5c */
    {0x898, &g_skill_test, 4},         /* SCENENUMBER */
    {0x9d8, &g_hud_imperial, 4},       /* 0x657dc0 */
    {0x9e0, &g_hud_damage_display, 4}, /* 0x657dc8 */
    {0x9ec, &g_hud_opponentlist, 4},   /* 0x657dd4 */
    {0x9f0, &g_hud_splits, 4},         /* 0x657dd8 */
    {0xa44, &s_mission_skip, 4},       /* 0x657e2c */
    {0xa4c, &g_race_quit, 4},          /* 0x657e34 */
    {0xc94, &g_ntrafficcars, 4},       /* NUMSMACKABLES */
    {0xc98, &g_nracecars, 4},          /* NUMCARS */
    {0xc9c, &g_race_inputs, 4},        /* NUMPLAYERRACECARS */
};

static uint8_t *s_loaded;   /* 0x5e9a20: the loaded copy (FILE_SIZE) */

static void put(uint8_t *f, int off, int32_t v) { memcpy(f + off, &v, 4); }
static int32_t get(const uint8_t *f, int off) { int32_t v; memcpy(&v, f + off, 4); return v; }

/* The block from the race's globals (0x414680's copy of 0x6573e8). */
static void build(uint8_t *f)
{
    s_mission_skip = g_mission_skip;
    for (size_t i = 0; i < sizeof k_fields / sizeof *k_fields; i++) memcpy(f + k_fields[i].off, k_fields[i].p, (size_t)k_fields[i].size);
    for (int i = 0; i < NDESCS; i++) memcpy(f + DESCS + CARDESC_SIZE * i, g_cardesc[i], CARDESC_SIZE);
}

bool Replay_Write(const char *file)
{
    uint8_t *f = calloc(1, FILE_SIZE);
    if (!f) return false;
    if (g_replay_mode == 0) g_replay_final = g_sim_tick;   /* (still recording) */
    build(f);
    put(f, 0x3c, 2);   /* (0x5e9a5c: the view, a replay) */
    put(f, FINAL, g_replay_final);
    memcpy(f + FINAL + 4, Input_ReplayBuffer(), REPLAY_BYTES);
    char b[48];
    snprintf(b, sizeof b, "FINAL GAMETICK=%d", g_replay_final);   /* (0x5cc678) */
    plat_log(b);
    bool ok = plat_save_user_file(file, f, FILE_SIZE);   /* ("%s%s" 0x5cc588 in 0x65b360, 0x59df20) */
    free(f);
    return ok;
}

/* A save directory file: the user's own, else the game's SaveData (as the players' files). */
static uint8_t *read_file(const char *file, size_t *n)
{
    uint8_t *d = plat_load_user_file(file, n);
    if (d) return d;
    char path[160];
    snprintf(path, sizeof path, "SaveData\\%s", file);
    return vfs_read_all(path, n);
}

bool Replay_Exists(const char *file)
{
    size_t n = 0;
    uint8_t *d = read_file(file, &n);
    free(d);
    return d != NULL;
}

bool Replay_Load(const char *file)
{
    size_t n = 0;
    uint8_t *d = file && file[0] ? read_file(file, &n) : NULL;
    if (!d) {
        plat_log("loading default replay.rpl ...");   /* (0x5cc6dc) */
        d = read_file("replay.rpl", &n);   /* (0x5cc68c) */
    }
    if (!d) {
        plat_log("No Replay file found ...");   /* (0x5cc6c0) */
        g_replay_mode = 0;
        return false;
    }
    if (!s_loaded) s_loaded = malloc(FILE_SIZE);
    if (!s_loaded) {
        free(d);
        return false;
    }
    memset(s_loaded, 0, FILE_SIZE);
    memcpy(s_loaded, d, n < FILE_SIZE ? n : FILE_SIZE);   /* (the file's own size, 0x533b50) */
    free(d);
    return true;
}

bool Replay_Loaded(void) { return s_loaded != NULL; }

void Replay_Unload(void)
{
    free(s_loaded);
    s_loaded = NULL;
}

void Replay_Apply(void)
{
    if (!s_loaded) return;
    /* (0x413830: the display settings, 0x657a48 .. 0x657a50, 0x657c84 .. 0x657d5c and the graphics ones
       from 0x657d5c, kept: none of them in k_fields) */
    for (size_t i = 0; i < sizeof k_fields / sizeof *k_fields; i++) memcpy(k_fields[i].p, s_loaded + k_fields[i].off, (size_t)k_fields[i].size);
    g_track_name[sizeof g_track_name - 1] = 0;
    g_mission_skip = s_mission_skip != 0;
    g_camera_car = g_race_local;   /* (the port's second name of 0x65741c) */
    if (g_nracecars > NDESCS) g_nracecars = NDESCS;
    for (int i = 0; i < NDESCS; i++) memcpy(g_cardesc[i], s_loaded + DESCS + CARDESC_SIZE * i, CARDESC_SIZE);
    g_replay_mode = g_race_view;   /* (0x5e99f4 from 0x657424: 2) */
    g_replay_final = get(s_loaded, FINAL);
    memcpy(Input_ReplayBuffer(), s_loaded + FINAL + 4, REPLAY_BYTES);
}
