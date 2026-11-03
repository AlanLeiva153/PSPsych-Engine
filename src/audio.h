#ifndef AUDIO_H
#define AUDIO_H

#include <pspkernel.h>
#include <pspaudio.h>
#include <stdio.h>
#include <string.h>

#define AUDIO_RATE    22050
#define AUDIO_BLOCK   1024
#define AUDIO_LATENCY_MS 0.0f

/* Musica del menu de pausa (breakfast): PCM mono 22050 Hz s16le. Si no existe, la pausa queda en silencio.
   Convertir: ffmpeg -i breakfast.ogg -ac 1 -ar 22050 -f s16le breakfast.pcm */
#ifndef PAUSE_MUSIC_PATH
#define PAUSE_MUSIC_PATH "assets/preload/music/breakfast.pcm"
#endif

typedef struct {
    FILE *inst, *voices;
    FILE *pmus;                              /* musica de pausa (solo la lee el hilo mientras paused=1) */
    long pmusSize;
    SceUID thread;
    int ch;
    volatile int running, quit, finished;
    volatile int reset_pending;
    volatile int reset_preroll;
    volatile int paused;                     /* pedido de pausa (lo pone el hilo principal) */
    volatile int inPause;                    /* el hilo de audio confirma que ya esta en pausa */
    volatile int pmusOn;
    volatile int pmusVol;                    /* 0..256 */
    float pmusVolF;                          /* 0..0.5, igual que pauseMusic.volume */
    volatile long long sent;
    volatile unsigned long long stampUs;
    long long prerollSamples;
    int volInst, volVoices;
} AudioState;

static AudioState A;

static short __attribute__((aligned(64))) a_mi[AUDIO_BLOCK];
static short __attribute__((aligned(64))) a_mv[AUDIO_BLOCK];
static short __attribute__((aligned(64))) a_st[AUDIO_BLOCK * 2];
static short __attribute__((aligned(64))) a_pm[AUDIO_BLOCK];

/* lee un bloque de la musica de pausa en bucle */
static void pm_read_block(void) {
    int got = 0, guard = 0;
    memset(a_pm, 0, sizeof(a_pm));
    while (got < AUDIO_BLOCK && guard < 3) {
        int n = (int)fread(a_pm + got, 2, AUDIO_BLOCK - got, A.pmus);
        if (n <= 0) {
            if (fseek(A.pmus, 0, SEEK_SET) != 0) break;
            guard++;
            continue;
        }
        got += n;
    }
}

static int audio_thread(SceSize args, void *argp) {
    long long pre = A.prerollSamples;
    int i;

    while (!A.quit) {
        /* Reset pedido desde audio_reset(): volver al inicio del audio */
        if (A.reset_pending) {
            pre = A.reset_preroll;
            A.sent = 0;
            A.prerollSamples = A.reset_preroll;
            A.stampUs = sceKernelGetSystemTimeWide();
            A.finished = 0;
            A.paused = 0; A.pmusOn = 0; A.inPause = 0;
            if (A.inst) fseek(A.inst, 0, SEEK_SET);
            if (A.voices) fseek(A.voices, 0, SEEK_SET);
            A.reset_pending = 0;
            continue;
        }

        /* Pausa: no se lee Inst/Voices, `sent` NO avanza (la posicion de la cancion queda congelada)
           y se sigue sacando audio (silencio o musica de pausa) para no cortar el canal. */
        if (A.paused) {
            A.inPause = 1;
            if (A.pmusOn && A.pmus) {
                pm_read_block();
                for (i = 0; i < AUDIO_BLOCK; i++) {
                    int s = (a_pm[i] * A.pmusVol) >> 8;
                    if (s > 32767) s = 32767;
                    if (s < -32768) s = -32768;
                    a_st[i * 2] = a_st[i * 2 + 1] = (short)s;
                }
            } else {
                memset(a_st, 0, sizeof(a_st));
            }
            sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX, a_st);
            A.stampUs = sceKernelGetSystemTimeWide();
            continue;
        }
        A.inPause = 0;

        {
            int gi = 0, gv = 0;
            memset(a_mi, 0, sizeof(a_mi));
            memset(a_mv, 0, sizeof(a_mv));

            if (pre > 0) {
                pre -= AUDIO_BLOCK;
            } else {
                if (A.inst)   gi = (int)fread(a_mi, 2, AUDIO_BLOCK, A.inst);
                if (A.voices) gv = (int)fread(a_mv, 2, AUDIO_BLOCK, A.voices);
                if (gi == 0 && gv == 0) A.finished = 1;
            }

            for (i = 0; i < AUDIO_BLOCK; i++) {
                int s = (a_mi[i] * A.volInst + a_mv[i] * A.volVoices) >> 8;
                if (s > 32767) s = 32767;
                if (s < -32768) s = -32768;
                a_st[i * 2] = a_st[i * 2 + 1] = (short)s;
            }
        }

        sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX, a_st);
        A.sent += AUDIO_BLOCK;
        A.stampUs = sceKernelGetSystemTimeWide();
    }
    return 0;
}

static int audio_start(const char *instPath, const char *voicesPath, int prerollMs) {
    memset(&A, 0, sizeof(A));
    A.thread = -1;
    A.ch = -1;
    A.volInst = 256;
    A.volVoices = 256;

    A.inst = fopen(instPath, "rb");
    if (!A.inst) return -1;
    setvbuf(A.inst, NULL, _IOFBF, 32768);
    if (voicesPath) {
        A.voices = fopen(voicesPath, "rb");
        if (A.voices) setvbuf(A.voices, NULL, _IOFBF, 32768);
    }

    A.pmus = fopen(PAUSE_MUSIC_PATH, "rb");
    if (A.pmus) {
        setvbuf(A.pmus, NULL, _IOFBF, 16384);
        fseek(A.pmus, 0, SEEK_END);
        A.pmusSize = ftell(A.pmus);
        fseek(A.pmus, 0, SEEK_SET);
        if (A.pmusSize < 4) { fclose(A.pmus); A.pmus = NULL; A.pmusSize = 0; }
    }

    A.prerollSamples = (((long long)prerollMs * AUDIO_RATE / 1000) + AUDIO_BLOCK - 1)
                       / AUDIO_BLOCK * AUDIO_BLOCK;

    A.ch = sceAudioSRCChReserve(AUDIO_BLOCK, AUDIO_RATE, 2);
    if (A.ch < 0) {
        if (A.inst) { fclose(A.inst); A.inst = NULL; }
        if (A.voices) { fclose(A.voices); A.voices = NULL; }
        if (A.pmus) { fclose(A.pmus); A.pmus = NULL; }
        return -2;
    }

    A.stampUs = sceKernelGetSystemTimeWide();
    A.thread = sceKernelCreateThread("audio", audio_thread, 0x12, 0x4000, 0, NULL);
    if (A.thread < 0) {
        if (A.inst) { fclose(A.inst); A.inst = NULL; }
        if (A.voices) { fclose(A.voices); A.voices = NULL; }
        if (A.pmus) { fclose(A.pmus); A.pmus = NULL; }
        sceAudioSRCChRelease();
        A.ch = -1;
        return -3;
    }
    A.running = 1;
    sceKernelStartThread(A.thread, 0, &A);
    return 0;
}

static void audio_stop(void) {
    if (!A.running) return;

    A.quit = 1;
    sceKernelWaitThreadEnd(A.thread, NULL);
    sceKernelDeleteThread(A.thread);
    A.thread = -1;

    sceAudioSRCChRelease();
    A.ch = -1;

    if (A.inst) { fclose(A.inst); A.inst = NULL; }
    if (A.voices) { fclose(A.voices); A.voices = NULL; }
    if (A.pmus) { fclose(A.pmus); A.pmus = NULL; }
    A.paused = 0; A.inPause = 0; A.pmusOn = 0;
    A.running = 0;
}

/* Reset sin cerrar el canal de audio. Para reiniciar la misma canción. */
static void audio_reset(int prerollMs) {
    long long new_preroll;

    if (A.paused) { A.paused = 0; A.pmusOn = 0; }

    if (!A.running) {
        audio_start("Inst.pcm", "Voices.pcm", prerollMs);
        return;
    }

    new_preroll = (((long long)prerollMs * AUDIO_RATE / 1000) + AUDIO_BLOCK - 1)
                  / AUDIO_BLOCK * AUDIO_BLOCK;
    A.reset_preroll = new_preroll;
    A.reset_pending = 1;

    /* Esperar a que el thread procese el reset (máx ~300 ms) */
    {
        int tries = 0;
        while (A.reset_pending && tries < 30) {
            sceKernelDelayThread(10000);
            tries++;
        }
    }
}

/* Pausa Inst/Voices y arranca la musica de pausa en un punto aleatorio de su primera mitad
   (PauseSubState: pauseMusic.play(false, random(0, length/2)), volumen 0). */
static void audio_pause(void) {
    int tries = 0;
    if (!A.running || A.paused) return;
    A.pmusVolF = 0.0f;
    A.pmusVol = 0;
    A.pmusOn = 0;
    if (A.pmus && A.pmusSize >= 4) {
        long off = (long)(sceKernelGetSystemTimeLow() % (unsigned int)(A.pmusSize / 2 + 1));
        off &= ~1L;
        fseek(A.pmus, off, SEEK_SET);
        A.pmusOn = 1;
    }
    A.paused = 1;
    while (!A.inPause && tries < 200) { sceKernelDelayThread(1000); tries++; }   /* espera confirmacion (<~50 ms) */
}

static void audio_resume(void) {
    int tries = 0;
    if (!A.running || !A.paused) return;
    A.paused = 0;
    A.pmusOn = 0;
    while (A.inPause && tries < 200) { sceKernelDelayThread(1000); tries++; }
}

/* fade-in de la musica de pausa: `if (volume < 0.5) volume += 0.01 * elapsed` (elapsed en segundos) */
static void audio_pause_tick(float dtSec) {
    if (!A.running || !A.paused || !A.pmusOn) return;
    if (A.pmusVolF < 0.5f) {
        A.pmusVolF += 0.01f * dtSec;
        if (A.pmusVolF > 0.5f) A.pmusVolF = 0.5f;
        A.pmusVol = (int)(A.pmusVolF * 256.0f);
    }
}

static float audio_position_ms(void) {
    long long s = A.sent - A.prerollSamples - AUDIO_BLOCK;
    float ms = (float)s * 1000.0f / AUDIO_RATE;
    float el = (float)(sceKernelGetSystemTimeWide() - A.stampUs) / 1000.0f;
    if (el < 0) el = 0;
    if (el > 46.0f) el = 46.0f;
    return ms + el + AUDIO_LATENCY_MS;
}

#endif