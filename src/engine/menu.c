/* menu.c — タイトルと隠しコンフィグ(デバッグプレイ用)
 *
 * タイトルでコナミコマンド(上上下下左右左右 B A。B と A はキーボードの B キー・A キー)を入れると、隠しコンフィグ画面へ。
 * 上下で項目、左右で値を変え、スペース(A ボタン)でタイトルへ戻る。設定は電源を切るまで残る。
 *
 * コード領域(0x4000-0xB3FF)が足りないので、このファイルはコードも定数もページ0の RAM(エリア _PAGE0)に置く
 * (Makefile で --codeseg PAGE0 --constseg PAGE0)。隠しコンフィグの画面は場面のモジュール(scene.c)。ページ0を BIOS に切り替える処理(ステージの読み込み・BIOS のフォント)は
 * ここから呼ばない。
 */
#include "game.h"
#include "hud.h"
#include "sound.h"

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

/* コナミコマンド: 上上下下左右左右 B A(キーボードの B キー・A キー) */
static const u8 konami[10] = {
    IN_UP, IN_UP, IN_DOWN, IN_DOWN, IN_LEFT, IN_RIGHT, IN_LEFT, IN_RIGHT, IN_KEY_B, IN_KEY_A,
};

void title(void)
{
    u16 t = 0;
    u8 prev = 0xFF, in, key, kn = 0;
    g_scene = 0;
    bgm_play(SONG_TITLE);                /* 馬鹿陽気なアメリカン(src/sound/songs.mml) */
#ifndef DEBUG_FPS
    /* タイトルの絵(SCREEN 11。main.c の title_show)。PUSH SPACE KEY は約 1 秒ごとに点滅 */
    title_show();
    for (;;) {
        wait_tick();
        t++;
        if (!(t & 31)) title_blink(!(t & 32));
        in = input_raw();
        key = in & ~prev & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT | IN_KEY_A | IN_KEY_B);
        if (key) {
            if (key == konami[kn]) kn++;
            else kn = (key == konami[0]);
            if (kn == sizeof(konami)) {
                kn = 0;
                vdp_init();
                config_show();
                title_show();
                prev = 0xFF;
                continue;
            }
        }
        if ((in & IN_A) && !(prev & IN_A)) break;
        prev = in;
    }
    bgm_stop();
    vdp_init();                          /* SCREEN 3 に戻す */
#else
    /* 計測用 ROM: 文字だけのタイトル(CPU の状態を出す) */
    set_stage_palette(pal_title);
    g_spr_attr[0] = 209;
    g_spr_attr[4] = 209;
    for (;;) {
        buf_fill(C_BLACK);
        g_rx = 0; g_ry = 30; g_rw = 64; g_rh = 1; g_color = 9; rect_fill();
        text_center(6, "COASTER", C_YELLOW, 2);
        text_center(18, "HOME RUN", C_ORANGE, 2);
        if (t & 32) text_center(34, "PUSH SPACE", C_WHITE, 1);
        {   /* 計測用: CPU の状態(実機・WebMSX で R800 DRAM モードに入っているかの確認) */
            u8 c = sys_cpu_status();
            text_center(41, (c & 0x20) ? "CPU:Z80" : (c & 0x40) ? "CPU:R800 ROM" : "CPU:R800 DRAM",
                        (c & 0x60) ? C_RED : C_CYAN, 1);
        }
        present();
        t++;
        in = input_raw();
        key = in & ~prev & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT | IN_KEY_A | IN_KEY_B);
        if (key) {
            if (key == konami[kn]) kn++;
            else kn = (key == konami[0]);
            if (kn == sizeof(konami)) {
                kn = 0;
                config_show();
                prev = 0xFF;
                continue;
            }
        }
        if ((in & IN_A) && !(prev & IN_A)) break;
        prev = in;
    }
    bgm_stop();
#endif
}
