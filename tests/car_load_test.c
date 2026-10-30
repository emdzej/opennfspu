/* The car loaders and the car draw (render/r3dcar.c) against every car of the game: the .tpg (INI) and
   .crp load, the style options, the LODs and their parts (option filter, sort order, entries, wheel
   slots), every primitive list's streams and index lists in range of the vertices they reference, and
   one draw through EAGL against a recording THRASH (tests/eagl_fake_thrash.h). Skips without data. */
#include "eagl/eagl.h"
#include "eagl_fake_thrash.h"
#include "exe.h"
#include "render/car_ini.h"
#include "render/r3dcar.h"
#include "vfs.h"
#include "vfs_host.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* ---- the INI module on a text of its own ---- */

static void ini_test(void)
{
    static const char text[] =
        "[header]\r\nnumfiles=10\r\n  drivercampos = -0.25, 0.5,1 \r\nflag=1 ; comment\r\n\r\n"
        "[style1]\ncount=2\ngeometry1=1\ntype1=2\ntexture2=3\ntype2=7\n[last]\nx=\n";
    IniFile f = {0};
    CHECK(IniFile_Parse(&f, text, (uint32_t)strlen(text)));
    CHECK(f.ini->nsections == 3);
    CHECK(IniData_FindSection(f.ini, "style1") == 1 && IniData_FindSection(f.ini, "Style1") == -1);
    CHECK(IniData_SelectSection(f.ini, "header") == 0 && f.ini->nkeys == 3);
    CHECK(IniFile_GetInt(&f, "numfiles", 0) == 10);
    CHECK(IniFile_GetInt(&f, "missing", -5) == -5);
    CHECK(IniFile_GetBool(&f, "flag", false));
    float v[3], def[3] = {9, 9, 9};
    IniFile_GetVec3(&f, v, "drivercampos", def);
    CHECK(v[0] == -0.25f && v[1] == 0.5f && v[2] == 1.0f);
    CHECK(IniData_SelectSection(f.ini, "nope") == -1 && f.ini->cursec == 0);
    CHECK(IniData_SelectSection(f.ini, "style1") == 1 && IniFile_GetInt(&f, "type2", 0) == 7);
    CHECK(IniData_SelectSection(f.ini, "last") == 2 && IniData_Get(f.ini, "x") != NULL);
    IniFile_Free(&f);
}

/* ---- every car ---- */

static uint32_t total_parts, total_prims, cars;
static char names[128][64];
static int nnames;

static void collect(const char *name, bool is_dir, void *user)
{
    size_t l = strlen(name);
    if (is_dir || l < 5 || strcasecmp(name + l - 4, ".crp") || nnames >= 128) return;
    snprintf(names[nnames], sizeof names[nnames], "%.*s", (int)(l - 4), name);
    nnames++;
}

/* Every element of every 'pr' of every part: its index lists within the referenced vertex array. */
static void check_prims(R3DPart *p)
{
    uint16_t lo = u16(p->entry + 4), hi = u16(p->entry + 6);
    UData *end = UData_itemsend(p->article);
    for (UData *it = UData_finditem(p->article, 0x70722020u, lo, hi); it != end && UData_key(it) == 0x70722020u; it++) {
        uint32_t idx = it->id & 0xffff;
        if (idx < lo || idx > hi) break;
        const uint8_t *b = UData_data(it);
        uint32_t ns = u32(b + 0x28), nl = u32(b + 0x2c), n = it->count;
        const uint8_t *st = b + 0x30, *lists = st + 16 * ns, *area = lists + 8 * nl;
        total_prims++;
        CHECK(n % 3 == 0 || (u32(b) & 0xff) != 3);
        for (uint32_t s = 0; s < ns; s++) {
            uint16_t kind = u16(st + 16 * s + 10), index = u16(st + 16 * s + 12), list = u16(st + 16 * s + 14);
            if (kind > 3) continue;
            uint32_t id = (exe_u32(0x5e5084 + 4u * kind) & 0xffff0000u) | index;
            UData *r = UData_find(p->article, id);
            CHECK(r != end);
            if (r == end) continue;
            uint32_t nv = r->count;
            if (list == 0xffff) {
                CHECK(n <= nv);
                continue;
            }
            CHECK(list < nl);
            const uint8_t *ix = area + u32(lists + 8 * list + 4);
            for (uint32_t k = 0; k < n; k++)
                if (ix[k] >= nv) { CHECK(ix[k] < nv); break; }
        }
    }
}

static int draws;
static uintptr_t count_setstate(uint32_t state, uintptr_t value)
{
    if (state == THRASH_STATE_DRAWPRIMITIVE) {
        const ThrashPrimitive *p = (const ThrashPrimitive *)value;
        draws++;
        const ThrashVertex *v = p->verts;
        for (uint32_t i = 0; i < p->nverts; i++)
            if (!isfinite(v[i].x) || !isfinite(v[i].y)) { CHECK(!"vertex not finite"); break; }
    }
    return fake_setstate(state, value);
}

static void check_car(const char *base)
{
    char name[96];
    snprintf(name, sizeof name, "GameData\\CarModel\\%s", base);
    R3DCarState *st = calloc(1, sizeof *st);
    R3DCar *car = calloc(1, sizeof *car);
    R3DCar_Init(car, st);
    st->rot[0] = st->rot[4] = st->rot[8] = 1;
    CHECK(R3DCar_LoadCrp(car, name));
    if (!car->loaded) {
        printf("FAIL %s: not loaded\n", base);
        fails++;
        goto out;
    }
    cars++;
    CHECK(car->nlods == CAR_NLODS);
    /* LOD 1 has parts, and of each option group only the style's value is drawn. */
    CHECK(car->lods[0]->nparts > 0);
    for (uint32_t l = 0; l < car->nlods; l++) {
        R3DCarLOD *lod = car->lods[l];
        CHECK(lod->lod == l + 1);
        int8_t prev = -128;
        for (uint32_t k = 0; k < lod->nparts; k++) {
            R3DPart *p = &lod->parts[k];
            CHECK(st->options[p->info[0]] == p->info[1]);
            CHECK(u16(p->entry + 8) == lod->lod);
            CHECK((int8_t)p->info[2] >= prev);   /* sorted by the key */
            prev = (int8_t)p->info[2];
            check_prims(p);
            total_parts++;
        }
    }
    /* The size comes from LOD list index 4 (flags 0x10 clear): the traffic cars (eas*, eden*, postship)
       have no LOD 5, so theirs is 0 unless the state says otherwise. */
    if (car->lods[4]->nparts) {
        CHECK(st->halfsize[0] > 0.3f && st->halfsize[0] < 2.0f);   /* half a car's width, in metres */
        CHECK(st->radius > st->halfsize[2]);
    } else {
        CHECK(st->halfsize[0] == 0 && st->radius == 0);
    }
    CHECK(car->materials->count > 0);
    if (strcmp(base, "356a") == 0) {
        /* 356a.tpg's header (its first lines). */
        CHECK(fabsf(st->drivercampos[0] - -0.289064f) < 1e-6f);
        CHECK(car->nsf == 10);
        /* Exactly one body of Body / Body1 / Body2 (option 1, style 1 has value 0). */
        int bodies = 0;
        for (uint32_t k = 0; k < car->lods[0]->nparts; k++) {
            const char *n = UData_name(car->lods[0]->parts[k].article);
            if (n && (!strcmp(n, "Body") || !strcmp(n, "Body1") || !strcmp(n, "Body2"))) bodies++;
        }
        CHECK(bodies == 1);
        /* Wheel slot 0: the front wheel's articles; the shadow sorts after the wheel and is kept (the
           last part with the slot wins, as in the original). */
        const char *w = car->lods[0]->slot[0] ? UData_name(car->lods[0]->slot[0]) : NULL;
        CHECK(w && !strcmp(w, "WheelShadowFront"));
        /* One draw of the front end's showroom path through EAGL. */
        EaglView view;
        EAGL_ViewInit(&view, NULL);
        EAGL_ViewSetViewport(&view, 640, 480, 0, 0, 1, 1000);
        view.fov = 60;
        view.aspect = 0.75f;
        view.znear = 0.1f;
        view.zfar = 1000;
        EaglMatrix cam;
        EAGL_MatTranslation(&cam, 0, -0.6f, -6);
        *EAGL_StackTopForWrite(&view.view) = cam;
        EAGL_ViewBegin(&view);
        g_r3d_frontend = 1;
        draws = 0;
        R3DCar_Draw(car, &view.model);
        EAGL_ViewEnd(&view);
        g_r3d_frontend = 0;
        printf("356a: %u parts in LOD 1, %d primitive draws\n", car->lods[0]->nparts, draws);
        CHECK(draws > 20);
    }
out:
    R3DCar_Free(car);
    free(car);
    free(st);
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) {
        printf("car_load_test: no game data, skipped\n");
        return 0;
    }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("car_load_test: %s\n", err); return 1; }
    ini_test();   /* the vector getters read their sscanf formats from the exe */
    fake_install_graphics();
    THRASH.fn.setstate = count_setstate;
    EAGL_ContextCreate();
    EAGL_ContextInit(g_eagl, 1);
    EAGL_ContextResetStages(g_eagl);
    EAGL_SetDisplaySize(640, 480);
    vfs_list("GameData/CarModel", collect, NULL);
    for (int i = 0; i < nnames; i++) check_car(names[i]);
    printf("car_load_test: %u cars, %u parts, %u primitive lists, %d checks: %s\n", cars, total_parts,
           total_prims, checks, fails ? "FAILED" : "ok");
    return fails != 0 || cars == 0;
}
