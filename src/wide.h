/* True widescreen (a port addition; the original's "wide screen" option is a letterbox): the options'
   screen size list gets wide sizes after the original's five, and a race at one of them draws at its
   true shape (Hor+); at the original's sizes the game draws as the original. Without a parameter the list
   has both shapes' sizes; --param aspect=16:9 or 16:10 keeps it to that shape's, and auto to the shape
   nearer the display's when the game starts (the nearer of 4:3, 16:10 and 16:9; 4:3 lists both).

   With auto and a wide display at the start, the shape then follows the window's: the mode list's wide
   sizes are the shape nearest the window's now (16:9 or 16:10), and a wide size asked for (the options'
   choice, kept in fe.txt) is taken in that shape at the same place, so a resized window gets its shape at
   the next mode set (the next race). */
#pragma once
#include <stdbool.h>

enum { WIDE_NONE = 0, WIDE_16_9 = 1, WIDE_16_10 = 2 };
enum { WIDE_SIZES = 4 };   /* the wide sizes of a shape, after the original's five */
int Wide_Shape(void);      /* the parameter's shape, chosen once (WIDE_NONE: no parameter, or 4:3) */
int Wide_Now(void);        /* the shape now: Wide_Shape's, or with auto the window's nearest wide one */
void Wide_Size(int shape, int i, int *w, int *h);   /* a wide shape's size i */
int Wide_List(int wh[][2]);   /* the wide sizes listed now (at most 2 * WIDE_SIZES), the count */
void Wide_Follow(int *w, int *h);   /* a wide size of either shape -> the same place in Wide_Now's */
