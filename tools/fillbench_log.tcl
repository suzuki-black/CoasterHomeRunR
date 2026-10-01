# fillbench_log.tcl — ゲーム中の塗り(raster.s の fill)の呼び出しを記録する(make fillbench の比較の入力)
#
#   TH_NOI=build/engine.noi TH_OUT=<出力ディレクトリ> \
#     openmsx -machine TH_turboR_dev -carta build/TrolleyHomeRun.rom -romtype ASCII8 -script tools/fillbench_log.tcl
#
# ステージ 1 を始め(ライフは減らさない)、開始から 20・45・75 秒の 3 か所で 12 フレームずつ記録する。
# 出力 $TH_OUT/spans.txt:
#   W 窓の番号 stage=n            記録の窓の始まり
#   P r0 行数 色 a0 b0 a1 b1 … µs   fill の 1 回の呼び出し(行ごとの span_a / span_b と、呼び出しにかかった時間)
#   F 時刻                         フレームの区切り(vdp_xfer)
# 集計: python3 tools/fillbench_stats.py $TH_OUT/spans.txt
set out $::env(TH_OUT)
array set ::syms {}
set fh [open $::env(TH_NOI) r]
foreach line [split [read $fh] "\n"] { if {[regexp {^DEF (\S+) 0x([0-9A-Fa-f]+)} $line -> n h]} { set ::syms($n) [expr {"0x$h"}] } }
close $fh
proc sym {n} { return $::syms($n) }
proc peek8 {a} { debug read memory $a }
proc peek16 {a} { expr {[peek8 $a] | ([peek8 [expr {$a + 1}]] << 8)} }
set mute on
set throttle off
set ::f [open [file join $out spans.txt] w]
set ::on 0
set ::frames 0
set ::started 0
proc tap {row mask} { keymatrixdown $row $mask; after time 0.12 [list keymatrixup $row $mask] }
proc start_game {} {
    if {[peek8 [sym _g_scene]] == 1} { set ::started 1; set ::t0 [machine_info time]; foreach w $::wins { after time $w [list arm] }; return }
    if {[debug read {S1990 regs} 6] == 0 && [peek8 [sym _g_scene]] == 0 && [peek16 [sym _g_tick]] > 60 && [peek16 [sym _g_tick]] < 3600} { tap 8 0x01 }
    after time 1.0 [list start_game]
}
proc god {} { debug write memory [sym _life] 8; after time 0.5 god }
set ::wins {20 45 75}
set ::nwin 0
proc arm {} { set ::on 1; set ::frames 0; puts $::f "W [incr ::nwin] stage=[peek8 [sym _g_stage]]" }
proc on_fill {} {
    if {!$::on} return
    set ::te [machine_info time]
    set r0 [reg c]; set n [reg b]
    set s "P $r0 $n [peek8 [sym _g_color]]"
    for {set i 0} {$i < $n} {incr i} { append s " [peek8 [expr {0xDE00 + $r0 + $i}]] [peek8 [expr {0xDF00 + $r0 + $i}]]" }
    set ::pend $s
}
proc on_ret {} {
    if {!$::on || ![info exists ::pend]} return
    puts $::f "$::pend [format %.2f [expr {([machine_info time] - $::te) * 1e6}]]"
    unset ::pend
}
proc on_xfer {} {
    if {!$::on} return
    puts $::f [format "F %.6f" [machine_info time]]
    if {[incr ::frames] > 12} { set ::on 0; if {$::nwin >= [llength $::wins]} { close $::f; exit } }
}
debug set_bp [sym fill] {} on_fill
debug set_bp [sym fill_done] {} on_ret
debug set_bp [sym _vdp_xfer] {} on_xfer
after time 2.0 start_game
after time 2.0 god
