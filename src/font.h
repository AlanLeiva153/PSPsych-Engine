#ifndef FONT_H
#define FONT_H
/* Renderizado de texto con atlas pre-renderizado (font_render.py). */

#include <pspgu.h>
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#define FONT_COLOR(r, g, b, a) ((((unsigned int)(a)) << 24) | (((unsigned int)(r)) << 16) | (((unsigned int)(g)) << 8) | ((unsigned int)(b)))

typedef struct {
    short u, v;
    unsigned int color;
    short x, y, z;
    short pad;
} FontVtx;

typedef struct {
    short u, v, w, h;
    short ox, oy;
    short advance;
} Glyph;

typedef struct {
    unsigned int *tex;
    int tex_w, tex_h;
    int ascent, descent, line_height;
    const Glyph *glyphs;
    int loaded;
} Font;

static int font_load(Font *f, const char *tex_path,
                     const Glyph *glyphs, int ascent, int descent, int line_height) {
    FILE *fp;
    unsigned short w, h;
    size_t total;
    memset(f, 0, sizeof(*f));
    fp = fopen(tex_path, "rb");
    if (!fp) return -1;
    if (fread(&w, 2, 1, fp) != 1 || fread(&h, 2, 1, fp) != 1) { fclose(fp); return -2; }
    total = (size_t)w * h * 4;
    f->tex = (unsigned int *)memalign(16, total);
    if (!f->tex) { fclose(fp); return -3; }
    if (fread(f->tex, 1, total, fp) != total) {
        free(f->tex); f->tex = NULL; fclose(fp); return -4;
    }
    fclose(fp);
    sceKernelDcacheWritebackInvalidateRange(f->tex, total);
    f->tex_w = w; f->tex_h = h;
    f->glyphs = glyphs;
    f->ascent = ascent;
    f->descent = descent;
    f->line_height = line_height;
    f->loaded = 1;
    return 0;
}

static void __attribute__((unused)) font_free(Font *f) {
    if (f->tex) { free(f->tex); f->tex = NULL; }
    f->loaded = 0;
}

static void font_begin(void) {
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
}
static void font_end(void) {
    sceGuDisable(GU_TEXTURE_2D);
}

/* Dibuja texto a escala 1.0. Devuelve ancho total. */
static int font_draw(Font *f, int x, int y, unsigned int col, const char *s) {
    int pen = x, top = y, max_w = 0;
    if (!f || !f->loaded || !s) return 0;
    sceGuTexImage(0, f->tex_w, f->tex_h, f->tex_w, f->tex);
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c == '\n') {
            if (pen - x > max_w) max_w = pen - x;
            pen = x;
            top += f->line_height;
            continue;
        }
        if (c < 32 || c > 126) continue;
        {
            const Glyph *g = &f->glyphs[c - 32];
            if (g->w > 0 && g->h > 0) {
                FontVtx *v = (FontVtx *)sceGuGetMemory(2 * sizeof(FontVtx));
                float gx = (float)(pen + g->ox);
                float gy = (float)(top + g->oy);
                v[0].u = g->u;
                v[0].v = g->v;
                v[1].u = (short)(g->u + g->w);
                v[1].v = (short)(g->v + g->h);
                v[0].color = v[1].color = col;
                v[0].x = (short)gx;             v[0].y = (short)gy;             v[0].z = 0;
                v[1].x = (short)(gx + g->w);    v[1].y = (short)(gy + g->h);   v[1].z = 0;
                v[0].pad = v[1].pad = 0;
                sceGuDrawArray(GU_SPRITES,
                    GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
                    2, 0, v);
            }
            pen += g->advance;
        }
    }
    if (pen - x > max_w) max_w = pen - x;
    return max_w;
}

/* Dibuja texto a escala `scale`. El avance entre glifos se mantiene en
   float para evitar jitter: solo se redondea al dibujar cada quad. */
static int font_draw_scaled(Font *f, int x, int y, float scale, unsigned int col, const char *s) {
    float pen = (float)x;
    float top = (float)y;
    float start_x = pen;
    float max_w = 0.0f;
    if (!f || !f->loaded || !s) return 0;
    if (scale <= 0.0f) return 0;
    sceGuTexImage(0, f->tex_w, f->tex_h, f->tex_w, f->tex);
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c == '\n') {
            if (pen - start_x > max_w) max_w = pen - start_x;
            pen = start_x;
            top += (float)f->line_height * scale;
            continue;
        }
        if (c < 32 || c > 126) continue;
        {
            const Glyph *g = &f->glyphs[c - 32];
            if (g->w > 0 && g->h > 0) {
                FontVtx *v = (FontVtx *)sceGuGetMemory(2 * sizeof(FontVtx));
                float gx = pen + (float)g->ox * scale;
                float gy = top + (float)g->oy * scale;
                float gw = (float)g->w * scale;
                float gh = (float)g->h * scale;
                v[0].u = g->u;
                v[0].v = g->v;
                v[1].u = (short)(g->u + g->w);
                v[1].v = (short)(g->v + g->h);
                v[0].color = v[1].color = col;
                v[0].x = (short)(gx + 0.5f);          v[0].y = (short)(gy + 0.5f);          v[0].z = 0;
                v[1].x = (short)(gx + gw + 0.5f);     v[1].y = (short)(gy + gh + 0.5f);     v[1].z = 0;
                v[0].pad = v[1].pad = 0;
                sceGuDrawArray(GU_SPRITES,
                    GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
                    2, 0, v);
            }
            pen += (float)g->advance * scale;
        }
    }
    if (pen - start_x > max_w) max_w = pen - start_x;
    return (int)(max_w + 0.5f);
}

static int font_measure(Font *f, const char *s) {
    int w = 0, max_w = 0;
    if (!f || !s) return 0;
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c == '\n') { if (w > max_w) max_w = w; w = 0; continue; }
        if (c < 32 || c > 126) continue;
        w += f->glyphs[c - 32].advance;
    }
    if (w > max_w) max_w = w;
    return max_w;
}

#endif