/* vdpbench.c — VDP への書き込みの速さを、表示中・垂直帰線中・画面オフ・50Hz で測る(make vdpbench の ROM だけ)
 *
 * 「R800 の VDP への OUT が 1 回 約 62 サイクル(8.7µs)かかるのは、表示中だけの制限か」を確かめる。
 * どれも VRAM の空き(0x3000-)へ OTIR で 256 バイト書き、ターボR のシステムタイマ(E6h、3.911µs/カウント)で測る。
 * 垂直帰線は VDP の S#2 の VR ビットで見る。結果は画面に出す(openMSX・WebMSX・実機で同じ ROM で測れる)。
 *
 *   DISP   表示中に 256 バイト        1 バイトの時間(0.01µs 単位)
 *   VBL    垂直帰線中に 256 バイト     同上(帰線の長さより短い間に書き終わるとは限らないので BYTES も見る)
 *   OFF    画面オフ(R#1 の BL = 0)    同上
 *   V60    60Hz の垂直帰線の長さ(µs)と、その間に書ける見込みのバイト数(左 µs / 右 バイト)
 *   V50    50Hz にしたときの同じ値
 *   ZERR   Z80 の OUTI の展開で表示中に書いた 256 バイトを読み戻して食い違ったバイトの数(0 なら取りこぼしていない)
 *
 * 右の列は Z80 モードに切り替えて測った同じ値(S1990 の待ちが R800 だけのものかを見る)。OFF の行の右は
 * CPU の切り替えの往復(R800 → Z80 → R800)にかかる時間(µs)。ZOUTI は Z80 で OUTI を展開して書いたとき(左 表示中 / 右 帰線中)。切り替えは BIOS の CHGCPU を使うので、
 * MSXVER < 3(C-BIOS など)では測らない(右の列が出ない)。
 */
#include "engine.h"
#include "hud.h"
#include "game.h"

#ifdef VDPBENCH

u16 vb_res[14];                   /* 0 DISP 1 VBL 2 OFF 3 V60 の長さ 4 V60 の帰線中 256B 5 V50 の長さ 6 V50 の帰線中 256B
                                     7 Z80 の DISP 8 Z80 の VBL 9 切り替えの往復 10 Z80 の DISP(OUTI の展開) 11 Z80 の VBL(OUTI の展開)
                                     (タイマのカウント。7-11 は 0 = 測っていない)
                                     12 Z80 の OUTI の展開(表示中)で書いた 256 バイトを読み戻して食い違ったバイトの数
                                     13 CHGCPU から戻った直後に割り込みが許可されていたら 1 */

/* BIOS の CHGCPU(0180h)で CPU を切り替える(A = 0x80: Z80 / 0x82: R800 DRAM)。戻り値 0 = BIOS が CHGCPU を持たない。
 * ページ0は自前の RAM にしているので、呼ぶ間だけ BIOS に戻す(sys.s の P0_TO_BIOS / P0_TO_RAM と同じ手順) */
u8 vb_iff;                        /* CHGCPU から戻ったとき割り込みが許可されていたら 1 */

u8 vb_rd;

static u8 cpu_switch(u8 mode) __naked
{
    (void)mode;
    __asm
    ld  c, a
    di
    in  a, (0xA8)
    and a, #0xFC
    ld  b, a
    ld  a, (0xE0F2)                       ; P0_BIOS
    or  a, b
    out (0xA8), a
    ld  a, (0x002D)                       ; MSXVER
    cp  a, #3
    ld  a, #0
    jr  c, 1$
    ld  a, c
    call    0x0180                        ; CHGCPU
    ld  a, i                              ; P/V = IFF2(割り込みが許可されていたか)
    di
    ld  a, #1
    jp  po, 3$
    ld  (_vb_iff), a
3$:
1$:
    ld  c, a
    in  a, (0xA8)
    and a, #0xFC
    ld  b, a
    ld  a, (0xE0F1)                       ; V_RAM
    and a, #0x03
    or  a, b
    out (0xA8), a
    ld  a, (0xE0F1)
    bit 7, a
    jr  z, 2$
    rrca
    rrca
    and a, #0x03
    ld  b, a
    ld  a, (0xFFFF)
    cpl
    and a, #0xFC
    or  a, b
    ld  (0xFFFF), a
2$:
    ld  a, c
    ret
    __endasm;
}

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

/* OUTI を 256 個並べたもの(Z80 では OTIR より速い: OUTI 16T + M1 の待ち、OTIR は 21T + M1 の待ち) */
static void outi256(void) __naked
{
    __asm
    ld  hl, #0xC000
    ld  c, #0x98
    .rept 256
    outi
    .endm
    ret
    __endasm;
}

/* VRAM 0x3000 から 256 バイト読み戻し、0xC000 と食い違うバイトの数を返す(R800 で読むので取りこぼさない) */
static u16 verify256(void)
{
    u16 i, bad = 0;
    const u8 *src = (const u8 *)0xC000;
    wreg_di(14, 0);
    __asm
    xor a, a
    out (0x99), a
    ld  a, #0x30
    out (0x99), a                         ; 0x3000 から読み出し
    __endasm;
    for (i = 0; i < 256; i++) {
        u8 v;
        __asm
        in  a, (0x98)
        ld  (_vb_rd), a
        __endasm;
        v = vb_rd;
        if (v != src[i]) bad++;
    }
    return bad;
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

static u16 meas_outi(void)
{
    u16 t0;
    setw_di();
    timer_reset();
    t0 = timer_read();
    outi256();
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
    /* Z80 モード(計測中は C のうち R800 固有の命令を使わない処理だけを呼ぶ) */
    {
        u16 t0;
        timer_reset();
        t0 = timer_read();
        if (cpu_switch(0x80)) {
            cpu_switch(0x82);
            vb_res[9] = timer_read() - t0;
            cpu_switch(0x80);
            wait_vr(1); wait_vr(0);
            vb_res[7] = meas_otir();
            wait_vr(0); wait_vr(1);
            vb_res[8] = meas_otir();
            wait_vr(0); wait_vr(1);
            vb_res[11] = meas_outi();
            wait_vr(1); wait_vr(0);
            vb_res[10] = meas_outi();             /* 表示中に書いたものを、R800 に戻ってから読み戻す */
            cpu_switch(0x82);
            vb_res[12] = verify256();
            vb_res[13] = vb_iff;
        }
    }
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
        text_draw(1, 1, ".01US/B R800 Z80", C_ORANGE, 1);
        line(7,  "DISP", per_byte(vb_res[0]), per_byte(vb_res[7]));
        line(13, "VBL",  per_byte(vb_res[1]), per_byte(vb_res[8]));
        line(19, "OFF",  per_byte(vb_res[2]), us(vb_res[9]));    /* 右: 切り替えの往復(µs) */
        line(25, "ZOUTI", per_byte(vb_res[10]), per_byte(vb_res[11]));   /* Z80 の OUTI の展開: 左 表示中 / 右 帰線中 */
        if (vb_res[10]) line(31, "ZERR", vb_res[12], 0);   /* Z80 で表示中に書いて読み戻した食い違い(0 であること) */
        /* 帰線の長さと、帰線中の 1 バイトの時間から見積もる書けるバイト数 */
        line(37, "V60", us(vb_res[3]), vb_res[4] ? (u16)((u32)vb_res[3] * 256 / vb_res[4]) : 0);
        line(43, "V50", us(vb_res[5]), vb_res[6] ? (u16)((u32)vb_res[5] * 256 / vb_res[6]) : 0);
        present();
    }
}

#endif
