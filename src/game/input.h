/* The race's input packets and the replay ([input], 0x412e90-0x415fa0, 0x499150). A player's controls
   are a 4-byte packet: +0 flags (bit 0 the handbrake, bit 1 the horn, bits 3.. a command: 2 shift up, 3
   down, 5 back on the road, 7 .. 15 the lights, views and signals), +1 the steering (signed, half the
   car's), +2 the throttle, +3 the brake (half the car's). Sampled each frame (Input_Sample 0x412e90) and
   queued per player in a 128-entry ring (Input_Enqueue 0x415ea0); each even tick takes the next into the
   player's current packet (Sim_HalfStep 0x415f10), which the human car's task (0x499150) reads, through
   the replay: while racing (mode 0, 1) each packet is recorded (0x414410), eight ticks a block of the
   steering raw and the pedals and flags run-length packed (0x414080, 0x413c70) into the replay buffer
   (0x5ed848, 0x19000 bytes); replaying (mode 2) they are read back from it (0x4144d0, 0x4141c0); past
   its end, mode 3. */
#pragma once
#include "game/car.h"
#include <stdbool.h>
#include <stdint.h>

extern int32_t g_replay_mode;    /* 0x5e99f4: 0, 1 racing (recording), 2 replaying, 3 the replay ended */
extern uint8_t g_input_packet[4];   /* 0x5e99dc: the local player's last sampled packet */

/* Clears the rings and the replay (a race's start); the replay's reading restarts at its beginning. */
void Input_Reset(bool keep_replay);
/* The packet into player n's ring; false if its slot is still taken. 0x415ea0 */
bool Input_Enqueue(int player, const uint8_t packet[4]);
/* The local player's sampled packet queued (one player). 0x413820 */
void Input_QueuePacket(void);
/* The packets of half-tick n into the players' current packets. 0x415f10 */
void Sim_HalfStep(int n);
/* The human car's controls from its packet (or the AI's when AI-driven, +0xda8), its command, its
   manual gear. 0x499150 */
void CarSim_PacketTask(void *car);
