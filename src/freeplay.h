#ifndef FREEPLAY_H
#define FREEPLAY_H
/* FreeplayState.hx de Psych 0.6.3 adaptado a PSP.

   MULTI-MOD:
   - fp_find_chart itera TODOS los mods activos (en orden) para buscar
     el chart y el audio. El primero que tenga el chart gana.
   - Luego cae a preload: assets/preload/data, assets/preload/songs, etc.

   DIFICULTADES por semana, MAYUSCULAS en el selector.
   Sufijo: NORMAL -> "", EASY -> "-easy", HARD -> "-hard", otras -> "-<minus>".

   Movimiento: vertical 1.3*120, horizontal 20, lerp 9.6.
   Fondo: base negra + menuDesat tintado (ABGR). */

#include <pspkernel.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "mods.h"
#include "weeks.h"

typedef enum { FREE_NONE = 0, FREE_PLAY, FREE_BACK } FreeplayAction;

static int  FP_curSelected = 0;
static char FP_lastDifficultyName[16] = "";

typedef struct {
    int active;
    int sel;
    int diff;
    int count;
    int songIdx[WEEK_SONGS_MAX];
    float t;
    float iy[WEEK_SONGS_MAX];
    float ix[WEEK_SONGS_MAX];

    int  diffCount;
    char diffNames[8][16];

    char chartPath[256];
    char instPath[256];
    char voicePath[256];
    char songLower[64];
    char songName[64];
    char songChar[32];

    float bgCurR, bgCurG, bgCurB;
    int   tweenActive;
    float tweenElapsed;
    float tweenSR, tweenSG, tweenSB;
    float tweenTR, tweenTG, tweenTB;

    int   intendedScore;
    float intendedRating;
    float lerpScore;
    float lerpRating;

    float holdTime;

    int   playScroll;
    int   playCancel;
} Freeplay;

static Freeplay FP;

static ImgTex FP_menuDesat;
static int    FP_menuDesatTried = 0;

static const char *FP_DEFAULT_DIFFS[3] = { "EASY", "NORMAL", "HARD" };

/* ===================================================================== */
/*  Dificultades                                                         */
/* ===================================================================== */

static void fp_to_upper(char *s) {
    int i;
    for (i = 0; s[i]; i++)
        if (s[i] >= 'a' && s[i] <= 'z') s[i] -= 32;
}

static void fp_to_lower(char *s) {
    int i;
    for (i = 0; s[i]; i++)
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] += 32;
}

static void fp_build_diffs(Freeplay *fp, WeekMeta *w) {
    int i;
    fp->diffCount = 0;

    if (w && w->difficulties[0]) {
        char buf[128];
        char *tok;
        strncpy(buf, w->difficulties, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        tok = strtok(buf, ",");
        while (tok && fp->diffCount < 8) {
            int n;
            while (*tok == ' ') tok++;
            n = (int)strlen(tok);
            while (n > 0 && tok[n - 1] == ' ') tok[--n] = 0;
            if (n > 0) {
                strncpy(fp->diffNames[fp->diffCount], tok, 15);
                fp->diffNames[fp->diffCount][15] = 0;
                fp_to_upper(fp->diffNames[fp->diffCount]);
                fp->diffCount++;
            }
            tok = strtok(NULL, ",");
        }
    }

    if (fp->diffCount == 0) {
        for (i = 0; i < 3; i++) {
            strncpy(fp->diffNames[i], FP_DEFAULT_DIFFS[i], 15);
            fp->diffNames[i][15] = 0;
        }
        fp->diffCount = 3;
    }
}

static int fp_diff_index(Freeplay *fp, const char *name) {
    int i;
    if (!name || !name[0]) return -1;
    for (i = 0; i < fp->diffCount; i++)
        if (strcmp(fp->diffNames[i], name) == 0) return i;
    return -1;
}

static void fp_build_suffix(const char *diffName, char *out, int outLen) {
    char lower[16];
    int i, n;

    if (!diffName || !diffName[0]) { out[0] = 0; return; }

    strncpy(lower, diffName, sizeof(lower) - 1);
    lower[sizeof(lower) - 1] = 0;
    fp_to_lower(lower);

    if (strcmp(lower, "normal") == 0) {
        out[0] = 0;
        return;
    }

    out[0] = '-';
    n = (int)strlen(lower);
    if (n > outLen - 2) n = outLen - 2;
    for (i = 0; i < n; i++) out[i + 1] = lower[i];
    out[n + 1] = 0;
}

/* ===================================================================== */
/*  Chart + audio lookup                                                 */
/* ===================================================================== */

static int fp_file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

/* Resuelve Inst/Voices del chart, iterando mods activos en orden y luego
   preload. Debe llamarse despues de fijar chartPath. */
static void fp_resolve_audio(Freeplay *fp, const char *songLower) {
    char p[256];
    int i;

    for (i = 0; i < ActiveMods.count; i++) {
        if (!ActiveMods.mods[i].active) continue;
        snprintf(p, sizeof(p), "mods/%s/songs/%s/Inst.pcm", ActiveMods.mods[i].name, songLower);
        if (fp_file_exists(p)) {
            snprintf(fp->instPath,  sizeof(fp->instPath),  "mods/%s/songs/%s/Inst.pcm",   ActiveMods.mods[i].name, songLower);
            snprintf(fp->voicePath, sizeof(fp->voicePath), "mods/%s/songs/%s/Voices.pcm", ActiveMods.mods[i].name, songLower);
            return;
        }
        snprintf(p, sizeof(p), "mods/%s/data/%s/Inst.pcm", ActiveMods.mods[i].name, songLower);
        if (fp_file_exists(p)) {
            snprintf(fp->instPath,  sizeof(fp->instPath),  "mods/%s/data/%s/Inst.pcm",   ActiveMods.mods[i].name, songLower);
            snprintf(fp->voicePath, sizeof(fp->voicePath), "mods/%s/data/%s/Voices.pcm", ActiveMods.mods[i].name, songLower);
            return;
        }
    }

    snprintf(p, sizeof(p), "assets/preload/songs/%s/Inst.pcm", songLower);
    if (fp_file_exists(p)) {
        snprintf(fp->instPath,  sizeof(fp->instPath),  "assets/preload/songs/%s/Inst.pcm",   songLower);
        snprintf(fp->voicePath, sizeof(fp->voicePath), "assets/preload/songs/%s/Voices.pcm", songLower);
        return;
    }
    snprintf(p, sizeof(p), "assets/preload/data/%s/Inst.pcm", songLower);
    if (fp_file_exists(p)) {
        snprintf(fp->instPath,  sizeof(fp->instPath),  "assets/preload/data/%s/Inst.pcm",   songLower);
        snprintf(fp->voicePath, sizeof(fp->voicePath), "assets/preload/data/%s/Voices.pcm", songLower);
        return;
    }
    snprintf(p, sizeof(p), "assets/songs/%s/Inst.pcm", songLower);
    if (fp_file_exists(p)) {
        snprintf(fp->instPath,  sizeof(fp->instPath),  "assets/songs/%s/Inst.pcm",   songLower);
        snprintf(fp->voicePath, sizeof(fp->voicePath), "assets/songs/%s/Voices.pcm", songLower);
        return;
    }

    strncpy(fp->instPath,  "Inst.pcm",   sizeof(fp->instPath) - 1);
    strncpy(fp->voicePath, "Voices.pcm", sizeof(fp->voicePath) - 1);
}

/* Itera todos los mods activos para encontrar el chart. */
static int fp_find_chart(Freeplay *fp, const char *songLower, int diff, int allow_root) {
    char sfx[32];
    char p[256];
    int i;

    if (diff < 0 || diff >= fp->diffCount) diff = 0;
    fp_build_suffix(fp->diffNames[diff], sfx, sizeof(sfx));

    NA_LOG("[freeplay] buscar chart: song='%s' sfx='%s'\n", songLower, sfx);

    for (i = 0; i < ActiveMods.count; i++) {
        const char *mod;
        if (!ActiveMods.mods[i].active) continue;
        mod = ActiveMods.mods[i].name;

        snprintf(p, sizeof(p), "mods/%s/data/%s/%s%s.json", mod, songLower, songLower, sfx);
        if (fp_file_exists(p)) goto found;
        if (sfx[0]) {
            snprintf(p, sizeof(p), "mods/%s/data/%s/%s.json", mod, songLower, songLower);
            if (fp_file_exists(p)) goto found;
        }
        snprintf(p, sizeof(p), "mods/%s/songs/%s/%s%s.json", mod, songLower, songLower, sfx);
        if (fp_file_exists(p)) goto found;
        if (sfx[0]) {
            snprintf(p, sizeof(p), "mods/%s/songs/%s/%s.json", mod, songLower, songLower);
            if (fp_file_exists(p)) goto found;
        }
    }

    snprintf(p, sizeof(p), "assets/preload/data/%s/%s%s.json", songLower, songLower, sfx);
    if (fp_file_exists(p)) goto found;
    if (sfx[0]) {
        snprintf(p, sizeof(p), "assets/preload/data/%s/%s.json", songLower, songLower);
        if (fp_file_exists(p)) goto found;
    }
    snprintf(p, sizeof(p), "assets/preload/songs/%s/%s%s.json", songLower, songLower, sfx);
    if (fp_file_exists(p)) goto found;
    if (sfx[0]) {
        snprintf(p, sizeof(p), "assets/preload/songs/%s/%s.json", songLower, songLower);
        if (fp_file_exists(p)) goto found;
    }
    snprintf(p, sizeof(p), "assets/data/%s/%s%s.json", songLower, songLower, sfx);
    if (fp_file_exists(p)) goto found;
    if (sfx[0]) {
        snprintf(p, sizeof(p), "assets/data/%s/%s.json", songLower, songLower);
        if (fp_file_exists(p)) goto found;
    }

    if (allow_root) {
        snprintf(p, sizeof(p), "%s%s.json", songLower, sfx);
        if (fp_file_exists(p)) goto found;
        if (sfx[0]) {
            snprintf(p, sizeof(p), "%s.json", songLower);
            if (fp_file_exists(p)) goto found;
        }
    }

    NA_LOG("[freeplay] chart NO encontrado para '%s%s'\n", songLower, sfx);
    return -1;

found:
    strncpy(fp->chartPath, p, sizeof(fp->chartPath) - 1);
    fp->chartPath[sizeof(fp->chartPath) - 1] = 0;
    fp_resolve_audio(fp, songLower);
    NA_LOG("[freeplay] chart OK: %s\n", fp->chartPath);
    NA_LOG("[freeplay] inst:     %s\n", fp->instPath);
    NA_LOG("[freeplay] voices:   %s\n", fp->voicePath);
    return 0;
}

/* ===================================================================== */
/*  Open / seleccion / dificultad                                        */
/* ===================================================================== */

static void fp_load_menu_desat(void) {
    char p[256];
    int i;

    if (FP_menuDesatTried) return;
    FP_menuDesatTried = 1;

    for (i = 0; i < ActiveMods.count; i++) {
        if (!ActiveMods.mods[i].active) continue;
        snprintf(p, sizeof(p), "mods/%s/images/menuDesat.img", ActiveMods.mods[i].name);
        img_load(&FP_menuDesat, p);
        if (FP_menuDesat.loaded) break;
        snprintf(p, sizeof(p), "mods/%s/images/menuDesat.tex", ActiveMods.mods[i].name);
        img_load(&FP_menuDesat, p);
        if (FP_menuDesat.loaded) break;
    }

    if (!FP_menuDesat.loaded) img_load(&FP_menuDesat, "assets/preload/images/menuDesat.img");
    if (!FP_menuDesat.loaded) img_load(&FP_menuDesat, "assets/preload/images/menuDesat.tex");
    if (!FP_menuDesat.loaded) img_load(&FP_menuDesat, "assets/shared/images/menuDesat.img");
    if (!FP_menuDesat.loaded) img_load(&FP_menuDesat, "assets/shared/images/menuDesat.tex");
    if (!FP_menuDesat.loaded) img_load(&FP_menuDesat, "menuDesat.img");
    if (!FP_menuDesat.loaded) img_load(&FP_menuDesat, "menuDesat.tex");

    NA_LOG("[freeplay] menuDesat: %s\n", FP_menuDesat.loaded ? "OK" : "no encontrado");
}

static void freeplay_open(void) {
    int i;

    memset(&FP, 0, sizeof(FP));
    FP.active = 1;

    for (i = 0; i < WD.songCount; i++) {
        int wk = WD.songs[i].weekIdx;
        if (weeks_week_is_locked(&WD, wk)) continue;
        if (WD.weeks[wk].hideFreeplay) continue;
        FP.songIdx[FP.count++] = i;
    }

    if (FP.count == 0) {
        fp_build_diffs(&FP, NULL);
        FP.diff = 0;
        return;
    }

    if (FP_curSelected < 0 || FP_curSelected >= FP.count) FP_curSelected = 0;
    FP.sel = FP_curSelected;

    for (i = 0; i < FP.count; i++) {
        float ty = (float)(i - FP.sel);
        FP.iy[i] = ty * 1.3f * 120.0f + 320.0f;
        FP.ix[i] = ty * 20.0f + 90.0f;
    }

    {
        int wk = WD.songs[FP.songIdx[FP.sel]].weekIdx;
        fp_build_diffs(&FP, &WD.weeks[wk]);
    }

    if (FP_lastDifficultyName[0] == 0)
        strncpy(FP_lastDifficultyName, "NORMAL", sizeof(FP_lastDifficultyName) - 1);

    {
        int newPos = fp_diff_index(&FP, FP_lastDifficultyName);
        if (newPos >= 0) FP.diff = newPos;
        else if ((newPos = fp_diff_index(&FP, "NORMAL")) >= 0) FP.diff = newPos;
        else FP.diff = 0;
        if (FP.diff >= FP.diffCount) FP.diff = FP.diffCount - 1;
    }

    {
        WeekSong *s = &WD.songs[FP.songIdx[FP.sel]];
        FP.bgCurR = (float)s->r;
        FP.bgCurG = (float)s->g;
        FP.bgCurB = (float)s->b;
    }

    FP.intendedScore  = 0;
    FP.intendedRating = 0.0f;
    FP.lerpScore      = 0.0f;
    FP.lerpRating     = 0.0f;

    fp_load_menu_desat();
}

static void fp_change_selection(int change, int playSound) {
    if (change != 0) {
        FP.sel = (FP.sel + change) % FP.count;
        if (FP.sel < 0) FP.sel += FP.count;
        FP_curSelected = FP.sel;
    }
    if (playSound) FP.playScroll = 1;

    {
        WeekSong *s = &WD.songs[FP.songIdx[FP.sel]];
        float tr = (float)s->r, tg = (float)s->g, tb = (float)s->b;
        if (tr != FP.bgCurR || tg != FP.bgCurG || tb != FP.bgCurB) {
            FP.tweenActive = 1;
            FP.tweenElapsed = 0.0f;
            FP.tweenSR = FP.bgCurR; FP.tweenSG = FP.bgCurG; FP.tweenSB = FP.bgCurB;
            FP.tweenTR = tr;        FP.tweenTG = tg;        FP.tweenTB = tb;
        }
    }

    {
        int wk = WD.songs[FP.songIdx[FP.sel]].weekIdx;
        WeekMeta *w = &WD.weeks[wk];
        char prevName[16];
        int newPos;

        strncpy(prevName, FP_lastDifficultyName, sizeof(prevName) - 1);
        prevName[sizeof(prevName) - 1] = 0;

        fp_build_diffs(&FP, w);

        newPos = fp_diff_index(&FP, prevName);
        if (newPos >= 0) FP.diff = newPos;
        else if ((newPos = fp_diff_index(&FP, "NORMAL")) >= 0) FP.diff = newPos;
        else FP.diff = 0;
        if (FP.diff >= FP.diffCount) FP.diff = FP.diffCount - 1;
    }

    FP.intendedScore  = 0;
    FP.intendedRating = 0.0f;
}

static void fp_change_diff(int change) {
    if (FP.diffCount <= 0) return;
    if (FP.diffCount == 1) change = 0;

    if (change != 0) {
        FP.diff = (FP.diff + change) % FP.diffCount;
        if (FP.diff < 0) FP.diff += FP.diffCount;
    }

    strncpy(FP_lastDifficultyName, FP.diffNames[FP.diff], sizeof(FP_lastDifficultyName) - 1);
    FP_lastDifficultyName[sizeof(FP_lastDifficultyName) - 1] = 0;

    FP.intendedScore  = 0;
    FP.intendedRating = 0.0f;
}

/* ===================================================================== */
/*  Update                                                               */
/* ===================================================================== */

static FreeplayAction freeplay_update(float dt,
                                       int upP, int downP,
                                       int leftP, int rightP,
                                       int acceptP, int backP) {
    int i;
    if (!FP.active) return FREE_NONE;

    FP.t += dt;
    FP.playScroll = 0;
    FP.playCancel = 0;

    if (FP.count == 0) return backP ? FREE_BACK : FREE_NONE;

    if (upP)   { fp_change_selection(-1, 1); FP.holdTime = 0; fp_change_diff(0); }
    if (downP) { fp_change_selection( 1, 1); FP.holdTime = 0; fp_change_diff(0); }

    if (leftP)       fp_change_diff(-1);
    else if (rightP) fp_change_diff( 1);

    for (i = 0; i < FP.count; i++) {
        float ty = (float)(i - FP.sel);
        float targetY = ty * 1.3f * 120.0f + 320.0f;
        float targetX = ty * 20.0f + 90.0f;
        float lv = dt * 9.6f;
        if (lv > 1.0f) lv = 1.0f;
        FP.iy[i] += (targetY - FP.iy[i]) * lv;
        FP.ix[i] += (targetX - FP.ix[i]) * lv;
    }

    if (FP.tweenActive) {
        FP.tweenElapsed += dt;
        if (FP.tweenElapsed >= 1.0f) { FP.tweenElapsed = 1.0f; FP.tweenActive = 0; }
        FP.bgCurR = FP.tweenSR + (FP.tweenTR - FP.tweenSR) * FP.tweenElapsed;
        FP.bgCurG = FP.tweenSG + (FP.tweenTG - FP.tweenSG) * FP.tweenElapsed;
        FP.bgCurB = FP.tweenSB + (FP.tweenTB - FP.tweenSB) * FP.tweenElapsed;
    }

    {
        float tS = dt * 24.0f; if (tS > 1.0f) tS = 1.0f;
        float tR = dt * 12.0f; if (tR > 1.0f) tR = 1.0f;
        FP.lerpScore  = floorf(FP.lerpScore  + ((float)FP.intendedScore  - FP.lerpScore ) * tS);
        FP.lerpRating = FP.lerpRating + (FP.intendedRating - FP.lerpRating) * tR;
        if (fabsf(FP.lerpScore  - (float)FP.intendedScore) <= 10.0f) FP.lerpScore  = (float)FP.intendedScore;
        if (fabsf(FP.lerpRating - FP.intendedRating)       <= 0.01f) FP.lerpRating = FP.intendedRating;
    }

    if (backP) { FP.playCancel = 1; return FREE_BACK; }

    if (acceptP) {
        WeekSong *s = &WD.songs[FP.songIdx[FP.sel]];
        if (fp_find_chart(&FP, s->lower, FP.diff, 1) == 0) {
            strncpy(FP.songLower, s->lower,     sizeof(FP.songLower) - 1);
            strncpy(FP.songName,  s->name,      sizeof(FP.songName)  - 1);
            strncpy(FP.songChar,  s->character, sizeof(FP.songChar)  - 1);
            return FREE_PLAY;
        }
        NA_LOG("[freeplay] chart missing: %s (diff=%s)\n", s->lower, FP.diffNames[FP.diff]);
        return FREE_NONE;
    }
    return FREE_NONE;
}

/* ===================================================================== */
/*  Draw                                                                 */
/* ===================================================================== */

static void freeplay_draw(void) {
    int i, a;
    if (!FP.active) return;

    a = (int)(FP.t * 3.0f * 255.0f);
    if (a > 255) a = 255;
    if (a < 0) a = 0;

    rect(0, 0, SCR_W, SCR_H, 0xff000000);

    if (FP_menuDesat.loaded) {
        unsigned int tint = COL(255,
            (((int)FP.bgCurB & 0xff) << 16) |
            (((int)FP.bgCurG & 0xff) << 8)  |
            ((int)FP.bgCurR & 0xff));
        img_begin();
        img_draw(&FP_menuDesat, 0, 0, SCR_W, SCR_H, tint);
        img_end();
    } else {
        unsigned int bgcol = COL(a,
            (((int)FP.bgCurB & 0xff) << 16) |
            (((int)FP.bgCurG & 0xff) << 8)  |
            ((int)FP.bgCurR & 0xff));
        rect(0, 0, SCR_W, SCR_H, bgcol);
    }

    if (FP.count > 0) {
        if (al_ok) {
            al_begin();
            for (i = 0; i < FP.count; i++) {
                float y720 = FP.iy[i];
                float yScr = y720 * K;
                if (yScr < -120 || yScr > SCR_H + 120) continue;
                al_draw_bold(FP.ix[i], y720, WD.songs[FP.songIdx[i]].name,
                             (i == FP.sel) ? 255u : 153u);
            }
            al_end();
        } else {
            for (i = 0; i < FP.count; i++) {
                int y = (int)(FP.iy[i] * K);
                if (y < -40 || y > SCR_H + 40) continue;
                text_fixed((int)(FP.ix[i] * K), y, 2,
                           COL((i == FP.sel) ? 255 : 153, 0xffffff),
                           WD.songs[FP.songIdx[i]].name);
            }
        }
    } else {
        text_fixed(20, SCR_H / 2 - 20, 2, 0xffffffff, "NO HAY CANCIONES");
        text_fixed(20, SCR_H / 2,      1, 0xffffffff,
                   "Revisa weekList.txt y weeks/*.json");
    }

    {
        char scoreBuf[64];
        int  pctX100 = (int)(FP.lerpRating * 10000.0f);
        int  sw, boxX, boxW, boxCenterX;

        snprintf(scoreBuf, sizeof(scoreBuf),
                 "PERSONAL BEST: %d (%d.%02d%%)",
                 (int)FP.lerpScore, pctX100 / 100, pctX100 % 100);
        sw = text_w(scoreBuf, 1);

        boxX = SCR_W - sw - 18;
        boxW = sw + 18;
        boxCenterX = boxX + boxW / 2;

        rect(boxX, 0, boxW, 34, 0xaa000000);
        text_fixed(boxX + 10, 4, 1, 0xffffffff, scoreBuf);

        if (FP.diffCount > 0) {
            char dbuf[48];
            int  dw, dx;
            snprintf(dbuf, sizeof(dbuf), "< %s >", FP.diffNames[FP.diff]);
            dw = text_w(dbuf, 1);
            dx = boxCenterX - dw / 2;
            text_fixed(dx, 20, 1, 0xffffffff, dbuf);
        }
    }

    rect(0, SCR_H - 12, SCR_W, 16, 0x99000000);
    text_fixed(2, SCR_H - 13, 1, 0xffffffff,
               "X=Play  O=Back  L/R=Diff  Press START to listen to the Song");
}

#endif