/* TIFF to 8-bit RGB, top-down (malloc'd), for a player's own picture (feuserpic.c). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool FETiff_Decode(const uint8_t *d, size_t n, int *w, int *h, uint8_t **rgb);
