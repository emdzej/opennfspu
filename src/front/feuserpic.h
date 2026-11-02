/* A player's own picture (FE_LoadDriverPictures 0x4db650): "<name>" with the first of .bmp, .jpg, .pcx,
   .tga, .tif (0x5d97b0) the game folder has, converted by gimme.dll's ConvertFile (its image 0 at its own
   size into 16 bits, SaveData\pic16.fsh) and shown in place of the driver's picture. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "front/fe2d.h"

/* An image file's pixels, 8-bit RGB top-down (malloc'd): BMP, TGA, PCX and baseline JPEG as gimme reads
   them; false for a format it can't read here (TIFF: pending; progressive JPEG) or a bad file. */
bool FEUserPic_Decode(const uint8_t *d, size_t n, const char *ext, int *w, int *h, uint8_t **rgb);

typedef enum { USERPIC_NONE, USERPIC_SHOWN, USERPIC_FAILED } UserPicState;
/* The player's own picture by name: none (the driver's picture then), shown (*img set), or a file that
   didn't convert (no picture: the original's ConvertFile failing). Kept until the name changes. */
UserPicState FEUserPic_Get(const char *name, FeImage **img);
