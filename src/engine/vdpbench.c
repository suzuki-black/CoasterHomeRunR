/* vdpbench.c — VDP への書き込みの速さを、表示中・垂直帰線中・画面オフ・50Hz で測る(make vdpbench の ROM だけ)
 *
 * 「R800 の VDP への OUT が 1 回 約 62 サイクル(8.7µs)かかるのは、表示中だけの制限か」を確かめる。
 * どれも VRAM の空き(0x3000-)へ OTIR で 256 バイト書き、ターボR のシステムタイマ(E6h、3.911µs/カウント)で測る。
 * 垂直帰線は VDP の S#2 の VR ビットで見る。結果は画面に出す(openMSX・WebMSX・実機で同じ ROM で測れる)。
 *
 *   DISP   表示中に 256 バイト        1 バイトの時間(0.01µs 単位)
 *   VBL    垂直帰線中に 256 バイト     同上(帰線の長さより短い間に書き終わるとは限らないので BYTES も見る)
 *   OFF    画面オフ(R#1 の BL = 0)    同上
 *   V60    60Hz の垂直帰線の長さ(µs)と、その間に書ける見込みのバイト数
 *   V50    50Hz にしたときの同じ値
 */
#include "engine.h"
#include "hud.h"
#include "game.h"

#ifdef VDPBENCH

u16 vb_res[8];                    /* 0 DISP 1 VBL 2 OFF 3 V60 の長さ 4 V60 の帰線中 256B 5 V50 の長さ 6 V50 の帰線中 256B(タイマのカウント) */

/* S#2 を読む(bit6 = VR: 垂直帰線中) */
static u8 vdp_s2(void) __naked
{
    __asm
    ld  a, #2
    out (0x99), a
    ld  a, #0x8F
    out (0x99), a
    in  a, (0x99)
    ld  l, a
    xor a, a
    out (0x99), a
    ld  a, #0x8F
    out (0x99), a
    ld  a, l
    ret
    __endasm;
}

/* 割り込みを止めたまま VDP のレジスタ・書き込み先を設定する(vdp_wreg / vdp_set_write は最後に ei するので、計測中は使わない。
 * 計測の途中で割り込みが入ると、R#15 = 2 のまま割り込み処理が S#0 の代わりに S#2 を読み、割り込みが解除されずに止まる) */
static void wreg_di(u8 reg, u8 val) __naked
{
    (void)reg; (void)val;
    __asm
    ld  c, a
    ld  a, l
    out (0x99), a
    ld  a, c
    or  a, #0x80
    out (0x99), a
    ret
    __endasm;
}

static void setw_di(void) __naked
{
    __asm
    xor a, a
    out (0x99), a
    ld  a, #0x8E
    out (0x99), a                         ; R#14 = 0
    xor a, a
    out (0x99), a
    ld  a, #0x70
    out (0x99), a                         ; 0x3000 へ書き込み
    ret
    __endasm;
}

static void otir256(void) __naked
{
    __asm
    ld  hl, #0xC000
    ld  bc, #0x0098
    otir
    ret
    __endasm;
}

static void wait_vr(u8 on)
{
    if (on) { while (!(vdp_s2() & 0x40)) ; }
    else    { while (vdp_s2() & 0x40) ; }
}

/* 256 バイト書くのにかかるタイマのカウント */
static u16 meas_otir(void)
{
    u16 t0;
    setw_di();
    timer_reset();
    t0 = timer_read();
    otir256();
    return timer_read() - t0;
}

/* 垂直帰線の長さ(タイマのカウント) */
static u16 meas_vblank(void)
{
    u16 t0;
    wait_vr(0);
    wait_vr(1);
    timer_reset();
    t0 = timer_read();
    wait_vr(0);
    return timer_read() - t0;
}

static void run(void)
{
    __asm__("di");
    wait_vr(1); wait_vr(0);                  /* 表示の始まり */
    vb_res[0] = meas_otir();
    wait_vr(0); wait_vr(1);                  /* 垂直帰線の始まり */
    vb_res[1] = meas_otir();
    wreg_di(1, 0xA3);                        /* 画面オフ */
    vb_res[2] = meas_otir();
    wreg_di(1, 0xEB);
    vb_res[3] = meas_vblank();
    wait_vr(0); wait_vr(1);
    vb_res[4] = meas_otir();
    wreg_di(9, 0x02);                        /* 50Hz */
    vb_res[5] = meas_vblank();
    wait_vr(0); wait_vr(1);
    vb_res[6] = meas_otir();
    wreg_di(9, 0x00);
    __asm__("ei");
}

/* カウント → 1 バイトの時間(0.01µs): c × 391.1 / 256 */
static u16 per_byte(u16 c) { return (u16)(((u32)c * 3911) / 2560); }
/* カウント → µs */
static u16 us(u16 c) { return (u16)(((u32)c * 3911) / 1000); }

static void line(i8 y, const char *name, u16 v, u16 w)
{
    text_draw(1, y, name, C_YELLOW, 1);
    num_draw(17, y, v, 5, C_WHITE);
    if (w) num_draw(41, y, w, 5, C_CYAN);
}

void vdp_bench(void)
{
    u8 n = 0;
    for (;;) {
        if (!(n++ & 63)) run();              /* 約 2 秒ごとに測り直す */
        buf_fill(C_BLACK, BUF);
        text_draw(1, 1, "VDP OUT  .01US/B", C_ORANGE, 1);
        line(8,  "DISP", per_byte(vb_res[0]), 0);
        line(14, "VBL",  per_byte(vb_res[1]), 0);
        line(20, "OFF",  per_byte(vb_res[2]), 0);
        text_draw(1, 27, "VBLANK US  BYTES", C_ORANGE, 1);
        /* 帰線の長さと、帰線中の 1 バイトの時間から見積もる書けるバイト数 */
        line(34, "V60", us(vb_res[3]), vb_res[4] ? (u16)((u32)vb_res[3] * 256 / vb_res[4]) : 0);
        line(40, "V50", us(vb_res[5]), vb_res[6] ? (u16)((u32)vb_res[5] * 256 / vb_res[6]) : 0);
        present();
    }
}

#endif
