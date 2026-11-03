#ifndef PLAY_H
#define PLAY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "conductor.h"
#include "chart.h"

#define GHOST_TAPPING  1
#define RATING_OFFSET  0.0f
#define SPAWN_TIME     2000.0f

static const int   R_WIN[4]   = { 45, 90, 135, 0 };
static const float R_MOD[4]   = { 1.0f, 0.7f, 0.4f, 0.0f };
static const int   R_SCORE[4] = { 350, 200, 100, 50 };
static const char *R_NAME[4]  = { "SICK", "GOOD", "BAD", "SHIT" };

static const char *RS_NAME[10] = { "You Suck!", "Shit", "Bad", "Bruh", "Meh", "Nice", "Good", "Great", "Sick!", "Perfect!!" };
static const float RS_VAL[10]  = { 0.2f, 0.4f, 0.5f, 0.6f, 0.69f, 0.7f, 0.8f, 0.9f, 1.0f, 1.0f };

typedef struct {
    float strumTime, missHealth, hitHealth, earlyHitMult;
    int prev;
    unsigned char lane, mustPress, isSustain, isEnd;
    unsigned char canBeHit, tooLate, wasGoodHit, ignoreNote, hitByOpponent, dead;
    unsigned char hitCausesMiss, lowPriority, type;
} GNote;

typedef struct {
    GNote *pool; int *order;
    int count, head, spawnEnd;
    float songSpeed, noteKillOffset;
    float health, healthGain, healthLoss;
    int combo, songScore, songHits, songMisses, totalPlayed;
    int sicks, goods, bads, shits;
    float totalNotesHit, ratingPercent;
    const char *ratingName, *ratingFC;
    int cpuControlled, practiceMode, endingSong, isDead;
    int cam_zooming;

    /* Miss combo popup: se muestra combo.img + el combo perdido */
    int miss_combo_value;               /* valor del combo antes de perderlo */
    float miss_combo_age;               /* ms desde el miss */
    int miss_combo_active;              /* 1 = mostrar */

    float vocals;
    int popRating, popCombo; float popAge;
    int strum[2][4];
    float strumReset[2][4];

    /* Eventos para el personaje (main.c los consume cada frame): carril+1, 0 = ninguno */
    int ev_hit, ev_hit_type;            /* nota del jugador acertada */
    int ev_miss, ev_miss_type;          /* nota del jugador fallada */
    int ev_dad_hit, ev_dad_hit_type;    /* nota del oponente (opponentNoteHit) */
} Game;

static Game g;

/* ---- Conductor.mapBPMChanges ---- */
static void conductor_map_bpm_changes(const Chart *ch) {
    float cur = ch->bpm, totalPos = 0;
    int totalSteps = 0, i, delta;
    C.mapCount = 0;
    for (i = 0; i < ch->sectionCount; i++) {
        const Section *s = &ch->sections[i];
        if (s->bpm != cur && C.mapCount < MAX_BPM_MAP) {
            cur = s->bpm;
            C.map[C.mapCount].stepTime = totalSteps;
            C.map[C.mapCount].songTime = totalPos;
            C.map[C.mapCount].bpm = cur;
            C.map[C.mapCount].stepCrochet = calc_crochet(cur) / 4.0f;
            C.mapCount++;
        }
        delta = (int)floorf(s->beats * 4.0f + 0.5f);
        totalSteps += delta;
        totalPos += ((60.0f / cur) * 1000.0f / 4.0f) * delta;
    }
}

static int judge_note(float diff) {
    int i;
    for (i = 0; i < 3; i++) if (diff <= (float)R_WIN[i]) return i;
    return 3;
}

static void apply_type(GNote *n) {
    if (n->type == 1) {
        n->ignoreNote = n->mustPress;
        n->hitCausesMiss = 1;
        n->lowPriority = 1;
        n->missHealth = n->isSustain ? 0.1f : 0.3f;
    }
}

static void play_free(void) {
    free(g.pool); free(g.order);
    g.pool = 0; g.order = 0;
}

static int cmp_order(const void *a, const void *b) {
    int ia = *(const int *)a, ib = *(const int *)b;
    float d = g.pool[ia].strumTime - g.pool[ib].strumTime;
    if (d < 0) return -1;
    if (d > 0) return 1;
    return ia - ib;
}

static int play_init(const Chart *ch, float songSpeed) {
    int i, j, n = 0, cap = 0, f;
    float step = C.stepCrochet, spd;

    memset(&g, 0, sizeof(g));
    g.songSpeed = songSpeed;
    g.noteKillOffset = 350.0f / songSpeed;
    g.health = 1.0f; g.healthGain = 1.0f; g.healthLoss = 1.0f;
    g.vocals = 1.0f;
    g.cam_zooming = 0;
    g.miss_combo_active = 0;
    g.ratingName = "?"; g.ratingFC = "";
    spd = floorf(songSpeed * 100.0f + 0.5f) / 100.0f;
    if (spd <= 0) spd = 1.0f;

    for (i = 0; i < ch->noteCount; i++) {
        f = (int)floorf((float)ch->notes[i].sustain / step);
        cap += 1;
        if (f > 0) cap += f + 1;
    }
    g.pool = (GNote *)malloc(sizeof(GNote) * (cap + 1));
    g.order = (int *)malloc(sizeof(int) * (cap + 1));
    if (!g.pool || !g.order) { play_free(); return -1; }
    memset(g.pool, 0, sizeof(GNote) * (cap + 1));

    for (i = 0; i < ch->noteCount; i++) {
        const Note *cn = &ch->notes[i];
        GNote *hd = &g.pool[n];
        hd->strumTime = (float)cn->time;
        hd->lane = cn->lane;
        hd->mustPress = cn->player;
        hd->earlyHitMult = 1.0f;
        hd->missHealth = 0.0475f; hd->hitHealth = 0.023f;
        hd->prev = n > 0 ? n - 1 : 0;
        hd->type = (unsigned char)cn->type;
        apply_type(hd);
        n++;

        f = (int)floorf((float)cn->sustain / step);
        if (f > 0) {
            for (j = 0; j <= f; j++) {
                GNote *s = &g.pool[n];
                s->strumTime = (float)cn->time + step * j + step / spd;
                s->lane = cn->lane;
                s->mustPress = cn->player;
                s->isSustain = 1;
                s->isEnd = (j == f);
                s->earlyHitMult = 0.5f;
                s->missHealth = 0.0475f; s->hitHealth = 0.023f;
                s->prev = n - 1;
                s->type = (unsigned char)cn->type;
                apply_type(s);
                n++;
            }
        }
    }
    g.count = n;
    for (i = 0; i < n; i++) g.order[i] = i;
    qsort(g.order, n, sizeof(int), cmp_order);
    return 0;
}

static void recalc_rating(void) {
    int i;
    float p;
    if (g.totalPlayed < 1) g.ratingName = "?";
    else {
        p = g.totalNotesHit / (float)g.totalPlayed;
        if (p > 1.0f) p = 1.0f;
        if (p < 0.0f) p = 0.0f;
        g.ratingPercent = p;
        if (p >= 1.0f) g.ratingName = RS_NAME[9];
        else for (i = 0; i < 9; i++)
            if (p < RS_VAL[i]) { g.ratingName = RS_NAME[i]; break; }
    }
    g.ratingFC = "";
    if (g.sicks > 0) g.ratingFC = "SFC";
    if (g.goods > 0) g.ratingFC = "GFC";
    if (g.bads > 0 || g.shits > 0) g.ratingFC = "FC";
    if (g.songMisses > 0 && g.songMisses < 10) g.ratingFC = "SDCB";
    else if (g.songMisses >= 10) g.ratingFC = "Clear";
}

static void strum_play(int side, int lane, float ms) {
    g.strum[side][lane] = 2;
    g.strumReset[side][lane] = ms;
}

/* ---- PlayState.noteMiss / noteMissPress ---- */
static void note_miss(GNote *n) {
    if (n->type != 3) { g.ev_miss = n->lane + 1; g.ev_miss_type = n->type; }   /* 3 = No Animation */
    /* Captura el combo perdido antes de resetear */
    if (g.combo > 0) {
        g.miss_combo_value = g.combo;
        g.miss_combo_age = 0;
        g.miss_combo_active = 1;
    }
    g.combo = 0;
    g.health -= n->missHealth * g.healthLoss;
    g.songMisses++;
    g.vocals = 0.0f;
    if (!g.practiceMode) g.songScore -= 10;
    g.totalPlayed++;
    recalc_rating();
}

static void note_miss_press(int lane) {
    if (GHOST_TAPPING) return;
    g.ev_miss = lane + 1; g.ev_miss_type = 0;
    if (g.combo > 0) {
        g.miss_combo_value = g.combo;
        g.miss_combo_age = 0;
        g.miss_combo_active = 1;
    }
    g.health -= 0.05f * g.healthLoss;
    g.combo = 0;
    if (!g.practiceMode) g.songScore -= 10;
    if (!g.endingSong) g.songMisses++;
    g.totalPlayed++;
    recalc_rating();
    g.vocals = 0.0f;
}

static void pop_up_score(GNote *n, float pos) {
    float diff = fabsf(n->strumTime - pos + RATING_OFFSET);
    int r = judge_note(diff);
    g.totalNotesHit += R_MOD[r];
    if (r == 0) g.sicks++; else if (r == 1) g.goods++; else if (r == 2) g.bads++; else g.shits++;
    if (!g.practiceMode && !g.cpuControlled) {
        g.songScore += R_SCORE[r];
        g.songHits++;
        g.totalPlayed++;
        recalc_rating();
    }
    g.popRating = r; g.popCombo = g.combo; g.popAge = 0;
}

static void good_note_hit(GNote *n, float pos) {
    if (n->wasGoodHit) return;
    if (g.cpuControlled && (n->ignoreNote || n->hitCausesMiss)) return;

    if (n->hitCausesMiss) {
        note_miss(n);
        n->wasGoodHit = 1;
        if (!n->isSustain) n->dead = 1;
        return;
    }
    if (!n->isSustain) {
        g.combo++;
        if (g.combo > 9999) g.combo = 9999;
        pop_up_score(n, pos);
    }
    g.health += n->hitHealth * g.healthGain;

    if (g.cpuControlled) {
        float t = 150.0f;
        if (n->isSustain && !n->isEnd) t += 150.0f;
        strum_play(1, n->lane, t);
    } else {
        g.strum[1][n->lane] = 2;
        g.strumReset[1][n->lane] = 0;
    }
    n->wasGoodHit = 1;
    g.vocals = 1.0f;
    g.ev_hit = n->lane + 1; g.ev_hit_type = n->type;
    if (!n->isSustain) n->dead = 1;
}

static void opponent_note_hit(GNote *n) {
    float t = 150.0f;
    g.cam_zooming = 1;
    g.vocals = 1.0f;
    if (n->isSustain && !n->isEnd) t += 150.0f;
    strum_play(0, n->lane, t);
    n->hitByOpponent = 1;
    g.ev_dad_hit = n->lane + 1; g.ev_dad_hit_type = n->type;
    if (!n->isSustain) n->dead = 1;
}

static void note_update(GNote *n, float pos) {
    float safe = C.safeZoneOffset;
    if (n->mustPress) {
        n->canBeHit = (n->strumTime > pos - safe * 1.0f && n->strumTime < pos + safe * n->earlyHitMult);
        if (n->strumTime < pos - safe && !n->wasGoodHit) n->tooLate = 1;
    } else {
        n->canBeHit = 0;
        if (n->strumTime < pos + safe * n->earlyHitMult) {
            if ((n->isSustain && g.pool[n->prev].wasGoodHit) || n->strumTime <= pos)
                n->wasGoodHit = 1;
        }
    }
}

static int hit_before(int a, int b) {
    GNote *x = &g.pool[a], *y = &g.pool[b];
    if (x->lowPriority != y->lowPriority)
        return (int)y->lowPriority - (int)x->lowPriority;
    if (x->strumTime < y->strumTime) return -1;
    if (x->strumTime > y->strumTime) return 1;
    return 0;
}

static void on_key_press(int key, float pos) {
    int cand[64], press[64], nc = 0, np = 0, stopped = 0, canMiss = !GHOST_TAPPING;
    int i, j, tmp;
    GNote *n;

    for (i = g.head; i < g.spawnEnd; i++) {
        n = &g.pool[g.order[i]];
        if (!n->dead && n->canBeHit && n->mustPress && !n->tooLate && !n->wasGoodHit && !n->isSustain) {
            if (n->lane == key && nc < 64) cand[nc++] = g.order[i];
            canMiss = 1;
        }
    }
    for (i = 1; i < nc; i++) {
        tmp = cand[i]; j = i - 1;
        while (j >= 0 && hit_before(tmp, cand[j]) < 0) { cand[j + 1] = cand[j]; j--; }
        cand[j + 1] = tmp;
    }
    if (nc > 0) {
        for (i = 0; i < nc; i++) {
            for (j = 0; j < np; j++) {
                if (fabsf(g.pool[press[j]].strumTime - g.pool[cand[i]].strumTime) < 1.0f)
                    g.pool[press[j]].dead = 1;
                else
                    stopped = 1;
            }
            if (!stopped) {
                good_note_hit(&g.pool[cand[i]], pos);
                press[np++] = cand[i];
            }
        }
    } else if (canMiss) {
        note_miss_press(key);
    }
    if (g.strum[1][key] != 2) { g.strum[1][key] = 1; g.strumReset[1][key] = 0; }
}

static void play_update(float pos, float dtMs, const int *held, const int *prevHeld) {
    int i, l, s;
    float t;
    GNote *n;

    t = SPAWN_TIME;
    if (g.songSpeed < 1.0f) t /= g.songSpeed;
    while (g.spawnEnd < g.count && g.pool[g.order[g.spawnEnd]].strumTime - pos < t) g.spawnEnd++;

    for (i = g.head; i < g.spawnEnd; i++) {
        n = &g.pool[g.order[i]];
        if (!n->dead) note_update(n, pos);
    }

    if (!g.cpuControlled) {
        for (l = 0; l < 4; l++) {
            if (held[l] && !prevHeld[l]) on_key_press(l, pos);
            if (!held[l] && prevHeld[l]) { g.strum[1][l] = 0; g.strumReset[1][l] = 0; }
        }
        for (i = g.head; i < g.spawnEnd; i++) {
            n = &g.pool[g.order[i]];
            if (!n->dead && n->isSustain && held[n->lane] && n->canBeHit && n->mustPress
                && !n->tooLate && !n->wasGoodHit)
                good_note_hit(n, pos);
        }
    }

    for (i = g.head; i < g.spawnEnd; i++) {
        n = &g.pool[g.order[i]];
        if (n->dead) continue;

        if (!n->mustPress && n->wasGoodHit && !n->hitByOpponent && !n->ignoreNote)
            opponent_note_hit(n);

        if (n->dead) continue;

        if (g.cpuControlled && n->mustPress && n->canBeHit) {
            if (n->isSustain) good_note_hit(n, pos);
            else if (n->strumTime <= pos) good_note_hit(n, pos);
        }
        if (n->dead) continue;

        if (pos > g.noteKillOffset + n->strumTime) {
            if (n->mustPress && !g.cpuControlled && !n->ignoreNote && !g.endingSong
                && (n->tooLate || !n->wasGoodHit))
                note_miss(n);
            n->dead = 1;
        }
    }

    for (s = 0; s < 2; s++)
        for (l = 0; l < 4; l++)
            if (g.strumReset[s][l] > 0) {
                g.strumReset[s][l] -= dtMs;
                if (g.strumReset[s][l] <= 0) { g.strum[s][l] = 0; g.strumReset[s][l] = 0; }
            }

    if (g.health > 2.0f) g.health = 2.0f;
    if (g.health <= 0.0f && !g.practiceMode) g.isDead = 1;
    g.popAge += dtMs;

    /* Miss combo popup: avanza edad y desactiva al pasar el tiempo */
    if (g.miss_combo_active) {
        g.miss_combo_age += dtMs;
        if (g.miss_combo_age > C.crochet * 2.0f + 200.0f) {
            g.miss_combo_active = 0;
            g.miss_combo_age = 0;
            g.miss_combo_value = 0;
        }
    }

    while (g.head < g.spawnEnd && g.pool[g.order[g.head]].dead) g.head++;
}

#endif