/* scene.c — 場面のモジュール(マッパーのセグメント 10。ゲームの最中には動かない画面)
 *
 * エンディング・ゲームオーバー・隠しコンフィグ・SCREEN 3 の作り直し(vdp_init)。常駐部の置き場を空けるため、地形モジュールや
 * 音のモジュールと同じくページ2のモジュールにした(2026-10-23)。起動時に 1 回読み込み、常駐部の中継(sys.s の vdp_init・
 * ending_show・gameover_show・config_show)がページ2をこのセグメントにして呼び、元のセグメントに戻す。
 * ページ2で動くので、ステージのデータ(ゲームモジュール)は読めない(エンディングの文は play が g_end_txt へ写す)。
 * タイトルの絵の読み込み(title_show)はページ2を使うので常駐部のまま。初期値つきの変数は置けない。
 */
#include "game.h"
#include "hud.h"
#include "sound.h"
#include "pcm.h"

extern const u8 pal_fixed[], pal_title[];
extern u8 g_end_txt[], g_end_bgm;
extern Config g_cfg;
void scm_vdp_init(void);

void scm_vdp_init(void)
{
    pcm_stop();                     /* VDP のレジスタを書き直すので、声(走査線割込み)を止めてふだんの割込みに戻す */
    static const u8 regs[] = {
        1, 0xA3,        /* 表示オフ、16K、M2(マルチカラー)、16×16、拡大 */
        0, 0x00,
        2, 0x04,        /* ネームテーブル 1000h */
        3, 0x00,
        4, 0x00,        /* パターンジェネレータ A */
        5, 0x28,        /* スプライトアトリビュート 1400h */
        6, 0x03,        /* スプライトパターン 1800h */
        7, 0x01,        /* 周辺色 = 1(黒) */
        8, 0x08,        /* 色 0 = 透明、スプライト表示 */
        9, 0x00,        /* 192 ライン、60Hz */
        10, 0x00, 11, 0x00, 14, 0x00,
        25, 0x00, 26, 0x00, 27, 0x00,
    };
    u8 i, r, c;
    for (i = 0; i < sizeof(regs); i += 2) vdp_wreg(regs[i], regs[i + 1]);
    vdp_set_write(0x0000);
    vdp_fill(0, 0x4000);
    xfer_invalidate();               /* 転送の写し(vdp.s)も捨てる */
    /* ネームテーブル: 行 r の値 = (r >> 2) * 32 + 列(仕様書 7.4) */
    vdp_set_write(0x1000);
    for (r = 0; r < 24; r++) {
        u8 row[32];
        for (c = 0; c < 32; c++) row[c] = (u8)((r >> 2) * 32 + c);
        vdp_out(row, 32);
    }
    vdp_set_write(0x1800);
    vdp_out(spr_patterns, SPR_PAT_BYTES);
    for (i = 0; i < 16; i += 4) g_spr_attr[i] = 209;
    g_spr_attr[16] = 208;
    vdp_set_write(0x1400);
    vdp_out(g_spr_attr, sizeof(g_spr_attr));
    vdp_set_palette(0, pal_fixed);   /* 0-8 に続けて 9-15 も書かれるので、この後に上書きする */
    set_stage_palette(pal_title);
    vdp_wreg(1, 0xEB);             /* 表示オン、垂直帰線割り込み */
}

/* エンディング(仕様書 5.2): SCREEN 1 に切り替え、BIOS のフォント(ひらがな)で最後のステージの ending の文を出す。
 * 約 8 秒後(1 秒過ぎたら A でも)タイトルへ。戻るときに SCREEN 3 を作り直す */
void scm_ending(void)
{
    static const u8 regs[] = {
        1, 0xA2,        /* 表示オフ、16×16(スプライトは隠す) */
        0, 0x00,        /* GRAPHIC 1 */
        2, 0x06,        /* ネームテーブル 1800h */
        3, 0x80,        /* カラーテーブル 2000h */
        4, 0x00,        /* パターンジェネレータ 0000h */
        7, 0x01,        /* 背景 = 黒 */
    };
    const u8 *p = g_end_txt;
    u16 t;
    u8 i, v, prev = 0xFF, in;
    u8 row[32];
    char num[6];
    u16 sc = g_score;

    while (g_flip_req) wait_tick();
    for (i = 0; i < 16; i += 4) g_spr_attr[i] = 209;
    for (i = 0; i < sizeof(regs); i += 2) vdp_wreg(regs[i], regs[i + 1]);
    bios_font(BUF);
    vdp_set_write(0x0000);
    vdp_out(BUF, 2048);
    for (i = 0; i < 32; i++) row[i] = 0x21;          /* 文字 = 白(2)、背景 = 黒(1)。8 文字ごとに 1 バイト */
    row[6] = row[7] = 0x61;                            /* 数字(30h-3Fh)は黄色 */
    vdp_set_write(0x2000);
    vdp_out(row, 32);
    vdp_set_write(0x1800);
    vdp_fill(0x20, 768);
    /* 文: (行, 桁, 文字…, 0) の並び。クリアすれば必ずこの文(グッドエンディング。仕様書 5.2) */
    while (p && *p != 0xFF) {
        u8 r = *p++, c = *p++;
        const u8 *q = p;
        while (*p) p++;
        vdp_set_write(0x1800 + (u16)r * 32 + c);
        vdp_out(q, (u16)(p - q));
        p++;
    }
    /* スコア(内部は 1/10 なので末尾に 0) */
    for (i = 0; i < 5; i++) { num[4 - i] = (char)('0' + sc % 10); sc /= 10; }
    for (i = 0; i < 4 && num[i] == '0'; i++) num[i] = ' ';   /* 先頭の 0 は空白に */
    vdp_set_write(0x1800 + 19 * 32 + 10);
    vdp_out((const u8 *)"SCORE ", 6);
    vdp_out((const u8 *)num, 5);
    vdp_out((const u8 *)"0", 1);
    vdp_wreg(1, 0xE2);                                 /* 表示オン、垂直帰線割り込み */
    if (g_end_bgm) bgm_play(g_end_bgm);
    for (t = 0; t < 570; t++) {                        /* 約 9.5 秒: エンディングの曲が 1 周鳴り終わるまで(1 周 約 9.6 秒) */
        wait_tick();
        in = input_raw();
        if (t > 60 && (in & IN_A) && !(prev & IN_A)) break;
        prev = in;
    }
    bgm_stop();
    scm_vdp_init();
}

/* ゲームオーバー(約 4 秒)。GAME OVER の点滅、COASTER BROKEN、最終スコア(内部は 1/10 なので末尾に 0)。
 */
void scm_gameover(void)
{
    u8 i, k = 0;
    char s[13], d[5];
    u16 sc = g_score;
    /* 「SCORE 300」のように、数字を詰めて全体を中央に(右詰めの欄だと間が空きすぎる) */
    do { d[k++] = (char)('0' + sc % 10); sc /= 10; } while (sc);
    for (i = 0; i < 6; i++) s[i] = "SCORE "[i];
    while (k) s[i++] = d[--k];
    if (g_score) s[i++] = '0';
    s[i] = 0;
    g_scene = 2;
    for (i = 0; i < 120; i++) {
        buf_fill(C_BLACK);
        if (i & 16) {
            text_center(4, "GAME", C_RED, 2);
            text_center(16, "OVER", C_RED, 2);
        }
        text_center(29, "COASTER BROKEN", C_YELLOW, 1);
        text_center(38, s, C_WHITE, 1);
        present();
    }
}

/* 項目: 名前、最小、最大、値の名前(0 = 数字で出す) */
enum { CF_DAMAGE, CF_GOD, CF_STAGE, CF_LIFE, CF_ONESHOT, CF_ROUTE, CF_EASY, CF_FPS, CF_XFER, CF_COUNT };
static const char * const cf_name[CF_COUNT] = {
    "DAMAGE", "NO DAMAGE", "STAGE", "LIFE", "ONE SHOT", "ROUTE", "EASY", "FPS", "XFER",
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

void scm_config(void)
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
