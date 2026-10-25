/* main.c — 初期化・表示(ダブルバッファ)・タイトル
 *
 * 表示の流れ(仕様書 7.4 の 6〜7):
 *   描画バッファ(RAM)に描く → 前回の切替が済むのを待つ → 2 ティック(30fps)経つのを待つ
 *   → 裏のパターンジェネレータへ転送 → 切替を予約(次の垂直帰線で ISR が R#4 を書く)
 */
#include "game.h"
#include "hud.h"
#include "sound.h"
#include "pcm.h"
#include "title.h"

u8 g_spr_attr[17];
u8 g_perf_ticks;
u16 g_perf_render;
u16 g_perf_xfer;
u16 g_perf_env;
u8 g_scene;
u8 g_stage;                      /* 遊んでいるステージ(1 から) */

static u8 back_page = 1;
static u16 last_flip;
const u8 *stage_pal;              /* いまの 9-15 の色(場面のモジュールの vdp_init も書く) */
u8 g_end_bgm;
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

/* ---------------------------------------------------------------------------
 * タイトルの絵(2026-10-22)。SCREEN 11(V9958 の YJK + YAE: 自然画に、パレットの色の文字を重ねる)。
 * 絵は tools/gen_title.py が data/title_art.webp から作り、ROM のバンク TITLE_BANK〜 に置く(1 ページ目 54272 バイトと、
 * 文字の無い PUSH SPACE KEY の帯)。16KB ずつページ2(ゲームのセグメント。ステージを始めるときに読み直す)へ写して VRAM へ書く。
 * 1 ページ目(0x00000-)は文字あり、2 ページ目(0x10000-)は PUSH SPACE KEY の帯だけ文字無し。表示するページを替えて点滅させる。
 * 戻るときは vdp_init で SCREEN 3 に戻す
 * --------------------------------------------------------------------------- */
#define TITLE_BANK 32
#define TITLE_PAGE 54272u                 /* 1 ページ(256 × 212) */
static const u8 title_pal[32] = TITLE_PAL;

void title_show(void)
{
    static const u8 regs[] = {
        1, 0x20,        /* 表示オフ(書いている間)、垂直帰線割り込み */
        0, 0x0E,        /* G7(SCREEN 8 の並び) */
        8, 0x0A,        /* スプライトを出さない */
        9, 0x80,        /* 212 ライン */
        2, 0x1F,        /* 1 ページ目 */
        7, 0x00,
        25, 0x18,       /* YJK + YAE = SCREEN 11 */
    };
    u8 i;
    u16 len;
    while (g_flip_req) wait_tick();      /* SCREEN 3 の表示の切り替え(割り込みが R#2 を書く)を済ませてから */
    for (i = 0; i < sizeof(regs); i += 2) vdp_wreg(regs[i], regs[i + 1]);
    vdp_set_palette(0, title_pal);
    sys_seg2(SEG_GAME);
    for (i = 0; i < 4; i++) {
        len = (i < 3) ? 0x4000 : (u16)(TITLE_PAGE - 0xC000);
        rom_load(TITLE_BANK + i * 2);
        vdp_set_write_hi(i, 0);           /* 1 ページ目 */
        vdp_out((const u8 *)0x8000, len);
        vdp_set_write_hi(4 + i, 0);       /* 2 ページ目 */
        vdp_out((const u8 *)0x8000, len);
    }
    /* 2 ページ目の PUSH SPACE KEY の帯を、文字の無い絵に(最後に写した 16KB の、1 ページ目の後ろ) */
    vdp_set_write_hi((u8)((0x10000ul + TITLE_BAND_Y * 256u) >> 14), (u16)(TITLE_BAND_Y * 256u) & 0x3FFF);
    vdp_out((const u8 *)0x8000 + (TITLE_PAGE - 0xC000), TITLE_BAND_LEN);
    vdp_wreg(1, 0x60);                    /* 表示オン */
}

void title_blink(u8 on)
{
    vdp_wreg(2, on ? 0x1F : 0x3F);
}

#ifdef BENCH
void bench_run(void);
#endif

/* ステージ n(1〜)の地形モジュールとゲームモジュールを ROM から読み込み、その Stage を返す(仕様書 7.3 のオーバーレイ) */
static const Stage *stage_load(u8 n)
{
    sys_seg2(SEG_ENV);
    mod_load(ENV_BANK(n));
    sys_seg2(SEG_GAME);
    mod_load(GAME_BANK(n));
    return MOD_STAGE;
}

/* ステージの始めに名前を出す(約 1.5 秒) */
static void stage_card(const Stage *st)
{
    u8 t;
    for (t = 0; t < 45; t++) {
        buf_fill(C_BLACK);
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
        st = stage_load(n + 1);
        g_stage = n + 1;
        stage_card(st);
        if (!MOD_GAME_RUN(st, n == n0)) { gameover_show(); return; }
    }
    g_score += (u16)life * 100;          /* 残りライフ × 1000 点(仕様書 5.2) */
    g_scene = 3;
    g_end_bgm = st->end_bgm;             /* エンドロール(scene.c。文とクレジットは場面のモジュールにある) */
    ending_show();
}

void main(void)
{
    g_buf_hi = BUF_A_HI;
    sys_install_loader();
    sys_install_isr();
    sys_page0_ram();
    sys_page0_data();                /* 表(sin・逆数・フォント・敵の絵など)とタイトル・コンフィグをページ0へ */
    sys_z80x_init();                 /* 実機のファームウェアなら描画バッファを Z80 で送る(vdp.s の vdp_xfer) */
    cfg_defaults();
    sys_seg2(SEG_SCENE);             /* 場面のモジュール(エンディング・ゲームオーバー・隠しコンフィグ・SCREEN 3 の作り直し)をセグメント 10 へ */
    mod_load(SCENE_BANK);
    sys_seg2(SEG_SND);               /* 音のモジュール(効果音・曲のドライバと FM の曲)をセグメント 8 へ */
    mod_load(SND_BANK);
    g_snd_ok = 1;                    /* ここから割り込みが音の処理を呼ぶ */
    sys_seg2(SEG_VOICE);             /* 声のモジュール(内蔵 PCM の英語の声)をセグメント 9 へ */
    mod_load(VOICE_BANK);
    sys_seg2(SEG_GAME);
#ifdef BENCH
    bench_run();
#endif
    sound_init();
    vdp_init();
#ifdef VDPBENCH
    {   /* VDP への書き込みの速さの計測用 ROM(make vdpbench)。結果を画面に出し続ける */
        void vdp_bench(void);
        vdp_bench();
    }
#endif
    for (;;) {
        title();
        play();
    }
}
