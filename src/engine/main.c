/* main.c — 初期化・表示(ダブルバッファ)・タイトル
 *
 * 表示の流れ(仕様書 7.4 の 6〜7):
 *   描画バッファ(RAM)に描く → 前回の切替が済むのを待つ → 2 ティック(30fps)経つのを待つ
 *   → 裏のパターンジェネレータへ転送 → 切替を予約(次の垂直帰線で ISR が R#4 を書く)
 */
#include "game.h"
#include "hud.h"
#include "sound.h"

u8 g_spr_attr[17];
u8 g_perf_ticks;
u16 g_perf_render;
u16 g_perf_xfer;
u16 g_perf_env;
u8 g_scene;
u8 g_stage;                      /* 遊んでいるステージ(1 から) */

static u8 back_page = 1;
static u16 last_flip;
static const u8 *stage_pal;
static u8 pal_state;             /* 0 = そのまま / 1 = 赤く光らせる / 2 = 元に戻す */

/* 固定色 0-8(仕様書 6.2)。(0RRR0BBB, 00000GGG) */
const u8 pal_fixed[] = {
    0x00, 0,        /* 0 透明 */
    0x00, 0,        /* 1 黒 */
    0x77, 7,        /* 2 白 */
    0x71, 1,        /* 3 赤 */
    0x51, 3,        /* 4 茶 */
    0x56, 2,        /* 5 紫 */
    0x71, 7,        /* 6 黄 */
    0x70, 4,        /* 7 橙 */
    0x37, 6,        /* 8 水色 */
};
static const u8 pal_red[14] = {
    0x71, 1, 0x60, 0, 0x71, 1, 0x60, 0, 0x71, 1, 0x60, 0, 0x71, 1 };

/* タイトル用の 9-15 */
const u8 pal_title[14] = {
    0x31, 2, 0x22, 2, 0x33, 3, 0x23, 2, 0x12, 1, 0x31, 2, 0x21, 1 };

void set_stage_palette(const u8 *pal)
{
    stage_pal = pal;
    vdp_set_palette(9, pal);
}

void flash_palette(void)
{
    pal_state = 1;
}

/* 1 フレームを表示に回す。
 *   前回の切替(垂直帰線)が済むのを待つ → 裏ページへ転送 → 切替を予約
 * 切替は ISR が 2 ティック間隔に揃える(30fps が上限) */
void present(void)
{
    u16 now, t0;
    while (g_flip_req) wait_tick();
    now = g_tick;
    g_perf_ticks = (u8)(now - last_flip);
    last_flip = now;
    /* 被弾フラッシュ: 1 フレームだけ 9-15 を赤系にする(仕様書 3.1) */
    if (pal_state == 1) { vdp_set_palette(9, pal_red); pal_state = 2; }
    else if (pal_state == 2) { vdp_set_palette(9, stage_pal); pal_state = 0; }
    timer_reset();
    t0 = timer_read();
    vdp_xfer(back_page);
    g_perf_xfer = timer_read() - t0;
    g_flip_page = back_page;
    g_flip_req = 1;
    back_page ^= 1;
}

static void vdp_init(void)
{
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
    vdp_set_palette(9, pal_title);
    stage_pal = pal_title;
    vdp_wreg(1, 0xEB);             /* 表示オン、垂直帰線割り込み */
}

/* エンディング(仕様書 5.2): SCREEN 1 に切り替え、BIOS のフォント(ひらがな)で最後のステージの ending の文を出す。
 * 約 8 秒後(1 秒過ぎたら A でも)タイトルへ。戻るときに SCREEN 3 を作り直す */
static void ending_run(const Stage *st)
{
    static const u8 regs[] = {
        1, 0xA2,        /* 表示オフ、16×16(スプライトは隠す) */
        0, 0x00,        /* GRAPHIC 1 */
        2, 0x06,        /* ネームテーブル 1800h */
        3, 0x80,        /* カラーテーブル 2000h */
        4, 0x00,        /* パターンジェネレータ 0000h */
        7, 0x01,        /* 背景 = 黒 */
    };
    const u8 *p = st->ending;
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
    /* 文: (行, 桁, 文字…, 0) の並び。行の bit7 が立っていれば「残った宝の量で選ぶ行」で、
     * bit5-6 = 0: 宝が多い(ライフ 6 以上)/ 1: そこそこ(3〜5)/ 2: 少ない(1〜2)のときだけ出す */
    v = (life >= 6) ? 0 : (life >= 3) ? 1 : 2;
    while (p && *p != 0xFF) {
        u8 r = *p++, c = *p++;
        const u8 *q = p;
        while (*p) p++;
        if (!(r & 0x80) || ((r >> 5) & 3) == v) {
            vdp_set_write(0x1800 + (u16)(r & 0x1F) * 32 + c);
            vdp_out(q, (u16)(p - q));
        }
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
    if (st->end_melody) bgm_play(st->end_melody, st->end_bass);
    for (t = 0; t < 480; t++) {
        wait_tick();
        in = input_raw();
        if (t > 60 && (in & IN_A) && !(prev & IN_A)) break;
        prev = in;
    }
    bgm_stop();
    vdp_init();
}

#ifdef BENCH
void bench_run(void);
#endif

/* ステージデータを ROM バンクから読み込む(仕様書 7.3) */
static const Stage *stage_load(u8 bank)
{
    rom_load(bank, (void *)STAGE_BUF);
    return *(const Stage * const *)STAGE_BUF;
}

/* ステージの始めに名前を出す(約 1.5 秒) */
static void stage_card(const Stage *st)
{
    u8 t;
    for (t = 0; t < 45; t++) {
        buf_fill(C_BLACK, BUF);
        if (t >= 2) {
            text_center(16, st->name, C_YELLOW, 1);
            text_center(24, st->title2, C_WHITE, 1);
        }
        present();
        if (t == 1) set_stage_palette(pal_title);   /* 前の画面が消えてから */
    }
}

/* ステージ 1 から順に遊ぶ。ライフとスコアはステージをまたいで持ち越す */
static void play(void)
{
    u8 n, n0 = g_cfg.stage - 1;         /* 隠しコンフィグのステージセレクト */
    const Stage *st = 0;
    for (n = n0; n < STAGE_COUNT; n++) {
        st = stage_load(STAGE1_BANK + n * 2);
        g_stage = n + 1;
        stage_card(st);
        if (!game_run(st, n == n0)) return;
    }
    g_score += (u16)life * 100;          /* 残りライフ × 1000 点(仕様書 5.2) */
    g_scene = 3;
    ending_run(st);                      /* 最後のステージのデータ(エンディングの文と BGM)は読み込んだまま */
}

void main(void)
{
    g_buf_hi = BUF_A_HI;
    sys_install_loader();
    sys_install_isr();
    sys_page0_ram();
    sys_page0_data();                /* 表(sin・逆数・フォント・敵の絵など)とタイトル・コンフィグをページ0へ */
    cfg_defaults();
#ifdef BENCH
    bench_run();
#endif
    sound_init();
    vdp_init();
    for (;;) {
        title();
        play();
    }
}
