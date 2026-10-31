/* The real library's random numbers (0x537db0): six 32-bit words (0x5de910..0x5de924) added into one
   another with carries, the last counting up; the value is the first word's low 16 bits. */
#pragma once
#include <stdint.h>

/* The next value (the whole first word). 0x537db0 */
uint32_t REAL_rand(void);
/* The six words from n plus fixed offsets. 0x537e80 */
void REAL_srand(uint32_t n);
/* REAL_rand()'s low 16 bits / 65536 (0x5b253c): 0 <= r < 1. 0x555f30 */
float REAL_randf(void);
