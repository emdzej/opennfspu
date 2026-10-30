/* The scene module (game/scene.c) against every track's .scn files: element counts per kind equal the
   keywords in the files, fields are finite, the start and end lines exist in both directions, trigger
   links point into the list, cameras of the race's direction exist, names come without the line end,
   skill tests load. Skips (exit 0) without data. */
#include "exe.h"
#include "game/scene.h"
#include "game/track.h"
#include "vfs.h"
#include "vfs_host.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* An independent count of a keyword at line starts in all of a track's files the loader takes. */
typedef struct { char names[256][64]; int n; } Files;
static void collect(const char *name, bool is_dir, void *user)
{
    Files *f = user;
    size_t l = strlen(name);
    if (!is_dir && l > 4 && !strcasecmp(name + l - 4, ".scn") && f->n < 256) snprintf(f->names[f->n++], 64, "%s", name);
}

static int count_kw(const char *path, const char *kw)
{
    size_t n = 0;
    uint8_t *d = vfs_read_all(path, &n);
    if (!d) return 0;
    int c = 0;
    size_t kl = strlen(kw);
    for (size_t i = 0; i + kl <= n; i++)
        if ((i == 0 || d[i - 1] == '\n') && !memcmp(d + i, kw, kl) && (i + kl == n || d[i + kl] == ' ')) c++;
    free(d);
    return c;
}

static bool finite3(const float *v) { return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]); }

static bool has_prefix_ci(const char *s, const char *p) { return strncasecmp(s, p, strlen(p)) == 0; }

static void check_list(const char *track, const Files *dir, bool reverse)
{
    static const char *kws[] = {"AUDIO_ELEMENT", "GEOM_ELEMENT", "TRIGGER_ELEMENT", "CAMERA_ELEMENT", "PARTICLE_ELEMENT"};
    int want[5] = {0}, got[5] = {0};
    char pre[64];
    snprintf(pre, sizeof pre, "%s", track);
    size_t tl = strlen(pre);
    for (int i = 0; i < dir->n; i++) {
        const char *nm = dir->names[i];
        if (!has_prefix_ci(nm, pre)) continue;
        bool start = !strncasecmp(nm + tl, "_fstart", 7), bstart = !strncasecmp(nm + tl, "_bstart", 7),
             st = !strncasecmp(nm + tl, "_st", 3);
        if (st && !start) continue;                     /* skill tests: not in a race */
        if (bstart && !reverse) continue;
        if (start && reverse) continue;
        char path[300];
        snprintf(path, sizeof path, "%s%s", g_track_dir, nm);
        for (int k = 0; k < 5; k++) want[k] += count_kw(path, kws[k]);
    }
    Track_LoadScenes(track, 0, reverse);
    SceneList *l = Scene_List();
    CHECK(l != NULL);
    if (!l) return;
    for (int i = 0; i < l->n; i++) {
        SceneElem *e = l->e[i];
        got[e->kind]++;
        float p[3], m[9];
        scene_pos(e, p);
        scene_mat(e, m);
        CHECK(finite3(p) && finite3(m) && finite3(m + 3) && finite3(m + 6));
        const char *n = scene_name(e);
        CHECK(memchr(n, 0, SCENE_NAME_LEN) != NULL);
        CHECK(!strchr(n, '\n') && !strchr(n, '\r'));
        if (e->kind == SCENE_TRIGGER) {
            static const int links[] = {TRIG_LINK1, TRIG_LINK2, TRIG_LINK3};
            for (int k = 0; k < 3; k++) {
                int16_t v = scene_i16(e, links[k]);
                CHECK(v == -1 || (v >= 0 && v < l->n));
            }
            float a[3], b[3];
            memcpy(a, e->b + TRIG_END_A, 12);
            memcpy(b, e->b + TRIG_END_B, 12);
            CHECK(finite3(a) && finite3(b));
        }
    }
    for (int k = 0; k < 5; k++) CHECK(got[k] == want[k]);
    SceneElem *s = Scene_FindTriggerByType(2), *f = Scene_FindTriggerByType(1);
    float sp[3] = {0}, sm[9];
    bool has_start = Scene_StartLine(sp, sm);
    printf("  %-10s %s: %3d audio %3d geom %3d trigger %3d camera %d particle; start %s (%.1f %.1f %.1f), end %s, "
           "first camera #%d of %d\n", track, reverse ? "rev" : "fwd", got[0], got[1], got[2], got[3], got[4],
           s ? "yes" : "no", sp[0], sp[1], sp[2], f ? "yes" : "no", Scene_FirstCamera(reverse), Scene_CameraCount());
    (void)has_start;
    /* Every track with a start file has a start and an end line. */
    char path[300];
    snprintf(path, sizeof path, "%s%s%s", g_track_dir, track, reverse ? "_bstart.scn" : "_fstart.scn");
    if (vfs_exists(path)) CHECK(s && f);
    if (Scene_CameraCount() > 0) {
        SceneElem *c = Scene_Camera(Scene_FirstCamera(reverse));
        CHECK(c != NULL);
    }
}

static void check_skill_tests(const char *track, const Files *dir)
{
    size_t tl = strlen(track);
    for (int i = 0; i < dir->n; i++) {
        const char *nm = dir->names[i];
        if (!has_prefix_ci(nm, track) || strncasecmp(nm + tl, "_st", 3) || !strncasecmp(nm + tl, "_start", 6)) continue;
        int t = atoi(nm + tl + 3);
        if (t <= 0) continue;
        char path[300];
        snprintf(path, sizeof path, "%s%s", g_track_dir, nm);
        int want = count_kw(path, "TRIGGER_ELEMENT");
        Track_LoadScenes(track, 4, (uint32_t)t);
        int got = 0;
        SceneList *l = Scene_List();
        Scene_LoadStarts(track);
        int before = Scene_List()->n;
        Scene_LoadSkillTest(track, t);
        l = Scene_List();
        for (int k = before; k < l->n; k++) got += l->e[k]->kind == SCENE_TRIGGER;
        CHECK(got == want);
        CHECK(Scene_FindTriggerByType(2) != NULL);   /* "Start" */
    }
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("scene_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("%s\n", err); return 1; }
    static Files dir;
    char d[260];
    snprintf(d, sizeof d, "%s", g_track_dir);
    size_t dl = strlen(d);
    if (dl && (d[dl - 1] == '\\' || d[dl - 1] == '/')) d[dl - 1] = 0;
    vfs_list(d, collect, &dir);
    int tracks = 0;
    for (int i = 0; i < dir.n; i++) {
        const char *nm = dir.names[i];
        const char *u = strcasestr(nm, "_cameras.scn");
        if (!u) continue;
        char track[64];
        snprintf(track, sizeof track, "%.*s", (int)(u - nm), nm);
        check_list(track, &dir, false);
        check_list(track, &dir, true);
        check_skill_tests(track, &dir);
        tracks++;
    }
    Scene_Clear();
    CHECK(tracks > 0);
    printf("scene_test: %d tracks, %d checks: %s\n", tracks, checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
