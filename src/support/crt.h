/* The C runtime's pieces the game's code calls (MSVC 6, statically linked in the original). */
#pragma once
#include <stdint.h>

/* rand (0x5a1579): MSVC's LCG (x * 214013 + 2531011, bits 16..30). The game never calls srand, so
   the seed stays 1. */
int CRT_rand(void);
