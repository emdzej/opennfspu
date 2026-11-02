/* The sky's textures (see sky_tex.h). */
#include "render/sky_tex.h"
#include "render/carpages.h"
#include "render/render.h"
#include "render/rtex.h"
#include <stdlib.h>
#include <string.h>

SkyTextures *g_skytex;

static uint32_t name_code(const char *name)   /* 0x56e058 with 4: the name's first four bytes */
{
    const uint8_t *p = (const uint8_t *)name;
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

SkyTextures *SkyTextures_ctor(SkyTextures *t, const uint8_t *fsh)
{
    memset(t, 0, sizeof *t);
    t->fsh = fsh;
    Texture *b = malloc(sizeof *b);
    TexManager *m = g_render ? g_render->textures : NULL;
    /* Named by the manager's counter ("tex%d") as the original; without one (the tests) by hand. */
    t->blank = b ? Texture_empty(b, m, 0, m ? NULL : "tex") : NULL;
    return t;
}

void SkyTextures_dtor(SkyTextures *t)
{
    t->fsh = NULL;
    if (t->blank) t->blank->vt->destroy(t->blank, 1);
    for (int i = 0; i < SKYTEX_MAX; i++)
        if (t->shape[i]) {
            if (t->tex[i]) t->tex[i]->vt->destroy(t->tex[i], 1);
            t->tex[i] = NULL;
            t->shape[i] = NULL;
        }
}

void SkyTextures_reload(SkyTextures *t, const uint8_t *fsh)
{
    t->fsh = fsh;
    for (int i = 0; i < SKYTEX_MAX; i++)
        if (t->shape[i]) {
            t->shape[i] = SHAPE_find(fsh, name_code(t->name[i]));
            if (t->tex[i]) RTex_reload(t->tex[i], t->shape[i], 1);   /* 0x44d870 ('SKY ', 1) */
        }
}

int SkyTextures_find(SkyTextures *t, const char *name)
{
    for (int i = 0; i < SKYTEX_MAX; i++)
        if (t->shape[i] && !strcmp(t->name[i], name)) return i;
    const uint8_t *s = t->fsh ? SHAPE_find(t->fsh, name_code(name)) : NULL;   /* 0x53c3f0 */
    if (!s) return -1;
    int i = 0;
    while (t->shape[i]) {
        if (++i > SKYTEX_MAX - 1) return 0;
    }
    t->shape[i] = s;
    strncpy(t->name[i], name, sizeof t->name[i]);   /* the names are at most 5 characters */
    t->name[i][sizeof t->name[i] - 1] = 0;
    return i;
}

Texture *SkyTextures_texture(SkyTextures *t, int i)
{
    if (i < 0 || i >= SKYTEX_MAX) return NULL;
    if (!t->tex[i] && t->shape[i])
        t->tex[i] = RTex_make(t->shape[i], 0x534b5920u, 1, g_render ? g_render->textures : NULL, 0);   /* 'SKY ' */
    return t->tex[i];
}

bool SkyTextures_opaque(SkyTextures *t, int i)
{
    uint32_t f = TEXTURE_formatof(t->shape[i]);
    return f == 3 || f == 4 || f == 5;
}
