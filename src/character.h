#ifndef CHARACTER_H
#define CHARACTER_H
/* Runtime + parser JSON para personajes Texture Atlas (Adobe Animate). */

#include <pspgu.h>
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <stdint.h>

#define JSMN_STATIC
#include "jsmn.h"

#define CHAR_DEBUG 1

#ifndef CHAR_OFFSET_REL_SIGN
#define CHAR_OFFSET_REL_SIGN (-1.0f)
#endif

static char g_char_debug_tex_path[512] = {0};
static char g_char_debug_folder[512] = {0};
static char g_char_debug_sprite_json[512] = {0};
static char g_char_debug_anim_json[512] = {0};
static int  g_char_debug_sym_count = 0;
static int  g_char_debug_layer_count = 0;
static int  g_char_debug_frame_count_json = 0;
static int  g_char_debug_elem_count = 0;
static int  g_char_debug_anim_json_count = 0;
static int  g_char_debug_anim_resolved = 0;
static char g_char_debug_sym0[64] = {0};
static char g_char_debug_anim0[64] = {0};
static int  g_char_debug_anim0_len = 0;
static int  g_char_debug_sym0_len = 0;
static char g_char_debug_first_miss[64] = {0};
static int  g_char_debug_first_match_type = 0;
static int  g_char_debug_first_sym_layer_count = 0;
static int  g_char_debug_first_sym_frame_count = 0;
static int  g_char_debug_first_sym_total_time = 0;

typedef struct {
    short atlas_idx;
    float a, b, c, d, tx, ty;
} CharSprite;

typedef struct {
    int   sprite_start;
    short sprite_count;
    short duration;
} CharFrame;

typedef struct {
    char *name;
    short fps;
    short loop;
    short offset_x;
    short offset_y;
    int frame_start;
    int frame_count;
} CharAnim;

typedef struct {
    char name[32];
    short x, y, w, h;
    short sw, sh;
} CharAtlasEntry;

typedef struct {
    short u, v;
    unsigned int color;
    short x, y, z;
    short pad;
} CharVtx;

typedef char char_vtx_size_check[(sizeof(CharVtx) == 16) ? 1 : -1];

typedef struct {
    int symbol_idx;
    int sprite_idx;
    int ff;
    int lp;
    char sym_name[64];
    float m[6];
} JsonElement;

typedef struct {
    int start;
    int duration;
    int elem_start;
    int elem_count;
} JsonFrame;

typedef struct {
    int frame_start;
    int frame_count;
} JsonLayer;

typedef struct {
    char name[64];
    int layer_start;
    int layer_count;
} JsonSymbol;

typedef struct {
    unsigned int *tex;
    int tex_w, tex_h;
    int loaded;

    float tex_scale_x, tex_scale_y;

    CharAtlasEntry *atlas;
    int atlas_count;

    CharSprite *sprites;
    int sprite_count;
    CharFrame *frames;
    int frame_count;
    CharAnim *anims;
    int anim_count;

    int current_anim;
    int current_frame_in_anim;
    float frame_timer_ms;
    int playing;

    int flip_x;
    int json_flip_x;
    float pos_x, pos_y;
    int shared;
} Character;

/* ================================================================== */
/* jsmn utils                                                         */
/* ================================================================== */

static int ja_skip(jsmntok_t *t, int i) {
    int n = t[i].size;
    i++;
    while (n-- > 0) i = ja_skip(t, i);
    return i;
}

static int ja_eq(const char *js, jsmntok_t *t, const char *s) {
    int len = t->end - t->start;
    return (int)strlen(s) == len && strncmp(js + t->start, s, len) == 0;
}

static int ja_get(const char *js, jsmntok_t *t, int obj, const char *key) {
    int i = obj + 1, n;
    if (obj < 0 || t[obj].type != JSMN_OBJECT) return -1;
    for (n = t[obj].size; n > 0; n--) {
        if (t[i].type == JSMN_STRING && ja_eq(js, &t[i], key)) return i + 1;
        i = ja_skip(t, i + 1);
    }
    return -1;
}

static double ja_num(const char *js, jsmntok_t *t) {
    char buf[32];
    int len = t->end - t->start;
    if (len > 31) len = 31;
    memcpy(buf, js + t->start, len);
    buf[len] = 0;
    return atof(buf);
}

static int ja_get_str(const char *js, jsmntok_t *t, int tok,
                      char *out, int out_size) {
    int len = t[tok].end - t[tok].start;
    if (len >= out_size) len = out_size - 1;
    memcpy(out, js + t[tok].start, len);
    out[len] = 0;
    return len;
}

#ifndef CHAR_MATRIX_FLASH
#define CHAR_MATRIX_FLASH 1
#endif

static void ja_set_matrix(float *m, float a, float b, float c, float d, float tx, float ty) {
#if CHAR_MATRIX_FLASH
    m[0] = a; m[1] = c; m[2] = b; m[3] = d;
#else
    m[0] = a; m[1] = b; m[2] = c; m[3] = d;
#endif
    m[4] = tx; m[5] = ty;
}

static void ja_read_matrix(const char *js, jsmntok_t *t, int obj, float *m) {
    int tok = ja_get(js, t, obj, "MX");
    int i, n;
    float v[16];
    ja_set_matrix(m, 1, 0, 0, 1, 0, 0);
    if (tok >= 0 && t[tok].type == JSMN_ARRAY && t[tok].size >= 6) {
        for (i = 0; i < 6; i++) v[i] = (float)ja_num(js, &t[tok + 1 + i]);
        ja_set_matrix(m, v[0], v[1], v[2], v[3], v[4], v[5]);
        return;
    }
    tok = ja_get(js, t, obj, "M3D");
    if (tok >= 0 && t[tok].type == JSMN_ARRAY && t[tok].size >= 14) {
        n = t[tok].size < 16 ? t[tok].size : 16;
        for (i = 0; i < n; i++) v[i] = (float)ja_num(js, &t[tok + 1 + i]);
        ja_set_matrix(m, v[0], v[1], v[4], v[5], v[12], v[13]);
    }
}

static void mat_compose(float *out, const float *m1, const float *m2) {
    float a1 = m1[0], b1 = m1[1], c1 = m1[2], d1 = m1[3], tx1 = m1[4], ty1 = m1[5];
    float a2 = m2[0], b2 = m2[1], c2 = m2[2], d2 = m2[3], tx2 = m2[4], ty2 = m2[5];
    out[0] = a1 * a2 + b1 * c2;
    out[1] = a1 * b2 + b1 * d2;
    out[2] = c1 * a2 + d1 * c2;
    out[3] = c1 * b2 + d1 * d2;
    out[4] = a1 * tx2 + b1 * ty2 + tx1;
    out[5] = c1 * tx2 + d1 * ty2 + ty1;
}

/* ================================================================== */
/* String utils                                                       */
/* ================================================================== */

static void str_trim(char *s) {
    int len = (int)strlen(s);
    int start = 0, end = len;
    while (start < end && (s[start] == ' ' || s[start] == '\t' ||
                           s[start] == '\r' || s[start] == '\n'))
        start++;
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' ||
                           s[end - 1] == '\r' || s[end - 1] == '\n'))
        end--;
    if (start > 0 || end < len) {
        memmove(s, s + start, end - start);
        s[end - start] = 0;
    }
}

static int str_ci_cmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (int)ca - (int)cb;
        a++; b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* ================================================================== */
/* File utils                                                         */
/* ================================================================== */

static char *read_file(const char *path, long *out_size) {
    FILE *f = fopen(path, "rb");
    long sz;
    char *buf;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    sz = (long)fread(buf, 1, sz, f);
    fclose(f);
    buf[sz] = 0;
    if (out_size) *out_size = sz;
    return buf;
}

static int char_file_exists(const char *path) {
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0;
}

static int char_name_starts_with(const char *name, const char *prefix) {
    while (*prefix) {
        char a = *name;
        char b = *prefix;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
        name++;
        prefix++;
    }
    return 1;
}

static int char_name_ends_with(const char *name, const char *ext) {
    int nl = (int)strlen(name);
    int el = (int)strlen(ext);
    int i;
    if (nl < el) return 0;
    for (i = 0; i < el; i++) {
        char a = name[nl - el + i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
    }
    return 1;
}

static int char_find_spritemap_json(const char *folder, char *out, int out_size) {
    SceUID d;
    SceIoDirent entry;
    d = sceIoDopen(folder);
    if (d < 0) return -1;
    while (sceIoDread(d, &entry) > 0) {
        int len = (int)strlen(entry.d_name);
        if (len < 5) continue;
        if (FIO_S_ISDIR(entry.d_stat.st_mode)) continue;
        if (!char_name_ends_with(entry.d_name, ".json")) continue;
        if (char_name_starts_with(entry.d_name, "animation")) continue;
        snprintf(out, out_size, "%s/%s", folder, entry.d_name);
        sceIoDclose(d);
        return 0;
    }
    sceIoDclose(d);
    return -2;
}

static int char_find_anim_json(const char *folder, char *out, int out_size) {
    SceUID d;
    SceIoDirent entry;
    char p[512];

    snprintf(p, sizeof(p), "%s/Animation.json", folder);
    if (char_file_exists(p)) { strncpy(out, p, out_size - 1); out[out_size-1] = 0; return 0; }
    snprintf(p, sizeof(p), "%s/animation.json", folder);
    if (char_file_exists(p)) { strncpy(out, p, out_size - 1); out[out_size-1] = 0; return 0; }

    d = sceIoDopen(folder);
    if (d < 0) return -1;
    while (sceIoDread(d, &entry) > 0) {
        int len = (int)strlen(entry.d_name);
        if (len < 5) continue;
        if (FIO_S_ISDIR(entry.d_stat.st_mode)) continue;
        if (!char_name_ends_with(entry.d_name, ".json")) continue;
        if (!char_name_starts_with(entry.d_name, "animation")) continue;
        snprintf(out, out_size, "%s/%s", folder, entry.d_name);
        sceIoDclose(d);
        return 0;
    }
    sceIoDclose(d);
    return -2;
}

static int char_read_image_field(const char *json_path, char *out, int out_size) {
    long sz;
    char *js = read_file(json_path, &sz);
    jsmn_parser p;
    jsmntok_t *tk;
    int ntok, img_tok;

    if (!js) return -1;
    jsmn_init(&p);
    ntok = jsmn_parse(&p, js, sz, NULL, 0);
    if (ntok <= 0) { free(js); return -2; }
    tk = (jsmntok_t *)malloc(sizeof(jsmntok_t) * ntok);
    if (!tk) { free(js); return -2; }
    jsmn_init(&p);
    jsmn_parse(&p, js, sz, tk, ntok);

    img_tok = ja_get(js, tk, 0, "image");
    if (img_tok < 0 || tk[img_tok].type != JSMN_STRING) {
        free(js); free(tk);
        return -3;
    }
    ja_get_str(js, tk, img_tok, out, out_size);
    free(js);
    free(tk);
    return 0;
}

/* ================================================================== */
/* Ajuste de textura                                                  */
/* ================================================================== */

static int char_is_pot(int n) {
    return n > 0 && (n & (n - 1)) == 0;
}

static int char_pot_ceil(int n) {
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

static void char_scale_atlas(Character *c, float sxf, float syf) {
    int i;
    for (i = 0; i < c->atlas_count; i++) {
        CharAtlasEntry *e = &c->atlas[i];
        int x0 = (int)((float)e->x * sxf + 0.5f);
        int y0 = (int)((float)e->y * syf + 0.5f);
        int x1 = (int)((float)(e->x + e->w) * sxf + 0.5f);
        int y1 = (int)((float)(e->y + e->h) * syf + 0.5f);
        if (x1 <= x0) x1 = x0 + 1;
        if (y1 <= y0) y1 = y0 + 1;
        e->x = (short)x0; e->y = (short)y0;
        e->w = (short)(x1 - x0); e->h = (short)(y1 - y0);
    }
}

/* FIX: ahora devuelve -1 si memalign falla. char_load_json verifica esto
   y aborta la carga si la textura no es POT. */
static int char_fix_tex(Character *c) {
    const int max_size = 512;
    int src_w = c->tex_w;
    int src_h = c->tex_h;
    int dst_w, dst_h;
    unsigned int *dst;
    int x, y;
    float sxf, syf;

    if (src_w <= max_size && src_h <= max_size &&
        char_is_pot(src_w) && char_is_pot(src_h)) {
        return 0;
    }

    if (src_w > max_size) {
        dst_w = max_size;
    } else if (char_is_pot(src_w)) {
        dst_w = src_w;
    } else {
        dst_w = char_pot_ceil(src_w);
        if (dst_w > max_size) dst_w = max_size;
    }

    if (src_h > max_size) {
        dst_h = max_size;
    } else if (char_is_pot(src_h)) {
        dst_h = src_h;
    } else {
        dst_h = char_pot_ceil(src_h);
        if (dst_h > max_size) dst_h = max_size;
    }

    if (dst_w == src_w && dst_h == src_h) return 0;

    dst = (unsigned int *)memalign(16, (size_t)dst_w * dst_h * 4);
    if (!dst) {
#if CHAR_DEBUG
        printf("[char] char_fix_tex: memalign FAIL (%d bytes), src=%dx%d dst=%dx%d\n",
               dst_w * dst_h * 4, src_w, src_h, dst_w, dst_h);
#endif
        return -1;
    }
    memset(dst, 0, (size_t)dst_w * dst_h * 4);

    sxf = (dst_w < src_w) ? ((float)dst_w / (float)src_w) : 1.0f;
    syf = (dst_h < src_h) ? ((float)dst_h / (float)src_h) : 1.0f;

    for (y = 0; y < dst_h; y++) {
        int sy;
        if (dst_h == src_h) sy = y;
        else if (dst_h < src_h) sy = (y * src_h) / dst_h;
        else { if (y >= src_h) continue; sy = y; }

        {
            const unsigned int *src_row = c->tex + (size_t)sy * src_w;
            unsigned int *dst_row = dst + (size_t)y * dst_w;

            if (dst_w == src_w) {
                memcpy(dst_row, src_row, (size_t)src_w * 4);
            } else if (dst_w < src_w) {
                for (x = 0; x < dst_w; x++) {
                    int sx = (x * src_w) / dst_w;
                    dst_row[x] = src_row[sx];
                }
            } else {
                memcpy(dst_row, src_row, (size_t)src_w * 4);
            }
        }
    }

    char_scale_atlas(c, sxf, syf);

    free(c->tex);
    c->tex = dst;
    c->tex_w = dst_w;
    c->tex_h = dst_h;
    c->tex_scale_x = sxf;
    c->tex_scale_y = syf;

    sceKernelDcacheWritebackInvalidateRange(dst, (size_t)dst_w * dst_h * 4);
    return 1;
}

/* ================================================================== */
/* Parser spritemap                                                   */
/* ================================================================== */

static int parse_spritemap(Character *c, const char *path) {
    long sz;
    char *js = read_file(path, &sz);
    jsmn_parser p;
    jsmntok_t *tk;
    int ntok, root, sprites, i, count;
    int cap = 64;
    int s;

    if (!js) return -1;
    jsmn_init(&p);
    ntok = jsmn_parse(&p, js, sz, NULL, 0);
    if (ntok <= 0) { free(js); return -2; }
    tk = (jsmntok_t *)malloc(sizeof(jsmntok_t) * ntok);
    if (!tk) { free(js); return -2; }
    jsmn_init(&p);
    jsmn_parse(&p, js, sz, tk, ntok);

    root = 0;
    if (tk[root].type == JSMN_OBJECT) {
        int atlas_tok = ja_get(js, tk, root, "ATLAS");
        if (atlas_tok >= 0) {
            sprites = ja_get(js, tk, atlas_tok, "SPRITES");
            if (sprites >= 0 && tk[sprites].type == JSMN_ARRAY) {
                count = tk[sprites].size;
                if (count > cap) cap = count;
                c->atlas = (CharAtlasEntry *)malloc(sizeof(CharAtlasEntry) * cap);
                c->atlas_count = 0;
                i = sprites + 1;
                for (s = 0; s < count; s++) {
                    int spr_tok = ja_get(js, tk, i, "SPRITE");
                    if (spr_tok >= 0) {
                        CharAtlasEntry *ae = &c->atlas[c->atlas_count];
                        int name_tok = ja_get(js, tk, spr_tok, "name");
                        int x_tok = ja_get(js, tk, spr_tok, "x");
                        int y_tok = ja_get(js, tk, spr_tok, "y");
                        int w_tok = ja_get(js, tk, spr_tok, "w");
                        int h_tok = ja_get(js, tk, spr_tok, "h");
                        if (name_tok >= 0) ja_get_str(js, tk, name_tok, ae->name, sizeof(ae->name));
                        ae->x = (x_tok >= 0) ? (short)ja_num(js, &tk[x_tok]) : 0;
                        ae->y = (y_tok >= 0) ? (short)ja_num(js, &tk[y_tok]) : 0;
                        ae->w = (w_tok >= 0) ? (short)ja_num(js, &tk[w_tok]) : 0;
                        ae->h = (h_tok >= 0) ? (short)ja_num(js, &tk[h_tok]) : 0;
                        ae->sw = ae->w;
                        ae->sh = ae->h;
                        c->atlas_count++;
                    }
                    i = ja_skip(tk, i);
                }
            }
        }
    }

    free(tk);
    free(js);
    return c->atlas_count > 0 ? 0 : -3;
}

/* ================================================================== */
/* Parser Animation.json                                              */
/* ================================================================== */

static int find_symbol_by_name(JsonSymbol *syms, int n, const char *name,
                               int *out_match_type) {
    int i;
    if (out_match_type) *out_match_type = 0;

    for (i = 0; i < n; i++)
        if (strcmp(syms[i].name, name) == 0) {
            if (out_match_type) *out_match_type = 1;
            return i;
        }
    for (i = 0; i < n; i++)
        if (str_ci_cmp(syms[i].name, name) == 0) {
            if (out_match_type) *out_match_type = 2;
            return i;
        }
    {
        char a[64];
        strncpy(a, name, sizeof(a) - 1); a[sizeof(a)-1] = 0;
        str_trim(a);
        for (i = 0; i < n; i++) {
            char b[64];
            strncpy(b, syms[i].name, sizeof(b) - 1); b[sizeof(b)-1] = 0;
            str_trim(b);
            if (str_ci_cmp(a, b) == 0) {
                if (out_match_type) *out_match_type = 3;
                return i;
            }
        }
    }
    {
        char a[64], b[64];
        int ia = 0, ib, k;
        for (k = 0; name[k] && ia < 63; k++) {
            char c = name[k];
            if (c == ' ' || c == '\t') continue;
            if (c >= 'A' && c <= 'Z') c += 32;
            a[ia++] = c;
        }
        a[ia] = 0;
        for (i = 0; i < n; i++) {
            ib = 0;
            for (k = 0; syms[i].name[k] && ib < 63; k++) {
                char c = syms[i].name[k];
                if (c == ' ' || c == '\t') continue;
                if (c >= 'A' && c <= 'Z') c += 32;
                b[ib++] = c;
            }
            b[ib] = 0;
            if (strcmp(a, b) == 0) {
                if (out_match_type) *out_match_type = 4;
                return i;
            }
        }
    }
    return -1;
}

static int find_sprite_by_name(CharAtlasEntry *atlas, int n, const char *name) {
    int i;
    for (i = 0; i < n; i++)
        if (strcmp(atlas[i].name, name) == 0) return i;
    for (i = 0; i < n; i++)
        if (str_ci_cmp(atlas[i].name, name) == 0) return i;
    return -1;
}

static int parse_animation(Character *c, const char *path,
                           JsonSymbol **out_syms, int *out_sym_count,
                           JsonLayer **out_layers, int *out_layer_count,
                           JsonFrame **out_frames, int *out_frame_count,
                           JsonElement **out_elems, int *out_elem_count) {
    long sz;
    char *js = read_file(path, &sz);
    jsmn_parser p;
    jsmntok_t *tk;
    int ntok, root, sd, s_tok, count;
    JsonSymbol *syms = NULL;
    JsonLayer *layers = NULL;
    JsonFrame *frames = NULL;
    JsonElement *elems = NULL;
    int sym_count = 0, layer_count = 0, frame_count = 0, elem_count = 0;
    int sym_cap = 128, layer_cap = 512, frame_cap = 2048, elem_cap = 4096;
    int s, l, f, e;

    if (!js) return -1;
    jsmn_init(&p);
    ntok = jsmn_parse(&p, js, sz, NULL, 0);
    if (ntok <= 0) { free(js); return -2; }
    tk = (jsmntok_t *)malloc(sizeof(jsmntok_t) * ntok);
    if (!tk) { free(js); return -2; }
    jsmn_init(&p);
    jsmn_parse(&p, js, sz, tk, ntok);

    syms = (JsonSymbol *)malloc(sizeof(JsonSymbol) * sym_cap);
    layers = (JsonLayer *)malloc(sizeof(JsonLayer) * layer_cap);
    frames = (JsonFrame *)malloc(sizeof(JsonFrame) * frame_cap);
    elems = (JsonElement *)malloc(sizeof(JsonElement) * elem_cap);
    if (!syms || !layers || !frames || !elems) {
        free(js); free(tk); free(syms); free(layers); free(frames); free(elems);
        return -2;
    }

    root = 0;
    sd = ja_get(js, tk, root, "SD");
    s_tok = (sd >= 0) ? ja_get(js, tk, sd, "S") : -1;
    if (s_tok < 0 || tk[s_tok].type != JSMN_ARRAY) {
        free(js); free(tk);
        *out_syms = syms; *out_sym_count = 0;
        *out_layers = layers; *out_layer_count = 0;
        *out_frames = frames; *out_frame_count = 0;
        *out_elems = elems; *out_elem_count = 0;
        return -3;
    }

    count = tk[s_tok].size;
    {
        int si = s_tok + 1;
        for (s = 0; s < count; s++) {
            JsonSymbol *sym;
            int sn_tok, tl_tok, l_tok;
            if (sym_count >= sym_cap) {
                JsonSymbol *tmp;
                sym_cap *= 2;
                tmp = (JsonSymbol *)realloc(syms, sizeof(JsonSymbol) * sym_cap);
                if (!tmp) {
                    free(js); free(tk); free(syms); free(layers); free(frames); free(elems);
                    return -2;
                }
                syms = tmp;
            }
            sym = &syms[sym_count];
            memset(sym, 0, sizeof(*sym));
            sn_tok = ja_get(js, tk, si, "SN");
            if (sn_tok >= 0) ja_get_str(js, tk, sn_tok, sym->name, sizeof(sym->name));
            sym->layer_start = layer_count;

            tl_tok = ja_get(js, tk, si, "TL");
            l_tok = (tl_tok >= 0) ? ja_get(js, tk, tl_tok, "L") : -1;
            if (l_tok >= 0 && tk[l_tok].type == JSMN_ARRAY) {
                int lcount = tk[l_tok].size;
                int li = l_tok + 1;
                for (l = 0; l < lcount; l++) {
                    JsonLayer *lyr;
                    int fr_tok;
                    if (layer_count >= layer_cap) {
                        JsonLayer *tmp;
                        layer_cap *= 2;
                        tmp = (JsonLayer *)realloc(layers, sizeof(JsonLayer) * layer_cap);
                        if (!tmp) {
                            free(js); free(tk); free(syms); free(layers); free(frames); free(elems);
                            return -2;
                        }
                        layers = tmp;
                    }
                    lyr = &layers[layer_count];
                    memset(lyr, 0, sizeof(*lyr));
                    lyr->frame_start = frame_count;

                    fr_tok = ja_get(js, tk, li, "FR");
                    if (fr_tok < 0) fr_tok = ja_get(js, tk, li, "frames");
                    if (fr_tok >= 0 && tk[fr_tok].type == JSMN_ARRAY) {
                        int fcount = tk[fr_tok].size;
                        int fi = fr_tok + 1;
                        for (f = 0; f < fcount; f++) {
                            JsonFrame *fr;
                            int i_tok, du_tok, e_tok;
                            if (frame_count >= frame_cap) {
                                JsonFrame *tmp;
                                frame_cap *= 2;
                                tmp = (JsonFrame *)realloc(frames, sizeof(JsonFrame) * frame_cap);
                                if (!tmp) {
                                    free(js); free(tk); free(syms); free(layers); free(frames); free(elems);
                                    return -2;
                                }
                                frames = tmp;
                            }
                            fr = &frames[frame_count];
                            memset(fr, 0, sizeof(*fr));

                            i_tok = ja_get(js, tk, fi, "I");
                            if (i_tok < 0) i_tok = ja_get(js, tk, fi, "index");
                            du_tok = ja_get(js, tk, fi, "DU");
                            if (du_tok < 0) du_tok = ja_get(js, tk, fi, "duration");

                            fr->start = (i_tok >= 0) ? (int)ja_num(js, &tk[i_tok]) : 0;
                            fr->duration = (du_tok >= 0) ? (int)ja_num(js, &tk[du_tok]) : 1;
                            if (fr->duration <= 0) fr->duration = 1;
                            fr->elem_start = elem_count;

                            e_tok = ja_get(js, tk, fi, "E");
                            if (e_tok < 0) e_tok = ja_get(js, tk, fi, "elements");
                            if (e_tok >= 0 && tk[e_tok].type == JSMN_ARRAY) {
                                int ecount = tk[e_tok].size;
                                int ei = e_tok + 1;
                                for (e = 0; e < ecount; e++) {
                                    JsonElement *el;
                                    int si_tok, asi_tok;
                                    if (elem_count >= elem_cap) {
                                        JsonElement *tmp;
                                        elem_cap *= 2;
                                        tmp = (JsonElement *)realloc(elems, sizeof(JsonElement) * elem_cap);
                                        if (!tmp) {
                                            free(js); free(tk); free(syms); free(layers); free(frames); free(elems);
                                            return -2;
                                        }
                                        elems = tmp;
                                    }
                                    el = &elems[elem_count];
                                    memset(el, 0, sizeof(*el));
                                    el->symbol_idx = -1;
                                    el->sprite_idx = -1;
                                    el->ff = 0;
                                    el->lp = 0;
                                    el->m[0] = 1; el->m[1] = 0;
                                    el->m[2] = 0; el->m[3] = 1;
                                    el->m[4] = 0; el->m[5] = 0;

                                    si_tok = ja_get(js, tk, ei, "SI");
                                    if (si_tok < 0) si_tok = ja_get(js, tk, ei, "symbol");
                                    asi_tok = ja_get(js, tk, ei, "ASI");
                                    if (asi_tok < 0) asi_tok = ja_get(js, tk, ei, "atlas");

                                    if (si_tok >= 0) {
                                        int sn2_tok = ja_get(js, tk, si_tok, "SN");
                                        int ff_tok = ja_get(js, tk, si_tok, "FF");
                                        int lp_tok = ja_get(js, tk, si_tok, "LP");
                                        if (ff_tok >= 0) el->ff = (int)ja_num(js, &tk[ff_tok]);
                                        if (lp_tok >= 0 && tk[lp_tok].type == JSMN_STRING) {
                                            if (ja_eq(js, &tk[lp_tok], "PO")) el->lp = 1;
                                            else if (ja_eq(js, &tk[lp_tok], "SF")) el->lp = 2;
                                        }
                                        if (sn2_tok >= 0)
                                            ja_get_str(js, tk, sn2_tok, el->sym_name, sizeof(el->sym_name));
                                        ja_read_matrix(js, tk, si_tok, el->m);
                                        elem_count++;
                                    } else if (asi_tok >= 0) {
                                        int n_tok = ja_get(js, tk, asi_tok, "N");
                                        if (n_tok >= 0) {
                                            char nm[64];
                                            ja_get_str(js, tk, n_tok, nm, sizeof(nm));
                                            el->sprite_idx = find_sprite_by_name(c->atlas, c->atlas_count, nm);
                                        }
                                        ja_read_matrix(js, tk, asi_tok, el->m);
                                        elem_count++;
                                    }
                                    ei = ja_skip(tk, ei);
                                }
                            }
                            fr->elem_count = elem_count - fr->elem_start;
                            frame_count++;
                            fi = ja_skip(tk, fi);
                        }
                    }
                    lyr->frame_count = frame_count - lyr->frame_start;
                    layer_count++;
                    li = ja_skip(tk, li);
                }
            }
            sym->layer_count = layer_count - sym->layer_start;
            sym_count++;
            si = ja_skip(tk, si);
        }
    }

    free(tk);
    free(js);

    *out_syms = syms; *out_sym_count = sym_count;
    *out_layers = layers; *out_layer_count = layer_count;
    *out_frames = frames; *out_frame_count = frame_count;
    *out_elems = elems; *out_elem_count = elem_count;

    g_char_debug_sym_count = sym_count;
    g_char_debug_layer_count = layer_count;
    g_char_debug_frame_count_json = frame_count;
    g_char_debug_elem_count = elem_count;
    if (sym_count > 0) {
        strncpy(g_char_debug_sym0, syms[0].name, sizeof(g_char_debug_sym0) - 1);
        g_char_debug_sym0_len = (int)strlen(syms[0].name);
    }
    return 0;
}

/* ================================================================== */
/* Resolución recursiva                                               */
/* ================================================================== */

static void push_sprite(CharSprite **sprites, int *count, int *cap,
                        int atlas_idx, const float *m) {
    if (*count >= *cap) {
        int new_cap = *cap * 2;
        CharSprite *tmp;
        tmp = (CharSprite *)realloc(*sprites, sizeof(CharSprite) * new_cap);
        if (!tmp) return;
        *sprites = tmp;
        *cap = new_cap;
    }
    (*sprites)[*count].atlas_idx = (short)atlas_idx;
    (*sprites)[*count].a = m[0];
    (*sprites)[*count].b = m[1];
    (*sprites)[*count].c = m[2];
    (*sprites)[*count].d = m[3];
    (*sprites)[*count].tx = m[4];
    (*sprites)[*count].ty = m[5];
    (*count)++;
}

static JsonFrame *layer_frame_at(JsonLayer *lyr, JsonFrame *frames, int t) {
    int f;
    for (f = 0; f < lyr->frame_count; f++) {
        JsonFrame *fr = &frames[lyr->frame_start + f];
        if (fr->start <= t && t < fr->start + fr->duration) return fr;
        if (fr->start > t) break;
    }
    return NULL;
}

static int symbol_length(const JsonSymbol *sym, const JsonLayer *layers,
                         const JsonFrame *frames) {
    int l, len = 0;
    for (l = 0; l < sym->layer_count; l++) {
        const JsonLayer *lyr = &layers[sym->layer_start + l];
        if (lyr->frame_count > 0) {
            const JsonFrame *fr = &frames[lyr->frame_start + lyr->frame_count - 1];
            int end = fr->start + fr->duration;
            if (end > len) len = end;
        }
    }
    return len;
}

static void resolve_element(JsonElement *el, const float *parent_m,
                            JsonSymbol *syms, int sym_count,
                            JsonLayer *layers,
                            JsonFrame *frames,
                            JsonElement *elems,
                            CharSprite **sprites, int *count, int *cap,
                            int parent_t, int frame_start, int depth) {
    float final_m[6];
    if (depth > 8) return;

    mat_compose(final_m, parent_m, el->m);

    if (el->sprite_idx >= 0) {
        push_sprite(sprites, count, cap, el->sprite_idx, final_m);
        return;
    }

    if (el->sym_name[0]) {
        JsonSymbol *child;
        int sym_idx, len, dt, ct, l, e2;

        if (el->symbol_idx == -1) {
            el->symbol_idx = find_symbol_by_name(syms, sym_count, el->sym_name, NULL);
            if (el->symbol_idx < 0) el->symbol_idx = -2;
        }
        sym_idx = el->symbol_idx;
        if (sym_idx < 0) return;

        child = &syms[sym_idx];
        len = symbol_length(child, layers, frames);
        dt = parent_t - frame_start;
        if (dt < 0) dt = 0;

        if (el->lp == 2) ct = el->ff;
        else if (el->lp == 1) { ct = el->ff + dt; if (len > 0 && ct >= len) ct = len - 1; }
        else ct = (len > 0) ? (el->ff + dt) % len : 0;
        if (ct < 0) ct = 0;

        for (l = child->layer_count - 1; l >= 0; l--) {
            JsonLayer *lyr = &layers[child->layer_start + l];
            JsonFrame *active = layer_frame_at(lyr, frames, ct);
            if (!active) continue;
            for (e2 = 0; e2 < active->elem_count; e2++) {
                resolve_element(&elems[active->elem_start + e2], final_m,
                                syms, sym_count, layers, frames, elems,
                                sprites, count, cap, ct, active->start, depth + 1);
            }
        }
    }
}

static int sprites_equal(const CharSprite *x, const CharSprite *y, int n) {
    int i;
    for (i = 0; i < n; i++) {
        if (x[i].atlas_idx != y[i].atlas_idx ||
            x[i].a != y[i].a || x[i].b != y[i].b || x[i].c != y[i].c ||
            x[i].d != y[i].d || x[i].tx != y[i].tx || x[i].ty != y[i].ty)
            return 0;
    }
    return 1;
}

static int resolve_anim(Character *c, const char *anim_name,
                        const char *symbol_name,
                        int fps, int loop, int off_x, int off_y,
                        JsonSymbol *syms, int sym_count,
                        JsonLayer *layers, int layer_count,
                        JsonFrame *frames, int frame_count,
                        JsonElement *elems, int elem_count,
                        CharSprite **out_sprites, int *out_sprite_count,
                        CharFrame **out_frames, int *out_frame_count,
                        CharAnim *out_anim) {
    int match_type = 0;
    int sym_idx = find_symbol_by_name(syms, sym_count, symbol_name, &match_type);
    JsonSymbol *sym;
    int l, t;
    int min_time = 999999;
    int max_time = 0;
    int sprite_cap = 64, frame_cap = 32;
    int sprite_count = 0, frame_count_out = 0;
    CharSprite *sprites = NULL;
    CharFrame *frames_out = NULL;
    float identity[6];

    (void)c;
    (void)layer_count;
    (void)frame_count;
    (void)elem_count;

    if (sym_idx < 0) {
        if (g_char_debug_first_miss[0] == 0) {
            strncpy(g_char_debug_first_miss, symbol_name, sizeof(g_char_debug_first_miss) - 1);
        }
        return -1;
    }

    if (g_char_debug_first_match_type == 0) {
        g_char_debug_first_match_type = match_type;
        if (sym_idx >= 0 && sym_idx < sym_count) {
            g_char_debug_first_sym_layer_count = syms[sym_idx].layer_count;
            g_char_debug_first_sym_frame_count = 0;
            {
                int ll;
                for (ll = 0; ll < syms[sym_idx].layer_count; ll++) {
                    JsonLayer *lyr2 = &layers[syms[sym_idx].layer_start + ll];
                    g_char_debug_first_sym_frame_count += lyr2->frame_count;
                }
            }
        }
    }

    sym = &syms[sym_idx];

    sprites = (CharSprite *)malloc(sizeof(CharSprite) * sprite_cap);
    frames_out = (CharFrame *)malloc(sizeof(CharFrame) * frame_cap);
    if (!sprites || !frames_out) { free(sprites); free(frames_out); return -2; }

    for (l = 0; l < sym->layer_count; l++) {
        JsonLayer *lyr = &layers[sym->layer_start + l];
        int f;
        for (f = 0; f < lyr->frame_count; f++) {
            JsonFrame *fr = &frames[lyr->frame_start + f];
            int end = fr->start + fr->duration;
            if (end > max_time) max_time = end;
            if (fr->start < min_time) min_time = fr->start;
        }
    }

    if (min_time > max_time) { min_time = 0; max_time = 1; }
    if (max_time <= min_time) max_time = min_time + 1;

    if (g_char_debug_first_sym_total_time == 0) {
        g_char_debug_first_sym_total_time = max_time - min_time;
    }

    identity[0] = 1; identity[1] = 0; identity[2] = 0;
    identity[3] = 1; identity[4] = 0; identity[5] = 0;

    {
        int last_sprite_start = -1;
        int last_sprite_count = -1;

        for (t = min_time; t < max_time; t++) {
            int this_sprite_start = sprite_count;
            int l2;

            for (l2 = sym->layer_count - 1; l2 >= 0; l2--) {
                JsonLayer *lyr = &layers[sym->layer_start + l2];
                JsonFrame *active = layer_frame_at(lyr, frames, t);
                int e;
                if (!active) continue;
                for (e = 0; e < active->elem_count; e++) {
                    resolve_element(&elems[active->elem_start + e], identity,
                                    syms, sym_count, layers, frames, elems,
                                    &sprites, &sprite_count, &sprite_cap,
                                    t, active->start, 0);
                }
            }

            if (frame_count_out > 0 &&
                (sprite_count - this_sprite_start) == last_sprite_count &&
                sprites_equal(&sprites[last_sprite_start], &sprites[this_sprite_start],
                              last_sprite_count)) {
                sprite_count = this_sprite_start;
                frames_out[frame_count_out - 1].duration++;
            } else {
                if (frame_count_out >= frame_cap) {
                    CharFrame *tmp;
                    frame_cap *= 2;
                    tmp = (CharFrame *)realloc(frames_out, sizeof(CharFrame) * frame_cap);
                    if (!tmp) { free(sprites); free(frames_out); return -2; }
                    frames_out = tmp;
                }
                frames_out[frame_count_out].sprite_start = this_sprite_start;
                frames_out[frame_count_out].sprite_count = (short)(sprite_count - this_sprite_start);
                frames_out[frame_count_out].duration = 1;
                frame_count_out++;
                last_sprite_start = this_sprite_start;
                last_sprite_count = sprite_count - this_sprite_start;
            }
        }
    }

    *out_sprites = sprites;
    *out_sprite_count = sprite_count;
    *out_frames = frames_out;
    *out_frame_count = frame_count_out;

    out_anim->name = strdup(anim_name);
    out_anim->fps = (short)fps;
    out_anim->loop = (short)loop;
    out_anim->offset_x = (short)off_x;
    out_anim->offset_y = (short)off_y;
    out_anim->frame_start = 0;
    out_anim->frame_count = frame_count_out;

    return 0;
}

/* ================================================================== */
/* Loader principal                                                   */
/* ================================================================== */

static int char_load_json_src(Character *c, const char *char_json) {
    char image_path[128] = {0};
    char folder[256] = {0};
    char anim_json[512] = {0};
    char sprite_json[512] = {0};
    char tex_path[512] = {0};
    char sprite_basename[64] = {0};
    FILE *f;
    unsigned short w, h;
    size_t total;
    char *js;
    long sz;
    jsmn_parser p;
    jsmntok_t *tk;
    int ntok, root, anims_tok, anim_count;
    JsonSymbol *syms = NULL;
    JsonLayer *layers = NULL;
    JsonFrame *frames_json = NULL;
    JsonElement *elems = NULL;
    int sym_count = 0, layer_count = 0, frame_count_json = 0, elem_count = 0;
    int sprite_cap = 2048, frame_cap = 512, anim_cap = 32;
    int sprite_total = 0, frame_total = 0, anim_total = 0;
    int a;
    int sl;

    memset(c, 0, sizeof(*c));
    c->tex_scale_x = 1.0f;
    c->tex_scale_y = 1.0f;
    g_char_debug_tex_path[0] = 0;
    g_char_debug_folder[0] = 0;
    g_char_debug_sprite_json[0] = 0;
    g_char_debug_anim_json[0] = 0;
    g_char_debug_sym_count = 0;
    g_char_debug_layer_count = 0;
    g_char_debug_frame_count_json = 0;
    g_char_debug_elem_count = 0;
    g_char_debug_anim_json_count = 0;
    g_char_debug_anim_resolved = 0;
    g_char_debug_sym0[0] = 0;
    g_char_debug_anim0[0] = 0;
    g_char_debug_anim0_len = 0;
    g_char_debug_sym0_len = 0;
    g_char_debug_first_miss[0] = 0;
    g_char_debug_first_match_type = 0;
    g_char_debug_first_sym_layer_count = 0;
    g_char_debug_first_sym_frame_count = 0;
    g_char_debug_first_sym_total_time = 0;

    if (char_read_image_field(char_json, image_path, sizeof(image_path)) != 0) {
        return -1;
    }

    snprintf(folder, sizeof(folder), "assets/shared/images/%s", image_path);
    if (!char_file_exists(folder)) {
        strncpy(folder, image_path, sizeof(folder) - 1);
        folder[sizeof(folder) - 1] = 0;
        if (!char_file_exists(folder)) {
            return -2;
        }
    }
    strncpy(g_char_debug_folder, folder, sizeof(g_char_debug_folder) - 1);

    if (char_find_anim_json(folder, anim_json, sizeof(anim_json)) != 0) {
        return -3;
    }
    strncpy(g_char_debug_anim_json, anim_json, sizeof(g_char_debug_anim_json) - 1);

    if (char_find_spritemap_json(folder, sprite_json, sizeof(sprite_json)) != 0) {
        return -4;
    }
    strncpy(g_char_debug_sprite_json, sprite_json, sizeof(g_char_debug_sprite_json) - 1);

    {
        const char *basename = strrchr(sprite_json, '/');
        if (basename) basename++;
        else basename = sprite_json;
        strncpy(sprite_basename, basename, sizeof(sprite_basename) - 1);
        sprite_basename[sizeof(sprite_basename) - 1] = 0;
        sl = (int)strlen(sprite_basename);
        if (sl > 5) sprite_basename[sl - 5] = 0;
        snprintf(tex_path, sizeof(tex_path), "%s/%s.tex", folder, sprite_basename);
    }
    strncpy(g_char_debug_tex_path, tex_path, sizeof(g_char_debug_tex_path) - 1);

    f = fopen(tex_path, "rb");
    if (!f) return -5;
    if (fread(&w, 2, 1, f) != 1 || fread(&h, 2, 1, f) != 1) {
        fclose(f); return -6;
    }
    total = (size_t)w * h * 4;
    {
        long fsize;
        fseek(f, 0, SEEK_END);
        fsize = ftell(f);
        fseek(f, 4, SEEK_SET);
        if (fsize == (long)(8 + total)) {
            unsigned short sw0, sh0;
            if (fread(&sw0, 2, 1, f) != 1 || fread(&sh0, 2, 1, f) != 1) {
                fclose(f); return -6;
            }
            if (sw0 > 0 && sh0 > 0) {
                c->tex_scale_x = (float)w / (float)sw0;
                c->tex_scale_y = (float)h / (float)sh0;
            }
        }
    }
    c->tex = (unsigned int *)memalign(16, total);
    if (!c->tex) { fclose(f); return -7; }
    if (fread(c->tex, 1, total, f) != total) {
        free(c->tex); c->tex = NULL; fclose(f); return -8;
    }
    fclose(f);
    sceKernelDcacheWritebackInvalidateRange(c->tex, total);
    c->tex_w = w;
    c->tex_h = h;

    if (parse_spritemap(c, sprite_json) != 0) {
        free(c->tex); c->tex = NULL;
        return -9;
    }

    /* FIX: verificar que la textura quedo POT. Si char_fix_tex falla por
       memalign, abortamos con -90 en vez de dibujar con textura invalida. */
    if (c->tex_scale_x != 1.0f || c->tex_scale_y != 1.0f) {
        char_scale_atlas(c, c->tex_scale_x, c->tex_scale_y);
    } else {
        int fixr = char_fix_tex(c);
        if (fixr < 0) {
            free(c->tex); c->tex = NULL;
            return -90;
        }
    }
    if (!char_is_pot(c->tex_w) || !char_is_pot(c->tex_h) ||
        c->tex_w > 512 || c->tex_h > 512) {
#if CHAR_DEBUG
        printf("[char] textura final no-POT: %dx%d\n", c->tex_w, c->tex_h);
#endif
        free(c->tex); c->tex = NULL;
        return -91;
    }

    if (parse_animation(c, anim_json, &syms, &sym_count,
                        &layers, &layer_count,
                        &frames_json, &frame_count_json,
                        &elems, &elem_count) != 0) {
        free(c->tex); c->tex = NULL;
        return -10;
    }

    js = read_file(char_json, &sz);
    if (!js) {
        free(syms); free(layers); free(frames_json); free(elems);
        free(c->tex); c->tex = NULL;
        return -11;
    }
    jsmn_init(&p);
    ntok = jsmn_parse(&p, js, sz, NULL, 0);
    if (ntok <= 0) {
        free(js); free(syms); free(layers); free(frames_json); free(elems);
        free(c->tex); c->tex = NULL;
        return -11;
    }
    tk = (jsmntok_t *)malloc(sizeof(jsmntok_t) * ntok);
    if (!tk) {
        free(js); free(syms); free(layers); free(frames_json); free(elems);
        free(c->tex); c->tex = NULL;
        return -11;
    }
    jsmn_init(&p);
    jsmn_parse(&p, js, sz, tk, ntok);

    c->sprites = (CharSprite *)malloc(sizeof(CharSprite) * sprite_cap);
    c->frames = (CharFrame *)malloc(sizeof(CharFrame) * frame_cap);
    c->anims = (CharAnim *)malloc(sizeof(CharAnim) * anim_cap);
    if (!c->sprites || !c->frames || !c->anims) {
        free(js); free(tk);
        free(syms); free(layers); free(frames_json); free(elems);
        free(c->tex); c->tex = NULL;
        return -12;
    }

    root = 0;
    {
        int fx = ja_get(js, tk, root, "flip_x");
        int pt = ja_get(js, tk, root, "position");
        c->json_flip_x = (fx >= 0 && ja_eq(js, &tk[fx], "true")) ? 1 : 0;
        if (pt >= 0 && tk[pt].type == JSMN_ARRAY && tk[pt].size >= 2) {
            c->pos_x = (float)ja_num(js, &tk[pt + 1]);
            c->pos_y = (float)ja_num(js, &tk[pt + 2]);
        }
    }
    anims_tok = ja_get(js, tk, root, "animations");
    if (anims_tok < 0 || tk[anims_tok].type != JSMN_ARRAY) {
        free(js); free(tk);
        free(syms); free(layers); free(frames_json); free(elems);
        free(c->tex); c->tex = NULL;
        return -13;
    }
    anim_count = tk[anims_tok].size;
    g_char_debug_anim_json_count = anim_count;

    {
        int ai = anims_tok + 1;
        for (a = 0; a < anim_count; a++) {
            int anim_name_tok = ja_get(js, tk, ai, "anim");
            int sym_name_tok = ja_get(js, tk, ai, "name");
            int fps_tok = ja_get(js, tk, ai, "fps");
            int loop_tok = ja_get(js, tk, ai, "loop");
            int off_tok = ja_get(js, tk, ai, "offsets");
            char anim_name[64] = {0};
            char sym_name[64] = {0};
            int fps = 24, loop = 0, off_x = 0, off_y = 0;
            CharSprite *new_sprites = NULL;
            CharFrame *new_frames = NULL;
            int new_sprite_count = 0, new_frame_count = 0;
            CharAnim new_anim;

            memset(&new_anim, 0, sizeof(new_anim));

            if (anim_name_tok >= 0) ja_get_str(js, tk, anim_name_tok, anim_name, sizeof(anim_name));
            if (sym_name_tok >= 0) ja_get_str(js, tk, sym_name_tok, sym_name, sizeof(sym_name));
            if (fps_tok >= 0) fps = (int)ja_num(js, &tk[fps_tok]);
            if (loop_tok >= 0) loop = (ja_eq(js, &tk[loop_tok], "true")) ? 1 : 0;
            if (off_tok >= 0 && tk[off_tok].type == JSMN_ARRAY && tk[off_tok].size >= 2) {
                off_x = (int)ja_num(js, &tk[off_tok + 1]);
                off_y = (int)ja_num(js, &tk[off_tok + 2]);
            }

            if (a == 0) {
                strncpy(g_char_debug_anim0, sym_name, sizeof(g_char_debug_anim0) - 1);
                g_char_debug_anim0_len = (int)strlen(sym_name);
            }

            if (resolve_anim(c, anim_name, sym_name, fps, loop, off_x, off_y,
                             syms, sym_count, layers, layer_count,
                             frames_json, frame_count_json, elems, elem_count,
                             &new_sprites, &new_sprite_count,
                             &new_frames, &new_frame_count,
                             &new_anim) == 0) {
                if (sprite_total + new_sprite_count > sprite_cap) {
                    CharSprite *tmp;
                    while (sprite_total + new_sprite_count > sprite_cap) sprite_cap *= 2;
                    tmp = (CharSprite *)realloc(c->sprites, sizeof(CharSprite) * sprite_cap);
                    if (!tmp) {
                        free(js); free(tk);
                        free(syms); free(layers); free(frames_json); free(elems);
                        free(c->tex); c->tex = NULL;
                        free(c->sprites); c->sprites = NULL;
                        free(c->frames); c->frames = NULL;
                        free(c->anims); c->anims = NULL;
                        return -12;
                    }
                    c->sprites = tmp;
                }
                if (frame_total + new_frame_count > frame_cap) {
                    CharFrame *tmp;
                    while (frame_total + new_frame_count > frame_cap) frame_cap *= 2;
                    tmp = (CharFrame *)realloc(c->frames, sizeof(CharFrame) * frame_cap);
                    if (!tmp) {
                        free(js); free(tk);
                        free(syms); free(layers); free(frames_json); free(elems);
                        free(c->tex); c->tex = NULL;
                        free(c->sprites); c->sprites = NULL;
                        free(c->frames); c->frames = NULL;
                        free(c->anims); c->anims = NULL;
                        return -12;
                    }
                    c->frames = tmp;
                }
                if (anim_total >= anim_cap) {
                    CharAnim *tmp;
                    anim_cap *= 2;
                    tmp = (CharAnim *)realloc(c->anims, sizeof(CharAnim) * anim_cap);
                    if (!tmp) {
                        free(js); free(tk);
                        free(syms); free(layers); free(frames_json); free(elems);
                        free(c->tex); c->tex = NULL;
                        free(c->sprites); c->sprites = NULL;
                        free(c->frames); c->frames = NULL;
                        free(c->anims); c->anims = NULL;
                        return -12;
                    }
                    c->anims = tmp;
                }
                memcpy(&c->sprites[sprite_total], new_sprites, sizeof(CharSprite) * new_sprite_count);
                memcpy(&c->frames[frame_total], new_frames, sizeof(CharFrame) * new_frame_count);
                {
                    int fk;
                    for (fk = 0; fk < new_frame_count; fk++)
                        c->frames[frame_total + fk].sprite_start += sprite_total;
                }
                c->anims[anim_total] = new_anim;
                c->anims[anim_total].frame_start = frame_total;
                c->anims[anim_total].frame_count = new_frame_count;
                sprite_total += new_sprite_count;
                frame_total += new_frame_count;
                anim_total++;
                free(new_sprites);
                free(new_frames);
            }

            ai = ja_skip(tk, ai);
        }
    }

    c->sprite_count = sprite_total;
    c->frame_count = frame_total;
    c->anim_count = anim_total;
    g_char_debug_anim_resolved = anim_total;

    free(js);
    free(tk);
    free(syms);
    free(layers);
    free(frames_json);
    free(elems);

    c->loaded = 1;
    c->playing = 0;
    c->current_anim = -1;
    c->current_frame_in_anim = 0;
    c->frame_timer_ms = 0;
    return 0;
}

/* ================================================================== */
/* Formato propio .chr: el personaje ya "horneado".                    */
/*                                                                    */
/* Leer Animation.json (varios MB) y resolver simbolos anidados gasta  */
/* mucha RAM y mucho tiempo, y en una PSP fat se cuelga. El .chr guarda */
/* el resultado final (atlas + sprites + frames + animaciones) como    */
/* arrays binarios: se carga con unos pocos fread y casi sin RAM.      */
/*                                                                    */
/* Se genera SOLO: la primera vez que el personaje carga bien desde el */
/* json (p. ej. en PPSSPP) se escribe <personaje>.chr junto a su json  */
/* (assets/preload/characters/bf.chr). Copia ese .chr a la PSP.        */
/* Si cambian bf.json o Animation.json (tamaño distinto) se ignora y   */
/* se vuelve a leer el json.                                           */
/* La textura (.tex) sigue leyendose igual.                            */
/* ================================================================== */

#ifndef CHAR_AUTOBAKE
#define CHAR_AUTOBAKE 1          /* 1 = escribir el .chr tras cargar bien desde json */
#endif
#ifndef CHAR_USE_BIN
#define CHAR_USE_BIN  1          /* 1 = preferir el .chr si existe */
#endif

static int g_char_no_bin = 0;    /* 1 = ignorar .chr (forzar lectura del json) */
static int g_char_from_bin = 0;  /* 1 = el ultimo personaje salio del .chr (para depurar) */

typedef struct {
    char  magic[4];
    int   ver;
    int   szSprite, szFrame, szAtlas;
    int   atlasN, spriteN, frameN, animN;
    int   texW, texH;
    float scx, scy, px, py;
    int   flip;
    int   cjSize, ajSize;
    int   animPathLen, texPathLen;
} CharBinHdr;

typedef struct { short fps, loop, ox, oy; int fs, fc; } CharBinAnim;

static long char_fsize(const char *path) {
    FILE *f = fopen(path, "rb");
    long n;
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n;
}

static void char_bin_path(const char *json, char *out, int n) {
    int l;
    snprintf(out, n, "%s", json);
    l = (int)strlen(out);
    if (l > 5 && strcmp(out + l - 5, ".json") == 0) out[l - 5] = 0;
    l = (int)strlen(out);
    if (l + 5 < n) strcpy(out + l, ".chr");
}

/* tamaño real de la textura segun su cabecera (4 o 8 bytes); 0 si no es valida */
static int char_tex_header(const char *path, int *w, int *h, long *dataOff) {
    FILE *f = fopen(path, "rb");
    unsigned short tw, th;
    long fsz;
    if (!f) return 0;
    if (fread(&tw, 2, 1, f) != 1 || fread(&th, 2, 1, f) != 1) { fclose(f); return 0; }
    fseek(f, 0, SEEK_END);
    fsz = ftell(f);
    fclose(f);
    if (tw == 0 || th == 0) return 0;
    if (fsz == 4 + (long)tw * th * 4)      *dataOff = 4;
    else if (fsz == 8 + (long)tw * th * 4) *dataOff = 8;
    else return 0;
    *w = tw; *h = th;
    return 1;
}

static int char_save_bin(const Character *c, const char *bin, const char *char_json) {
    FILE *f;
    CharBinHdr h;
    int i, ok = 1, tw, th;
    long off;
    const char *animPath = g_char_debug_anim_json;
    const char *texPath  = g_char_debug_tex_path;

    if (!c->loaded || !c->atlas || !c->sprites || !c->frames || !c->anims) return -1;
    /* solo se hornea si la textura en memoria es exactamente la del archivo (sin remuestreo) */
    if (!char_tex_header(texPath, &tw, &th, &off) || tw != c->tex_w || th != c->tex_h) return -2;

    memset(&h, 0, sizeof(h));
    memcpy(h.magic, "FNC1", 4);
    h.ver = 1;
    h.szSprite = (int)sizeof(CharSprite);
    h.szFrame  = (int)sizeof(CharFrame);
    h.szAtlas  = (int)sizeof(CharAtlasEntry);
    h.atlasN = c->atlas_count; h.spriteN = c->sprite_count;
    h.frameN = c->frame_count; h.animN = c->anim_count;
    h.texW = c->tex_w; h.texH = c->tex_h;
    h.scx = c->tex_scale_x; h.scy = c->tex_scale_y;
    h.px = c->pos_x; h.py = c->pos_y;
    h.flip = c->json_flip_x;
    h.cjSize = (int)char_fsize(char_json);
    h.ajSize = (int)char_fsize(animPath);
    h.animPathLen = (int)strlen(animPath);
    h.texPathLen = (int)strlen(texPath);

    f = fopen(bin, "wb");
    if (!f) return -3;
    ok &= fwrite(&h, sizeof(h), 1, f) == 1;
    ok &= fwrite(animPath, 1, h.animPathLen, f) == (size_t)h.animPathLen;
    ok &= fwrite(texPath, 1, h.texPathLen, f) == (size_t)h.texPathLen;
    ok &= fwrite(c->atlas,   sizeof(CharAtlasEntry), c->atlas_count,  f) == (size_t)c->atlas_count;
    ok &= fwrite(c->sprites, sizeof(CharSprite),     c->sprite_count, f) == (size_t)c->sprite_count;
    ok &= fwrite(c->frames,  sizeof(CharFrame),      c->frame_count,  f) == (size_t)c->frame_count;
    for (i = 0; i < c->anim_count && ok; i++) {
        const CharAnim *a = &c->anims[i];
        CharBinAnim ba;
        int nl = a->name ? (int)strlen(a->name) : 0;
        ba.fps = a->fps; ba.loop = a->loop; ba.ox = a->offset_x; ba.oy = a->offset_y;
        ba.fs = a->frame_start; ba.fc = a->frame_count;
        ok &= fwrite(&nl, sizeof(int), 1, f) == 1;
        if (nl > 0) ok &= fwrite(a->name, 1, nl, f) == (size_t)nl;
        ok &= fwrite(&ba, sizeof(ba), 1, f) == 1;
    }
    fclose(f);
    if (!ok) { remove(bin); return -4; }
    return 0;
}

/* devuelve 0 si cargo; <0 si no existe / esta desactualizado / no cabe (entonces se usa el json) */
static int char_load_bin(Character *c, const char *bin, const char *char_json) {
    FILE *f = fopen(bin, "rb");
    CharBinHdr h;
    char animPath[512], texPath[512];
    int i, tw, th, ok = 1;
    long off;
    size_t total;

    if (!f) return -1;
    memset(c, 0, sizeof(*c));
    if (fread(&h, sizeof(h), 1, f) != 1 || memcmp(h.magic, "FNC1", 4) != 0 || h.ver != 1 ||
        h.szSprite != (int)sizeof(CharSprite) || h.szFrame != (int)sizeof(CharFrame) ||
        h.szAtlas != (int)sizeof(CharAtlasEntry) ||
        h.animPathLen <= 0 || h.animPathLen >= (int)sizeof(animPath) ||
        h.texPathLen  <= 0 || h.texPathLen  >= (int)sizeof(texPath) ||
        h.atlasN <= 0 || h.spriteN < 0 || h.frameN <= 0 || h.animN <= 0 ||
        h.atlasN > 100000 || h.spriteN > 4000000 || h.frameN > 1000000 || h.animN > 4096) {
        fclose(f); return -2;
    }
    if (fread(animPath, 1, h.animPathLen, f) != (size_t)h.animPathLen ||
        fread(texPath,  1, h.texPathLen,  f) != (size_t)h.texPathLen) { fclose(f); return -3; }
    animPath[h.animPathLen] = 0;
    texPath[h.texPathLen] = 0;

    /* desactualizado: cambio bf.json o Animation.json */
    if (char_fsize(char_json) != h.cjSize || char_fsize(animPath) != h.ajSize) { fclose(f); return -4; }
    if (!char_tex_header(texPath, &tw, &th, &off) || tw != h.texW || th != h.texH ||
        !char_is_pot(tw) || !char_is_pot(th) || tw > 512 || th > 512) { fclose(f); return -5; }

    c->atlas   = (CharAtlasEntry *)malloc(sizeof(CharAtlasEntry) * h.atlasN);
    c->sprites = (CharSprite *)malloc(sizeof(CharSprite) * (h.spriteN + 1));
    c->frames  = (CharFrame *)malloc(sizeof(CharFrame) * h.frameN);
    c->anims   = (CharAnim *)calloc(h.animN, sizeof(CharAnim));
    if (!c->atlas || !c->sprites || !c->frames || !c->anims) ok = 0;
    if (ok) {
        ok &= fread(c->atlas,   sizeof(CharAtlasEntry), h.atlasN,  f) == (size_t)h.atlasN;
        ok &= fread(c->sprites, sizeof(CharSprite),     h.spriteN, f) == (size_t)h.spriteN;
        ok &= fread(c->frames,  sizeof(CharFrame),      h.frameN,  f) == (size_t)h.frameN;
    }
    for (i = 0; i < h.animN && ok; i++) {
        CharBinAnim ba;
        int nl = 0;
        CharAnim *a = &c->anims[i];
        if (fread(&nl, sizeof(int), 1, f) != 1 || nl < 0 || nl > 200) { ok = 0; break; }
        a->name = (char *)malloc(nl + 1);
        if (!a->name) { ok = 0; break; }
        if (nl > 0 && fread(a->name, 1, nl, f) != (size_t)nl) { ok = 0; break; }
        a->name[nl] = 0;
        if (fread(&ba, sizeof(ba), 1, f) != 1) { ok = 0; break; }
        a->fps = ba.fps; a->loop = ba.loop; a->offset_x = ba.ox; a->offset_y = ba.oy;
        a->frame_start = ba.fs; a->frame_count = ba.fc;
        c->anim_count = i + 1;            /* para poder liberar si falla a mitad */
    }
    fclose(f);

    if (ok) {
        FILE *tf = fopen(texPath, "rb");
        total = (size_t)tw * th * 4;
        c->tex = tf ? (unsigned int *)memalign(16, total) : NULL;
        if (c->tex && fseek(tf, off, SEEK_SET) == 0 && fread(c->tex, 1, total, tf) == total) {
            sceKernelDcacheWritebackInvalidateRange(c->tex, total);
        } else ok = 0;
        if (tf) fclose(tf);
    }
    if (!ok) {
        for (i = 0; i < c->anim_count; i++) free(c->anims[i].name);
        free(c->atlas); free(c->sprites); free(c->frames); free(c->anims); free(c->tex);
        memset(c, 0, sizeof(*c));
        return -6;
    }

    c->tex_w = tw; c->tex_h = th;
    c->tex_scale_x = h.scx; c->tex_scale_y = h.scy;
    c->pos_x = h.px; c->pos_y = h.py;
    c->json_flip_x = h.flip;
    c->atlas_count = h.atlasN; c->sprite_count = h.spriteN;
    c->frame_count = h.frameN; c->anim_count = h.animN;
    c->loaded = 1;
    c->playing = 0;
    c->current_anim = -1;
    return 0;
}

static int char_load_json(Character *c, const char *char_json) {
    char bin[512];
    int r;
    char_bin_path(char_json, bin, sizeof(bin));
    g_char_from_bin = 0;
#if CHAR_USE_BIN
    if (!g_char_no_bin && char_load_bin(c, bin, char_json) == 0) {
        g_char_from_bin = 1;
        return 0;
    }
#endif
    r = char_load_json_src(c, char_json);
#if CHAR_AUTOBAKE
    if (r == 0) char_save_bin(c, bin, char_json);
#endif
    return r;
}

static void char_clone(Character *dst, const Character *src) {
    *dst = *src;
    dst->shared = 1;
    dst->current_anim = -1;
    dst->current_frame_in_anim = 0;
    dst->frame_timer_ms = 0;
    dst->playing = 0;
    dst->flip_x = 0;
}

static void char_free(Character *c) {
    int i;
    if (c->shared) { memset(c, 0, sizeof(*c)); return; }
    if (c->tex) { free(c->tex); c->tex = NULL; }
    if (c->atlas) { free(c->atlas); c->atlas = NULL; }
    if (c->sprites) { free(c->sprites); c->sprites = NULL; }
    if (c->frames) { free(c->frames); c->frames = NULL; }
    if (c->anims) {
        for (i = 0; i < c->anim_count; i++)
            if (c->anims[i].name) free(c->anims[i].name);
        free(c->anims);
        c->anims = NULL;
    }
    c->loaded = 0;
}

/* ================================================================== */
/* Runtime                                                            */
/* ================================================================== */

static int char_find_anim(Character *c, const char *name) {
    int i;
    for (i = 0; i < c->anim_count; i++)
        if (strcmp(c->anims[i].name, name) == 0) return i;
    return -1;
}

static void char_play(Character *c, const char *name) {
    int idx = char_find_anim(c, name);
    if (idx < 0) return;
    if (c->current_anim == idx && c->playing) return;
    c->current_anim = idx;
    c->current_frame_in_anim = 0;
    c->frame_timer_ms = 0;
    c->playing = 1;
}

static int char_play_ex(Character *c, const char *name, int force) {
    int idx = char_find_anim(c, name);
    if (idx < 0) return 0;
    if (!force && c->current_anim == idx && c->playing) return 1;
    c->current_anim = idx;
    c->current_frame_in_anim = 0;
    c->frame_timer_ms = 0;
    c->playing = 1;
    return 1;
}

static void char_seek(Character *c, float ms) {
    const CharAnim *anim;
    float total = 0.0f, t, frame_ms;
    int i;
    if (!c->loaded || c->current_anim < 0 || c->current_anim >= c->anim_count) return;
    anim = &c->anims[c->current_anim];
    if (anim->frame_count <= 0 || anim->fps <= 0) return;
    if (ms < 0.0f) ms = 0.0f;

    for (i = 0; i < anim->frame_count; i++)
        total += (1000.0f / (float)anim->fps) * (float)c->frames[anim->frame_start + i].duration;

    if (ms >= total) {
        if (anim->loop && total > 0.0f) ms = fmodf(ms, total);
        else {
            c->current_frame_in_anim = anim->frame_count - 1;
            c->frame_timer_ms = 0.0f;
            c->playing = 0;
            return;
        }
    }
    t = ms;
    for (i = 0; i < anim->frame_count; i++) {
        frame_ms = (1000.0f / (float)anim->fps) * (float)c->frames[anim->frame_start + i].duration;
        if (t < frame_ms) {
            c->current_frame_in_anim = i;
            c->frame_timer_ms = t;
            c->playing = 1;
            return;
        }
        t -= frame_ms;
    }
}

static const char *char_cur_name(const Character *c) {
    if (c->current_anim < 0 || c->current_anim >= c->anim_count) return "";
    return c->anims[c->current_anim].name;
}

static int char_play_or_first(Character *c, const char *name) {
    int idx = char_find_anim(c, name);
    if (idx < 0) {
        int i;
        for (i = 0; i < c->anim_count; i++) {
            if (strstr(c->anims[i].name, "idle") || strstr(c->anims[i].name, "Idle")) {
                idx = i; break;
            }
        }
    }
    if (idx >= 0) {
        char_play(c, c->anims[idx].name);
        return 0;
    }
    if (c->anim_count > 0) {
        char_play(c, c->anims[0].name);
        return 1;
    }
    return -1;
}

static void char_update(Character *c, float dt_ms) {
    const CharAnim *anim;
    if (!c->loaded || !c->playing) return;
    if (c->current_anim < 0 || c->current_anim >= c->anim_count) return;

    anim = &c->anims[c->current_anim];
    if (anim->frame_count <= 0) return;

    c->frame_timer_ms += dt_ms;
    {
        int fidx = anim->frame_start + c->current_frame_in_anim;
        const CharFrame *fr = &c->frames[fidx];
        float frame_ms = (1000.0f / (float)anim->fps) * (float)fr->duration;
        while (c->frame_timer_ms >= frame_ms) {
            c->frame_timer_ms -= frame_ms;
            c->current_frame_in_anim++;
            if (c->current_frame_in_anim >= anim->frame_count) {
                if (anim->loop) {
                    c->current_frame_in_anim = 0;
                } else {
                    c->current_frame_in_anim = anim->frame_count - 1;
                    c->playing = 0;
                    break;
                }
            }
            fidx = anim->frame_start + c->current_frame_in_anim;
            fr = &c->frames[fidx];
            frame_ms = (1000.0f / (float)anim->fps) * (float)fr->duration;
        }
    }
}

static void char_draw(Character *c, float cx, float cy, float extra_scale,
                      unsigned int tint) {
    const CharAnim *anim;
    const CharFrame *fr;
    int i;

    if (!c->loaded || c->current_anim < 0 || c->current_anim >= c->anim_count) return;
    anim = &c->anims[c->current_anim];
    if (anim->frame_count <= 0) return;

    fr = &c->frames[anim->frame_start + c->current_frame_in_anim];

    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, c->tex_w, c->tex_h, c->tex_w, c->tex);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);

    {
        float scx = (c->tex_scale_x > 0.0001f) ? c->tex_scale_x : 1.0f;
        float scy = (c->tex_scale_y > 0.0001f) ? c->tex_scale_y : 1.0f;

        for (i = 0; i < fr->sprite_count; i++) {
            const CharSprite *cs = &c->sprites[fr->sprite_start + i];
            const CharAtlasEntry *ae = &c->atlas[cs->atlas_idx];
            CharVtx *v;
            float corners_x[4], corners_y[4];
            short u0, v0, u1, v1;
            int k;

            u0 = ae->x;
            v0 = ae->y;
            u1 = (short)(ae->x + ae->w);
            v1 = (short)(ae->y + ae->h);

            {
                float wx = (ae->sw > 0) ? (float)ae->sw : (float)ae->w / scx;
                float wy = (ae->sh > 0) ? (float)ae->sh : (float)ae->h / scy;
                corners_x[0] = 0;   corners_y[0] = 0;
                corners_x[1] = wx;  corners_y[1] = 0;
                corners_x[2] = wx;  corners_y[2] = wy;
                corners_x[3] = 0;   corners_y[3] = wy;
            }

            for (k = 0; k < 4; k++) {
                float lx = corners_x[k];
                float ly = corners_y[k];
                corners_x[k] = cs->a * lx + cs->b * ly + cs->tx;
                corners_y[k] = cs->c * lx + cs->d * ly + cs->ty;
            }

            for (k = 0; k < 4; k++) {
                if (c->flip_x) corners_x[k] = -corners_x[k];
                corners_x[k] *= extra_scale;
                corners_y[k] *= extra_scale;
            }

            {
                float ref_x = (float)anim->offset_x, ref_y = (float)anim->offset_y;
                float ox, oy;
                int ri = char_find_anim(c, "idle");
                if (ri >= 0) { ref_x = (float)c->anims[ri].offset_x; ref_y = (float)c->anims[ri].offset_y; }
                ox = ref_x + CHAR_OFFSET_REL_SIGN * ((float)anim->offset_x - ref_x);
                oy = ref_y + CHAR_OFFSET_REL_SIGN * ((float)anim->offset_y - ref_y);
                if (c->flip_x) ox = -ox;
                for (k = 0; k < 4; k++) {
                    corners_x[k] += cx + ox * extra_scale;
                    corners_y[k] += cy + oy * extra_scale;
                }
            }

            v = (CharVtx *)sceGuGetMemory(4 * sizeof(CharVtx));
            v[0].u = u0;  v[0].v = v0;
            v[0].x = (short)(corners_x[0] + 0.5f);
            v[0].y = (short)(corners_y[0] + 0.5f);
            v[1].u = u1;  v[1].v = v0;
            v[1].x = (short)(corners_x[1] + 0.5f);
            v[1].y = (short)(corners_y[1] + 0.5f);
            v[2].u = u0;  v[2].v = v1;
            v[2].x = (short)(corners_x[3] + 0.5f);
            v[2].y = (short)(corners_y[3] + 0.5f);
            v[3].u = u1;  v[3].v = v1;
            v[3].x = (short)(corners_x[2] + 0.5f);
            v[3].y = (short)(corners_y[2] + 0.5f);
            for (k = 0; k < 4; k++) {
                v[k].color = tint;
                v[k].z = 0;
                v[k].pad = 0;
            }

            sceGuDrawArray(GU_TRIANGLE_STRIP,
                GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
                4, 0, v);
        }
    }

    sceGuDisable(GU_TEXTURE_2D);
}

#endif /* CHARACTER_H */