#ifndef IMAGE_TEX_H
#define IMAGE_TEX_H
#include <pspgu.h>
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#ifndef SCR_W
#define SCR_W 480
#endif
#ifndef SCR_H
#define SCR_H 272
#endif

extern float g_draw_zoom;

typedef struct {
    short u, v;
    unsigned int color;
    short x, y, z;
    short pad;
} ImgVtx;

typedef struct {
    unsigned int *tex;
    int tex_w, tex_h;
    int img_w, img_h;
    int loaded;
} ImgTex;

static int img_next_pot(int n) {
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

static int img_load(ImgTex *t, const char *path) {
    FILE *f;
    unsigned short iw, ih;
    size_t total;
    int tw, th;

    memset(t, 0, sizeof(*t));
    f = fopen(path, "rb");
    if (!f) return -1;
    if (fread(&iw, 2, 1, f) != 1 || fread(&ih, 2, 1, f) != 1) {
        fclose(f); return -2;
    }
    tw = img_next_pot(iw);
    th = img_next_pot(ih);
    if (tw > 512 || th > 512 || iw == 0 || ih == 0) { fclose(f); return -3; }

    total = (size_t)tw * th * 4;
    t->tex = (unsigned int *)memalign(16, total);
    if (!t->tex) { fclose(f); return -4; }
    if (fread(t->tex, 1, total, f) != total) {
        free(t->tex); t->tex = NULL; fclose(f); return -5;
    }
    fclose(f);
    sceKernelDcacheWritebackInvalidateRange(t->tex, total);
    t->tex_w = tw; t->tex_h = th;
    t->img_w = iw; t->img_h = ih;
    t->loaded = 1;
    return 0;
}

static void img_free(ImgTex *t) {
    if (t->tex) { free(t->tex); t->tex = NULL; }
    t->loaded = 0;
}

static void img_begin(void) {
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
}

static void img_end(void) {
    sceGuDisable(GU_TEXTURE_2D);
}

static void img_draw(ImgTex *t, int x, int y, int w, int h, unsigned int col) {
    ImgVtx *v;
    if (!t || !t->loaded) return;

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

    sceGuTexImage(0, t->tex_w, t->tex_h, t->tex_w, t->tex);
    v = (ImgVtx *)sceGuGetMemory(2 * sizeof(ImgVtx));
    v[0].u = 0;               v[0].v = 0;
    v[1].u = (short)t->img_w; v[1].v = (short)t->img_h;
    v[0].color = v[1].color = col;
    v[0].x = (short)x;       v[0].y = (short)y;       v[0].z = 0;
    v[1].x = (short)(x + w); v[1].y = (short)(y + h); v[1].z = 0;
    v[0].pad = v[1].pad = 0;
    sceGuDrawArray(GU_SPRITES,
        GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
        2, 0, v);
}

static void img_draw_centered(ImgTex *t, int scr_w, int scr_h, unsigned int col) {
    if (!t || !t->loaded) return;
    img_draw(t, (scr_w - t->img_w) / 2, (scr_h - t->img_h) / 2,
             t->img_w, t->img_h, col);
}

static void img_draw_at_center(ImgTex *t, int cx, int cy, unsigned int col) {
    if (!t || !t->loaded) return;
    img_draw(t, cx - t->img_w / 2, cy - t->img_h / 2,
             t->img_w, t->img_h, col);
}

#endif