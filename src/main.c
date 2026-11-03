#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <malloc.h>
#include "conductor.h"
#include "chart.h"
#include "mods.h"
#include "weeks.h"
#include "play.h"
#include "audio.h"
#define ATLAS_MAX_FRAMES 512
#include "sprites.h"
#include "image_tex.h"
#include "font.h"
#include "font_vcr_14.h"
#include "font_vcr_20.h"
#include "font_vcr_32.h"
#include "character.h"
#include "stage.h"

PSP_MODULE_INFO("FNF", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(256);

#define BUF_W   512
#define SCR_W   480
#define SCR_H   272
#define FRAME_SIZE (BUF_W * SCR_H * 4)

#define K          0.375f
#define NOTE_W     42
#define STRUM_X    37
#define PLAYER_OFF 240
#define STRUM_Y    19
#define COL(a, rgb) ((((unsigned int)(a)) << 24) | ((rgb) & 0x00ffffff))

#define COMBO_NUM_SCALE   0.75f
#define COMBO_NUM_GAP     (-3.0f)

#define CAM_ZOOM_BUMP   0.03f
#define CAM_ZOOM_DECAY  3.125f

#define ZOOM_EVENT_SOURCE  0
#define ZOOM_EVENT_SCALE   0.5f
#define ZOOM_EVENT_MAX     1.15f

#define DIR_ATLAS    "assets/shared/images"
#define DIR_RATINGS  "assets/shared/images"
#define DIR_NUMS     "assets/preload/images"
#define DIR_COMBO    "assets/shared/images"
#define DIR_FONTS    "assets/fonts"

#define BF_CX       340.0f
#define BF_CY       240.0f
#define BF_SCALE    0.60f

#define BF_DEBUG_VISUAL 1

#define DAD_CHAR_NAME     "bf"
#define STAGE_DEBUG_GRID  0
#define CAM_DEBUG         1
#define CAM_GAME_BUMP     0.015f
#define CAM_GAME_MAX      1.35f

#define BF_DANCE_EVERY    2
#define BF_SING_DURATION  4.0f

#define TRANS_FADE_DUR_MS 400.0f
#define TRANS_FADE_BAND   48

#define DEBUG_LOAD_VISUAL 1

static unsigned int __attribute__((aligned(16))) list[131072];
typedef struct { unsigned int color; short x, y, z; } Vtx;

float g_draw_zoom = 1.0f;

static Chart ch;
static int chart_ok = 0, audio_ok = 0, preroll = 0, ended = 0;
static float lastNoteTime = 0;

static int   paused = 0;
static float pause_pos = 0;
static int   death_counter = 0;
static int   dead_counted = 0;

static float hud_zoom = 1.0f;
static int cur_section = 0;
static int cur_zoom_event = 0;

static ImgTex img_ready, img_set, img_go;
static ImgTex img_rating[4];
static ImgTex img_num[10];
static ImgTex img_combo;

static int combo_num_w = 26;
static int combo_num_h = 32;

static Font font_14, font_20, font_32;

static Character bf_char;
static int bf_loaded = 0;
static int bf_load_ret = -999;
static char bf_path_tried[256] = "?";

static Character dad_char;
static int dad_loaded = 0;
static int dad_load_ret = -999;

typedef struct {
    Character *c;
    int   loaded;
    int   isPlayer;
    float hold;
    float hey;
    int   special;
    int   idle_sync;
    float idle_t0;
    float wx, wy;
} Actor;

static Actor bf_a, dad_a;
static const char *SING_ANIM[4] = { "singLEFT", "singDOWN", "singUP", "singRIGHT" };
static int bf_last_beat = 0;

static Stage stage;

typedef struct {
    int active;
    int phase;
    int pending;
    int pending_after_out;
    float progress;
    unsigned long long start_us;
    float duration_ms;
} TransFade;

static TransFade trans_fade = {0, 0, 0, 0, 0.0f, 0, TRANS_FADE_DUR_MS};

enum GameState { ST_FREE, ST_PLAY };
static int gameState = ST_FREE;

static char g_inst_path[256]   = "Inst.pcm";
static char g_voices_path[256] = "Voices.pcm";

static int fp_preview_active = 0;
static int fp_preview_song   = -1;

static void restart(void);
static void load_song_from_freeplay(void);
static void fp_stop_preview(void);
static void freeplay_open(void);
static int  audio_start_safe(const char *instPath, const char *voicesPath, int prerollMs);
static void audio_stop_safe(void);

static void trans_fade_start(int pending) {
    trans_fade.active = 1;
    trans_fade.phase = 0;
    trans_fade.pending = pending;
    trans_fade.pending_after_out = 0;
    trans_fade.progress = 0.0f;
    trans_fade.start_us = sceKernelGetSystemTimeWide();
}

static void trans_fade_update(void) {
    unsigned long long now;
    float elapsed_ms;
    if (!trans_fade.active) return;
    now = sceKernelGetSystemTimeWide();
    elapsed_ms = (float)(now - trans_fade.start_us) / 1000.0f;

    if (trans_fade.phase == 0) {
        trans_fade.progress = elapsed_ms / trans_fade.duration_ms;
        if (trans_fade.progress >= 1.0f) {
            trans_fade.progress = 1.0f;
            if (trans_fade.pending == 1) {
                restart();
            } else if (trans_fade.pending == 2) {
                audio_stop_safe();
                audio_ok = 0;
                sceKernelExitGame();
            } else if (trans_fade.pending == 3) {
                load_song_from_freeplay();
            } else if (trans_fade.pending == 4) {
                trans_fade.pending_after_out = 1;
            } else if (trans_fade.pending == 5) {
                fp_stop_preview();
                audio_stop_safe();
                audio_ok = 0;
                chart_ok = 0;
                g.isDead = 0;
                ended = 0;
                dead_counted = 0;
                gameState = ST_FREE;
                freeplay_open();
            }
            trans_fade.phase = 1;
            trans_fade.progress = 1.0f;
            trans_fade.start_us = sceKernelGetSystemTimeWide();
            trans_fade.pending = 0;
        }
    } else {
        trans_fade.progress = 1.0f - elapsed_ms / trans_fade.duration_ms;
        if (trans_fade.progress <= 0.0f) {
            trans_fade.progress = 0.0f;
            trans_fade.active = 0;
            if (trans_fade.pending_after_out == 1) {
                trans_fade.pending_after_out = 0;
                audio_stop_safe();
                audio_ok = 0;
                sceKernelExitGame();
            }
        }
    }
}

static int str_ends_with(const char *s, const char *suf) {
    int a = (int)strlen(s), b = (int)strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

static int act_is_sing(const Actor *a) { return strncmp(char_cur_name(a->c), "sing", 4) == 0; }
static int act_is_idle(const Actor *a) { return strcmp(char_cur_name(a->c), "idle") == 0; }

static void act_start_idle(Actor *a, float t0) {
    if (!a->loaded || a->special) return;
    if (char_play_ex(a->c, "idle", 1)) {
        a->idle_sync = 1;
        a->idle_t0 = t0;
        char_seek(a->c, C.songPosition - t0);
    }
}

static void act_dance(Actor *a) {
    if (!a->loaded || a->special) return;
    if (act_is_idle(a) && a->c->playing) return;
    act_start_idle(a, C.songPosition);
}

static void act_anim_update(Actor *a, float dtMs) {
    if (!a->loaded) return;
    if (a->idle_sync && act_is_idle(a) && !a->special) char_seek(a->c, C.songPosition - a->idle_t0);
    else { a->idle_sync = 0; char_update(a->c, dtMs); }
}

static void act_sing(Actor *a, int lane, int type) {
    char name[40];
    if (!a->loaded) return;
    if (type != 3 && type != 4) {
        a->idle_sync = 0;
        snprintf(name, sizeof(name), "%s%s", SING_ANIM[lane], type == 2 ? "-alt" : "");
        if (!char_play_ex(a->c, name, 1))
            char_play_ex(a->c, SING_ANIM[lane], 1);
        a->hold = 0.0f;
    }
    if (type == 5 && char_play_ex(a->c, "hey", 1)) {
        a->idle_sync = 0;
        a->special = 1;
        a->hey = 0.6f;
    }
}

static void act_miss(Actor *a, int lane) {
    char name[40];
    if (!a->loaded) return;
    snprintf(name, sizeof(name), "%smiss", SING_ANIM[lane]);
    a->idle_sync = 0;
    char_play_ex(a->c, name, 1);
}

static void act_after_update(Actor *a, float dtMs, int anyHeld) {
    const char *nm;
    float limit = C.stepCrochet * 0.0011f * BF_SING_DURATION;
    if (!a->loaded) return;

    nm = char_cur_name(a->c);
    if (a->hey > 0.0f) {
        a->hey -= dtMs / 1000.0f;
        if (a->hey <= 0.0f) {
            a->hey = 0.0f;
            if (a->special && (strcmp(nm, "hey") == 0 || strcmp(nm, "cheer") == 0)) {
                a->special = 0;
                act_dance(a);
            }
        }
    } else if (a->special && !a->c->playing) {
        a->special = 0;
        act_dance(a);
    } else if (str_ends_with(nm, "miss") && !a->c->playing) {
        act_dance(a);
    }

    if (act_is_sing(a)) a->hold += dtMs / 1000.0f;
    else if (a->isPlayer) a->hold = 0.0f;

    nm = char_cur_name(a->c);
    if (a->isPlayer) {
        if (act_is_sing(a) && !str_ends_with(nm, "miss") && !anyHeld && a->hold > limit) {
            act_dance(a);
            a->hold = 0.0f;
        }
    } else if (a->hold >= limit) {
        act_dance(a);
        a->hold = 0.0f;
    }
}

static void actors_events(void) {
    if (g.ev_miss) { act_miss(&bf_a, (g.ev_miss - 1) & 3); g.ev_miss = 0; }
    if (g.ev_hit)  { act_sing(&bf_a, (g.ev_hit - 1) & 3, g.ev_hit_type); g.ev_hit = 0; }
    if (g.ev_dad_hit) { act_sing(&dad_a, (g.ev_dad_hit - 1) & 3, g.ev_dad_hit_type); g.ev_dad_hit = 0; }
}

static void actors_beat(float pos) {
    int cb = (int)floorf(conductor_beat(pos)), b;
    if (cb - bf_last_beat > 8) bf_last_beat = cb - 1;
    for (b = bf_last_beat + 1; b <= cb; b++) {
        if ((((b % BF_DANCE_EVERY) + BF_DANCE_EVERY) % BF_DANCE_EVERY) == 0) {
            float t0 = conductor_beat_to_seconds((float)b);
            if (bf_a.loaded && !act_is_sing(&bf_a) && !bf_a.special) act_start_idle(&bf_a, t0);
            if (dad_a.loaded && !act_is_sing(&dad_a) && !dad_a.special) act_start_idle(&dad_a, t0);
        }
    }
    if (cb > bf_last_beat) bf_last_beat = cb;
}

typedef struct {
    float x, y;
    float tx, ty;
    float zoom;
    float speed;
    int   focusDad;
} Cam;
static Cam cam;

static float cam_sx(float wx) { return SCR_W * 0.5f + (wx - cam.x) * K * cam.zoom; }
static float cam_sy(float wy) { return SCR_H * 0.5f + (wy - cam.y) * K * cam.zoom; }

static void cam_move(int isDad) {
    const Actor *a = isDad ? &dad_a : &bf_a;
    float fx = STAGE_FOCUS_DX;
    if (a->c && a->c->flip_x) fx = -fx;
    cam.focusDad = isDad;
    cam.tx = a->wx + fx;
    cam.ty = a->wy + STAGE_FOCUS_DY;
    if (isDad) {
        cam.tx += 150.0f + stage.cam_dad[0];
        cam.ty += -100.0f + stage.cam_dad[1];
    } else {
        cam.tx += -100.0f + stage.cam_bf[0];
        cam.ty += -100.0f + stage.cam_bf[1];
    }
}

static void cam_focus_section(int idx) {
    if (idx < 0 || idx >= ch.sectionCount) return;
    if (ch.sections[idx].gf) return;
    cam_move(!ch.sections[idx].mustHit);
}

static void cam_update(float dtMs) {
    float dt_s = dtMs / 1000.0f, lv = dt_s * 2.4f * cam.speed, zf;
    if (lv < 0.0f) lv = 0.0f;
    if (lv > 1.0f) lv = 1.0f;
    cam.x += (cam.tx - cam.x) * lv;
    cam.y += (cam.ty - cam.y) * lv;

    zf = 1.0f - dt_s * CAM_ZOOM_DECAY;
    if (zf < 0.0f) zf = 0.0f;
    cam.zoom = stage.defaultZoom + (cam.zoom - stage.defaultZoom) * zf;
    if (fabsf(cam.zoom - stage.defaultZoom) < 0.0005f) cam.zoom = stage.defaultZoom;
}

static void cam_reset(void) {
    cam.speed = stage.cameraSpeed;
    cam.zoom = stage.defaultZoom;
    cam_focus_section(0);
    cam.x = cam.tx;
    cam.y = cam.ty;
}

static void actor_place(Actor *a, Character *c, int loaded, int isPlayer, const float *pos) {
    memset(a, 0, sizeof(*a));
    a->c = c;
    a->loaded = loaded;
    a->isPlayer = isPlayer;
    a->wx = pos[0] + (loaded ? c->pos_x : 0.0f);
    a->wy = pos[1] + (loaded ? c->pos_y : 0.0f);
}

static void rect(int x, int y, int w, int h, unsigned int col);

#if STAGE_DEBUG_GRID
static void draw_stage_debug(void) {
    int i;
    float saved = g_draw_zoom;
    g_draw_zoom = 1.0f;
    rect((int)cam_sx(-4000), (int)cam_sy(700), (int)(8000 * K * cam.zoom), 400, 0xff2a3a2a);
    for (i = -8; i <= 16; i++) {
        float x = (float)i * 160.0f;
        rect((int)cam_sx(x), 0, 1, SCR_H, (i % 4 == 0) ? 0x66ffffff : 0x22ffffff);
    }
    rect((int)cam_sx(0), (int)cam_sy(0), (int)(1280 * K * cam.zoom), 1, 0x88ffff00);
    rect((int)cam_sx(0), (int)cam_sy(720), (int)(1280 * K * cam.zoom), 1, 0x88ffff00);
    rect((int)cam_sx(cam.tx) - 2, (int)cam_sy(cam.ty) - 2, 5, 5, cam.focusDad ? 0xff40c0ff : 0xff40ff40);
    g_draw_zoom = saved;
}
#endif

static int exit_cb(int a, int b, void *c) { sceKernelExitGame(); return 0; }
static int cb_thread(SceSize args, void *argp) {
    sceKernelRegisterExitCallback(sceKernelCreateCallback("exit", exit_cb, NULL));
    sceKernelSleepThreadCB();
    return 0;
}

static void gu_setup(void) {
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)FRAME_SIZE, BUF_W);
    sceGuDepthBuffer((void *)(FRAME_SIZE * 2), BUF_W);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

static void rect(int x, int y, int w, int h, unsigned int col) {
    Vtx *v;
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
    v = (Vtx *)sceGuGetMemory(2 * sizeof(Vtx));
    v[0].color = col; v[0].x = (short)x;       v[0].y = (short)y;       v[0].z = 0;
    v[1].color = col; v[1].x = (short)(x + w); v[1].y = (short)(y + h); v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
}

static void draw_vgradient(int x, int y, int w, int h,
                           unsigned int col_top, unsigned int col_bot) {
    Vtx *v;
    if (w <= 0 || h <= 0) return;
    v = (Vtx *)sceGuGetMemory(4 * sizeof(Vtx));
    v[0].color = col_top; v[0].x = (short)x;       v[0].y = (short)y;       v[0].z = 0;
    v[1].color = col_top; v[1].x = (short)(x + w); v[1].y = (short)y;       v[1].z = 0;
    v[2].color = col_bot; v[2].x = (short)x;       v[2].y = (short)(y + h); v[2].z = 0;
    v[3].color = col_bot; v[3].x = (short)(x + w); v[3].y = (short)(y + h); v[3].z = 0;
    sceGuDrawArray(GU_TRIANGLE_STRIP,
        GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 4, 0, v);
}

static void trans_fade_draw(void) {
    float saved;
    int cover_h, band;
    if (!trans_fade.active && trans_fade.progress <= 0.0f) return;
    saved = g_draw_zoom;
    g_draw_zoom = 1.0f;

    cover_h = (int)(SCR_H * trans_fade.progress + 0.5f);
    if (cover_h > SCR_H) cover_h = SCR_H;
    if (cover_h <= 0) { g_draw_zoom = saved; return; }

    band = TRANS_FADE_BAND;
    if (band > cover_h) band = cover_h;

    if (trans_fade.phase == 1) {
        int top_y = SCR_H - cover_h;
        int solid_y = top_y + band;
        int solid_h = cover_h - band;
        draw_vgradient(0, top_y, SCR_W, band, 0x00000000, 0xff000000);
        if (solid_h > 0) rect(0, solid_y, SCR_W, solid_h, 0xff000000);
    } else {
        int solid_h = cover_h - band;
        if (solid_h > 0) rect(0, 0, SCR_W, solid_h, 0xff000000);
        draw_vgradient(0, solid_h, SCR_W, band, 0xff000000, 0x00000000);
    }

    g_draw_zoom = saved;
}

static Font *pick_font(int sc) {
    if (sc >= 4) return &font_32;
    if (sc >= 2) return &font_20;
    return &font_14;
}

static int text_w(const char *s, int sc) { return font_measure(pick_font(sc), s); }

static void text(int x, int y, int sc, unsigned int col, const char *s) {
    Font *f = pick_font(sc);
    float scale = g_draw_zoom;
    if (!f->loaded) return;
    if (scale != 1.0f) {
        int w = font_measure(f, s);
        int h = f->line_height;
        float cx = (float)x + w * 0.5f;
        float cy = (float)y + h * 0.5f;
        float scx = SCR_W * 0.5f + (cx - SCR_W * 0.5f) * scale;
        float scy = SCR_H * 0.5f + (cy - SCR_H * 0.5f) * scale;
        int nw = (int)(w * scale + 0.5f);
        int nh = (int)(h * scale + 0.5f);
        x = (int)(scx - nw * 0.5f + 0.5f);
        y = (int)(scy - nh * 0.5f + 0.5f);
    }
    font_begin();
    if (scale == 1.0f) {
        font_draw(f, x + 1, y + 1, COL(col >> 24, 0x000000), s);
        font_draw(f, x, y, col, s);
    } else {
        font_draw_scaled(f, x + 1, y + 1, scale, COL(col >> 24, 0x000000), s);
        font_draw_scaled(f, x, y, scale, col, s);
    }
    font_end();
}

static void text_fixed(int x, int y, int sc, unsigned int col, const char *s) {
    Font *f = pick_font(sc);
    float saved = g_draw_zoom;
    if (!f->loaded) return;
    g_draw_zoom = 1.0f;
    font_begin();
    font_draw(f, x + 1, y + 1, COL(col >> 24, 0x000000), s);
    font_draw(f, x, y, col, s);
    font_end();
    g_draw_zoom = saved;
}

#include "pause.h"
#include "freeplay.h"

/* ===================================================================== */
/*  Audio helpers seguros                                                */
/* ===================================================================== */

static int audio_start_safe(const char *instPath, const char *voicesPath, int prerollMs) {
    int tries;
    for (tries = 0; tries < 5; tries++) {
        if (A.running) {
            audio_stop();
            sceKernelDelayThread(30000);
        }
        A.running = 0;
        if (audio_start(instPath, voicesPath, prerollMs) == 0) {
            NA_LOG("[audio] start OK (intento %d) %s\n", tries + 1, instPath);
            return 0;
        }
        NA_LOG("[audio] start FAIL (intento %d) %s\n", tries + 1, instPath);
        sceKernelDelayThread(200000);
    }
    sceAudioSRCChRelease();
    sceKernelDelayThread(200000);
    if (audio_start(instPath, voicesPath, prerollMs) == 0) return 0;
    return -1;
}

static void audio_stop_safe(void) {
    if (!A.running) return;
    audio_stop();
    sceKernelDelayThread(30000);
}

/* ===================================================================== */
/*  read_current_mod (compatibilidad con codigo de stages/characters)    */
/* ===================================================================== */

static int read_current_mod(char *out, int len) {
    out[0] = 0;
    mods_first_active(out, len);
    return out[0] != 0;
}

/* ===================================================================== */
/*  Cargas de assets                                                     */
/* ===================================================================== */

static int try_load_atlas(int noteW) {
    char cur_mod[64];
    char tex[256], xml[256];
    int i;

    for (i = 0; i < ActiveMods.count; i++) {
        if (!ActiveMods.mods[i].active) continue;
        snprintf(tex, sizeof(tex), "mods/%s/images/NOTE_assets.tex", ActiveMods.mods[i].name);
        snprintf(xml, sizeof(xml), "mods/%s/images/NOTE_assets.xml", ActiveMods.mods[i].name);
        if (spr_load(tex, xml, noteW) == 0) return 0;
    }
    snprintf(tex, sizeof(tex), DIR_ATLAS "/NOTE_assets.tex");
    snprintf(xml, sizeof(xml), DIR_ATLAS "/NOTE_assets.xml");
    if (spr_load(tex, xml, noteW) == 0) return 0;
    snprintf(tex, sizeof(tex), "NOTE_assets.tex");
    snprintf(xml, sizeof(xml), "NOTE_assets.xml");
    if (spr_load(tex, xml, noteW) == 0) return 0;
    return -1;
}

static void try_load_ui_images(void) {
    char p[256];
    static const char *rnames[4] = { "sick", "good", "bad", "shit" };
    int i, m;

    for (m = 0; m < ActiveMods.count; m++) {
        if (!ActiveMods.mods[m].active) continue;
        snprintf(p, sizeof(p), "mods/%s/images/ready.img", ActiveMods.mods[m].name);
        img_load(&img_ready, p);
        if (img_ready.loaded) break;
    }
    for (m = 0; m < ActiveMods.count; m++) {
        if (!ActiveMods.mods[m].active) continue;
        snprintf(p, sizeof(p), "mods/%s/images/set.img", ActiveMods.mods[m].name);
        img_load(&img_set, p);
        if (img_set.loaded) break;
    }
    for (m = 0; m < ActiveMods.count; m++) {
        if (!ActiveMods.mods[m].active) continue;
        snprintf(p, sizeof(p), "mods/%s/images/go.img", ActiveMods.mods[m].name);
        img_load(&img_go, p);
        if (img_go.loaded) break;
    }
    for (i = 0; i < 4; i++) {
        for (m = 0; m < ActiveMods.count; m++) {
            if (!ActiveMods.mods[m].active) continue;
            snprintf(p, sizeof(p), "mods/%s/images/%s.img", ActiveMods.mods[m].name, rnames[i]);
            img_load(&img_rating[i], p);
            if (img_rating[i].loaded) break;
        }
    }
    for (i = 0; i < 10; i++) {
        for (m = 0; m < ActiveMods.count; m++) {
            if (!ActiveMods.mods[m].active) continue;
            snprintf(p, sizeof(p), "mods/%s/images/num%d.img", ActiveMods.mods[m].name, i);
            img_load(&img_num[i], p);
            if (img_num[i].loaded) break;
        }
    }
    for (m = 0; m < ActiveMods.count; m++) {
        if (!ActiveMods.mods[m].active) continue;
        snprintf(p, sizeof(p), "mods/%s/images/combo.img", ActiveMods.mods[m].name);
        img_load(&img_combo, p);
        if (img_combo.loaded) break;
    }

    if (!img_ready.loaded) img_load(&img_ready, DIR_RATINGS "/ready.img");
    if (!img_set.loaded)   img_load(&img_set,   DIR_RATINGS "/set.img");
    if (!img_go.loaded)    img_load(&img_go,    DIR_RATINGS "/go.img");
    for (i = 0; i < 4; i++) {
        if (!img_rating[i].loaded) {
            snprintf(p, sizeof(p), DIR_RATINGS "/%s.img", rnames[i]);
            img_load(&img_rating[i], p);
        }
    }
    for (i = 0; i < 10; i++) {
        if (!img_num[i].loaded) {
            snprintf(p, sizeof(p), DIR_NUMS "/num%d.img", i);
            img_load(&img_num[i], p);
        }
    }
    if (!img_combo.loaded) img_load(&img_combo, DIR_COMBO "/combo.img");

    for (i = 0; i < 10; i++) {
        if (img_num[i].loaded) {
            combo_num_w = img_num[i].img_w;
            combo_num_h = img_num[i].img_h;
            break;
        }
    }
}

static void try_load_fonts(void) {
    char p[256];
    int m;

    for (m = 0; m < ActiveMods.count; m++) {
        if (!ActiveMods.mods[m].active) continue;
        snprintf(p, sizeof(p), "mods/%s/fonts/vcr_14.tex", ActiveMods.mods[m].name);
        font_load(&font_14, p, font_14_glyphs, FONT_14_ASCENT, FONT_14_DESCENT, FONT_14_LINE_HEIGHT);
        if (font_14.loaded) break;
    }
    if (!font_14.loaded)
        font_load(&font_14, DIR_FONTS "/vcr_14.tex", font_14_glyphs, FONT_14_ASCENT, FONT_14_DESCENT, FONT_14_LINE_HEIGHT);

    for (m = 0; m < ActiveMods.count; m++) {
        if (!ActiveMods.mods[m].active) continue;
        snprintf(p, sizeof(p), "mods/%s/fonts/vcr_20.tex", ActiveMods.mods[m].name);
        font_load(&font_20, p, font_20_glyphs, FONT_20_ASCENT, FONT_20_DESCENT, FONT_20_LINE_HEIGHT);
        if (font_20.loaded) break;
    }
    if (!font_20.loaded)
        font_load(&font_20, DIR_FONTS "/vcr_20.tex", font_20_glyphs, FONT_20_ASCENT, FONT_20_DESCENT, FONT_20_LINE_HEIGHT);

    for (m = 0; m < ActiveMods.count; m++) {
        if (!ActiveMods.mods[m].active) continue;
        snprintf(p, sizeof(p), "mods/%s/fonts/vcr_32.tex", ActiveMods.mods[m].name);
        font_load(&font_32, p, font_32_glyphs, FONT_32_ASCENT, FONT_32_DESCENT, FONT_32_LINE_HEIGHT);
        if (font_32.loaded) break;
    }
    if (!font_32.loaded)
        font_load(&font_32, DIR_FONTS "/vcr_32.tex", font_32_glyphs, FONT_32_ASCENT, FONT_32_DESCENT, FONT_32_LINE_HEIGHT);
}

/* ===================================================================== */
/*  Personajes                                                           */
/* ===================================================================== */

static void char_normalize_name(const char *filename, char *out, int out_size) {
    int len = (int)strlen(filename);
    int i, j = 0;
    if (len > 5 && strcmp(filename + len - 5, ".json") == 0) len -= 5;
    for (i = 0; i < len && j < out_size - 1; i++) {
        char c = filename[i];
        if (c == ' ' || c == '_') continue;
        if (c >= 'A' && c <= 'Z') c += 32;
        out[j++] = c;
    }
    out[j] = 0;
}

static int char_filename_exact_match(const char *filename, const char *char_name) {
    char norm_file[64];
    char norm_name[64];
    char_normalize_name(filename, norm_file, sizeof(norm_file));
    char_normalize_name(char_name, norm_name, sizeof(norm_name));
    return strcmp(norm_file, norm_name) == 0;
}

static int try_load_character(Character *c, const char *char_name) {
    char p[256];
    SceUID d;
    SceIoDirent entry;
    int ret = -1, m;

    for (m = 0; m < ActiveMods.count; m++) {
        if (!ActiveMods.mods[m].active) continue;
        snprintf(p, sizeof(p), "mods/%s/characters/%s.json", ActiveMods.mods[m].name, char_name);
        strncpy(bf_path_tried, p, sizeof(bf_path_tried) - 1);
        ret = char_load_json(c, p);
        if (ret == 0) return 0;
    }
    snprintf(p, sizeof(p), "assets/preload/characters/%s.json", char_name);
    strncpy(bf_path_tried, p, sizeof(bf_path_tried) - 1);
    ret = char_load_json(c, p);
    if (ret == 0) return 0;

    d = sceIoDopen("assets/preload/characters");
    if (d >= 0) {
        while (sceIoDread(d, &entry) > 0) {
            int len = (int)strlen(entry.d_name);
            if (len < 6) continue;
            if (FIO_S_ISDIR(entry.d_stat.st_mode)) continue;
            if (strcmp(entry.d_name + len - 5, ".json") != 0) continue;
            if (!char_filename_exact_match(entry.d_name, char_name)) continue;
            snprintf(p, sizeof(p), "assets/preload/characters/%s", entry.d_name);
            strncpy(bf_path_tried, p, sizeof(bf_path_tried) - 1);
            ret = char_load_json(c, p);
            if (ret == 0) { sceIoDclose(d); return 0; }
        }
        sceIoDclose(d);
    }
    return ret;
}

/* ===================================================================== */
/*  Restart, song loading                                                */
/* ===================================================================== */

static void restart(void) {
    paused = 0;
    PZ.active = 0;
    dead_counted = 0;
    play_free();
    ended = 0;
    g.isDead = 0;
    conductor_init(ch.bpm);
    conductor_map_bpm_changes(&ch);
    play_init(&ch, ch.speed);
    preroll = (int)(C.crochet * 5.0f);
    conductor_start(-(float)preroll);

    audio_ok = (audio_start_safe(g_inst_path,
                                 ch.needsVoices ? g_voices_path : NULL,
                                 preroll) == 0);

    hud_zoom = 1.0f;
    cur_section = 0;
    cur_zoom_event = 0;
    g_draw_zoom = 1.0f;

    bf_a.hold = bf_a.hey = 0.0f;  bf_a.special = 0;
    dad_a.hold = dad_a.hey = 0.0f; dad_a.special = 0;
    bf_last_beat = (int)floorf(conductor_beat(-(float)preroll));
    C.songPosition = -(float)preroll;
    bf_a.idle_sync = dad_a.idle_sync = 0;
    act_start_idle(&bf_a, C.songPosition);
    act_start_idle(&dad_a, C.songPosition);
    cam_reset();
}

static void load_song_from_freeplay(void) {
    if (chart_load_json(FP.chartPath, &ch) == 0) {
        char mod[64] = "";
        int has_mod;
        chart_ok = 1;
        lastNoteTime = ch.noteCount > 0
                     ? (float)ch.notes[ch.noteCount - 1].time : 0;
        has_mod = read_current_mod(mod, sizeof(mod));
        stage_load(&stage, ch.stage, has_mod ? mod : NULL);
        actor_place(&bf_a,  &bf_char,  bf_loaded,  1, stage.bf);
        actor_place(&dad_a, &dad_char, dad_loaded, 0, stage.dad);
        strncpy(g_inst_path,   FP.instPath,   sizeof(g_inst_path)   - 1);
        strncpy(g_voices_path, FP.voicePath,  sizeof(g_voices_path) - 1);
        restart();
        gameState = ST_PLAY;
        FP.active = 0;
    } else {
        NA_LOG("[main] chart_load_json FAIL: %s\n", FP.chartPath);
        chart_ok = 0;
    }
}

/* ===================================================================== */
/*  Preview del Freeplay (START)                                         */
/* ===================================================================== */

static void fp_stop_preview(void) {
    if (!fp_preview_active) return;
    audio_stop_safe();
    audio_ok = 0;
    fp_preview_active = 0;
    fp_preview_song = -1;
}

static void fp_inst_path_for_selected(char *out, int outLen) {
    WeekSong *s = &WD.songs[FP.songIdx[FP.sel]];
    FILE *f;
    int i;

    for (i = 0; i < ActiveMods.count; i++) {
        if (!ActiveMods.mods[i].active) continue;
        snprintf(out, outLen, "mods/%s/songs/%s/Inst.pcm", ActiveMods.mods[i].name, s->lower);
        f = fopen(out, "rb");
        if (f) { fclose(f); NA_LOG("[freeplay] preview inst: %s\n", out); return; }
        snprintf(out, outLen, "mods/%s/data/%s/Inst.pcm", ActiveMods.mods[i].name, s->lower);
        f = fopen(out, "rb");
        if (f) { fclose(f); NA_LOG("[freeplay] preview inst: %s\n", out); return; }
    }

    snprintf(out, outLen, "assets/preload/songs/%s/Inst.pcm", s->lower);
    f = fopen(out, "rb");
    if (f) { fclose(f); NA_LOG("[freeplay] preview inst: %s\n", out); return; }

    snprintf(out, outLen, "assets/preload/data/%s/Inst.pcm", s->lower);
    f = fopen(out, "rb");
    if (f) { fclose(f); NA_LOG("[freeplay] preview inst: %s\n", out); return; }

    snprintf(out, outLen, "assets/songs/%s/Inst.pcm", s->lower);
    f = fopen(out, "rb");
    if (f) { fclose(f); NA_LOG("[freeplay] preview inst: %s\n", out); return; }

    snprintf(out, outLen, "Inst.pcm");
    NA_LOG("[freeplay] preview inst: fallback Inst.pcm (raiz)\n");
}

static void fp_start_preview(void) {
    char instPath[256];
    if (FP.count == 0) return;
    fp_inst_path_for_selected(instPath, sizeof(instPath));
    audio_ok = (audio_start_safe(instPath, NULL, 0) == 0);
    if (audio_ok) {
        fp_preview_active = 1;
        fp_preview_song = FP.sel;
        NA_LOG("[freeplay] preview: %s\n", instPath);
    } else {
        NA_LOG("[freeplay] preview FAIL: %s\n", instPath);
        fp_preview_active = 0;
        fp_preview_song = -1;
    }
}

/* ===================================================================== */
/*  Controles                                                            */
/* ===================================================================== */

static const unsigned int lane_col[4] = { 0xff994bc2, 0xffffff00, 0xff05fa12, 0xff3f39f9 };
static const unsigned int lane_btn[4][2] = {
    { PSP_CTRL_LEFT,  PSP_CTRL_SQUARE   },
    { PSP_CTRL_DOWN,  PSP_CTRL_CROSS    },
    { PSP_CTRL_UP,    PSP_CTRL_TRIANGLE },
    { PSP_CTRL_RIGHT, PSP_CTRL_CIRCLE   },
};

#if DEBUG_LOAD_VISUAL
static void debug_show_text(const char *msg, int frames) {
    while (frames-- > 0) {
        sceGuStart(GU_DIRECT, list);
        sceGuClearColor(0xff000000);
        sceGuClear(GU_COLOR_BUFFER_BIT);
        text_fixed(10, SCR_H / 2 - 10, 2, 0xffffffff, msg);
        sceGuFinish(); sceGuSync(0, 0);
        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
    }
}
#endif

#define BOOT_LOG 1
static char boot_lines[14][72];
static int  boot_n = 0;

static void boot_step(const char *msg) {
#if BOOT_LOG
    int i, f;
    if (boot_n < 14) snprintf(boot_lines[boot_n++], 72, "%s", msg);
    for (f = 0; f < 3; f++) {
        sceGuStart(GU_DIRECT, list);
        sceGuClearColor(0xff000000);
        sceGuClear(GU_COLOR_BUFFER_BIT);
        for (i = 0; i < boot_n; i++)
            text_fixed(6, 6 + i * 16, 1, (i == boot_n - 1) ? 0xff00ffff : 0xffffffff, boot_lines[i]);
        sceGuFinish(); sceGuSync(0, 0);
        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
    }
#else
    (void)msg;
#endif
}

static int mem_start_max_kb = 0;

static void mem_report(char *out, int n, int withHeap) {
    int used = 0;
    if (withHeap) { struct mallinfo mi = mallinfo(); used = mi.uordblks / 1024; }
    snprintf(out, n, "MEM heap=%dK libre=%dK max=%dK ini=%dK",
             used,
             (int)(sceKernelTotalFreeMemSize() / 1024),
             (int)(sceKernelMaxFreeMemSize() / 1024),
             mem_start_max_kb);
}

#if DEBUG_LOAD_VISUAL
static void debug_show_lines(const char *l1, const char *l2, const char *l3, const char *l4, int frames) {
    while (frames-- > 0) {
        sceGuStart(GU_DIRECT, list);
        sceGuClearColor(0xff000000);
        sceGuClear(GU_COLOR_BUFFER_BIT);
        text_fixed(6, 30,  1, 0xff6060ff, l1);
        text_fixed(6, 60,  1, 0xffffffff, l2);
        text_fixed(6, 90,  1, 0xffffffff, l3);
        text_fixed(6, 120, 1, 0xff00ffff, l4);
        sceGuFinish(); sceGuSync(0, 0);
        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
    }
}
#endif

/* ===================================================================== */
/*  main                                                                 */
/* ===================================================================== */

int main(void) {
    SceCtrlData pad;
    int r, i, l, s, held[4], prevHeld[4] = {0, 0, 0, 0};
    unsigned int btn, prevBtn = 0, col;
    float pos = 0, prevPos = 0, dt = 16.0f, y, pxms, center, stepPx;
    char buf[256];
    unsigned long long fp_last_frame_us;

    int t = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, 0, NULL);
    if (t >= 0) sceKernelStartThread(t, 0, NULL);

    gu_setup();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    mem_start_max_kb = (int)(sceKernelMaxFreeMemSize() / 1024);

    boot_step("gu ok");
    boot_step("mods...");
    mods_load();
    boot_step("mods ok");

    try_load_fonts();
    boot_step("fuentes ok");
    boot_step("cargando BF (.chr o Animation.json)...");

    bf_load_ret = try_load_character(&bf_char, "bf");
    bf_loaded = (bf_load_ret == 0);
    { char m[72]; snprintf(m, sizeof(m), "BF ret=%d (%s)", bf_load_ret, g_char_from_bin ? "desde .chr" : "desde json"); boot_step(m); }
    if (bf_loaded) {
        char_play_or_first(&bf_char, "idle");
        bf_char.flip_x = !bf_char.json_flip_x;
    }

    if (bf_loaded && strcmp(DAD_CHAR_NAME, "bf") == 0) {
        char_clone(&dad_char, &bf_char);
        dad_loaded = 1;
        dad_load_ret = 0;
    } else {
        dad_load_ret = try_load_character(&dad_char, DAD_CHAR_NAME);
        dad_loaded = (dad_load_ret == 0);
    }
    if (dad_loaded) {
        char_play_or_first(&dad_char, "idle");
        dad_char.flip_x = dad_char.json_flip_x;
    }

#if DEBUG_LOAD_VISUAL
    if (!bf_loaded || !dad_loaded) {
        char l1[96], l2[160], l3[96], l4[160];
        snprintf(l1, sizeof(l1), "PERSONAJE NO CARGO: bf=%d dad=%d", bf_load_ret, dad_load_ret);
        snprintf(l2, sizeof(l2), "%s", bf_path_tried);
        snprintf(l3, sizeof(l3), "-5 sin .tex  -7/-90 sin RAM  -10/-12 sin RAM al leer json");
        mem_report(l4, sizeof(l4), 1);
        debug_show_lines(l1, l2, l3, l4, 300);
    }
#endif

    boot_step("atlas de notas...");
    try_load_atlas(NOTE_W);
    boot_step("imagenes UI...");
    try_load_ui_images();
    boot_step("alphabet...");
    al_load();
    boot_step("weeks...");

    r = weeks_load(&WD);
    {
        char m[72];
        snprintf(m, sizeof(m), "weeks: %d semanas, %d canciones (r=%d)",
                 WD.weekCount, WD.songCount, r);
        boot_step(m);
    }

    freeplay_open();

    if (r == 0 && WD.songCount > 0) {
        boot_step("freeplay listo");
        gameState = ST_FREE;
    } else {
        boot_step("fallback: bopeebo.json");
        r = chart_load_json("bopeebo.json", &ch);
        chart_ok = (r == 0);
        if (chart_ok) {
            char cur_mod[64];
            int has_mod;
            lastNoteTime = ch.noteCount > 0 ? (float)ch.notes[ch.noteCount - 1].time : 0;
            has_mod = read_current_mod(cur_mod, sizeof(cur_mod));
            stage_load(&stage, ch.stage, has_mod ? cur_mod : NULL);
            actor_place(&bf_a,  &bf_char,  bf_loaded,  1, stage.bf);
            actor_place(&dad_a, &dad_char, dad_loaded, 0, stage.dad);
            strncpy(g_inst_path,   "Inst.pcm",   sizeof(g_inst_path) - 1);
            strncpy(g_voices_path, "Voices.pcm", sizeof(g_voices_path) - 1);
            restart();
            gameState = ST_PLAY;
            boot_step("fallback listo");
        } else {
            chart_ok = 0;
            gameState = ST_FREE;
            boot_step("sin canciones ni bopeebo.json");
        }
    }

    fp_last_frame_us = sceKernelGetSystemTimeWide();

    while (1) {
        float frame_dt;
        unsigned long long now_us;

        sceCtrlReadBufferPositive(&pad, 1);
        btn = pad.Buttons;

        now_us = sceKernelGetSystemTimeWide();
        frame_dt = (float)(now_us - fp_last_frame_us) / 1000000.0f;
        fp_last_frame_us = now_us;
        if (frame_dt <= 0.0f || frame_dt > 0.1f) frame_dt = 1.0f / 60.0f;

        for (l = 0; l < 4; l++)
            held[l] = (btn & lane_btn[l][0]) || (btn & lane_btn[l][1]);

        if (gameState == ST_FREE) {
            if (!trans_fade.active) {
                int p_up    = (btn & PSP_CTRL_UP)    && !(prevBtn & PSP_CTRL_UP);
                int p_down  = (btn & PSP_CTRL_DOWN)  && !(prevBtn & PSP_CTRL_DOWN);
                int p_left  = (btn & PSP_CTRL_LEFT)  && !(prevBtn & PSP_CTRL_LEFT);
                int p_right = (btn & PSP_CTRL_RIGHT) && !(prevBtn & PSP_CTRL_RIGHT);
                int p_ok    = (btn & PSP_CTRL_CROSS) && !(prevBtn & PSP_CTRL_CROSS);
                int p_start = (btn & PSP_CTRL_START) && !(prevBtn & PSP_CTRL_START);
                int p_circ  = (btn & PSP_CTRL_CIRCLE) && !(prevBtn & PSP_CTRL_CIRCLE);

                int prev_sel = FP.sel;

                FreeplayAction fa = freeplay_update(frame_dt,
                                                    p_up, p_down, p_left, p_right,
                                                    p_ok, 0);

                if (FP.playScroll) { /* TODO: scrollMenu.pcm */ }
                if (FP.playCancel) { /* TODO: cancelMenu.pcm */ }

                if (p_start && FP.count > 0) {
                    if (fp_preview_active) fp_stop_preview();
                    else                    fp_start_preview();
                }

                if (FP.sel != prev_sel && fp_preview_active) {
                    fp_stop_preview();
                }

                if (fp_preview_active && audio_ok && A.running && A.finished) {
                    fp_stop_preview();
                }

                if (fa == FREE_PLAY) {
                    fp_stop_preview();
                    trans_fade_start(3);
                }
                else if (p_circ) {
                    fp_stop_preview();
                    trans_fade_start(4);
                }
            } else {
                FP.t += frame_dt;
            }
            pos = 0;
            prevPos = 0;
            dt = 0.0f;
        } else if (gameState == ST_PLAY) {
            if (chart_ok && paused) {
                PzAction act;
                int p_up   = (btn & PSP_CTRL_UP)    && !(prevBtn & PSP_CTRL_UP);
                int p_down = (btn & PSP_CTRL_DOWN)  && !(prevBtn & PSP_CTRL_DOWN);
                int p_ok   = (btn & PSP_CTRL_CROSS) && !(prevBtn & PSP_CTRL_CROSS);
                int p_back = (btn & PSP_CTRL_START) && !(prevBtn & PSP_CTRL_START);

                act = pause_update(p_up, p_down, p_ok, p_back);
                if (act == PZ_RESUME) {
                    paused = 0;
                    if (audio_ok && A.running) audio_resume();
                    else conductor_start(pause_pos);
                } else if (act == PZ_RESTART) {
                    paused = 0;
                    trans_fade_start(1);
                } else if (act == PZ_EXIT) {
                    paused = 0;
                    PZ.active = 0;
                    trans_fade_start(5);
                }
                pos = C.songPosition;
                dt = 0.0f;
                prevPos = pos;
            } else if (chart_ok) {
                if ((btn & PSP_CTRL_START) && !(prevBtn & PSP_CTRL_START) && !trans_fade.active) {
                    if (g.isDead || ended) {
                        restart();
                    } else {
                        const char *diff_label = (FP.diffCount > 0 && FP.diff >= 0 && FP.diff < FP.diffCount)
                                               ? FP.diffNames[FP.diff] : "NORMAL";
                        paused = 1;
                        pause_pos = C.songPosition;
                        if (audio_ok && A.running) audio_pause();
                        else C.running = 0;
                        pause_open(ch.songName, diff_label, death_counter);
                    }
                }
                if ((btn & PSP_CTRL_SELECT) && !(prevBtn & PSP_CTRL_SELECT)) g.cpuControlled = !g.cpuControlled;

                if (trans_fade.active && trans_fade.phase == 0) {
                    pos = C.songPosition;
                    dt = 0.0f;
                    prevPos = pos;
                } else {
                    if (audio_ok && A.running && !A.finished) {
                        float target = audio_position_ms();
                        float diff = target - C.songPosition;
                        float adiff = (diff < 0.0f) ? -diff : diff;
                        if (adiff > 20.0f) C.songPosition = target;
                        else               C.songPosition += diff * 0.5f;
                    } else if (audio_ok && A.finished) {
                        C.songPosition += 16.0f;
                    } else {
                        conductor_update();
                    }

                    pos = C.songPosition;
                    dt = pos - prevPos; prevPos = pos;
                    if (dt < 0 || dt > 100) dt = 16.0f;

                    while (cur_section < ch.sectionCount && pos >= (float)ch.sections[cur_section].time) {
                        cam_focus_section(cur_section);
                        if (g.cam_zooming) {
                            hud_zoom += CAM_ZOOM_BUMP;
                            if (cam.zoom < CAM_GAME_MAX) cam.zoom += CAM_GAME_BUMP;
                        }
                        cur_section++;
                    }

                    while (cur_zoom_event < ch.zoomEventCount &&
                           pos >= (float)ch.zoomEvents[cur_zoom_event].time) {
                        const ZoomEvent *ze = &ch.zoomEvents[cur_zoom_event];
                        float z;
                        #if ZOOM_EVENT_SOURCE == 1
                            z = ze->camZoom;
                        #elif ZOOM_EVENT_SOURCE == 2
                            z = ze->hudZoom;
                        #else
                            z = ze->camZoom;
                            if (ze->hudZoom > z) z = ze->hudZoom;
                        #endif
                        hud_zoom += z * ZOOM_EVENT_SCALE;
                        if (cam.zoom < CAM_GAME_MAX) cam.zoom += ze->camZoom * ZOOM_EVENT_SCALE;
                        cur_zoom_event++;
                    }

                    if (hud_zoom > 1.0f) {
                        float dt_s = dt / 1000.0f;
                        float lf = 1.0f - (dt_s * CAM_ZOOM_DECAY);
                        if (lf < 0.0f) lf = 0.0f;
                        hud_zoom = 1.0f + (hud_zoom - 1.0f) * lf;
                        if (hud_zoom < 1.001f) hud_zoom = 1.0f;
                    }
                    if (hud_zoom > ZOOM_EVENT_MAX) hud_zoom = ZOOM_EVENT_MAX;
                    if (!g.isDead) cam_update(dt);

                    if (!g.isDead && !ended) {
                        g.endingSong = 0;
                        play_update(pos, dt, held, prevHeld);
                        if (pos > lastNoteTime && (audio_ok ? A.finished : pos > lastNoteTime + 2000.0f)) {
                            ended = 1; g.endingSong = 1;
                        }
                    }
                    if (g.isDead && !dead_counted) { death_counter++; dead_counted = 1; }
                    if (audio_ok) A.volVoices = (g.vocals > 0.5f) ? 256 : 0;
                }
            } else {
                pos = 0;
            }
        }

        for (l = 0; l < 4; l++) prevHeld[l] = held[l];
        prevBtn = btn;

        if (gameState == ST_PLAY && chart_ok && !paused) {
            int anyHeld = !g.cpuControlled && (held[0] || held[1] || held[2] || held[3]);
            if (!g.isDead) actors_beat(pos);
            actors_events();
            act_anim_update(&dad_a, dt);
            act_anim_update(&bf_a, dt);
            act_after_update(&dad_a, dt, 0);
            act_after_update(&bf_a, dt, anyHeld);
        }

        trans_fade_update();

        g_draw_zoom = hud_zoom;

        sceGuStart(GU_DIRECT, list);
        sceGuClearColor(chart_ok ? 0xff201818 : 0xff000060);
        sceGuClear(GU_COLOR_BUFFER_BIT);

        if (gameState == ST_FREE) {
            g_draw_zoom = 1.0f;
            freeplay_draw();
        } else if (gameState == ST_PLAY) {
            if (!chart_ok) {
                text_fixed(20, 120, 2, 0xffffffff, "NO HAY CHART");
            } else {
                #if STAGE_DEBUG_GRID
                draw_stage_debug();
                #endif

                if (dad_loaded)
                    char_draw(&dad_char, cam_sx(dad_a.wx), cam_sy(dad_a.wy), BF_SCALE * cam.zoom, 0xffffffff);
                if (bf_loaded)
                    char_draw(&bf_char, cam_sx(bf_a.wx), cam_sy(bf_a.wy), BF_SCALE * cam.zoom, 0xffffffff);

                #if BF_DEBUG_VISUAL
                rect((int)cam_sx(bf_a.wx) - 20, (int)cam_sy(bf_a.wy) - 20, 40, 40, 0x44ff00ff);
                rect((int)cam_sx(dad_a.wx) - 20, (int)cam_sy(dad_a.wy) - 20, 40, 40, 0x4400ffff);
                #endif

                pxms = 0.45f * g.songSpeed * K;
                center = (float)(STRUM_Y + NOTE_W / 2);
                stepPx = C.stepCrochet * pxms;

                if (spr_ok) spr_begin();

                for (s = 0; s < 2; s++)
                    for (l = 0; l < 4; l++) {
                        int st = g.strum[s][l];
                        if (spr_ok) {
                            spr_strum(s, l, st, dt, STRUM_X + s * PLAYER_OFF + l * NOTE_W, STRUM_Y, NOTE_W, 0xff);
                        } else {
                            unsigned int a = (st == 2) ? 0xff : (st == 1 ? 0xaa : 0x66);
                            col = (st == 1) ? COL(a, 0xcccccc) : COL(a, lane_col[l]);
                            rect(STRUM_X + s * PLAYER_OFF + l * NOTE_W + 2, STRUM_Y, NOTE_W - 4, NOTE_W - 4, col);
                        }
                    }

                for (i = g.head; i < g.spawnEnd; i++) {
                    GNote *n = &g.pool[g.order[i]];
                    int x;
                    if (n->dead) continue;
                    y = STRUM_Y + (n->strumTime - pos) * pxms;
                    if (y > SCR_H) break;
                    x = STRUM_X + n->mustPress * PLAYER_OFF + n->lane * NOTE_W;
                    col = (n->type == 1) ? 0x00303030 : (lane_col[n->lane] & 0x00ffffff);
                    if (n->isSustain) {
                        float top = y, bot = y + stepPx + 1.0f;
                        int clip;
                        if (n->mustPress)
                            clip = n->wasGoodHit || (g.pool[n->prev].wasGoodHit && !n->canBeHit);
                        else clip = 1;
                        if (clip && top < center) top = center;
                        if (bot > top) {
                            if (spr_ok)
                                spr_hold(n->lane, n->isEnd, x, (int)top, (int)(bot - top), NOTE_W,
                                         COL(n->tooLate ? 0x4d : 0x99, n->type == 1 ? 0x505050 : 0xffffff));
                            else
                                rect(x + NOTE_W / 2 - 6, (int)top, 12, (int)(bot - top), COL(0x99, col));
                        }
                    } else if (y + NOTE_W > 0) {
                        if (spr_ok)
                            spr_note(n->lane, x, (int)y, NOTE_W, COL(n->tooLate ? 0x4d : 0xff, n->type == 1 ? 0x505050 : 0xffffff));
                        else
                            rect(x + 2, (int)y, NOTE_W - 4, NOTE_W - 4, COL(n->tooLate ? 0x4d : 0xff, col));
                    }
                }
                if (spr_ok) spr_end();

                {
                    int bx = (SCR_W - 225) / 2, by = (int)(SCR_H * 0.89f), iw = 221;
                    int fill = (int)(iw * (g.health / 2.0f));
                    rect(bx, by, 225, 9, 0xff000000);
                    rect(bx + 2, by + 2, iw, 5, 0xffce66af);
                    rect(bx + 2 + iw - fill, by + 2, fill, 5, 0xffd1b031);
                }

                if (g.ratingName[0] != '?')
                    sprintf(buf, "SCORE: %d | MISSES: %d | RATING: %s (%.2f%%) - %s", g.songScore, g.songMisses,
                            g.ratingName, floorf(g.ratingPercent * 10000.0f) / 100.0f, g.ratingFC);
                else
                    sprintf(buf, "SCORE: %d | MISSES: %d | RATING: ?", g.songScore, g.songMisses);
                s = 1;
                text((SCR_W - text_w(buf, s)) / 2, (int)(SCR_H * 0.89f) + 14, s, 0xffffffff, buf);

                if (g.cpuControlled) {
                    float a = 1.0f - sinf((pos * 0.18f) * 3.14159f / 180.0f);
                    if (a < 0) a = 0;
                    text((SCR_W - text_w("BOTPLAY", 2)) / 2, 90, 2, COL((int)(a * 255), 0xffffff), "BOTPLAY");
                }

                {
                    float c = C.crochet;
                    const char *w = 0;
                    float ph = 0;
                    ImgTex *img = 0;
                    if (pos >= -3 * c && pos < -2 * c) { w = "READY"; ph = (pos + 3 * c) / c; img = &img_ready; }
                    else if (pos >= -2 * c && pos < -c) { w = "SET"; ph = (pos + 2 * c) / c; img = &img_set; }
                    else if (pos >= -c && pos < 0)      { w = "GO"; ph = (pos + c) / c; img = &img_go; }
                    if (w) {
                        unsigned int alpha = (unsigned int)((1.0f - ph) * 255.0f);
                        if (alpha > 0) {
                            if (img && img->loaded) {
                                img_begin();
                                img_draw_centered(img, SCR_W, SCR_H, COL(alpha, 0xffffff));
                                img_end();
                            } else {
                                text((SCR_W - text_w(w, 4)) / 2, 110, 4, COL(alpha, 0xffffff), w);
                            }
                        }
                    }
                }

                if (g.popAge >= 0 && g.totalPlayed > 0 && g.popAge < C.crochet * 2.0f + 200.0f) {
                    float ts = g.popAge / 1000.0f;
                    float ry = SCR_H / 2 - 60 * K + (-157.0f * ts + 0.5f * 550.0f * ts * ts) * K;
                    float cy = SCR_H / 2 + 80 * K + (-150.0f * ts + 0.5f * 250.0f * ts * ts) * K;
                    float ra = 1.0f, ca = 1.0f, f;
                    f = (g.popAge - C.crochet) / 200.0f;
                    if (f > 0) ra = 1.0f - (f > 1 ? 1 : f);
                    f = (g.popAge - C.crochet * 2.0f) / 200.0f;
                    if (f > 0) ca = 1.0f - (f > 1 ? 1 : f);
                    if (ra > 0) {
                        ImgTex *ri = &img_rating[g.popRating];
                        unsigned int acol = COL((int)(ra * 255), 0xffffff);
                        if (ri->loaded) {
                            img_begin();
                            img_draw_at_center(ri, (int)(SCR_W * 0.35f) + 4, (int)ry, acol);
                            img_end();
                        } else {
                            text((int)(SCR_W * 0.35f - 20), (int)ry, 3, acol, R_NAME[g.popRating]);
                        }
                    }
                    if (ca > 0) {
                        int combo = g.popCombo;
                        int nd = (combo >= 1000) ? 4 : 3;
                        int num_w_draw, num_h_draw, spacing, gap_scaled;
                        int group_w, start_x, base_y, d;
                        int digits[4];
                        int any_loaded = 0;
                        unsigned int ccol = COL((int)(ca * 255), 0xffffff);
                        num_w_draw  = (int)(combo_num_w * COMBO_NUM_SCALE);
                        num_h_draw  = (int)(combo_num_h * COMBO_NUM_SCALE);
                        gap_scaled  = (int)(COMBO_NUM_GAP * COMBO_NUM_SCALE);
                        spacing     = num_w_draw + gap_scaled;
                        if (num_w_draw < 1) num_w_draw = 1;
                        if (num_h_draw < 1) num_h_draw = 1;
                        if (spacing < 1) spacing = 1;
                        if (nd == 4) {
                            digits[0] = (combo / 1000) % 10; digits[1] = (combo / 100) % 10;
                            digits[2] = (combo / 10) % 10;   digits[3] = combo % 10;
                        } else {
                            digits[0] = (combo / 100) % 10;  digits[1] = (combo / 10) % 10;
                            digits[2] = combo % 10;
                        }
                        for (d = 0; d < nd; d++)
                            if (img_num[digits[d]].loaded) { any_loaded = 1; break; }
                        group_w = num_w_draw + (nd - 1) * spacing;
                        start_x = (int)(SCR_W * 0.35f) - group_w / 2;
                        base_y  = (int)cy - num_h_draw / 2;
                        if (any_loaded) {
                            img_begin();
                            for (d = 0; d < nd; d++) {
                                int x = start_x + d * spacing;
                                ImgTex *ni = &img_num[digits[d]];
                                if (ni->loaded) img_draw(ni, x, base_y, num_w_draw, num_h_draw, ccol);
                                else { char dstr[2] = { (char)('0' + digits[d]), 0 }; text(x, base_y, 2, ccol, dstr); }
                            }
                            img_end();
                        } else {
                            char cb[8];
                            if (nd == 4) sprintf(cb, "%04d", combo % 10000);
                            else sprintf(cb, "%03d", combo);
                            text((int)(SCR_W * 0.35f - 30), (int)cy, 3, ccol, cb);
                        }
                    }
                }

                if (paused) pause_draw();

                if (g.isDead) {
                    rect(0, 0, SCR_W, SCR_H, 0xaa000000);
                    text((SCR_W - text_w("GAME OVER", 4)) / 2, 100, 4, 0xff3030ff, "GAME OVER");
                    text((SCR_W - text_w("PRESS START", 2)) / 2, 150, 2, 0xffffffff, "PRESS START");
                } else if (ended) {
                    rect(0, 0, SCR_W, SCR_H, 0xaa000000);
                    text((SCR_W - text_w("SONG COMPLETE", 3)) / 2, 70, 3, 0xffffffff, "SONG COMPLETE");
                    sprintf(buf, "SICKS: %d  GOODS: %d  BADS: %d  SHITS: %d", g.sicks, g.goods, g.bads, g.shits);
                    text((SCR_W - text_w(buf, 2)) / 2, 110, 2, 0xffffffff, buf);
                    sprintf(buf, "SCORE: %d  MISSES: %d", g.songScore, g.songMisses);
                    text((SCR_W - text_w(buf, 2)) / 2, 130, 2, 0xffffffff, buf);
                    sprintf(buf, "%s (%.2f%%) %s", g.ratingName, floorf(g.ratingPercent * 10000.0f) / 100.0f, g.ratingFC);
                    text((SCR_W - text_w(buf, 2)) / 2, 150, 2, 0xffffffff, buf);
                }

                #if CAM_DEBUG
                sprintf(buf, "CAM %s z=%.2f spd=%.1f sec=%d stage=%s", cam.focusDad ? "OPONENTE" : "BF",
                        cam.zoom, cam.speed, cur_section > 0 ? cur_section - 1 : 0, stage.name);
                text_fixed(4, 4, 1, 0xffffffff, buf);
                #endif

                #if BF_DEBUG_VISUAL
                {
                    int dy = 14;
                    mem_report(buf, sizeof(buf), 0);
                    text_fixed(4, dy, 1, 0xff00ff00, buf); dy += 10;
                    sprintf(buf, "B=%d L=%d D=%d L=%d", bf_load_ret, bf_loaded, dad_load_ret, dad_loaded);
                    text_fixed(4, dy, 1, 0xffff00ff, buf); dy += 10;
                    sprintf(buf, "cam=%.0f,%.0f z=%.2f", cam.x, cam.y, cam.zoom);
                    text_fixed(4, dy, 1, 0xff00ffff, buf); dy += 10;
                    sprintf(buf, "bf=%.0f,%.0f scr=%.0f,%.0f", bf_a.wx, bf_a.wy, cam_sx(bf_a.wx), cam_sy(bf_a.wy));
                    text_fixed(4, dy, 1, 0xff88ff88, buf); dy += 10;
                    if (bf_loaded && bf_char.anim_count > 0) {
                        int ca = bf_char.current_anim;
                        int f_cur = 0, sp_cur = 0;
                        if (ca < 0) ca = 0;
                        if (ca < bf_char.anim_count) {
                            f_cur = bf_char.anims[ca].frame_start + bf_char.current_frame_in_anim;
                            if (f_cur >= 0 && f_cur < bf_char.frame_count)
                                sp_cur = bf_char.frames[f_cur].sprite_count;
                        }
                        sprintf(buf, "a=%d/%d f=%d/%d sp=%d", ca, bf_char.anim_count,
                                bf_char.current_frame_in_anim, bf_char.anims[ca].frame_count, sp_cur);
                        text_fixed(4, dy, 1, 0xffff8888, buf); dy += 10;
                    }
                }
                #endif
            }
        }

        trans_fade_draw();

        sceGuFinish();
        sceGuSync(0, 0);
        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
    }
    return 0;
}