/* engine.h — エンジン共通の型・定数・アセンブラ関数の宣言 */
#ifndef ENGINE_H
#define ENGINE_H

typedef unsigned char  u8;
typedef signed char    i8;
typedef unsigned short u16;
typedef signed short   i16;
typedef unsigned long  u32;
typedef signed long    i32;

/* ---- メモリ配置(仕様書 7.3) ---- */
extern u8 g_buf_hi;                 /* 描画バッファの上位バイト(gfx.s)。いまは常に 0xC0 */
#define BUF        ((u8 *)((u16)g_buf_hi << 8))   /* 描画バッファ: 64×48 を VRAM と同じ並びに詰めた 1536 バイト(gfx.s)。
                                                   書くのは gfx.s / raster.s の関数だけ。エンディングでは 2KB の一時領域にも使う */
#define BUF_A_HI   0xC0
#define GAME_ROWS  46               /* 3D ゲーム画面の行数(46-47 はライフゲージ) */
#define SLOT_CART  (*(u8 *)0xE0F0)  /* ローダーが書く(src/boot/boot.s の V_CART) */
#define SLOT_RAM   (*(u8 *)0xE0F1)

/* ---- パレット番号(仕様書 6.2) ---- */
#define C_CLEAR  0
#define C_BLACK  1
#define C_WHITE  2
#define C_RED    3
#define C_BROWN  4
#define C_PURPLE 5
#define C_YELLOW 6
#define C_ORANGE 7
#define C_CYAN   8

/* ---- 入力ビット(sys.s input_raw) ---- */
#define IN_UP    0x01
#define IN_DOWN  0x02
#define IN_LEFT  0x04
#define IN_RIGHT 0x08
#define IN_A     0x10
#define IN_B     0x20
#define IN_KEY_A 0x40               /* キーボードの A キー(隠しコマンド用) */
#define IN_KEY_B 0x80               /* キーボードの B キー(IN_B も立つ) */

/* ---- sys.s ---- */
extern volatile u16 g_tick;
extern volatile u8  g_flip_req;
extern volatile u8  g_flip_page;
void sys_install_isr(void);
void wait_tick(void);
u8   input_raw(void);
void psg_write(u8 reg, u8 val);
void timer_reset(void);
u16  timer_read(void);
u8   sys_cpu_status(void);          /* S1990 レジスタ 6(bit5 = 0 で R800、bit6 = 0 で DRAM モード) */
void sys_install_loader(void);
void sys_page0_ram(void);           /* ページ0を自前の RAM に(0x0038 に割り込み処理) */
void mod_load(u8 bank);             /* ROM の bank, bank+1(16KB)をページ2(いまのセグメント)の 0x8000- へ写す */
void sys_seg2(u8 seg);              /* ページ2のマッパーのセグメントを切り替える */
void sys_page0_data(void);          /* ページ0の表とコード(エリア _PAGE0 = 0x1A00-)を ROM のバンク 1 から読み込む */
void bios_font(u8 *dst);            /* BIOS のフォント 2KB を dst へ(ページ2・3) */
extern u8 g_z80x;                   /* 1 = 描画バッファを Z80 に切り替えて送る */
extern u8 g_z80x_ok;                /* 1 = ファームウェアに CHGCPU がある(Z80 に切り替えられる) */
void sys_z80x_init(void);           /* g_z80x_ok と g_z80x を決める(起動時に 1 回) */

/* ---- vdp.s ---- */
void vdp_wreg(u8 reg, u8 val);
void vdp_set_write(u16 addr);
void vdp_out(const u8 *src, u16 len);
void vdp_fill(u8 val, u16 len);
void vdp_set_palette(u8 first, const u8 *pal);
void vdp_xfer(u8 page);
void xfer_invalidate(void);         /* 下の帯の写しを捨てる(VRAM を vdp_xfer 以外で書き換えたとき) */

/* ---- gfx.s ---- */
extern u8  g_color;
extern u8  g_rx, g_ry, g_rw, g_rh;
void buf_fill(u8 color);            /* バッファ全体(48 行) */
void buf_fill_rows(u8 color, u8 row);   /* row から g_rh 行 */
void rect_fill(void);               /* g_rx, g_ry, g_rw, g_rh(クリップ済み)を g_color で */
void pset(void);                    /* (g_rx, g_ry) の 1 ブロックを g_color に(画面内であること) */

/* ---- raster.s ---- */
extern i16 scr[20];                 /* 多角形の画面座標 (x, y) × n(12.4) */
void raster(u8 n, u8 color);
void project2(const void *v3, i16 *out);   /* z >= NEAR_Z の点を投影 */

/* ---- math.s ---- */
i16 fmul(i16 a, i16 b);
i16 muldiv(i16 a, i16 b, i16 c);
u32 div_slope(u16 n, u16 d);

/* ---- gen_data.c(tools/gen_data.py が生成)。ページ0の RAM(0x1A00-)に置く(sys_page0_data) ---- */
extern const i16 sin_tab[320];      /* 8.8。cos は sin_tab[a + 64] */
#define SIN(a) (sin_tab[(u8)(a)])
#define COS(a) (sin_tab[(u8)(a) + 64])
extern const u8 font35[];           /* 3×5 ブロックフォント。1 文字 5 バイト(下位 3bit) */
extern const u8 font35_map[96];     /* ASCII 0x20-0x7F → font35 の文字番号(0xFF = 無し) */
extern const u8 spr_patterns[];     /* 16×16 スプライトパターン(32 バイトずつ) */
#define SPR_PAT_BYTES 128
extern const u16 recip9[256];      /* round(2^24 / m)、m = 256..511 */

#endif
