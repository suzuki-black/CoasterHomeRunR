/* menu.c — タイトルと隠しコンフィグ(デバッグプレイ用)
 *
 * タイトルでコナミコマンド(上上下下左右左右 B A。B と A はキーボードの B キー・A キー)を入れると、隠しコンフィグ画面へ。
 * 上下で項目、左右で値を変え、スペース(A ボタン)でタイトルへ戻る。設定は電源を切るまで残る。
 *
 * コード領域(0x4000-0xB3FF)が足りないので、このファイルはコードも定数もページ0の RAM(エリア _PAGE0)に置く
 * (Makefile で --codeseg PAGE0 --constseg PAGE0)。ページ0を BIOS に切り替える処理(ステージの読み込み・BIOS のフォント)は
 * ここから呼ばない。
 */
#include "game.h"
#include "hud.h"

Config g_cfg;
extern const u8 pal_title[];

void cfg_defaults(void)
{
    g_cfg.damage = 1;
    g_cfg.stage = 1;
    g_cfg.life = 8;
#ifdef DEBUG_FPS
    g_cfg.fps = 1;
#endif
}

/* 項目: 名前、最小、最大、値の名前(0 = 数字で出す) */
enum { CF_DAMAGE, CF_GOD, CF_STAGE, CF_LIFE, CF_ONESHOT, CF_ROUTE, CF_EASY, CF_FPS, CF_XFER, CF_COUNT };
static const char * const cf_name[CF_COUNT] = {
    "DAMAGE", "NO DAMAGE", "STAGE", "TREASURE", "ONE SHOT", "ROUTE", "EASY", "FPS", "XFER",
};
static const u8 cf_min[CF_COUNT] = { 1, 0, 1, 1, 0, 0, 0, 0, 0 };
static const u8 cf_max[CF_COUNT] = { 8, 1, STAGE_COUNT, 8, 1, 2, 2, 1, 1 };
static const char * const v_onoff[] = { "OFF", "ON" };
static const char * const v_route[] = { "FREE", "LEFT", "RIGHT" };
static const char * const v_easy[]  = { "STAGE", "ON", "OFF" };
static const char * const v_xfer[]  = { "AUTO", "R800" };

static u8 *cf_val(u8 i)
{
    return &((u8 *)&g_cfg)[i];
}

static void config(void)
{
    u8 sel = 0, i, prev = 0xFF, in, edge;
    u8 *v;
    for (;;) {
        buf_fill(C_BLACK);
        for (i = 0; i < CF_COUNT; i++) {
            /* 9 項目 × 5 行で画面いっぱい(行の間が空かないので、選んでいない項目は白と水色を交互に) */
            u8 y = 2 + i * 5, c = (i == sel) ? C_YELLOW : (i & 1) ? C_CYAN : C_WHITE;
            const char *s = 0;
            v = cf_val(i);
            text_draw(2, (i8)y, cf_name[i], c, 1);
            if (i == CF_GOD || i == CF_ONESHOT || i == CF_FPS) s = v_onoff[*v];
            else if (i == CF_ROUTE) s = v_route[*v];
            else if (i == CF_EASY) s = v_easy[*v];
            else if (i == CF_XFER) s = v_xfer[*v];
            if (s) text_draw(42, (i8)y, s, c, 1);
            else num_draw(42, (i8)y, *v, 1, c);
        }
        present();
        in = input_raw();
        edge = in & ~prev;
        prev = in;
        v = cf_val(sel);
        if (edge & IN_UP) sel = sel ? sel - 1 : CF_COUNT - 1;
        if (edge & IN_DOWN) sel = (sel + 1 < CF_COUNT) ? sel + 1 : 0;
        if ((edge & IN_LEFT) && *v > cf_min[sel]) (*v)--;
        if ((edge & IN_RIGHT) && *v < cf_max[sel]) (*v)++;
        if (edge & IN_A) {
            g_z80x = g_z80x_ok && !g_cfg.xfer;   /* 転送のしかた(vdp.s の vdp_xfer) */
            return;
        }
    }
}

/* コナミコマンド: 上上下下左右左右 B A(キーボードの B キー・A キー) */
static const u8 konami[10] = {
    IN_UP, IN_UP, IN_DOWN, IN_DOWN, IN_LEFT, IN_RIGHT, IN_LEFT, IN_RIGHT, IN_KEY_B, IN_KEY_A,
};

void title(void)
{
    u16 t = 0;
    u8 prev = 0xFF, in, key, kn = 0;
    g_scene = 0;
    set_stage_palette(pal_title);
    g_spr_attr[0] = 209;
    g_spr_attr[4] = 209;
    for (;;) {
        buf_fill(C_BLACK);
        g_rx = 0; g_ry = 30; g_rw = 64; g_rh = 1; g_color = 9; rect_fill();
        text_center(6, "COASTER", C_YELLOW, 2);
        text_center(18, "HOME RUN", C_ORANGE, 2);
        if (t & 32) text_center(34, "PUSH SPACE", C_WHITE, 1);
#ifdef DEBUG_FPS
        {   /* 計測用: CPU の状態(実機・WebMSX で R800 DRAM モードに入っているかの確認) */
            u8 c = sys_cpu_status();
            text_center(41, (c & 0x20) ? "CPU:Z80" : (c & 0x40) ? "CPU:R800 ROM" : "CPU:R800 DRAM",
                        (c & 0x60) ? C_RED : C_CYAN, 1);
        }
#else
        text_center(41, "A:FIRE  B:TURN", C_CYAN, 1);
#endif
        present();
        t++;
        in = input_raw();
        key = in & ~prev & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT | IN_KEY_A | IN_KEY_B);
        if (key) {
            if (key == konami[kn]) kn++;
            else kn = (key == konami[0]);
            if (kn == sizeof(konami)) {
                kn = 0;
                config();
                prev = 0xFF;
                continue;
            }
        }
        if ((in & IN_A) && !(prev & IN_A)) break;
        prev = in;
    }
}
