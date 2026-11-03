#ifndef PAUSE_H
#define PAUSE_H
/* Menu de pausa (PauseSubState.hx de Psych 0.6.3) + texto "bold" de Alphabet.hx. */

#include <pspgu.h>
#include <pspkernel.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#ifndef DIR_ALPHABET
#define DIR_ALPHABET "assets/preload/images"
#endif

#ifndef ALPHA_ATLAS_SCALE
#define ALPHA_ATLAS_SCALE 0.64f
#endif

#ifndef PAUSE_DIFFICULTY
#define PAUSE_DIFFICULTY "NORMAL"
#endif

#ifndef PAUSE_EXIT_QUITS
#define PAUSE_EXIT_QUITS 1
#endif

#ifndef PZ_BG_FADE_DUR
#define PZ_BG_FADE_DUR  0.40f
#endif

#ifndef PZ_MAX_DARKNESS
#define PZ_MAX_DARKNESS 0.60f
#endif

#ifndef PZ_UNPAUSE_DELAY
#define PZ_UNPAUSE_DELAY 0.10f
#endif

/* ===================================================================== */
/*  Alphabet                                                              */
/* ===================================================================== */

typedef struct {
    short u, v;
    unsigned int color;
    short x, y, z;
    short pad;
} AlVtx;

static NoteAtlas al_atlas;
static int al_ok = 0;
static short al_idx[128];
static signed char al_offy[128];

static const char *al_name_for_char(int c) {
    static char letter[2];
    switch (c) {
        case '&': return "ampersand";
        case '<': return "anglebracket-left";
        case '>': return "anglebracket-right";
        case '*': return "asterisk";
        case '@': return "at";
        case '\\': return "backslash";
        case '`': return "backtick";
        case '^': return "caret";
        case ':': return "colon";
        case ',': return "comma";
        case '{': return "curlybracket-left";
        case '}': return "curlybracket-right";
        case '$': return "dollar";
        case '"': return "doublequote-alt";
        case '=': return "equal";
        case '!': return "exclamationmark-alt";
        case '-': return "hyphen";
        case '%': return "percent";
        case '.': return "period";
        case '+': return "plus";
        case '#': return "pound";
        case '?': return "questionmark-alt";
        case '(': return "roundbracket-left";
        case ')': return "roundbracket-right";
        case ';': return "semicolon";
        case '\'': return "singlequote";
        case '/': return "slash";
        case '[': return "squarebracket-left";
        case ']': return "squarebracket-right";
        case '~': return "tilde";
        case '_': return "underscore";
        case '|': return "verticalbar";
        case '0': return "zero";
        case '1': return "one";
        case '2': return "two";
        case '3': return "three";
        case '4': return "four";
        case '5': return "five";
        case '6': return "six";
        case '7': return "seven";
        case '8': return "eight";
        case '9': return "nine";
        default:
            if (c >= 'a' && c <= 'z') { letter[0] = (char)c; letter[1] = 0; return letter; }
            if (c >= 'A' && c <= 'Z') { letter[0] = (char)(c + 32); letter[1] = 0; return letter; }
            return NULL;
    }
}

static int al_known(int c) { return al_name_for_char(c) != NULL; }

static int al_find(const char *name) {
    char n[ATLAS_NAME_LEN];
    snprintf(n, sizeof(n), "character-%s0000", name);
    return na_find(&al_atlas, n);
}

static int al_frame_rotated(const AFrame *a) {
    return (fabsf(a->tw - a->sh) < 1.5f &&
            fabsf(a->th - a->sw) < 1.5f &&
            fabsf(a->tw - a->sw) > 1.0f);
}

static int al_load(void) {
    int r, c, q;
    const char *nm;
    memset(&al_atlas, 0, sizeof(al_atlas));
    r = na_load_tex(&al_atlas, DIR_ALPHABET "/alphabet.tex");
    if (r != 0) return r;
    r = na_load_xml(&al_atlas, DIR_ALPHABET "/alphabet.xml");
    if (r != 0) { free(al_atlas.tex); al_atlas.tex = NULL; return r; }
    al_atlas.loaded = 1;
    if (al_atlas.count >= ATLAS_MAX_FRAMES)
        NA_LOG("[alphabet] AVISO: XML truncado a %d frames\n", ATLAS_MAX_FRAMES);

    q = al_find("questionmark-alt");
    if (q < 0) q = al_find("questionmark");

    for (c = 0; c < 128; c++) {
        al_idx[c] = -1;
        al_offy[c] = 0;
        nm = al_name_for_char(c);
        if (nm == NULL) continue;
        al_idx[c] = (short)al_find(nm);
        if (al_idx[c] < 0) al_idx[c] = (short)q;
    }

    al_offy['.']  = -6;
    al_offy[',']  = -4;
    al_offy['-']  = -4;
    al_offy[':']  = -2;
    al_offy[';']  = -2;
    al_offy['!']  =  3;
    al_offy['?']  =  3;
    al_offy['\''] =  4;

    al_ok = (q >= 0);
    NA_LOG("[alphabet] ok=%d frames=%d q=%d\n", al_ok, al_atlas.count, q);
    return al_ok ? 0 : -50;
}

static void al_begin(void) {
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, al_atlas.tex_w, al_atlas.tex_h, al_atlas.tex_w, al_atlas.tex);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
}
static void al_end(void) { sceGuDisable(GU_TEXTURE_2D); }

static void al_quad(int fi, int x, int y, int w, int h, unsigned int col) {
    const AFrame *a = &al_atlas.frames[fi];
    AlVtx *v;
    int k;
    if (w <= 0 || h <= 0) return;

    if (!al_frame_rotated(a)) {
        v = (AlVtx *)sceGuGetMemory(2 * sizeof(AlVtx));
        v[0].u = (short)(a->u + 0.5f);
        v[0].v = (short)(a->v + 0.5f);
        v[1].u = (short)(a->u + a->tw + 0.5f);
        v[1].v = (short)(a->v + a->th + 0.5f);
        v[0].color = v[1].color = col;
        v[0].x = (short)x;       v[0].y = (short)y;       v[0].z = v[1].z = 0;
        v[1].x = (short)(x + w); v[1].y = (short)(y + h);
        v[0].pad = v[1].pad = 0;
        sceGuDrawArray(GU_SPRITES,
            GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
            2, 0, v);
    } else {
        short u0 = (short)(a->u + 0.5f);
        short v0 = (short)(a->v + 0.5f);
        short u1 = (short)(a->u + a->tw + 0.5f);
        short v1 = (short)(a->v + a->th + 0.5f);
        v = (AlVtx *)sceGuGetMemory(4 * sizeof(AlVtx));
        v[0].u = u1; v[0].v = v0;  v[0].x = (short)x;       v[0].y = (short)y;
        v[1].u = u1; v[1].v = v1;  v[1].x = (short)(x + w); v[1].y = (short)y;
        v[2].u = u0; v[2].v = v0;  v[2].x = (short)x;       v[2].y = (short)(y + h);
        v[3].u = u0; v[3].v = v1;  v[3].x = (short)(x + w); v[3].y = (short)(y + h);
        for (k = 0; k < 4; k++) { v[k].color = col; v[k].z = 0; v[k].pad = 0; }
        sceGuDrawArray(GU_TRIANGLE_STRIP,
            GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
            4, 0, v);
    }
}

static void al_draw_bold(float gx, float gy, const char *s, unsigned int alpha) {
    float xPos = 0.0f, gs = 1.0f / ALPHA_ATLAS_SCALE;
    int rows = 0, consecutive = 0;
    unsigned int col = ((alpha & 0xff) << 24) | 0x00ffffff;
    for (; *s; s++) {
        int c = (unsigned char)*s;
        if (c == '\n') { xPos = 0.0f; rows++; continue; }
        c = tolower(c);
        {
            int spaceChar = (c == ' ' || c == '_');
            if (spaceChar) consecutive++;
            if (!spaceChar && c >= 0 && c < 128 && al_idx[c] >= 0) {
                const AFrame *f = &al_atlas.frames[al_idx[c]];
                float lx, ly;
                if (consecutive > 0) xPos += 28.0f * consecutive;
                consecutive = 0;
                lx = xPos;
                ly = rows * 85.0f - (float)al_offy[c];
                al_quad(al_idx[c],
                        (int)((gx + lx + f->ox * gs) * K + 0.5f),
                        (int)((gy + ly + f->oy * gs) * K + 0.5f),
                        (int)(f->sw * gs * K + 0.5f),
                        (int)(f->sh * gs * K + 0.5f), col);
                xPos += f->sw * gs;
            }
        }
    }
}

/* ===================================================================== */
/*  Menu de pausa                                                         */
/* ===================================================================== */

typedef enum { PZ_NONE = 0, PZ_RESUME, PZ_RESTART, PZ_EXIT } PzAction;

typedef struct {
    int active;
    int sel, count;
    int kind[4];
    const char *label[4];
    float ix[4], iy[4];
    float t;
    float cant_unpause;
    char song[48], diff[16], blue[32];
} Pause;

static Pause PZ;
static unsigned long long pz_last_us = 0;

static float pz_ease(float t) {
    float u;
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    if (t <= 0.5f) return t * t * t * t * 8.0f;
    u = t * 2.0f - 2.0f;
    return (1.0f - u * u * u * u) * 0.5f + 0.5f;
}

static void pause_open(const char *song, const char *diff, int deaths) {
    int i;
    memset(&PZ, 0, sizeof(PZ));
    PZ.active = 1;
    PZ.t = 0.0f;
    PZ.cant_unpause = PZ_UNPAUSE_DELAY;
    PZ.sel = 0;
    PZ.label[PZ.count] = "Resume";          PZ.kind[PZ.count++] = PZ_RESUME;
    PZ.label[PZ.count] = "Restart Song";    PZ.kind[PZ.count++] = PZ_RESTART;
#if PAUSE_EXIT_QUITS
    PZ.label[PZ.count] = "Exit to menu";    PZ.kind[PZ.count++] = PZ_EXIT;
#endif
    for (i = 0; i < PZ.count; i++) {
        float ty = (float)(i - PZ.sel);
        PZ.ix[i] = ty * 20.0f + 90.0f;
        PZ.iy[i] = ty * 1.3f * 120.0f + 320.0f;
    }
    strncpy(PZ.song, (song && song[0]) ? song : "Song", sizeof(PZ.song) - 1);
    strncpy(PZ.diff, diff ? diff : "", sizeof(PZ.diff) - 1);
    sprintf(PZ.blue, "Blueballed: %d", deaths);
    pz_last_us = sceKernelGetSystemTimeWide();
}

static PzAction pause_update(int up, int down, int accept, int back) {
    unsigned long long now = sceKernelGetSystemTimeWide();
    float dt = (float)(now - pz_last_us) / 1000000.0f, lv;
    int i;
    pz_last_us = now;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;

    PZ.t += dt;
    if (PZ.cant_unpause > 0.0f) PZ.cant_unpause -= dt;

    audio_pause_tick(dt);

    if (up)   PZ.sel = (PZ.sel + PZ.count - 1) % PZ.count;
    if (down) PZ.sel = (PZ.sel + 1) % PZ.count;

    lv = dt * 9.6f;
    if (lv > 1.0f) lv = 1.0f;
    for (i = 0; i < PZ.count; i++) {
        float ty = (float)(i - PZ.sel);
        float tx_ = ty * 20.0f + 90.0f;
        float ty_ = ty * 1.3f * 120.0f + 320.0f;
        PZ.ix[i] += (tx_ - PZ.ix[i]) * lv;
        PZ.iy[i] += (ty_ - PZ.iy[i]) * lv;
    }

    if (PZ.cant_unpause > 0.0f) return PZ_NONE;
    if (back) return PZ_RESUME;
    if (accept) return (PzAction)PZ.kind[PZ.sel];
    return PZ_NONE;
}

static void pause_info_line(const char *s, float y0, float y1, float delay) {
    float t = PZ.t - delay;
    float p = pz_ease(t / 0.4f);
    int a = (int)(p * 255.0f);
    int y = (int)((y0 + (y1 - y0) * p) * K + 0.5f);
    if (a <= 0) return;
    text_fixed(SCR_W - text_w(s, 1) - (int)(20.0f * K + 0.5f), y, 1, COL(a, 0xffffff), s);
}

static void pause_draw(void) {
    float saved = g_draw_zoom;
    float bg_p;
    int i;

    g_draw_zoom = 1.0f;

    bg_p = pz_ease(PZ.t / PZ_BG_FADE_DUR);
    rect(0, 0, SCR_W, SCR_H, COL((int)(bg_p * PZ_MAX_DARKNESS * 255.0f), 0x000000));

    pause_info_line(PZ.song, 15.0f, 20.0f, 0.30f);
    pause_info_line(PZ.diff, 47.0f, 52.0f, 0.50f);
    pause_info_line(PZ.blue, 79.0f, 84.0f, 0.70f);

    if (g.practiceMode) {
        int a = (int)(bg_p * 255.0f);
        text_fixed(SCR_W - text_w("PRACTICE MODE", 1) - (int)(20.0f * K + 0.5f),
                   (int)((15.0f + 101.0f) * K + 0.5f), 1, COL(a, 0xffffff), "PRACTICE MODE");
    }

    if (al_ok) {
        al_begin();
        for (i = 0; i < PZ.count; i++) {
            unsigned int a = (i == PZ.sel) ? 255u : 153u;
            float sy = PZ.iy[i] * K;
            if (sy > SCR_H || sy < -60.0f) continue;
            al_draw_bold(PZ.ix[i], PZ.iy[i], PZ.label[i], a);
        }
        al_end();
    } else {
        for (i = 0; i < PZ.count; i++) {
            unsigned int a = (i == PZ.sel) ? 255u : 153u;
            text_fixed((int)(PZ.ix[i] * K), (int)(PZ.iy[i] * K), 4, COL(a, 0xffffff), PZ.label[i]);
        }
    }

    g_draw_zoom = saved;
}

#endif