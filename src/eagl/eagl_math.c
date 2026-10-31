/* EAGL matrices and matrix stacks (0x539600..0x539ca0, 0x539cc0, 0x539fe0). Row vectors: v' = v * M with
   the translation in elements 12..14.

   Precision: Direct3D 7 switches the x87 to 24-bit precision when the game creates its device, so the
   game's float code runs with every operation rounded to single precision: plain float arithmetic, in
   the original's order of operations (kept below term by term; sums are left to right as the original
   adds them). */
#include "eagl/eagl.h"
#include <stdlib.h>
#include <string.h>

void EAGL_MatIdentity(EaglMatrix *m)
{
    memset(m, 0, sizeof *m);
    m->m[0] = m->m[5] = m->m[10] = m->m[15] = 1.0f;
}

void EAGL_MatMul(const EaglMatrix *pa, const EaglMatrix *pb, EaglMatrix *out)
{
    const float *a = pa->m, *b = pb->m;
    float *o = out->m;
    o[0] = a[3] * b[12] + a[1] * b[4] + b[8] * a[2] + a[0] * b[0];
    o[1] = b[13] * a[3] + a[1] * b[5] + a[0] * b[1] + b[9] * a[2];
    o[2] = b[14] * a[3] + a[1] * b[6] + b[10] * a[2] + a[0] * b[2];
    o[3] = b[11] * a[2] + b[3] * a[0] + a[1] * b[7] + a[3] * b[15];
    o[4] = a[4] * b[0] + b[8] * a[6] + a[7] * b[12] + a[5] * b[4];
    o[5] = b[9] * a[6] + b[13] * a[7] + a[4] * b[1] + b[5] * a[5];
    o[6] = a[4] * b[2] + b[10] * a[6] + b[14] * a[7] + a[5] * b[6];
    o[7] = a[5] * b[7] + a[6] * b[11] + a[7] * b[15] + a[4] * b[3];
    o[8] = a[8] * b[0] + b[8] * a[10] + a[11] * b[12] + a[9] * b[4];
    o[9] = b[9] * a[10] + b[13] * a[11] + a[8] * b[1] + b[5] * a[9];
    o[10] = a[8] * b[2] + b[10] * a[10] + b[14] * a[11] + a[9] * b[6];
    o[11] = a[9] * b[7] + a[10] * b[11] + a[11] * b[15] + a[8] * b[3];
    o[12] = a[12] * b[0] + b[8] * a[14] + a[15] * b[12] + a[13] * b[4];
    o[13] = b[9] * a[14] + b[13] * a[15] + a[12] * b[1] + b[5] * a[13];
    o[14] = a[12] * b[2] + b[10] * a[14] + b[14] * a[15] + a[13] * b[6];
    o[15] = a[13] * b[7] + a[14] * b[11] + a[15] * b[15] + a[12] * b[3];
}

void EAGL_MatMulInto(const EaglMatrix *a, const EaglMatrix *b, EaglMatrix *out) { EAGL_MatMul(a, b, out); }

void EAGL_MatPostMul(EaglMatrix *m, const EaglMatrix *b)
{
    EaglMatrix t;
    EAGL_MatMul(m, b, &t);
    *m = t;
}

void EAGL_MatPreMul(EaglMatrix *m, const EaglMatrix *b)
{
    EaglMatrix t;
    EAGL_MatMul(b, m, &t);
    *m = t;
}

void EAGL_MatScale(EaglMatrix *m, float sx, float sy, float sz, float sw)
{
    memset(m, 0, sizeof *m);
    m->m[0] = sx;
    m->m[5] = sy;
    m->m[10] = sz;
    m->m[15] = sw;
}

void EAGL_MatTranslation(EaglMatrix *m, float x, float y, float z)
{
    memset(m, 0, sizeof *m);
    m->m[0] = m->m[5] = m->m[10] = m->m[15] = 1.0f;
    m->m[12] = x;
    m->m[13] = y;
    m->m[14] = z;
}

void EAGL_MatFromRotTrans(EaglMatrix *m, const float r[9], const float t[3])
{
    float *o = m->m;
    o[0] = r[0]; o[1] = r[1]; o[2] = r[2]; o[3] = 0.0f;
    o[4] = r[3]; o[5] = r[4]; o[6] = r[5]; o[7] = 0.0f;
    o[8] = r[6]; o[9] = r[7]; o[10] = r[8]; o[11] = 0.0f;
    o[12] = t[0]; o[13] = t[1]; o[14] = t[2]; o[15] = 1.0f;
}

void EAGL_MatToRotTrans(const EaglMatrix *m, float r[9], float t[3])
{
    const float *i = m->m;
    r[0] = i[0]; r[1] = i[1]; r[2] = i[2];
    r[3] = i[4]; r[4] = i[5]; r[5] = i[6];
    r[6] = i[8]; r[7] = i[9]; r[8] = i[10];
    t[0] = i[12]; t[1] = i[13]; t[2] = i[14];
}

void EAGL_MatCopy(EaglMatrix *m, const EaglMatrix *src) { *m = *src; }

/* The inverse of a rotation + translation: the 3x3 transposed, the translation rotated back. */
void EAGL_MatInvertOrtho(EaglMatrix *pm)
{
    float *m = pm->m;
    float t;
    t = m[4]; m[4] = m[1]; m[1] = t;
    t = m[2]; m[2] = m[8]; m[8] = t;
    t = m[6]; m[6] = m[9]; m[9] = t;
    float x = m[12], y = m[13], z = m[14];
    m[12] = (-(x * m[0]) - y * m[4]) - z * m[8];
    m[13] = (-(x * m[1]) - y * m[5]) - z * m[9];
    m[14] = (-(x * m[2]) - y * m[6]) - z * m[10];
}

void EAGL_MatTransform4(const EaglMatrix *pm, int n, const float *src, int sstride, float *dst, int dstride)
{
    const float *m = pm->m;
    if (!n) return;
    if (!sstride) sstride = 16;
    if (!dstride) dstride = 16;
    /* The two loops of the original sum in different orders (in place it loads every component first). */
    bool inplace = src == dst;
    for (; n; n--) {
        float x = src[0], y = src[1], z = src[2], w = src[3];
        float ox, oy, oz, ow;
        if (inplace) {
            ox = z * m[8] + y * m[4] + x * m[0] + w * m[12];
            oy = m[5] * y + m[1] * x + m[9] * z + w * m[13];
            oz = m[2] * x + z * m[10] + w * m[14] + y * m[6];
            ow = y * m[7] + m[15] * w + m[3] * x + m[11] * z;
        } else {
            ox = w * m[12] + z * m[8] + y * m[4] + x * m[0];
            oy = w * m[13] + y * m[5] + m[1] * x + z * m[9];
            oz = y * m[6] + z * m[10] + m[2] * x + w * m[14];
            ow = x * m[3] + m[11] * z + y * m[7] + m[15] * w;
        }
        dst[0] = ox;
        dst[1] = oy;
        dst[2] = oz;
        dst[3] = ow;
        src = (const float *)((const uint8_t *)src + sstride);
        dst = (float *)((uint8_t *)dst + dstride);
    }
}

void EAGL_StackInit(EaglMatStack *s, int capacity)
{
    memset(s, 0, sizeof *s);
    s->capacity = capacity ? capacity : 1;
    s->base = malloc((size_t)s->capacity * sizeof(EaglMatrix));
    s->index = 0;
    s->top = s->base;
    EAGL_MatIdentity(s->top);
    s->seen = 0;
    s->serial = 1;
    s->dirty = 1;
}

void EAGL_StackFree(EaglMatStack *s)
{
    free(s->base);
    s->base = s->top = NULL;
}

/* Push copies the top up; a full stack ignores it (and its pop then drops a level the caller still
   holds, as in the original). */
void EAGL_StackPush(EaglMatStack *s)
{
    int i = s->index + 1;
    if (i == s->capacity) return;
    s->index = i;
    s->base[i] = *s->top;
    s->dirty = 1;
    s->top = s->base + s->index;
    s->serial++;
}

void EAGL_StackPop(EaglMatStack *s)
{
    if (!s->index) return;
    s->index--;
    s->dirty = 1;
    s->top = s->base + s->index;
    s->serial++;
}

EaglMatrix *EAGL_StackTop(EaglMatStack *s) { return s->top; }

EaglMatrix *EAGL_StackTopForWrite(EaglMatStack *s)
{
    s->dirty = 1;
    s->serial++;
    return s->top;
}

/* The determinant of the leading n x n of a matrix in rows of 4, by cofactors along the first row
   (0x53a430; x87 sums, stored as floats each step). */
static double det_n(const float *m, int n)
{
    if (n == 1) return m[0];
    float sum = 0, sign = 1.0f;
    for (int c = 0; c < n; c++) {
        float minor[16];
        int k = 0;
        for (int r = 1; r < n; r++) {
            int j = 0;
            for (int cc = 0; cc < n; cc++)
                if (cc != c) minor[k + j++] = m[cc + r * 4];
            k += 4;
        }
        sum = (float)(det_n(minor, n - 1) * (double)m[c] * (double)sign + (double)sum);
        sign = -sign;
    }
    return sum;
}

bool EAGL_MatInvert(const EaglMatrix *src, EaglMatrix *dst)
{
    double det = det_n(src->m, 4);
    float d = (float)det;
    if (!((double)1e-5f <= det || d <= -1e-5f)) return false;   /* 0x5bb89c, 0x5bb898: dst untouched */
    float t[16];
    for (int r = 0; r < 4; r++)   /* 0x53a3c0: transposed */
        for (int c = 0; c < 4; c++) t[r * 4 + c] = src->m[c * 4 + r];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            float minor[16];
            int k = 0;
            for (int rr = 0; rr < 4; rr++) {
                if (rr == r) continue;
                int j = 0;
                for (int cc = 0; cc < 4; cc++)
                    if (cc != c) minor[k + j++] = t[cc + rr * 4];
                k += 4;
            }
            float sign = (r + c) & 1 ? -1.0f : 1.0f;
            dst->m[c + r * 4] = (float)((det_n(minor, 3) * (double)sign) / (double)d);
        }
    return true;
}
