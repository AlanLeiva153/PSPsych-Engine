#ifndef CHART_H
#define CHART_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define JSMN_STATIC
#include "jsmn.h"

/* type: 0 normal, 1 Hurt Note, 2 Alt Animation, 3 No Animation, 4 GF Sing, 5 Hey! */
typedef struct { int32_t time; uint8_t lane, player; uint16_t type; uint32_t sustain; } Note;
typedef struct { int32_t time; uint8_t mustHit, gf; float bpm, beats; } Section;

/* Evento "Add Camera Zoom" */
typedef struct {
    int32_t time;
    float camZoom;
    float hudZoom;
} ZoomEvent;

typedef struct {
    float bpm, speed;
    int needsVoices;
    char stage[48];                 /* SONG.stage (por defecto "stage") */
    char songName[48];              /* SONG.song (lo usa el menu de pausa) */
    int noteCount, sectionCount;
    Note *notes;
    Section *sections;

    int zoomEventCount;
    ZoomEvent *zoomEvents;
} Chart;

static int jskip(jsmntok_t *t, int i) {
    int n = t[i].size;
    i++;
    while (n-- > 0) i = jskip(t, i);
    return i;
}

static int jeq(const char *js, jsmntok_t *t, const char *s) {
    int len = t->end - t->start;
    return (int)strlen(s) == len && strncmp(js + t->start, s, len) == 0;
}

static int jget(const char *js, jsmntok_t *t, int obj, const char *key) {
    int i = obj + 1, n;
    for (n = t[obj].size; n > 0; n--) {
        if (t[i].type == JSMN_STRING && jeq(js, &t[i], key)) return i + 1;
        i = jskip(t, i + 1);
    }
    return -1;
}

static double jnum(const char *js, jsmntok_t *t) {
    char buf[32];
    int len = t->end - t->start;
    if (len > 31) len = 31;
    memcpy(buf, js + t->start, len);
    buf[len] = 0;
    return atof(buf);
}

/* Variante con valor por defecto si el token es string vacío */
static double jnum_or(const char *js, jsmntok_t *t, double def) {
    int len;
    char buf[32];
    char *p;
    if (!t) return def;
    len = t->end - t->start;
    if (len <= 0) return def;
    if (len > 31) len = 31;
    memcpy(buf, js + t->start, len);
    buf[len] = 0;
    p = buf;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == 0) return def;
    return atof(buf);
}

static int jbool(const char *js, jsmntok_t *t) { return js[t->start] == 't'; }

static int cmp_notes(const void *a, const void *b) {
    const Note *x = (const Note *)a, *y = (const Note *)b;
    if (x->time != y->time) return x->time - y->time;
    return (int)x->lane - (int)y->lane;
}

static int cmp_zoom_events(const void *a, const void *b) {
    const ZoomEvent *x = (const ZoomEvent *)a;
    const ZoomEvent *y = (const ZoomEvent *)b;
    if (x->time < y->time) return -1;
    if (x->time > y->time) return 1;
    return 0;
}

static int note_type_from_token(const char *js, jsmntok_t *t) {
    if (t->type == JSMN_STRING) {
        if (jeq(js, t, "Hurt Note"))     return 1;
        if (jeq(js, t, "Alt Animation")) return 2;
        if (jeq(js, t, "No Animation"))  return 3;
        if (jeq(js, t, "GF Sing"))       return 4;
        if (jeq(js, t, "Hey!"))          return 5;
        return 0;
    }
    if (t->type == JSMN_PRIMITIVE) {
        switch ((int)jnum(js, t)) {
            case 1: return 2;
            case 2: return 5;
            case 3: return 1;
            case 4: return 4;
            case 5: return 3;
        }
    }
    return 0;
}

static int chart_load_json(const char *path, Chart *c) {
    FILE *f;
    char *js;
    long size;
    jsmn_parser p;
    jsmntok_t *tk;
    int ntok, song, v, secs, i, j, ni = 0, si;
    float bpm, t_sec = 0;

    memset(c, 0, sizeof(Chart));
    f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    js = (char *)malloc(size + 1);
    if (!js) { fclose(f); return -2; }
    size = (long)fread(js, 1, size, f);
    fclose(f);
    while (size > 0 && js[size - 1] != '}') size--;
    js[size] = 0;

    jsmn_init(&p);
    ntok = jsmn_parse(&p, js, size, NULL, 0);
    if (ntok <= 0) { free(js); return -3; }
    tk = (jsmntok_t *)malloc(sizeof(jsmntok_t) * ntok);
    if (!tk) { free(js); return -2; }
    jsmn_init(&p);
    jsmn_parse(&p, js, size, tk, ntok);

    song = jget(js, tk, 0, "song");
    if (song < 0 || tk[song].type != JSMN_OBJECT) song = 0;

    v = jget(js, tk, song, "bpm");   bpm = v >= 0 ? (float)jnum(js, &tk[v]) : 100.0f;
    c->bpm = bpm;
    v = jget(js, tk, song, "speed"); c->speed = v >= 0 ? (float)jnum(js, &tk[v]) : 1.0f;
    v = jget(js, tk, song, "needsVoices"); c->needsVoices = v >= 0 ? jbool(js, &tk[v]) : 1;
    strcpy(c->stage, "stage");
    v = jget(js, tk, song, "stage");
    if (v >= 0 && tk[v].type == JSMN_STRING && tk[v].end > tk[v].start) {
        int n = tk[v].end - tk[v].start;
        if (n > (int)sizeof(c->stage) - 1) n = (int)sizeof(c->stage) - 1;
        memcpy(c->stage, js + tk[v].start, n);
        c->stage[n] = 0;
    }
    strcpy(c->songName, "Song");
    v = jget(js, tk, song, "song");
    if (v >= 0 && tk[v].type == JSMN_STRING && tk[v].end > tk[v].start) {
        int n = tk[v].end - tk[v].start;
        if (n > (int)sizeof(c->songName) - 1) n = (int)sizeof(c->songName) - 1;
        memcpy(c->songName, js + tk[v].start, n);
        c->songName[n] = 0;
    }
    secs = jget(js, tk, song, "notes");
    if (secs < 0) { free(tk); free(js); return -4; }

    c->sectionCount = tk[secs].size;
    c->sections = (Section *)malloc(sizeof(Section) * (c->sectionCount + 1));

    i = secs + 1;
    for (si = 0; si < c->sectionCount; si++) {
        int sn = jget(js, tk, i, "sectionNotes");
        if (sn >= 0) c->noteCount += tk[sn].size;
        i = jskip(tk, i);
    }
    c->notes = (Note *)malloc(sizeof(Note) * (c->noteCount + 1));

    i = secs + 1;
    for (si = 0; si < c->sectionCount; si++) {
        int must, beats_i, sn, ch;
        float beats;

        ch = jget(js, tk, i, "changeBPM");
        if (ch >= 0 && jbool(js, &tk[ch])) {
            v = jget(js, tk, i, "bpm");
            if (v >= 0) bpm = (float)jnum(js, &tk[v]);
        }
        v = jget(js, tk, i, "mustHitSection");
        must = (v >= 0) ? jbool(js, &tk[v]) : 0;

        beats_i = jget(js, tk, i, "sectionBeats");
        if (beats_i >= 0) beats = (float)jnum(js, &tk[beats_i]);
        else {
            v = jget(js, tk, i, "lengthInSteps");
            beats = (v >= 0 ? (float)jnum(js, &tk[v]) : 16.0f) / 4.0f;
        }

        c->sections[si].time = (int32_t)(t_sec + 0.5f);
        c->sections[si].mustHit = (uint8_t)must;
        v = jget(js, tk, i, "gfSection");
        c->sections[si].gf = (v >= 0 && jbool(js, &tk[v])) ? 1 : 0;
        c->sections[si].bpm = bpm;
        c->sections[si].beats = beats;

        sn = jget(js, tk, i, "sectionNotes");
        if (sn >= 0) {
            int k = sn + 1;
            for (j = 0; j < tk[sn].size; j++) {
                int cnt = tk[k].size;
                if (tk[k].type == JSMN_ARRAY && cnt >= 3) {
                    int d = (int)jnum(js, &tk[k + 2]);
                    if (d >= 0) {
                        Note *n = &c->notes[ni];
                        float sus = (float)jnum(js, &tk[k + 3]);
                        n->time = (int32_t)(jnum(js, &tk[k + 1]) + 0.5);
                        n->lane = (uint8_t)(d % 4);
                        n->player = (uint8_t)((d < 4) ? must : !must);
                        n->sustain = sus > 0 ? (uint32_t)(sus + 0.5f) : 0;
                        n->type = (cnt >= 4) ? (uint16_t)note_type_from_token(js, &tk[k + 4]) : 0;
                        ni++;
                    }
                }
                k = jskip(tk, k);
            }
        }
        t_sec += beats * (60000.0f / bpm);
        i = jskip(tk, i);
    }
    c->noteCount = ni;
    qsort(c->notes, c->noteCount, sizeof(Note), cmp_notes);

    /* ---- Parse events: "Add Camera Zoom" ---- */
    c->zoomEventCount = 0;
    c->zoomEvents = NULL;
    {
        int ev = jget(js, tk, song, "events");
        if (ev >= 0 && tk[ev].type == JSMN_ARRAY && tk[ev].size > 0) {
            int total = tk[ev].size;
            c->zoomEvents = (ZoomEvent *)malloc(sizeof(ZoomEvent) * total);
            if (c->zoomEvents) {
                int k = ev + 1;
                for (j = 0; j < total; j++) {
                    if (tk[k].type == JSMN_ARRAY && tk[k].size >= 2) {
                        int32_t evTime = (int32_t)(jnum(js, &tk[k + 1]) + 0.5);
                        int sub = k + 2;
                        if (tk[sub].type == JSMN_ARRAY) {
                            int sub_count = tk[sub].size;
                            int sidx = sub + 1;
                            int s;
                            for (s = 0; s < sub_count; s++) {
                                if (tk[sidx].type == JSMN_ARRAY && tk[sidx].size >= 2 &&
                                    tk[sidx + 1].type == JSMN_STRING &&
                                    jeq(js, &tk[sidx + 1], "Add Camera Zoom")) {
                                    ZoomEvent *ze = &c->zoomEvents[c->zoomEventCount];
                                    ze->time = evTime;
                                    ze->camZoom = 0.015f;
                                    ze->hudZoom = 0.03f;
                                    if (tk[sidx].size >= 2)
                                        ze->camZoom = (float)jnum_or(js, &tk[sidx + 2], 0.015);
                                    if (tk[sidx].size >= 3)
                                        ze->hudZoom = (float)jnum_or(js, &tk[sidx + 3], 0.03);
                                    c->zoomEventCount++;
                                }
                                sidx = jskip(tk, sidx);
                            }
                        }
                    }
                    k = jskip(tk, k);
                }
                if (c->zoomEventCount > 0) {
                    qsort(c->zoomEvents, c->zoomEventCount, sizeof(ZoomEvent), cmp_zoom_events);
                }
            }
        }
    }

    free(tk);
    free(js);
    return 0;
}

static void chart_free(Chart *c) {
    free(c->notes);
    free(c->sections);
    free(c->zoomEvents);
    c->notes = 0;
    c->sections = 0;
    c->zoomEvents = 0;
    c->zoomEventCount = 0;
}

#endif