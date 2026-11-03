#ifndef NOTE_ATLAS_H
#define NOTE_ATLAS_H
/* Carga en runtime un Sparrow atlas XML + textura .tex (RGBA cruda).
   Compatible con mods: cambiar .xml/.tex sin recompilar.

   Formatos .tex soportados:
     - CON cabecera:  4 bytes (uint16 W, uint16 H) + W*H*4 bytes RGBA
     - SIN cabecera:  W*H*4 bytes RGBA (asume 512x512)
   La deteccion es automatica por tamaño del archivo.

   NA_DEBUG=1 imprime trazas en la consola de PPSSPP (Debug -> Log).
   Ponlo a 0 cuando ya funcione. */

#include <pspgu.h>
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <ctype.h>
#include <math.h>

#ifndef NA_DEBUG
#define NA_DEBUG 1
#endif

#if NA_DEBUG
#define NA_LOG(...) printf(__VA_ARGS__)
#else
#define NA_LOG(...) ((void)0)
#endif

#ifndef ATLAS_MAX_FRAMES
#define ATLAS_MAX_FRAMES  256
#endif
#define ATLAS_NAME_LEN    64
#define ANIM_MAX          32

typedef struct {
    char name[ATLAS_NAME_LEN];
    float u, v, tw, th;
    float ox, oy, w, h, sw, sh;
} AFrame;

typedef struct {
    unsigned int *tex;
    int tex_w, tex_h;
    int loaded;

    AFrame frames[ATLAS_MAX_FRAMES];
    int count;

    short A_STATIC[4];
    short A_NOTE[4];
    short A_HOLD[4];
    short A_END[4];
    short A_PRESS[4][ANIM_MAX];
    short A_CONFIRM[4][ANIM_MAX];
    short A_PRESS_N[4];
    short A_CONFIRM_N[4];
} NoteAtlas;

static int na_is_pot(int n) { return n > 0 && (n & (n - 1)) == 0; }

/* ------------------------------------------------------------------ */
/* Mini parser XML: busca key="value" dentro de [p, end).              */
/* ------------------------------------------------------------------ */
static int na_xml_attr(const char *p, const char *end, const char *key,
                       char *out, int out_len) {
    int klen = (int)strlen(key);
    const char *q = p;
    while (q < end) {
        if ((q == p || q[-1] == ' ' || q[-1] == '\t' ||
             q[-1] == '\n' || q[-1] == '\r' || q[-1] == '<') &&
            q + klen + 2 < end &&
            strncmp(q, key, klen) == 0 &&
            q[klen] == '=' && q[klen + 1] == '"') {
            const char *v = q + klen + 2;
            const char *ve = v;
            int n;
            while (ve < end && *ve != '"') ve++;
            if (ve >= end) return 0;
            n = (int)(ve - v);
            if (n >= out_len) n = out_len - 1;
            memcpy(out, v, n);
            out[n] = 0;
            return 1;
        }
        q++;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Carga la textura .tex. Deteccion por tamaño:                        */
/*   - con header:  4 + W*H*4 bytes                                    */
/*   - sin header:  W*H*4 bytes (asume 512x512)                       */
/* Rechaza texturas no-POT (GU las necesita POT).                      */
/* ------------------------------------------------------------------ */
static int na_load_tex(NoteAtlas *a, const char *path) {
    FILE *f;
    unsigned short w, h;
    size_t total, file_size, rd;

    f = fopen(path, "rb");
    if (!f) { NA_LOG("[atlas] fopen('%s') FAIL\n", path); return -1; }

    fseek(f, 0, SEEK_END);
    file_size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    NA_LOG("[atlas] tex '%s' size=%zu\n", path, file_size);

    if (file_size >= 4 && (file_size - 4) % 4 == 0) {
        unsigned short tw, th;
        size_t expected;
        if (fread(&tw, 2, 1, f) != 1 || fread(&th, 2, 1, f) != 1) {
            fclose(f); return -2;
        }
        expected = (size_t)tw * th * 4;
        if (tw > 0 && tw <= 512 && th > 0 && th <= 512 &&
            na_is_pot(tw) && na_is_pot(th) &&
            (file_size - 4) == expected) {
            w = tw; h = th; total = expected;
            NA_LOG("[atlas] header OK w=%d h=%d\n", w, h);
        } else {
            /* no coincide: volver y asumir 512x512 sin header */
            rewind(f);
            w = 512; h = 512; total = (size_t)w * h * 4;
            if (file_size < total) {
                NA_LOG("[atlas] tex size mismatch (file=%zu, esperado=%zu)\n",
                       file_size, total);
                fclose(f); return -3;
            }
            NA_LOG("[atlas] asumiendo 512x512 sin header\n");
        }
    } else if (file_size == (size_t)512 * 512 * 4) {
        w = 512; h = 512; total = file_size;
        NA_LOG("[atlas] 512x512 sin header\n");
    } else {
        NA_LOG("[atlas] tex size invalido: %zu\n", file_size);
        fclose(f); return -3;
    }

    if (!na_is_pot(w) || !na_is_pot(h)) {
        NA_LOG("[atlas] tex no-POT: %dx%d\n", w, h);
        fclose(f); return -6;
    }

    a->tex = (unsigned int *)memalign(16, total);
    if (!a->tex) {
        NA_LOG("[atlas] memalign(%zu) FAIL\n", total);
        fclose(f); return -4;
    }
    rd = fread(a->tex, 1, total, f);
    if (rd != total) {
        NA_LOG("[atlas] fread leyo %zu de %zu\n", rd, total);
        free(a->tex); a->tex = NULL; fclose(f); return -5;
    }
    fclose(f);
    sceKernelDcacheWritebackInvalidateRange(a->tex, total);
    a->tex_w = w;
    a->tex_h = h;
    NA_LOG("[atlas] tex OK %dx%d (%.2f MB)\n", w, h, total / 1048576.0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Parsea el XML de Sparrow.                                           */
/* ------------------------------------------------------------------ */
static int na_load_xml(NoteAtlas *a, const char *path) {
    FILE *f;
    char *buf;
    long sz;
    const char *p, *end;
    int n = 0;

    f = fopen(path, "rb");
    if (!f) { NA_LOG("[atlas] fopen('%s') FAIL\n", path); return -1; }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return -2; }
    sz = (long)fread(buf, 1, sz, f);
    fclose(f);
    buf[sz] = 0;
    NA_LOG("[atlas] xml '%s' size=%ld\n", path, sz);

    p = buf;
    end = buf + sz;
    while (p < end && n < ATLAS_MAX_FRAMES) {
        const char *t, *te;
        AFrame *fr;
        char name[ATLAS_NAME_LEN], xs[32], ys[32], ws[32], hs[32];
        char fxs[32] = {0}, fys[32] = {0}, fws[32] = {0}, fhs[32] = {0};
        float x, y, w, h, fx, fy, fw, fh;

        t = p;
        while (t < end) {
            if (*t == '<' && t + 11 < end && strncmp(t, "<SubTexture", 11) == 0) break;
            t++;
        }
        if (t >= end) break;
        te = t + 11;
        while (te < end && *te != '>') te++;
        if (te >= end) break;

        if (!na_xml_attr(t, te, "name", name, sizeof(name)) ||
            !na_xml_attr(t, te, "x", xs, sizeof(xs)) ||
            !na_xml_attr(t, te, "y", ys, sizeof(ys)) ||
            !na_xml_attr(t, te, "width", ws, sizeof(ws)) ||
            !na_xml_attr(t, te, "height", hs, sizeof(hs))) {
            p = te + 1;
            continue;
        }
        na_xml_attr(t, te, "frameX", fxs, sizeof(fxs));
        na_xml_attr(t, te, "frameY", fys, sizeof(fys));
        na_xml_attr(t, te, "frameWidth", fws, sizeof(fws));
        na_xml_attr(t, te, "frameHeight", fhs, sizeof(fhs));

        fr = &a->frames[n];
        memset(fr, 0, sizeof(*fr));
        strncpy(fr->name, name, ATLAS_NAME_LEN - 1);

        x = (float)atof(xs); y = (float)atof(ys);
        w = (float)atof(ws); h = (float)atof(hs);
        fx = fxs[0] ? (float)atof(fxs) : 0.0f;
        fy = fys[0] ? (float)atof(fys) : 0.0f;
        fw = fws[0] ? (float)atof(fws) : w;
        fh = fhs[0] ? (float)atof(fhs) : h;

        fr->u  = x;  fr->v  = y;
        fr->tw = w;  fr->th = h;
        fr->ox = -fx; fr->oy = -fy;
        fr->w  = w;  fr->h  = h;
        fr->sw = fw; fr->sh = fh;

        n++;
        p = te + 1;
    }
    free(buf);
    a->count = n;
    NA_LOG("[atlas] xml parsed: %d frames\n", n);
    return n > 0 ? 0 : -3;
}

static int na_find(const NoteAtlas *a, const char *name) {
    int i;
    for (i = 0; i < a->count; i++)
        if (strcmp(a->frames[i].name, name) == 0) return i;
    return -1;
}

static int na_anim(const NoteAtlas *a, const char *prefix,
                   short *out, int max) {
    int plen = (int)strlen(prefix);
    short idxs[ATLAS_MAX_FRAMES];
    int nums[ATLAS_MAX_FRAMES];
    int found = 0, i, j, n_out;

    for (i = 0; i < a->count; i++) {
        const char *s;
        if (strncmp(a->frames[i].name, prefix, plen) != 0) continue;
        s = a->frames[i].name + plen;
        if (*s == 0 || !isdigit((unsigned char)*s)) continue;
        idxs[found] = (short)i;
        nums[found] = atoi(s);
        found++;
    }
    for (i = 1; i < found; i++) {
        short ti = idxs[i];
        int tn = nums[i];
        j = i - 1;
        while (j >= 0 && nums[j] > tn) {
            idxs[j + 1] = idxs[j]; nums[j + 1] = nums[j]; j--;
        }
        idxs[j + 1] = ti; nums[j + 1] = tn;
    }
    n_out = found < max ? found : max;
    for (i = 0; i < n_out; i++) out[i] = idxs[i];
    for (i = n_out; i < max; i++) out[i] = -1;
    return n_out;
}

static int note_atlas_load(NoteAtlas *a, const char *tex_path, const char *xml_path) {
    static const char *dirs[4]   = { "left", "down", "up", "right" };
    static const char *dirs_u[4] = { "LEFT", "DOWN", "UP", "RIGHT" };
    static const char *colors[4] = { "purple", "blue", "green", "red" };
    int r, i;
    char name[ATLAS_NAME_LEN];

    memset(a, 0, sizeof(*a));
    NA_LOG("=== note_atlas_load ===\n");

    r = na_load_tex(a, tex_path);
    if (r != 0) { NA_LOG("na_load_tex FAIL r=%d\n", r); return -10 + r; }

    r = na_load_xml(a, xml_path);
    if (r != 0) {
        free(a->tex); a->tex = NULL;
        NA_LOG("na_load_xml FAIL r=%d\n", r);
        return -20 + r;
    }
    a->loaded = 1;

    for (i = 0; i < 4; i++) {
        snprintf(name, sizeof(name), "arrow%s0000", dirs_u[i]);
        a->A_STATIC[i] = (short)na_find(a, name);

        snprintf(name, sizeof(name), "%s0000", colors[i]);
        a->A_NOTE[i] = (short)na_find(a, name);

        snprintf(name, sizeof(name), "%s hold piece0000", colors[i]);
        a->A_HOLD[i] = (short)na_find(a, name);

        snprintf(name, sizeof(name), "%s hold end0000", colors[i]);
        a->A_END[i] = (short)na_find(a, name);
        if (a->A_END[i] < 0) {
            snprintf(name, sizeof(name), "%s end hold0000", colors[i]);
            a->A_END[i] = (short)na_find(a, name);
        }
        if (a->A_END[i] < 0 && strcmp(colors[i], "purple") == 0)
            a->A_END[i] = (short)na_find(a, "pruple end hold0000");

        snprintf(name, sizeof(name), "%s press", dirs[i]);
        a->A_PRESS_N[i]   = (short)na_anim(a, name, a->A_PRESS[i], ANIM_MAX);
        snprintf(name, sizeof(name), "%s confirm", dirs[i]);
        a->A_CONFIRM_N[i] = (short)na_anim(a, name, a->A_CONFIRM[i], ANIM_MAX);
    }

    NA_LOG("STATIC  %d/%d/%d/%d\n",
           a->A_STATIC[0], a->A_STATIC[1], a->A_STATIC[2], a->A_STATIC[3]);
    NA_LOG("NOTE    %d/%d/%d/%d\n",
           a->A_NOTE[0], a->A_NOTE[1], a->A_NOTE[2], a->A_NOTE[3]);
    NA_LOG("HOLD    %d/%d/%d/%d\n",
           a->A_HOLD[0], a->A_HOLD[1], a->A_HOLD[2], a->A_HOLD[3]);
    NA_LOG("END     %d/%d/%d/%d\n",
           a->A_END[0], a->A_END[1], a->A_END[2], a->A_END[3]);
    NA_LOG("PRES_N  %d/%d/%d/%d\n",
           a->A_PRESS_N[0], a->A_PRESS_N[1], a->A_PRESS_N[2], a->A_PRESS_N[3]);
    NA_LOG("CONF_N  %d/%d/%d/%d\n",
           a->A_CONFIRM_N[0], a->A_CONFIRM_N[1], a->A_CONFIRM_N[2], a->A_CONFIRM_N[3]);
    return 0;
}

static void note_atlas_free(NoteAtlas *a) {
    if (a->tex) { free(a->tex); a->tex = NULL; }
    a->loaded = 0;
}

#endif