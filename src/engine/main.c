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
static const u8 pal_title[14] = {
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

/* 仮エンディング(森を抜けた後)。本エンディングは SCREEN 1 のひらがなで作る(仕様書 5.2) */
void ending_run(void)
{
    u16 t;
    u8 prev = 0xFF, in;
    for (t = 0; t < 400; t++) {
        buf_fill(C_BLACK, BUF);
        text_center(2, "FOREST CLEARED!", C_YELLOW, 1);
        text_center(11, "THE TOWN", C_WHITE, 1);
        text_center(17, "IS NEAR...", C_WHITE, 1);
        text_center(26, "SCORE", C_CYAN, 1);
        num_draw(18, 32, g_score, 5, C_WHITE);
        if (g_score) text_draw(38, 32, "0", C_WHITE, 1);
        if (t & 32) text_center(41, "TO BE CONTINUED", C_ORANGE, 1);
        present();
        if (t == 1) set_stage_palette(pal_title);   /* 前の画面が消えてから(一瞬でも前の画面の色が変わらないように) */
        in = input_raw();
        if (t > 60 && (in & IN_A) && !(prev & IN_A)) break;
        prev = in;
    }
}

static void title(void)
{
    u16 t = 0;
    u8 prev = 0xFF, in;
    g_scene = 0;
    set_stage_palette(pal_title);
    g_spr_attr[0] = 209;
    g_spr_attr[4] = 209;
    for (;;) {
        buf_fill(C_BLACK, BUF);
        g_rx = 0; g_ry = 30; g_rw = 64; g_rh = 1; g_color = 9; rect_fill();
        text_center(6, "TROLLEY", C_YELLOW, 2);
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
        if ((in & IN_A) && !(prev & IN_A)) break;
        prev = in;
    }
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
    u8 n;
    for (n = 0; n < STAGE_COUNT; n++) {
        const Stage *st = stage_load(STAGE1_BANK + n * 2);
        g_stage = n + 1;
        stage_card(st);
        if (!game_run(st, n == 0)) return;
    }
    g_score += (u16)life * 100;          /* 残りライフ × 1000 点(仕様書 5.2) */
    g_scene = 3;
    ending_run();
}

void main(void)
{
    g_buf_hi = BUF_A_HI;
#ifdef BENCH
    bench_run();
#endif
    sys_install_loader();
    sys_install_isr();
    sys_page0_ram();
    sound_init();
    vdp_init();
    for (;;) {
        title();
        play();
    }
}
