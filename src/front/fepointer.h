/* The front end's mouse pointer (0x4e1400 .. 0x4e1650): the game draws its own from FEData\Art\Pointer.fsh
   (the system's is hidden). Each frame starts it at shape 0, the arrow (0x4e1460); what is under the mouse
   asks for others and the highest asked wins (0x4e1630): 1 a control to click, 3 a car's hood or trunk,
   4 a picture to drag, 5 a car's door, 6 its roof, 7 inside the car, 8 a door open to get in, 9 busy.
   Shape 1 runs through its frames over and over; the others run forward while a mouse button is down and
   back when it is let go (0x4e1470). */
#pragma once

/* A frame begins: the arrow (0x4e1460). */
void FEPointer_Begin(void);
/* A shape asked for (0x4e1630): kept when above the one asked before this frame. */
void FEPointer_Want(int shape);
/* The pointer drawn at the mouse (0x4e1470, from 0x4d2980 with FEDEBUGPRINTSENABLED off); nothing
   without a mouse. */
void FEPointer_Draw(void);
/* Pointer.fsh let go (0x4e1450). */
void FEPointer_Free(void);
