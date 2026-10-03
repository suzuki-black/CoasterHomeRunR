# frame_dump.tcl — ゲーム開始から決まった番号のフレームの描画バッファを書き出す(変更の前後で絵が同じか比べる用)
#
#   TH_NOI=build/engine.noi TH_OUT=<出力ディレクトリ> [DUMP_EVERY=50] [DUMP_N=40] [DUMP_STAGE=1] [DUMP_TURN=1] [DUMP_TITLE=1] \
#     openmsx -machine TH_turboR_dev -carta build/TrolleyHomeRun.rom -romtype ASCII8 -script frame_dump.tcl
#
# ゲームの進みはフレーム単位で決まっているので、描画の処理だけを変えたなら、同じ番号のフレームの絵は 1 ブロックも違わないはず。
# タイトルが出たらゲームを始め(無敵、撃たない)、転送(vdp_xfer)の直前に、DUMP_EVERY フレームごとに
# 前のフレームが転送した VRAM のパターンジェネレータ(1536 バイト = 実際に表示される中身)と、地形を描き終えた時点の
# 描画バッファを 16 進にして $TH_OUT/frames.txt へ書く(DUMP_N 枚)。1 行 = フレーム番号 位置 VRAM 地形だけの絵。
# 描画バッファは 1 ブロック 1 バイト(64×48 = 3072 バイト)の版と、詰めた形(VRAM と同じ並び、1536 バイト。
# シンボル _pset がある版)のどちらでも書き出す。比べるのは tools/frame_cmp.py(同じ 64×48 の並びに直して比べる)。
# 電飾の点滅は割り込みで数えるティック(g_tick、実時間)で決まり、描画が速くなると位相がずれるので、地形を描く間だけ
# g_tick をフレーム番号から決めた値に差し替え、描き終えたら実際の値(その間に進んだ分を足す)に戻す。
# 被弾で飛び散る破片の種(debris_start)も同じように差し替える。

set out_dir $::env(TH_OUT)
array set ::syms {}
set fh [open $::env(TH_NOI) r]
foreach line [split [read $fh] "\n"] { if {[regexp {^DEF (\S+) 0x([0-9A-Fa-f]+)} $line -> n h]} { set ::syms($n) [expr {"0x$h"}] } }
close $fh
proc sym {n} { return $::syms($n) }
proc peek8 {a} { debug read memory $a }
proc peek16 {a} { expr {[peek8 $a] | ([peek8 [expr {$a + 1}]] << 8)} }
set mute on
set throttle off
set ::every [expr {[info exists ::env(DUMP_EVERY)] ? $::env(DUMP_EVERY) : 50}]
set ::count [expr {[info exists ::env(DUMP_N)] ? $::env(DUMP_N) : 40}]
set ::f [open [file join $out_dir frames.txt] w]
set ::turn [expr {[info exists ::env(DUMP_TURN)] && $::env(DUMP_TURN)}]
set ::title [expr {[info exists ::env(DUMP_TITLE)] && $::env(DUMP_TITLE)}]
set ::buf_len [expr {[info exists ::syms(_pset)] ? 1536 : 3072}]
set ::on 0
set ::fr 0
set ::done 0
proc tap {row mask} { keymatrixdown $row $mask; after time 0.12 [list keymatrixup $row $mask] }
proc start_game {} {
    if {[peek8 [sym _g_scene]] == 1} return
    if {[debug read {S1990 regs} 6] == 0 && [peek8 [sym _g_scene]] == 0 && [peek16 [sym _g_tick]] > 60 && [peek16 [sym _g_tick]] < 3600} {
        # 始めるステージ(隠しコンフィグの g_cfg.stage = 3 バイト目)
        if {[info exists ::env(DUMP_STAGE)]} { debug write memory [expr {[sym _g_cfg] + 2}] $::env(DUMP_STAGE) }
        # 無敵(隠しコンフィグの NO DAMAGE = g_cfg の 2 バイト目)。ライフを戻すやり方だと被弾のたびに宝の山の表を作り直し、
        # 実際のプレイに無い時間が約 3ms 入っていた(docs/SDCCとR800の調査.md)
        debug write memory [expr {[sym _g_cfg] + 1}] 1
        tap 8 0x01
    }
    after time 1.0 start_game
}
proc set_tick {v} { debug write memory [sym _g_tick] [expr {$v & 255}]; debug write memory [expr {[sym _g_tick] + 1}] [expr {($v >> 8) & 255}] }
# 被弾で飛び散る破片の種も g_tick(fx.c の debris_start)。入口で差し替え、地形を描き始めるときに実際の値へ戻す
proc on_coins {} {
    if {!$::on && !$::title && [peek8 [sym _g_scene]] != 1} return
    set ::coin_real [peek16 [sym _g_tick]]
    set ::coin_fake [expr {($::fr * 8) & 0xFFFF}]
    set_tick $::coin_fake
}
proc on_env {} {
    # 数え始める前(最初の転送の前)のフレームも、ゲームの場面なら差し替える(そのフレームの絵も書き出すため)
    if {!$::on && !$::title && [peek8 [sym _g_scene]] != 1} return
    if {[info exists ::coin_real]} {
        set_tick [expr {$::coin_real + [peek16 [sym _g_tick]] - $::coin_fake}]
        unset ::coin_real
    }
    set ::real_tick [peek16 [sym _g_tick]]
    set ::fake_tick [expr {($::fr * 8) & 0xFFFF}]   ;# 点滅は g_tick >> 3。描画中に 7 ティック進んでも位相が変わらない
    debug write memory [sym _g_tick] [expr {$::fake_tick & 255}]
    debug write memory [expr {[sym _g_tick] + 1}] [expr {$::fake_tick >> 8}]
}
proc on_env_back {} {
    if {![info exists ::real_tick]} return
    set t [expr {($::real_tick + [peek16 [sym _g_tick]] - $::fake_tick) & 0xFFFF}]
    debug write memory [sym _g_tick] [expr {$t & 255}]
    debug write memory [expr {[sym _g_tick] + 1}] [expr {$t >> 8}]
    unset ::real_tick
    # 書き出すフレームなら、地形を描き終えた時点の絵も取っておく(敵・コイン・HUD を重ねる前)
    if {($::fr + 1) % $::every == 0} {
        binary scan [debug read_block memory [expr {[peek8 [sym _g_buf_hi]] << 8}] $::buf_len] H* ::env_hex
    }
}
proc on_xfer {} {
    # 数え始めは「場面がゲームになって最初の転送」(実時間の見回りで拾うと、新旧で 1 フレームずれる)
    if {!$::on} { if {!$::title && [peek8 [sym _g_scene]] != 1} return; set ::on 1 }
    if {$::turn} {
        if {$::fr % 300 == 150} { keymatrixdown 4 0x04 }
        if {$::fr % 300 == 153} { keymatrixup 4 0x04 }
    }
    incr ::fr
    if {$::fr % $::every} return
    # 前のフレームが転送したページ = いま転送しようとしているページ(A)の反対
    binary scan [debug read_block VRAM [expr {(1 - ([reg a] & 1)) * 0x800}] 1536] H* hex
    if {![info exists ::env_hex]} { set ::env_hex - }
    puts $::f "$::fr [format %.2f [expr {[peek16 [sym _cam_s]] / 64.0}]] $hex $::env_hex"
    unset ::env_hex
    if {[incr ::done] >= $::count} { close $::f; exit }
}
debug set_bp [sym _debris_start] {} on_coins
debug set_bp [sym _env_draw] {} on_env
debug set_bp [sym env_back] {} on_env_back
debug set_bp [sym _vdp_xfer] {} on_xfer
if {!$::title} { after time 2.0 start_game }
