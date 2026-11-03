#ifndef CONDUCTOR_H
#define CONDUCTOR_H

#include <pspkernel.h>
#include <math.h>

#define SAFE_FRAMES   10.0f      /* ClientPrefs.safeFrames */
#define MAX_BPM_MAP   256

typedef struct { int stepTime; float songTime, bpm, stepCrochet; } BPMChange;

typedef struct {
    float bpm, crochet, stepCrochet;
    float songPosition, lastSongPos, offset;
    float safeZoneOffset;
    BPMChange map[MAX_BPM_MAP];
    int mapCount;
    long long startUs;
    int running;
} Conductor;

static Conductor C;

static inline float calc_crochet(float bpm) { return (60.0f / bpm) * 1000.0f; }

static inline void conductor_change_bpm(float bpm) {
    C.bpm = bpm;
    C.crochet = calc_crochet(bpm);
    C.stepCrochet = C.crochet / 4.0f;
}

static inline void conductor_init(float bpm) {
    conductor_change_bpm(bpm);
    C.songPosition = 0; C.lastSongPos = 0; C.offset = 0;
    C.safeZoneOffset = (SAFE_FRAMES / 60.0f) * 1000.0f;
    C.mapCount = 0;
    C.running = 0;
}

static inline void conductor_start(float startMs) {
    C.startUs = (long long)sceKernelGetSystemTimeWide() - (long long)(startMs * 1000.0f);
    C.running = 1;
}

static inline void conductor_update(void) {
    if (C.running)
        C.songPosition = (float)((long long)sceKernelGetSystemTimeWide() - C.startUs) / 1000.0f;
}

static inline BPMChange conductor_bpm_from_seconds(float t) {
    BPMChange last; int i;
    last.stepTime = 0; last.songTime = 0; last.bpm = C.bpm; last.stepCrochet = C.stepCrochet;
    for (i = 0; i < C.mapCount; i++)
        if (t >= C.map[i].songTime) last = C.map[i];
    return last;
}

static inline BPMChange conductor_bpm_from_step(float step) {
    BPMChange last; int i;
    last.stepTime = 0; last.songTime = 0; last.bpm = C.bpm; last.stepCrochet = C.stepCrochet;
    for (i = 0; i < C.mapCount; i++)
        if (C.map[i].stepTime <= step) last = C.map[i];
    return last;
}

static inline float conductor_beat_to_seconds(float beat) {
    float step = beat * 4.0f;
    BPMChange l = conductor_bpm_from_step(step);
    return l.songTime + ((step - l.stepTime) / (l.bpm / 60.0f) / 4.0f) * 1000.0f;
}

static inline float conductor_step(float t) {
    BPMChange l = conductor_bpm_from_seconds(t);
    return l.stepTime + (t - l.songTime) / l.stepCrochet;
}

static inline float conductor_beat(float t) { return conductor_step(t) / 4.0f; }

#endif