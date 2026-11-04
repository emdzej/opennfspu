/* True widescreen (a port addition; the original's "wide screen" option is a letterbox): the shape the
   races draw at, from --param aspect=16:9, 16:10, or auto (the display's own shape when the game starts:
   the nearer of 4:3, 16:10 and 16:9). Without the parameter, 4:3 and the game as the original.

   With auto and a wide display at the start, the shape then follows the window's: the mode list's wide
   sizes are the shape nearest the window's now (16:9 or 16:10), and a wide size asked for (the options'
   choice, kept in fe.txt) is taken in that shape at the same place, so a resized window gets its shape at
   the next mode set (the next race). */
#pragma once
#include <stdbool.h>

enum { WIDE_NONE = 0, WIDE_16_9 = 1, WIDE_16_10 = 2 };
enum { WIDE_SIZES = 4 };   /* the wide sizes of a shape, after the original's five */
int Wide_Shape(void);      /* the shape at the start, chosen once: whether there are wide sizes */
int Wide_Now(void);        /* the shape now: Wide_Shape's, or with auto the window's nearest wide one */
void Wide_Size(int shape, int i, int *w, int *h);   /* a wide shape's size i */
void Wide_Follow(int *w, int *h);   /* a wide size of either shape -> the same place in Wide_Now's */
