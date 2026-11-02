/* Port addition until the front end is ported: the race's car descriptions as the front end's hand-off
   (0x4ee430, the Car%d.* keys) would write them, for one human player's car taken from the car database
   FEData\Data\nfs5.car (FE_LoadCarList 0x4f30c0: 0x670-byte records). docs/re/cars.md has the fields. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The record whose FILENAME (+0x7f, the model: "964") or SPECNAME (+0xb1: "964coupe36") is name, as race
   car 0, human (CLASS 1); one race car, no traffic. False if the database has no such car. */
bool RaceSetup_SingleCar(const char *name);
/* n opponents (CLASS 2) in car 0's car, as the front end's race set-up writes them (0x4eb430): opponent i's
   driver the i-th record of FEData\Data\nfs5.per (0x2c bytes: +0x0 the id, +0x4 the name's string, then
   four skills and their ranges, slow, medium, fast and default) scaled by the event's difficulty (1 here),
   its personality i; the player at the back of the grid. */
bool RaceSetup_Opponents(int n);
/* n road traffic cars (CLASS 4, 0x4ec590) after the racers, at most nine cars in all: the track's
   record's seven traffic models (nfs5.trk +0x7c, ids of the car database's kind 1 records) in turn, each
   in one random colour, named "Traffic". */
bool RaceSetup_Traffic(int n, const char *track);
extern int g_race_ntraffic;
/* The career race's own traffic cars when any is set (0x4ec590 with the tournament's +0x380): the first
   n slots that hold a car id (n at most 7), all four colours 0xffff0000. */
extern int16_t g_race_traffic_slots[7];
/* The front end's cheats (0x65b940, the bits FE_Cheat reads) the descriptions follow (0x4ee430). */
extern uint32_t g_race_cheats;
/* The human car's colours the front end chose (the description's +0x218 order), when set. */
extern uint32_t g_race_colours[4];
extern bool g_race_colours_set;
/* The human car's parts (PARTINDEX ids) the front end chose, when set. */
extern int32_t g_race_parts[20];
extern bool g_race_parts_set;
/* The front end's car settings (the car object's seventeen from +0x6d0) when g_race_setup_set; else a
   new car's (0x4ee230). */
extern float g_race_setup[17];
extern bool g_race_setup_set;
/* A grid the front end chose (the knockout's, 0x4ddc00): the opponents' personalities (1-based) and
   the starting places (start[0] the player's), when g_race_grid_set. */
extern int g_race_grid_pers[8], g_race_grid_start[9];
extern bool g_race_grid_set;
/* Opponents the front end set up one by one (Factory Driver's, 0x4eb830), when g_race_opp_set: the
   nfs5.per record (0-based), the car (a SPECNAME or FILENAME; none: car 0's) and its colours (+0x218
   order), stripes package and race number. */
typedef struct {
    int pers;
    char spec[0x40];
    uint32_t colours[4];
    int32_t stripe, racenum;
} RaceOpponent;
extern RaceOpponent g_race_opp[8];
extern bool g_race_opp_set;
/* The player's name (the race description's +0x558 +0x17c: the front end's player, "" for none). */
extern char g_race_player[64];
/* The track's records the front end gives the race (its nfs5.trk record's eight, 0x4ec200; the race
   options TRACKRECORDS1.., LAPRECORDS1.., TRACKRECORDSNAME1.., TRACKRECORDSCAR1..): the race times
   (0x657498), the lap times (0x6574b8; the front end passes the same times for both), the drivers
   (0x6574d8) and their cars (0x6575d8), best first. HTrackRecords enters the player's. */
extern int32_t g_track_records[8], g_lap_records[8];
extern char g_record_names[8][32], g_record_cars[8][32];
/* The career's tournament as the front end gives the race (0x4ebb70's options, by the race's racer
   order): the points by place (TOURNAMENTRACEPOINTS 0x6576d8: 8 .. 1), the totals so far
   (TOURNAMENTTOTALPOINTS 0x6576f8), the race's prize by place (TOURNRACEPRIZEMONEY 0x65771c) and its
   safe-driving bonus (TOURNRACESAFEBONUS 0x65773c), the earnings so far (TOURNPLAYEREARNINGS 0x657740),
   the bonus prize by final standing (TOURNBONUSPRIZEMONEY 0x657760), the race (TOURNRACEINDEX
   0x657780), the player's winnings by race (0x657784, 0x6577ac: the race's own), the races run in the
   race (0x6577d4), the last race (TOURNAMENTLASTRACE 0x6577d8), the era's end (0x6577dc), the era
   (0x6577e0), a rally (TOURNAMENTRALLY 0x6577e4: by time) and the total times (0x6577e8). */
typedef struct {
    int32_t race_points[8], total_points[8], prize[8], safe_bonus, earnings[8], bonus[8];
    int32_t race, won[10], safe_won[10], runs, last_race, end_of_era, era, rally, total_time[8];
} RaceTournament;
extern RaceTournament g_race_tour;
