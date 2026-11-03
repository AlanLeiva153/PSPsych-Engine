#ifndef STAGE_H
#define STAGE_H
/* StageData.hx de Psych 0.6.3: lee mods/<mod>/stages/<nombre>.json y, si no existe, assets/preload/stages/<nombre>.json.
   Mismos campos que Psych. DIFERENCIA de este port: boyfriend / opponent / girlfriend son el ORIGEN
   del atlas del personaje en coordenadas de mundo de Psych (1280x720), no la esquina de su sprite. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chart.h"          /* jsmn + jget/jnum/jbool/jeq */

/* Centro visual aproximado del personaje respecto a su origen (para el enfoque de la camara),
   para un personaje SIN voltear. Si esta volteado se invierte X. Ajustalo a ojo. */
#ifndef STAGE_FOCUS_DX
#define STAGE_FOCUS_DX (-187.0f)
#endif
#ifndef STAGE_FOCUS_DY
#define STAGE_FOCUS_DY (-250.0f)
#endif

typedef struct {
    char  name[48];
    float defaultZoom;
    int   isPixelStage;
    float bf[2], gf[2], dad[2];
    int   hide_gf;
    float cam_bf[2], cam_dad[2], cam_gf[2];
    float cameraSpeed;
    int   fromFile;
} Stage;

static void stage_defaults(Stage *s) {
    memset(s, 0, sizeof(*s));
    strcpy(s->name, "stage");
    s->defaultZoom = 0.9f;               /* stage.json de Psych */
    s->bf[0]  = 1187.0f; s->bf[1]  = 637.0f;
    s->dad[0] =   93.0f; s->dad[1] = 637.0f;
    s->gf[0]  =  640.0f; s->gf[1]  = 637.0f;
    s->hide_gf = 1;
    s->cameraSpeed = 1.0f;
}

static void stage_arr2(const char *js, jsmntok_t *tk, int obj, const char *key, float *out) {
    int v = jget(js, tk, obj, key);
    if (v >= 0 && tk[v].type == JSMN_ARRAY && tk[v].size >= 2) {
        out[0] = (float)jnum(js, &tk[v + 1]);
        out[1] = (float)jnum(js, &tk[v + 2]);
    }
}

static int stage_parse_file(Stage *s, const char *path) {
    FILE *f = fopen(path, "rb");
    long size;
    char *js;
    jsmn_parser p;
    jsmntok_t *tk;
    int ntok, v;

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

    if (tk[0].type == JSMN_OBJECT) {
        v = jget(js, tk, 0, "defaultZoom");
        if (v >= 0 && tk[v].type == JSMN_PRIMITIVE) s->defaultZoom = (float)jnum(js, &tk[v]);
        v = jget(js, tk, 0, "isPixelStage");
        if (v >= 0) s->isPixelStage = jbool(js, &tk[v]);
        v = jget(js, tk, 0, "hide_girlfriend");
        if (v >= 0) s->hide_gf = jbool(js, &tk[v]);
        v = jget(js, tk, 0, "camera_speed");
        if (v >= 0 && tk[v].type == JSMN_PRIMITIVE && js[tk[v].start] != 'n')   /* null = valor por defecto */
            s->cameraSpeed = (float)jnum(js, &tk[v]);
        stage_arr2(js, tk, 0, "boyfriend", s->bf);
        stage_arr2(js, tk, 0, "girlfriend", s->gf);
        stage_arr2(js, tk, 0, "opponent", s->dad);
        stage_arr2(js, tk, 0, "camera_boyfriend", s->cam_bf);
        stage_arr2(js, tk, 0, "camera_opponent", s->cam_dad);
        stage_arr2(js, tk, 0, "camera_girlfriend", s->cam_gf);
    }
    free(tk);
    free(js);
    if (s->defaultZoom <= 0.01f) s->defaultZoom = 0.9f;
    if (s->cameraSpeed <= 0.0f) s->cameraSpeed = 1.0f;
    return 0;
}

/* Orden de busqueda igual que StageData.getStageFile: mods primero, luego assets/preload. */
static int stage_load(Stage *s, const char *name, const char *mod) {
    char p[256];
    stage_defaults(s);
    if (!name || !name[0]) name = "stage";
    strncpy(s->name, name, sizeof(s->name) - 1);

    if (mod && mod[0]) {
        snprintf(p, sizeof(p), "mods/%s/stages/%s.json", mod, name);
        if (stage_parse_file(s, p) == 0) { s->fromFile = 1; return 0; }
    }
    snprintf(p, sizeof(p), "assets/preload/stages/%s.json", name);   /* Paths.getPreloadPath('stages/...') */
    if (stage_parse_file(s, p) == 0) { s->fromFile = 1; return 0; }
    snprintf(p, sizeof(p), "%s.json", name);              /* junto al EBOOT */
    if (stage_parse_file(s, p) == 0) { s->fromFile = 1; return 0; }
    return -1;                                            /* se quedan los valores por defecto */
}

#endif
