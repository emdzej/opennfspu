/* The engine's sound (game/audio.c on snd/): a 964 on the autobahn's grid, its race camera behind it,
   idling through the countdown and on (the engine's start sound first), then on full throttle: the
   mix at the library's rate has sound while the engine idles, higher on the throttle. AUDIO_WAV=<file> writes it. Skips (exit 0) without data. */
#include "eagl_fake_thrash.h"
#include "exe.h"
#include "game/audio.h"
#include "game/camera.h"
#include "game/car.h"
#include "game/input.h"
#include "game/race.h"
#include "game/race_car.h"
#include "game/race_setup.h"
#include "game/scene.h"
#include "game/music.h"
#include "game/hud.h"
#include "game/race_progress.h"
#include "game/track.h"
#include "snd/snd.h"
#include "vfs.h"
#include "vfs_host.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static int16_t *pcm;
static size_t npcm, cap;

/* n ticks of the race, the audio mixed alongside (the library's rate over the 64 Hz ticks); the
   level of the last `last` ticks. */
static double zc;   /* the zero crossings a second of the measured part (the pitch, roughly) */

static int8_t steer;

static double run(int ticks, int last, uint8_t throttle)
{
    uint8_t pkt[4] = {0, (uint8_t)steer, throttle, 0};
    double sum = 0;
    size_t n0 = npcm + (size_t)(ticks - last) * (size_t)(SND_Rate() / 64) * 2;
    for (int t = 0; t < ticks; t++) {
        if (!(g_sim_tick & 1)) {
            Input_Enqueue(0, pkt);
            Sim_HalfStep(g_sim_tick >> 1);
        }
        Sim_Step();
        Camera_Update();
        int frames = SND_Rate() / 64;
        if (cap < npcm + (size_t)frames * 2) {
            cap = (npcm + (size_t)frames * 2) * 2;
            pcm = realloc(pcm, cap * sizeof *pcm);
        }
        SND_Fill(pcm + npcm, frames);
        npcm += (size_t)frames * 2;
    }
    int cross = 0;
    for (size_t i = n0; i < npcm; i++) {
        sum += (double)pcm[i] * pcm[i];
        if (i >= n0 + 2 && (i & 1) == 0 && (pcm[i] < 0) != (pcm[i - 2] < 0)) cross++;
    }
    zc = cross * 64.0 / last;
    return sqrt(sum / (double)(npcm - n0));
}

static void wav(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t rate = (uint32_t)SND_Rate(), bytes = (uint32_t)(npcm * 2), v;
    fwrite("RIFF", 1, 4, f);
    v = 36 + bytes;
    fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    uint32_t fmt[4] = {16, 0x00020001u, rate, rate * 4};
    fwrite(fmt, 4, 4, f);
    uint32_t align = 0x00100004u;
    fwrite(&align, 4, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&bytes, 4, 1, f);
    fwrite(pcm, 2, npcm, f);
    fclose(f);
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("audio_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    fake_install_graphics();
    snprintf(g_track_name, sizeof g_track_name, "%s", getenv("AUDIO_TRACK") ? getenv("AUDIO_TRACK") : "autobahn");
    Track_Load();
    CHECK(RaceSetup_SingleCar("964"));
    if (getenv("AUDIO_OPP")) CHECK(RaceSetup_Opponents(1));
    Car_Construct();
    if (g_ncars < 1) return 1;
    g_sim_tick = 0x1c0 - 0x40;
    for (int i = 0; i < g_ncars; i++) Car_RaceInit(g_cars[i], i);
    Input_Reset(false);
    Camera_Init(0, g_cars[0], 2);
    CHECK(Audio_Init());
    Audio_LoadCollisionSounds((const char *)CAR_DESCB(g_cars[0]) + 0x2e8);
    Audio_LoadTrackSounds();
    Audio_StartRace();
    Music_SetVolume(0);   /* (the engine's checks without the music) */
    double idle = run(0x40 + 64 * 3, 64, 0);   /* the start's sound over, a second of idling */
    double zidle = zc;
    double full = run(64 * 4, 64 * 2, 0x7f);
    printf("  idle: rms %.0f, %.0f crossings/s; full throttle: rms %.0f, %.0f crossings/s (rpm %.0f)\n", idle, zidle, full, zc,
           *CAR_F(g_cars[0], 0xdb4));
    CHECK(idle > 300 && full > 300);
    CHECK(zc > zidle * 1.5);
    /* A hard landing's impact (an event as 0x49bcb0's) heard over the idling engine. */
    double before = run(32 + (g_sim_tick & 1 ? 0 : 1), 16, 0);   /* (the even ticks clear the events: add on an odd one) */
    Car_AddEvent(g_cars[0], 1, -1, 0, 5, 6.0f, 0);
    double after = run(16, 16, 0);
    printf("  impact: rms %.0f before, %.0f after\n", before, after);
    CHECK(after > before * 1.2);
    /* A hard turn on the throttle: the tyres skid (the wheels' events 0x12, 0x14). */
    steer = 0x3f;
    run(64 * 2, 64, 0x7f);
    steer = 0;
    /* castle's sound triggers (type 4): the car's diagonals across the first one's line set its sound
       element's one-shot going (Scene_Tick 0x470b10). */
    SceneList *ev = Scene_EventTriggers();
    for (int i = 0; ev && i < ev->n; i++) {
        SceneElem *t = ev->e[i];
        if (scene_i32(t, TRIG_TYPE) != 4) continue;
        const float *a = (const float *)(void *)(t->b + TRIG_END_A), *b = (const float *)(void *)(t->b + TRIG_END_B);
        float m[3] = {(a[0] + b[0]) / 2, a[1], (a[2] + b[2]) / 2}, n[3] = {-(b[2] - a[2]), 0, b[0] - a[0]};
        float l = sqrtf(n[0] * n[0] + n[2] * n[2]);
        Car *c = g_cars[0];
        for (int k = 0; k < 3; k += 2) {
            CAR_F(c, 0x7f8)[k] = CAR_F(c, 0x8bc)[k] = m[k] + n[k] / l * 2;
            CAR_F(c, 0xa44)[k] = CAR_F(c, 0x980)[k] = m[k] - n[k] / l * 2;
        }
        CAR_F(c, 0x7f8)[1] = CAR_F(c, 0xa44)[1] = m[1];
        const float *f = (const float *)(void *)(t->b + 0xc8), *g = (const float *)(void *)(t->b + 0xbc);
        memcpy(CAR_F(c, 0x37c), f, 12);
        for (int k = 0; k < 3; k++) CAR_F(c, 0x33c)[k] = g[k] * 10.0f;
        *CAR_F(c, 0x35c) = 10.0f;
        Scene_Tick();
        SceneElem *e = Scene_List()->e[scene_i16(t, TRIG_LINK1)];
        printf("  trigger %d: fired %d, its sound element's one-shot %d\n", i, t->b[0x78], e->b[0xa2]);
        CHECK(t->b[0x78] == 1 && e->b[0xa2] == 1);
        break;
    }
    /* A lap done: the announcer says its number (0x427790). */
    double still = run(64, 64, 0);
    g_race_laps = 3;
    *CAR_I(g_cars[0], 0xb28) = 64 * 70;
    HUD_LapDone();
    double said = run(64, 64, 0);
    run(64 * 3, 64, 0);   /* (its end) */
    printf("  lap's announcement: rms %.0f before, %.0f after\n", still, said);
    CHECK(said > still * 1.2);
    /* The music: a song of the playlist streamed (zzzymus.viv), heard over the idling car. */
    double quiet = run(64, 64, 0);
    Music_SetVolume(0x4f);
    double loud = run(64 * 6, 64 * 4, 0);
    const MusicSong *song = Music_Current();
    printf("  music: rms %.0f without, %.0f with: %s (%s, %d ms)\n", quiet, loud, song ? song->name : "none",
           song && song->artist ? song->artist : "", song ? song->length : 0);
    CHECK(song && song->name && !strncmp(song->name, "game", 4) && 60000 < song->length);
    CHECK(loud > quiet * 1.2);
    if (getenv("AUDIO_WAV")) wav(getenv("AUDIO_WAV"));
    Audio_Shutdown();
    printf("audio_test: %d checks: %s\n", checks, fails ? "FAILED" : "ok");
    return fails != 0;
}
