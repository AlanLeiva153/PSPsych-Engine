#ifndef WEEKS_H
#define WEEKS_H
/* WeekData.hx de Psych 0.6.3 adaptado a PSP.

   ORDEN: preload primero, luego los mods en el orden de modlist.txt.
   Cada mod sin weekList -> sus weeks en orden alfabetico.
   Cada mod con weekList -> whitelist estricta, en el orden del txt.

   OVERRIDE: si un mod define una week que ya existe (p.ej. week1),
   su JSON reemplaza al del preload/mods anteriores, pero la posicion
   en el listado se mantiene en el slot original. El ULTIMO mod de la
   lista gana si varios definen la misma week.

   Sin mods activos -> solo preload. */

#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chart.h"
#include "mods.h"

#define WEEKS_MAX       32
#define WEEK_SONGS_MAX  256

#ifndef WEEKS_UNLOCK_ALL
#define WEEKS_UNLOCK_ALL 0
#endif

#ifndef WEEKS_DEBUG
#define WEEKS_DEBUG 0
#endif
#if WEEKS_DEBUG
#define WK_LOG(...) printf(__VA_ARGS__)
#else
#define WK_LOG(...) ((void)0)
#endif

typedef struct {
    char name[64];
    char lower[64];
    char character[32];
    int  r, g, b;
    int  weekIdx;
} WeekSong;

typedef struct {
    char name[48];
    char weekName[64];
    char storyName[64];
    char weekBefore[48];
    char weekBackground[48];
    char characters[3][32];
    int  startUnlocked;
    int  hideStoryMode;
    int  hideFreeplay;
    char difficulties[128];
    int  songStart, songCount;
    char folder[48];
} WeekMeta;

typedef struct {
    WeekMeta weeks[WEEKS_MAX];
    WeekSong songs[WEEK_SONGS_MAX];
    int weekCount;
    int songCount;
} WeekData;

typedef struct {
    char name[48];
    char path[256];
    char folder[48];
} WeekSlot;

static WeekData WD;
static WeekSlot WSLOTS[WEEKS_MAX];
static int WSLOT_COUNT = 0;

/* ===================================================================== */
/*  Helpers                                                              */
/* ===================================================================== */

static void wk_format_song_path(char *s) {
    int i, j = 0;
    for (i = 0; s[i]; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c == ' ') c = '-';
        s[j++] = c;
    }
    s[j] = 0;
}

static int wk_read_file(const char *path, char **out, long *outSize) {
    FILE *f = fopen(path, "rb");
    long sz;
    char *buf;
    if (!f) return -1;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return -1; }
    buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return -2; }
    sz = (long)fread(buf, 1, sz, f);
    fclose(f);
    buf[sz] = 0;
    if (sz >= 3 && (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        memmove(buf, buf + 3, sz - 3 + 1);
        sz -= 3;
    }
    *out = buf;
    if (outSize) *outSize = sz;
    return 0;
}

static int wk_parse_week(WeekMeta *w, const char *path, const char *folder, int weekIdx) {
    char *js = NULL;
    long sz = 0;
    jsmn_parser prs;
    jsmntok_t *tk;
    int ntok, v, i, secTok, secCount, si;

    if (wk_read_file(path, &js, &sz) != 0) return -1;
    jsmn_init(&prs);
    ntok = jsmn_parse(&prs, js, sz, NULL, 0);
    if (ntok <= 0) { free(js); return -2; }
    tk = (jsmntok_t *)malloc(sizeof(jsmntok_t) * ntok);
    if (!tk) { free(js); return -2; }
    jsmn_init(&prs);
    jsmn_parse(&prs, js, sz, tk, ntok);

    memset(w, 0, sizeof(*w));
    w->songStart = WD.songCount;
    w->songCount = 0;
    strncpy(w->folder, folder ? folder : "", sizeof(w->folder) - 1);

    v = jget(js, tk, 0, "weekName");
    if (v >= 0 && tk[v].type == JSMN_STRING) {
        int n = tk[v].end - tk[v].start;
        if (n > (int)sizeof(w->weekName) - 1) n = sizeof(w->weekName) - 1;
        memcpy(w->weekName, js + tk[v].start, n); w->weekName[n] = 0;
    }
    v = jget(js, tk, 0, "storyName");
    if (v >= 0 && tk[v].type == JSMN_STRING) {
        int n = tk[v].end - tk[v].start;
        if (n > (int)sizeof(w->storyName) - 1) n = sizeof(w->storyName) - 1;
        memcpy(w->storyName, js + tk[v].start, n); w->storyName[n] = 0;
    }
    v = jget(js, tk, 0, "weekBefore");
    if (v >= 0 && tk[v].type == JSMN_STRING) {
        int n = tk[v].end - tk[v].start;
        if (n > (int)sizeof(w->weekBefore) - 1) n = sizeof(w->weekBefore) - 1;
        memcpy(w->weekBefore, js + tk[v].start, n); w->weekBefore[n] = 0;
    }
    v = jget(js, tk, 0, "weekBackground");
    if (v >= 0 && tk[v].type == JSMN_STRING) {
        int n = tk[v].end - tk[v].start;
        if (n > (int)sizeof(w->weekBackground) - 1) n = sizeof(w->weekBackground) - 1;
        memcpy(w->weekBackground, js + tk[v].start, n); w->weekBackground[n] = 0;
    }
    v = jget(js, tk, 0, "startUnlocked");
    if (v >= 0) w->startUnlocked = jbool(js, &tk[v]);
    v = jget(js, tk, 0, "hideStoryMode");
    if (v >= 0) w->hideStoryMode = jbool(js, &tk[v]);
    v = jget(js, tk, 0, "hideFreeplay");
    if (v >= 0) w->hideFreeplay = jbool(js, &tk[v]);
    v = jget(js, tk, 0, "difficulties");
    if (v >= 0 && tk[v].type == JSMN_STRING) {
        int n = tk[v].end - tk[v].start;
        if (n > (int)sizeof(w->difficulties) - 1) n = sizeof(w->difficulties) - 1;
        memcpy(w->difficulties, js + tk[v].start, n); w->difficulties[n] = 0;
    }

    v = jget(js, tk, 0, "weekCharacters");
    if (v >= 0 && tk[v].type == JSMN_ARRAY && tk[v].size >= 3) {
        for (i = 0; i < 3; i++) {
            int t = v + 1 + i;
            int n = tk[t].end - tk[t].start;
            if (n > (int)sizeof(w->characters[i]) - 1) n = sizeof(w->characters[i]) - 1;
            memcpy(w->characters[i], js + tk[t].start, n);
            w->characters[i][n] = 0;
        }
    }

    secTok = jget(js, tk, 0, "songs");
    if (secTok >= 0 && tk[secTok].type == JSMN_ARRAY) {
        secCount = tk[secTok].size;
        si = secTok + 1;
        for (i = 0; i < secCount && WD.songCount < WEEK_SONGS_MAX; i++) {
            if (tk[si].type == JSMN_ARRAY && tk[si].size >= 2) {
                WeekSong *s = &WD.songs[WD.songCount];
                int nameTok  = si + 1;
                int charTok  = si + 2;
                int colorTok = (tk[si].size >= 3) ? si + 3 : -1;
                int n;

                memset(s, 0, sizeof(*s));
                n = tk[nameTok].end - tk[nameTok].start;
                if (n > (int)sizeof(s->name) - 1) n = sizeof(s->name) - 1;
                memcpy(s->name, js + tk[nameTok].start, n); s->name[n] = 0;
                strncpy(s->lower, s->name, sizeof(s->lower) - 1);
                wk_format_song_path(s->lower);

                n = tk[charTok].end - tk[charTok].start;
                if (n > (int)sizeof(s->character) - 1) n = sizeof(s->character) - 1;
                memcpy(s->character, js + tk[charTok].start, n); s->character[n] = 0;

                s->r = 146; s->g = 113; s->b = 253;
                if (colorTok >= 0 && tk[colorTok].type == JSMN_ARRAY && tk[colorTok].size >= 3) {
                    s->r = (int)jnum(js, &tk[colorTok + 1]);
                    s->g = (int)jnum(js, &tk[colorTok + 2]);
                    s->b = (int)jnum(js, &tk[colorTok + 3]);
                }
                s->weekIdx = weekIdx;
                WD.songCount++;
            }
            si = jskip(tk, si);
        }
        w->songCount = WD.songCount - w->songStart;
    }

    free(tk);
    free(js);
    return 0;
}

/* ===================================================================== */
/*  Slots (descubrimiento)                                               */
/* ===================================================================== */

static int wk_slot_find(const char *name) {
    int i;
    for (i = 0; i < WSLOT_COUNT; i++)
        if (strcmp(WSLOTS[i].name, name) == 0) return i;
    return -1;
}

static void wk_slot_add_or_override(const char *name, const char *path, const char *folder) {
    int idx = wk_slot_find(name);
    if (idx >= 0) {
        strncpy(WSLOTS[idx].path, path, sizeof(WSLOTS[idx].path) - 1);
        WSLOTS[idx].path[sizeof(WSLOTS[idx].path) - 1] = 0;
        strncpy(WSLOTS[idx].folder, folder ? folder : "", sizeof(WSLOTS[idx].folder) - 1);
        WSLOTS[idx].folder[sizeof(WSLOTS[idx].folder) - 1] = 0;
        WK_LOG("[weeks]   override '%s' -> %s\n", name, path);
        return;
    }
    if (WSLOT_COUNT >= WEEKS_MAX) return;
    strncpy(WSLOTS[WSLOT_COUNT].name, name, sizeof(WSLOTS[WSLOT_COUNT].name) - 1);
    WSLOTS[WSLOT_COUNT].name[sizeof(WSLOTS[WSLOT_COUNT].name) - 1] = 0;
    strncpy(WSLOTS[WSLOT_COUNT].path, path, sizeof(WSLOTS[WSLOT_COUNT].path) - 1);
    WSLOTS[WSLOT_COUNT].path[sizeof(WSLOTS[WSLOT_COUNT].path) - 1] = 0;
    strncpy(WSLOTS[WSLOT_COUNT].folder, folder ? folder : "", sizeof(WSLOTS[WSLOT_COUNT].folder) - 1);
    WSLOTS[WSLOT_COUNT].folder[sizeof(WSLOTS[WSLOT_COUNT].folder) - 1] = 0;
    WK_LOG("[weeks]   + slot '%s' -> %s\n", name, path);
    WSLOT_COUNT++;
}

/* Ordena nombres alfabeticamente (bubble simple, N pequeño) */
static void wk_sort_names(char names[][48], int n) {
    int i, j;
    for (i = 1; i < n; i++) {
        char tmp[48];
        strncpy(tmp, names[i], sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = 0;
        j = i - 1;
        while (j >= 0 && strcmp(names[j], tmp) > 0) {
            strncpy(names[j + 1], names[j], 48);
            j--;
        }
        strncpy(names[j + 1], tmp, 48);
    }
}

/* Lee una lista (txt) y devuelve nombres en names[]. Devuelve count o -1. */
static int wk_read_name_list(const char *path, char names[][48], int max) {
    char *txt = NULL;
    long sz = 0;
    const char *line, *end;
    int count = 0;

    if (wk_read_file(path, &txt, &sz) != 0) return -1;

    line = txt; end = txt + sz;
    while (line < end && count < max) {
        const char *eol = line;
        char name[48];
        int n;
        while (eol < end && *eol != '\n' && *eol != '\r') eol++;
        n = (int)(eol - line);
        if (n > (int)sizeof(name) - 1) n = sizeof(name) - 1;
        memcpy(name, line, n);
        name[n] = 0;
        while (n > 0 && (name[n - 1] == ' ' || name[n - 1] == '\t')) name[--n] = 0;
        {
            int s = 0;
            while (s < n && (name[s] == ' ' || name[s] == '\t')) s++;
            if (s > 0) memmove(name, name + s, n - s + 1);
        }
        if (name[0] && name[0] != '#') {
            strncpy(names[count], name, 48);
            names[count][47] = 0;
            count++;
        }
        while (eol < end && (*eol == '\n' || *eol == '\r')) eol++;
        line = eol;
    }

    free(txt);
    return count;
}

/* Escanea dir y devuelve nombres de .json (sin ext) en names[]. Ordena alfa. */
static int wk_scan_dir_names(const char *dir, char names[][48], int max) {
    SceUID dh = sceIoDopen(dir);
    SceIoDirent entry;
    int count = 0;

    if (dh < 0) return -1;
    while (sceIoDread(dh, &entry) > 0 && count < max) {
        int len = (int)strlen(entry.d_name);
        char name[48];
        int n2, k, dup;
        if (FIO_S_ISDIR(entry.d_stat.st_mode)) continue;
        if (len < 6) continue;
        if (strcmp(entry.d_name + len - 5, ".json") != 0) continue;

        n2 = len - 5;
        if (n2 > (int)sizeof(name) - 1) n2 = sizeof(name) - 1;
        memcpy(name, entry.d_name, n2);
        name[n2] = 0;

        dup = 0;
        for (k = 0; k < count; k++)
            if (strcmp(names[k], name) == 0) { dup = 1; break; }
        if (dup) continue;

        strncpy(names[count], name, 48);
        names[count][47] = 0;
        count++;
    }
    sceIoDclose(dh);

    if (count > 0) wk_sort_names(names, count);
    return count;
}

/* ===================================================================== */
/*  Descubrimiento: preload                                              */
/* ===================================================================== */

static void wk_discover_preload(void) {
    static const char *LIST_TXT[] = { "weeksList.txt", "weekList.txt", "weeks.txt" };
    char p[256];
    char names[WEEKS_MAX][48];
    int count = -1, ci, i;

    for (ci = 0; ci < 3 && count < 0; ci++) {
        snprintf(p, sizeof(p), "assets/preload/%s", LIST_TXT[ci]);
        count = wk_read_name_list(p, names, WEEKS_MAX);
        if (count >= 0) {
            WK_LOG("[weeks] preload: %s (%d nombres)\n", p, count);
            break;
        }
        snprintf(p, sizeof(p), "%s", LIST_TXT[ci]);
        count = wk_read_name_list(p, names, WEEKS_MAX);
        if (count >= 0) {
            WK_LOG("[weeks] preload: %s (%d nombres)\n", p, count);
            break;
        }
    }

    if (count < 0) {
        /* Sin txt: escanear */
        count = wk_scan_dir_names("assets/preload/weeks", names, WEEKS_MAX);
        if (count < 0) count = 0;
        if (count == 0) {
            count = wk_scan_dir_names("weeks", names, WEEKS_MAX);
            if (count < 0) count = 0;
        }
        WK_LOG("[weeks] preload: scan (%d nombres)\n", count);
    }

    for (i = 0; i < count; i++) {
        char path[256];
        FILE *f;

        snprintf(path, sizeof(path), "assets/preload/weeks/%s.json", names[i]);
        f = fopen(path, "rb");
        if (f) { fclose(f); wk_slot_add_or_override(names[i], path, ""); continue; }

        snprintf(path, sizeof(path), "weeks/%s.json", names[i]);
        f = fopen(path, "rb");
        if (f) { fclose(f); wk_slot_add_or_override(names[i], path, ""); continue; }

        WK_LOG("[weeks] preload: '%s' no existe\n", names[i]);
    }
}

/* ===================================================================== */
/*  Descubrimiento: mod                                                  */
/* ===================================================================== */

static void wk_discover_mod(const char *mod) {
    static const char *LIST_TXT[] = { "weeksList.txt", "weekList.txt", "weeks.txt" };
    char p[256];
    char names[WEEKS_MAX][48];
    int count = -1, ci, i;

    for (ci = 0; ci < 3 && count < 0; ci++) {
        snprintf(p, sizeof(p), "mods/%s/%s", mod, LIST_TXT[ci]);
        count = wk_read_name_list(p, names, WEEKS_MAX);
        if (count >= 0) {
            WK_LOG("[weeks] mod '%s': %s (%d nombres, whitelist)\n", mod, p, count);
            break;
        }
    }

    if (count < 0) {
        snprintf(p, sizeof(p), "mods/%s/weeks", mod);
        count = wk_scan_dir_names(p, names, WEEKS_MAX);
        if (count < 0) count = 0;
        WK_LOG("[weeks] mod '%s': scan %s (%d nombres)\n", mod, p, count);
    }

    for (i = 0; i < count; i++) {
        char path[256];
        FILE *f;
        snprintf(path, sizeof(path), "mods/%s/weeks/%s.json", mod, names[i]);
        f = fopen(path, "rb");
        if (f) { fclose(f); wk_slot_add_or_override(names[i], path, mod); }
        else WK_LOG("[weeks] mod '%s': '%s' no existe, se omite\n", mod, names[i]);
    }
}

/* ===================================================================== */
/*  Load (fase 2)                                                        */
/* ===================================================================== */

static int weeks_load(WeekData *wd) {
    int i;

    memset(wd, 0, sizeof(*wd));
    WSLOT_COUNT = 0;

    WK_LOG("[weeks] === fase 1: descubrimiento ===\n");
    wk_discover_preload();

    for (i = 0; i < ActiveMods.count; i++) {
        if (!ActiveMods.mods[i].active) continue;
        wk_discover_mod(ActiveMods.mods[i].name);
    }

    WK_LOG("[weeks] === fase 2: carga (%d slots) ===\n", WSLOT_COUNT);
    for (i = 0; i < WSLOT_COUNT && wd->weekCount < WEEKS_MAX; i++) {
        WeekMeta *w = &wd->weeks[wd->weekCount];
        int idx = wd->weekCount;
        int r;

        memset(w, 0, sizeof(*w));
        strncpy(w->name, WSLOTS[i].name, sizeof(w->name) - 1);

        r = wk_parse_week(w, WSLOTS[i].path, WSLOTS[i].folder, idx);
        if (r == 0) {
            wd->weekCount++;
            WK_LOG("[weeks] cargada '%s' (%d songs) <- %s\n",
                   w->name, w->songCount, WSLOTS[i].path);
        } else {
            WK_LOG("[weeks] ERROR '%s' <- %s (r=%d)\n",
                   WSLOTS[i].name, WSLOTS[i].path, r);
        }
    }

    WK_LOG("[weeks] total: %d semanas, %d canciones\n", wd->weekCount, wd->songCount);
    return wd->weekCount > 0 ? 0 : -1;
}

static int weeks_week_is_locked(WeekData *wd, int weekIdx) {
#if WEEKS_UNLOCK_ALL
    (void)wd; (void)weekIdx;
    return 0;
#else
    WeekMeta *w = &wd->weeks[weekIdx];
    if (w->startUnlocked) return 0;
    if (w->weekBefore[0]) return 1;
    return 0;
#endif
}

#endif