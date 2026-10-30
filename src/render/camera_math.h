/* 3x3 matrix helpers of the game's maths module (0x532000-0x533000) that the scene and the cameras use.
   Matrices are 9 floats, row-major, as the original stores them (rows: right, up, forward). */
#pragma once

/* out = a * b (row-major: out[r][c] = sum over k of a[r][k] * b[k][c]); out may be a or b. 0x5321e0 */
void Mat3_Mul(const float a[9], const float b[9], float out[9]);
