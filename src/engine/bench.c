/* bench.c — 演算ルーチンの実測(make BENCH=1 のときだけリンク)。openMSX の Tcl が
 * bench_mark のブレークポイント間の時間を測る */
#include "engine.h"

volatile i16 bench_sink;
void bench_mark(u8 id) { bench_sink = id; }
void bench_fill64(void);
void bench_nop64(void);
void bench_push64(void);

static void bench_body(void)
{
    u16 i;
    i16 acc = 0;
    bench_mark(0);
    for (i = 0; i < 1000; i++) acc += fmul((i16)i, 300);
    bench_mark(1);
    for (i = 0; i < 1000; i++) acc += muldiv((i16)i, 512, 700);
    bench_mark(2);
    for (i = 0; i < 1000; i++) acc += (i16)div_slope(i, 37);
    bench_mark(3);
    for (i = 0; i < 1000; i++) acc += muldiv((i16)i, 3, 7);
    bench_mark(4);
    for (i = 0; i < 1000; i++) acc += (i16)i;
    bench_mark(5);
    for (i = 0; i < 1000; i++) bench_fill64();
    bench_mark(6);
    for (i = 0; i < 1000; i++) bench_nop64();
    bench_mark(7);
    for (i = 0; i < 1000; i++) bench_push64();
    bench_mark(8);
    bench_sink = acc;
}

void bench_run(void)
{
    __asm__("di");
    bench_body();
    __asm__("ei");
}
