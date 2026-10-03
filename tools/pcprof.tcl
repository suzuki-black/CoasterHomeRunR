# pcprof.tcl — PC のサンプリングによる統計プロファイラ
#
#   TH_NOI=build/engine.noi TH_OUT=<出力ディレクトリ> PROF_SYMS=build/prof_syms.txt [PROF_AT="45 75"] [PROF_LEN=2] \
#     [PROF_FRAMES="600 1000"] [PROF_DT=0.00002] [PROF_STAGE=1] \
#     openmsx -machine TH_turboR_dev -carta build/TrolleyHomeRun.rom -romtype ASCII8 -script pcprof.tcl
#
# タイトルが出たらゲームを始め(無敵、撃たない)、ゲーム開始から PROF_AT 秒(既定 45 と 75)の
# それぞれ PROF_LEN 秒間、エミュレータ時刻で 10µs ごとに PC を読み、直前のシンボルごとに数える。
# シンボル(PROF_SYMS、make pcprof が tools/prof_syms.py で作る)は領域ごと: R 常駐部、E 地形モジュール、
# G ゲームモジュール。ページ2の PC は g_seg2(5 = 地形、それ以外 = ゲーム)で引き分ける。
# PROF_FRAMES を渡すと、時刻ではなくゲーム開始からのフレーム番号の範囲で測る(ゲームの進みはフレーム単位で決まるので、
# 変更の前後で同じ場面になる)。その区間の実時間も出すので、割合 × 実時間 / フレーム数で 1 フレームあたりの ms に直せる。
# 結果は $TH_OUT/pcprof.txt: 関数ごと(ローカルラベルをまとめたもの)と、ラベルごとの多い順。

set out_dir $::env(TH_OUT)
set mute on
set throttle off
array set ::gsyms {}
set fh [open $::env(TH_NOI) r]
foreach line [split [read $fh] "\n"] { if {[regexp {^DEF (\S+) 0x([0-9A-Fa-f]+)} $line -> n h]} { set ::gsyms($n) [expr {"0x$h"}] } }
close $fh
proc sym {n} { return $::gsyms($n) }
proc peek8 {a} { debug read memory $a }
proc peek16 {a} { expr {[peek8 $a] | ([peek8 [expr {$a + 1}]] << 8)} }

array set ::tbl {R {} E {} G {}}
set fh [open $::env(PROF_SYMS) r]
foreach line [split [read $fh] "\n"] {
    if {[regexp {^([REG]) ([0-9A-Fa-f]+) (\S+)} $line -> reg hex name]} { lappend ::tbl($reg) [list [expr {"0x$hex"}] $name] }
}
close $fh
foreach r {R E G} { set ::tbl($r) [lsort -integer -index 0 $::tbl($r)] }

proc sym_of {reg pc} {
    set t $::tbl($reg)
    set lo 0
    set hi [expr {[llength $t] - 1}]
    if {$hi < 0 || $pc < [lindex $t 0 0]} { return [format "(%s %04X)" $reg $pc] }
    while {$lo < $hi} {
        set mid [expr {($lo + $hi + 1) / 2}]
        if {[lindex $t $mid 0] <= $pc} { set lo $mid } else { set hi [expr {$mid - 1}] }
    }
    return [lindex $t $lo 1]
}

set ::at [expr {[info exists ::env(PROF_AT)] ? $::env(PROF_AT) : "45 75"}]
set ::len [expr {[info exists ::env(PROF_LEN)] ? $::env(PROF_LEN) : 2}]
set ::dt [expr {[info exists ::env(PROF_DT)] ? $::env(PROF_DT) : 0.00001}]
set ::frames [expr {[info exists ::env(PROF_FRAMES)] ? $::env(PROF_FRAMES) : ""}]
set ::fr 0
set ::sampling 0
array set ::hist {}
set ::n 0
set ::left 0
set ::wins 0
proc sample {} {
    set pc [reg PC]
    if {$pc >= 0xC000} {
        set s [format "(page3 %04X)" [expr {$pc & 0xFF00}]]
    } elseif {$pc >= 0x8000} {
        set s [sym_of [expr {[peek8 [sym _g_seg2]] == 5 ? "E" : "G"}] $pc]
    } else {
        set s [sym_of R $pc]
    }
    if {[info exists ::hist($s)]} { incr ::hist($s) } else { set ::hist($s) 1 }
    incr ::n
    if {$::frames ne ""} { if {$::sampling} { after time $::dt sample }; return }
    if {[incr ::left -1] > 0} { after time $::dt sample } elseif {[incr ::wins] >= [llength $::at]} { report }
}
# フレーム番号で区切る: 場面がゲームになって最初の転送から数える
proc on_xfer {} {
    if {$::fr == 0 && [peek8 [sym _g_scene]] != 1} return
    incr ::fr
    if {$::fr == [lindex $::frames 0]} { set ::sampling 1; set ::t0 [machine_info time]; sample }
    if {$::fr == [lindex $::frames 1]} { set ::sampling 0; set ::t1 [machine_info time]; report }
}
proc window {} { set ::left [expr {int($::len / $::dt)}]; sample }
proc report {} {
    set f [open [file join $::out_dir pcprof.txt] w]
    array set fn {}
    foreach k [array names ::hist] {
        regsub {\+.*$} $k {} base
        if {[info exists fn($base)]} { incr fn($base) $::hist($k) } else { set fn($base) $::hist($k) }
    }
    if {$::frames ne ""} {
        set nf [expr {[lindex $::frames 1] - [lindex $::frames 0]}]
        puts $f [format "== 関数ごと(サンプル %d、フレーム %s、%d フレームで %.1f ms = 1 フレーム %.2f ms)" $::n $::frames $nf [expr {($::t1 - $::t0) * 1000}] [expr {($::t1 - $::t0) * 1000 / $nf}]]
    } else {
        puts $f "== 関数ごと(サンプル $::n、窓 $::at 秒 × $::len 秒)"
    }
    set l {}
    foreach k [array names fn] { lappend l [list $k $fn($k)] }
    foreach e [lsort -integer -decreasing -index 1 $l] {
        puts $f [format "%6.2f%%  %s" [expr {100.0 * [lindex $e 1] / $::n}] [lindex $e 0]]
    }
    puts $f "\n== ラベルごと"
    set l {}
    foreach k [array names ::hist] { lappend l [list $k $::hist($k)] }
    foreach e [lsort -integer -decreasing -index 1 $l] {
        puts $f [format "%6.2f%%  %s" [expr {100.0 * [lindex $e 1] / $::n}] [lindex $e 0]]
    }
    close $f
    exit
}

proc tap {row mask} { keymatrixdown $row $mask; after time 0.12 [list keymatrixup $row $mask] }
proc start_game {} {
    if {[peek8 [sym _g_scene]] == 1} {
        if {$::frames eq ""} { foreach t $::at { after time $t window } }
        return
    }
    if {[debug read {S1990 regs} 6] == 0 && [peek8 [sym _g_scene]] == 0 && [peek16 [sym _g_tick]] > 60 && [peek16 [sym _g_tick]] < 3600} {
        if {[info exists ::env(PROF_STAGE)]} { debug write memory [expr {[sym _g_cfg] + 2}] $::env(PROF_STAGE) }
        # 無敵(隠しコンフィグの NO DAMAGE = g_cfg の 2 バイト目)。ライフを戻すやり方だと被弾のたびに宝の山の表を作り直し、
        # 実際のプレイに無い時間が約 3ms 入っていた(docs/SDCCとR800の調査.md)
        debug write memory [expr {[sym _g_cfg] + 1}] 1
        tap 8 0x01
    }
    after time 1.0 start_game
}
if {$::frames ne ""} { debug set_bp [sym _vdp_xfer] {} on_xfer }
after time 2.0 start_game
