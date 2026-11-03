/* True widescreen (a port addition; the original's "wide screen" option is a letterbox): the shape the
   races draw at, from --param aspect=16:9, 16:10, or auto (the display's own shape when the game starts:
   the nearer of 4:3, 16:10 and 16:9). Without the parameter, 4:3 and the game as the original. */
#pragma once

enum { WIDE_NONE = 0, WIDE_16_9 = 1, WIDE_16_10 = 2 };
int Wide_Shape(void);   /* the shape, chosen once */
