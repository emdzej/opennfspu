/* The simulation's random numbers (the generator the race code inlines: state 0x5d1028, multiplier
   0x5d102c, the last product 0x655a0c): state = (multiplier * state) & 0xffff, the value the product's
   bits 8-23. Seeded per race (0x4a4560) so that replays and network games repeat. */
#pragma once
#include <stdint.h>

/* The next value, 0..0xffff. */
uint32_t SimRand_Next(void);
/* Multiplier 123456789, state 987654321, then n % 500 steps; the last state (n / 500 without steps).
   0x4a4560 */
uint32_t SimRand_Seed(uint32_t n);
