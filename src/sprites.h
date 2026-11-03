#ifndef SPRITES_H
#define SPRITES_H
/* Dibujo de flechas, notas y sustains usando NoteAtlas cargado del XML.
   Si el atlas falla, spr_ok = 0 y main.c dibuja rectangulos. */
#include <pspgu.h>
#include <pspkernel.h>
#include <stdio.h>
#include <string.h>
#include "note_atlas.h"

#ifndef SCR_W
#define SCR_W 480
#endif
#ifndef SCR_H
#define SCR_H 272
#endif

/* Zoom global aplicado por main.c (camera boom). 1.0 = sin zoom. */
extern float g_draw_zoom;

#ifndef SPR_FILTER
#define SPR_FILTER GU_LINEAR
#endif

typedef struct {
    short u, v;
    unsigned int color;
    short x, y, z;
    short pad;
} SVtx;

#if defined(__GNUC__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(SVtx) == 16, "SVtx debe medir 16 bytes");
#endif

static NoteAtlas spr_atlas;
static int spr_ok = 0;
static float spr_sc = 0.56f;
static int spr_state[2][4];
static float spr_time[2][4];

static int spr_load(const char *tex_path, const char *xml_path, int noteW) {
    int r, s, l;
    r = note_atlas_load(&spr_atlas, tex_path, xml_path);
    if (r != 0) return r;
    if (spr_atlas.A_STATIC[2] < 0) return -100;
    for (l = 0; l < 4; l++)
        if (spr_atlas.A_STATIC[l] < 0 || spr_atlas.A_NOTE[l] < 0) return -101;
    spr_sc = (float)noteW / spr_atlas.frames[spr_atlas.A_STATIC[2]].sw;
    for (s = 0; s < 2; s++)
        for (l = 0; l < 4; l++) { spr_state[s][l] = 0; spr_time[s][l] = 0; }
    spr_ok = 1;
    return 0;
}

static void spr_begin(void) {
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, spr_atlas.tex_w, spr_atlas.tex_h, spr_atlas.tex_w, spr_atlas.tex);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(SPR_FILTER, SPR_FILTER);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
}
static void spr_end(void) { sceGuDisable(GU_TEXTURE_2D); }

/* Aplica g_draw_zoom alrededor del centro de pantalla. */
static void spr_quad(int fi, int x, int y, int w, int h, unsigned int col) {
    const AFrame *a = &spr_atlas.frames[fi];
    SVtx *v;
    if (w <= 0 || h <= 0) return;

    if (g_draw_zoom != 1.0f) {
        float cx = x + w * 0.5f;
        float cy = y + h * 0.5f;
        float scx = SCR_W * 0.5f + (cx - SCR_W * 0.5f) * g_draw_zoom;
        float scy = SCR_H * 0.5f + (cy - SCR_H * 0.5f) * g_draw_zoom;
        w = (int)(w * g_draw_zoom + 0.5f);
        h = (int)(h * g_draw_zoom + 0.5f);
        x = (int)(scx - w * 0.5f + 0.5f);
        y = (int)(scy - h * 0.5f + 0.5f);
        if (w <= 0 || h <= 0) return;
    }

    v = (SVtx *)sceGuGetMemory(2 * sizeof(SVtx));
    v[0].u = (short)(a->u + 0.5f);          v[0].v = (short)(a->v + 0.5f);
    v[1].u = (short)(a->u + a->tw + 0.5f);  v[1].v = (short)(a->v + a->th + 0.5f);
    v[0].color = v[1].color = col;
    v[0].x = (short)x;       v[0].y = (short)y;       v[0].z = v[1].z = 0;
    v[1].x = (short)(x + w); v[1].y = (short)(y + h);
    v[0].pad = v[1].pad = 0;
    sceGuDrawArray(GU_SPRITES,
        GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
        2, 0, v);
}

static void spr_frame(int fi, float cx, float cy, unsigned int col) {
    const AFrame *a = &spr_atlas.frames[fi];
    float x0 = cx - a->sw * spr_sc * 0.5f + a->ox * spr_sc;
    float y0 = cy - a->sh * spr_sc * 0.5f + a->oy * spr_sc;
    spr_quad(fi, (int)(x0 + 0.5f), (int)(y0 + 0.5f),
             (int)(a->w * spr_sc + 0.5f), (int)(a->h * spr_sc + 0.5f), col);
}

static void spr_strum(int side, int lane, int state, float dtMs,
                      int x, int y, int cell, unsigned int alpha) {
    unsigned int col = ((alpha & 0xff) << 24) | 0x00ffffff;
    float cx = x + cell * 0.5f, cy = y + cell * 0.5f;
    int fi, n;
    if (state != spr_state[side][lane]) {
        spr_state[side][lane] = state;
        spr_time[side][lane] = 0;
    } else spr_time[side][lane] += dtMs;
    if (state == 0 ||
        (state == 1 && spr_atlas.A_PRESS_N[lane] == 0) ||
        (state == 2 && spr_atlas.A_CONFIRM_N[lane] == 0)) {
        fi = spr_atlas.A_STATIC[lane];
    } else {
        n = (int)(spr_time[side][lane] * 0.024f);
        if (state == 1) {
            if (n >= spr_atlas.A_PRESS_N[lane]) n = spr_atlas.A_PRESS_N[lane] - 1;
            fi = spr_atlas.A_PRESS[lane][n];
        } else {
            if (n >= spr_atlas.A_CONFIRM_N[lane]) n = spr_atlas.A_CONFIRM_N[lane] - 1;
            fi = spr_atlas.A_CONFIRM[lane][n];
        }
    }
    if (fi >= 0) spr_frame(fi, cx, cy, col);
}

static void spr_note(int lane, int x, int y, int cell, unsigned int col) {
    if (spr_atlas.A_NOTE[lane] >= 0)
        spr_frame(spr_atlas.A_NOTE[lane], x + cell * 0.5f, y + cell * 0.5f, col);
}

static void spr_hold(int lane, int isEnd, int x, int top, int h, int cell,
                     unsigned int col) {
    int fi = isEnd ? spr_atlas.A_END[lane] : spr_atlas.A_HOLD[lane];
    int w;
    if (fi < 0) return;
    w = (int)(spr_atlas.frames[fi].w * spr_sc + 0.5f);
    if (!isEnd) h += 2;
    spr_quad(fi, x + (cell - w) / 2, top, w, h, col);
}
#endif