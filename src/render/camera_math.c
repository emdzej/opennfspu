/* See camera_math.h. */
#include "render/camera_math.h"
#include <string.h>

/* Mat3_Mul 0x5321e0. Each element is three products summed in two additions; the compiler grouped them
   differently per element (x87 code, single precision as Direct3D 7 leaves the FPU), so the port adds in
   the original's order: two products first, then the third, which is a[r][0]*b[0][c] except for element
   (1,1) (the a[r][2] product last) and (2,2) (the a[r][1] product last). A copy is taken when out is one
   of the operands, as the original does. */
void Mat3_Mul(const float a[9], const float b[9], float out[9])
{
    static const unsigned char last[9] = {0, 0, 0, 0, 2, 0, 0, 0, 1};
    float ta[9], tb[9];
    if (a == out) { memcpy(ta, a, sizeof ta); a = ta; }
    if (b == out) {
        if (a != b) { memcpy(tb, b, sizeof tb); b = tb; }
        else b = ta;
    }
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            int e = r * 3 + c, l = last[e];
            int i = l == 0 ? 1 : 0, j = l == 2 ? 1 : 2;   /* the two summed first */
            float s = a[r * 3 + i] * b[i * 3 + c] + a[r * 3 + j] * b[j * 3 + c];
            out[e] = s + a[r * 3 + l] * b[l * 3 + c];
        }
}
