/* scene.c — 場面のモジュール(マッパーのセグメント 10。ゲームの最中には動かない画面)
 *
 * エンディング・ゲームオーバー・隠しコンフィグ・SCREEN 3 の作り直し(vdp_init)。常駐部の置き場を空けるため、地形モジュールや
 * 音のモジュールと同じくページ2のモジュールにした(2026-10-23)。起動時に 1 回読み込み、常駐部の中継(sys.s の vdp_init・
 * ending_show・gameover_show・config_show)がページ2をこのセグメントにして呼び、元のセグメントに戻す。
 * ページ2で動くので、ステージのデータ(ゲームモジュール)は読めない(エンドロールの曲の番号は play が g_end_bgm へ写す)。
 * タイトルの絵の読み込み(title_show)はページ2を使うので常駐部のまま。初期値つきの変数は置けない。
 */
#include "game.h"
#include "hud.h"
#include "sound.h"
#include "pcm.h"

extern const u8 pal_fixed[], pal_title[];
extern u8 g_end_bgm;
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

/* エンディング(エンドロール。2026-10-25 ユーザーの指定で英語にし、クレジットを流す): SCREEN 1 に切り替え、BIOS のフォントで
 * 結びの文とスタッフのクレジットを下から上へ流す(VDP の縦スクロール R#23 で 1 ドットずつ。名前表は 1800h-1BFFh の 32 行を
 * 輪にして使い、画面に入る直前の行に次の文を書く)。流し終えたら PUSH SPACE KEY を点滅させて待ち、スペースでタイトルへ。
 * 流れている間にスペースを押すと最後まで飛ばす。曲はエンディングの曲をくり返す */
#define ROLL_STEP 4                          /* 1 ドット流すティック数(1 秒に 15 ドット。2 だと速すぎた) */
static const char * const roll[] = {
    "The young man brought the",
    "village treasure back home.",
    "",
    "And the village went back",
    "to its peaceful days.",
    "", "", "",
    "COASTER HOME RUN",
    "",
    "- STAFF -",
    "",
    "ORIGINAL CONCEPT / DIRECTION",
    "suzuki-black",
    "",
    "PROGRAM / GRAPHICS",
    "Claude Code (Anthropic Claude)",
    "",
    "MUSIC / SOUND EFFECTS",
    "Claude Code (Anthropic Claude)",
    "",
    "VOICES",
    "Flite (Carnegie Mellon Univ.)",
    "",
    "TITLE ILLUSTRATION",
    "Microsoft Copilot",
    "x Claude Code",
    "", "", "",
    "THANK YOU FOR PLAYING!",
    "",
    "\x01",                                  /* スコア */
    "",
    "(C) 2026 suzuki-black",
    "", "",
    "PUSH SPACE KEY",
};
#define ROLL_N (sizeof(roll) / sizeof(roll[0]))
static char roll_score[13];

/* 輪の行 k(0〜)に、文の k - 24 行目(範囲の外は空白)を中央にそろえて書く。blank なら空白だけ */
static void roll_row(u16 k, u8 blank)
{
    u8 row[32], i, n;
    const char *t = "";
    i16 li = (i16)k - 24;
    if (!blank && li >= 0 && li < (i16)ROLL_N) t = (roll[li][0] == 1) ? roll_score : roll[li];
    for (i = 0; i < 32; i++) row[i] = ' ';
    for (n = 0; t[n]; n++) ;
    for (i = 0; i < n; i++) row[(32 - n) / 2 + i] = (u8)t[i];
    vdp_set_write(0x1800 + (u16)(k & 31) * 32);
    vdp_out(row, 32);
}

void scm_ending(void)
{
    static const u8 regs[] = {
        1, 0xA2,        /* 表示オフ、16×16(スプライトは隠す) */
        0, 0x00,        /* GRAPHIC 1 */
        2, 0x06,        /* ネームテーブル 1800h */
        3, 0x80,        /* カラーテーブル 2000h */
        4, 0x00,        /* パターンジェネレータ 0000h */
        7, 0x01,        /* 背景 = 黒 */
        23, 0x00,       /* 縦スクロール */
    };
    const u16 y_end = (u16)(ROLL_N - 1 + 4) * 8;   /* PUSH SPACE KEY が画面の 20 行目に来たら止める */
    u16 y = 0, t = 0;
    u8 i, k = 0, prev = 0xFF, in, d[5];
    u8 row[32];
    u16 sc = g_score;

    while (g_flip_req) wait_tick();
    for (i = 0; i < 16; i += 4) g_spr_attr[i] = 209;
    for (i = 0; i < sizeof(regs); i += 2) vdp_wreg(regs[i], regs[i + 1]);
    bios_font(BUF);
    {   /* 太字: 各行を 1 ドット右へずらして重ねる(BIOS のそのままのフォントは細い。2026-10-25 ユーザーの指定) */
        u8 *f = BUF;
        u16 j;
        for (j = 0; j < 2048; j++, f++) *f |= *f >> 1;
    }
    vdp_set_write(0x0000);
    vdp_out(BUF, 2048);
    for (i = 0; i < 32; i++) row[i] = 0x21;          /* 文字 = 白(2)、背景 = 黒(1)。8 文字ごとに 1 バイト */
    row[6] = row[7] = 0x61;                            /* 数字(30h-3Fh)は黄色 */
    vdp_set_write(0x2000);
    vdp_out(row, 32);
    vdp_set_write(0x1800);
    vdp_fill(0x20, 1024);
    vdp_set_write(0x1400);                             /* スプライトを出さない(形の表 1800h が名前の表と重なり、流す文字が形として出た) */
    vdp_fill(0xD0, 1);
    /* 「SCORE 224600」(内部は 1/10 なので末尾に 0) */
    do { d[k++] = (u8)('0' + sc % 10); sc /= 10; } while (sc);
    for (i = 0; i < 6; i++) roll_score[i] = "SCORE "[i];
    while (k) roll_score[i++] = (char)d[--k];
    if (g_score) roll_score[i++] = '0';
    roll_score[i] = 0;
    vdp_wreg(1, 0xE2);                                 /* 表示オン、垂直帰線割り込み */
    if (g_end_bgm) bgm_play(g_end_bgm);
    for (;;) {
        wait_tick();
        t++;
        in = input_raw();
        if ((in & IN_A) && !(prev & IN_A)) {
            if (y >= y_end) break;                     /* 流し終えていればタイトルへ */
            y = y_end;                                 /* 流れている間なら最後まで飛ばす */
            for (i = 0; i < 25; i++) roll_row((y >> 3) + i, 0);
            vdp_wreg(23, (u8)y);
        }
        prev = in;
        if (y < y_end) {
            if (!(t % ROLL_STEP)) {
                if (!(y & 7)) roll_row((y >> 3) + 24, 0);   /* 画面の下に入る直前の行に次の文を書く */
                y++;
                vdp_wreg(23, (u8)y);
            }
        } else if (!(t & 31)) {
            roll_row(ROLL_N - 1 + 24, !(t & 32));      /* PUSH SPACE KEY の点滅 */
        }
    }
    bgm_stop();
    vdp_wreg(23, 0);
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

/* 耐久が 0 になったとき、止まった画面に CONTINUE? を重ねる。続けるなら耐久を満タンに、誘導弾を始めの数まで戻し、スコアを 0 にして 1 を返す。
 * 撃ち続けていた指で選ばないように、0.75 秒は入力を受けない */
u8 scm_continue(void)
{
    char s[13];
    u8 i, in, edge, prev = 0xFF;
    u16 t0 = g_tick;
    if (!g_cont_left) return 0;
    for (i = 0; i < 10; i++) s[i] = "CONTINUE? "[i];
    if (g_cont_left >= 10) s[i++] = (char)('0' + g_cont_left / 10);
    s[i++] = (char)('0' + g_cont_left % 10);
    s[i] = 0;
    g_color = C_BLACK; g_rx = 6; g_ry = 12; g_rw = 52; g_rh = 18;
    rect_fill();
    g_spr_attr[0] = 209;                 /* 照準を文字に重ねない */
    g_spr_attr[4] = 209;
    text_center(14, s, C_YELLOW, 1);
    text_center(23, "A YES  B NO", C_WHITE, 1);
    for (;;) {
        present();
        in = input_raw();
        edge = in & ~prev;
        prev = in;
        if ((u16)(g_tick - t0) < 45) continue;
        if (edge & (IN_A | IN_KEY_A)) break;
        if (edge & IN_B) return 0;
    }
    g_cont_left--;
    life = 8;
    if (mis_left < MIS_START) mis_left = MIS_START;
    g_score = 0;
    return 1;
}

/* 項目: 名前、最小、最大、値の名前(0 = 数字で出す) */
enum { CF_DAMAGE, CF_GOD, CF_STAGE, CF_LIFE, CF_ONESHOT, CF_ROUTE, CF_EASY, CF_FPS, CF_XFER, CF_CONT, CF_COUNT };
static const char * const cf_name[CF_COUNT] = {
    "DAMAGE", "NO DAMAGE", "STAGE", "LIFE", "ONE SHOT", "ROUTE", "EASY", "FPS", "XFER", "CONTINUE",
};
static const u8 cf_min[CF_COUNT] = { 1, 0, 1, 1, 0, 0, 0, 0, 0, 0 };
static const u8 cf_max[CF_COUNT] = { 8, 1, STAGE_COUNT, 8, 1, 2, 2, 1, 1, 99 };
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
    u8 sel = 0, i, prev = 0xFF, in, edge, top;
    u8 *v;
    for (;;) {
        buf_fill(C_BLACK);
        top = (sel > 8) ? sel - 8 : 0;                   /* 画面に入るのは 9 項目。下の項目を選ぶと 1 行ずつ送る */
        for (i = top; i < top + 9; i++) {
            /* 9 項目 × 5 行で画面いっぱい(行の間が空かないので、選んでいない項目は白と水色を交互に) */
            u8 y = 2 + (i - top) * 5, c = (i == sel) ? C_YELLOW : (i & 1) ? C_CYAN : C_WHITE;
            const char *s = 0;
            v = cf_val(i);
            text_draw(2, (i8)y, cf_name[i], c, 1);
            if (i == CF_GOD || i == CF_ONESHOT || i == CF_FPS) s = v_onoff[*v];
            else if (i == CF_ROUTE) s = v_route[*v];
            else if (i == CF_EASY) s = v_easy[*v];
            else if (i == CF_XFER) s = v_xfer[*v];
            if (s) text_draw(42, (i8)y, s, c, 1);
            else num_draw(42, (i8)y, *v, (i == CF_CONT) ? 2 : 1, c);
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
