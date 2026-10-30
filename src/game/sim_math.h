/* The simulation's matrix and vector helpers (0x532070..0x5327b0 of the maths module, 0x4a45f0). Matrices
   are 3 x 3 floats, row-major as the original stores them; angles are in turns. Mat3_Mul (0x5321e0) is in
   render/camera_math.h. */
#pragma once

void Vec3_AddArray(int n, const float *src, const float add[3], float *dst);   /* 0x532070 (0x56e410 each) */
void Mat3_RotX(float m[9], float turns);                                        /* 0x532310 */
void Mat3_RotY(float m[9], float turns);                                        /* 0x532370 */
void Mat3_RotZ(float m[9], float turns);                                        /* 0x5323d0 */
void Mat3_Transpose(const float m[9], float out[9]);                            /* 0x532770 */
/* Four rounds of m = m * (I + c1 E + c2 E^2 + c3 E^3) with E = m^T m - I (the coefficients at 0x5d1040). 0x4a45f0 */
void Mat3_Orthonormalize(float m[9]);
