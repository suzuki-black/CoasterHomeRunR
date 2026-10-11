# overdraw.tcl — 重ね塗りの計測: 描画バッファ(0xC000-0xC5FF)への書き込みを数え、同じフレームのうちに後から上書きされた分を、
#   描いた区分(env 地形 / alt 分岐の線路 / enemy 敵 / trolley 乗り物とライフゲージ / hud)と関数ごとに集計する
#
#   TH_NOI=build/engine.noi TH_OUT=<出力ディレクトリ> PROF_SYMS=build/prof_syms.txt PROF_STAGE=3 OD_FRAMES="60 900 20" \
#     ALT_DRAW=<ゲームモジュールの _alt_draw の番地> \
#     openmsx -machine TH_turboR_dev -carta build/TrolleyHomeRun.rom -romtype ASCII8 -script overdraw.tcl
#
# ゲームを始め(無敵、撃たない)、ゲーム開始からのフレーム番号 OD_FRAMES(最初 最後 間隔)の各フレームで書き込みを見る。
# 数えるのはバイト単位(1 バイト = 横 2 ブロック)。結果は $TH_OUT/od.txt
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
    if {[regexp {^([REG]) ([0-9A-Fa-f]+) (\S+)} $line -> reg hex name]} { regsub {\+.*$} $name {} name; lappend ::tbl($reg) [list [expr {"0x$hex"}] $name] }
}
close $fh
foreach r {R E G} { set ::tbl($r) [lsort -integer -index 0 $::tbl($r)] }
proc sym_of {reg pc} {
    set t $::tbl($reg); set lo 0; set hi [expr {[llength $t] - 1}]
    if {$hi < 0 || $pc < [lindex $t 0 0]} { return "?" }
    while {$lo < $hi} {
        set mid [expr {($lo + $hi + 1) / 2}]
        if {[lindex $t $mid 0] <= $pc} { set lo $mid } else { set hi [expr {$mid - 1}] }
    }
    return [lindex $t $lo 1]
}
set ::frames $::env(OD_FRAMES)
set ::fr 0
set ::on 0
set ::phase x
array set ::tot {}
array set ::waste {}
array set ::by {}
set ::nfr 0
set ::sum_w 0
set ::sum_d 0
proc wcb {} {
    set a $::wp_last_address
    set pc [reg PC]
    if {$pc >= 0xC000} { set f page3 } elseif {$pc >= 0x8000} { set f [sym_of [expr {[peek8 [sym _g_seg2]] == 5 ? "E" : "G"}] $pc] } else { set f [sym_of R $pc] }
    set k "$::phase:$f"
    incr ::nw
    if {[info exists ::last($a)]} {
        set o $::last($a)
        if {[info exists ::waste($o)]} { incr ::waste($o) } else { set ::waste($o) 1 }
        set p "[lindex [split $o :] 0]>$::phase"
        if {[info exists ::by($p)]} { incr ::by($p) } else { set ::by($p) 1 }
    }
    set ::last($a) $k
    if {[info exists ::tot($k)]} { incr ::tot($k) } else { set ::tot($k) 1 }
}
proc ph {p} { if {$::on} { set ::phase $p } }
proc ph_hud {} { if {$::on && $::phase eq "trolley"} { set ::phase hud } }
proc on_xfer {} {
    if {$::fr == 0 && [peek8 [sym _g_scene]] != 1} return
    incr ::fr
    if {$::on} {
        debug remove_watchpoint $::wp
        set ::on 0
        incr ::nfr
        incr ::sum_w $::nw
        incr ::sum_d [array size ::last]
    }
    if {$::fr > [lindex $::frames 1]} { report; return }
    if {$::fr >= [lindex $::frames 0] && ($::fr - [lindex $::frames 0]) % [lindex $::frames 2] == 0} {
        array unset ::last; array set ::last {}
        set ::nw 0
        set ::phase pre
        set ::on 1
        set ::wp [debug set_watchpoint write_mem {0xC000 0xC5FF} {} wcb]
    }
}
proc report {} {
    set f [open [file join $::out_dir od.txt] w]
    puts $f [format "フレーム %d 枚: 書き込み 平均 %.0f バイト、書かれたバイト 平均 %.0f(1536 中)、上書き 平均 %.0f" $::nfr [expr {1.0*$::sum_w/$::nfr}] [expr {1.0*$::sum_d/$::nfr}] [expr {1.0*($::sum_w-$::sum_d)/$::nfr}]]
    puts $f "== 上書きされた側 → 上書きした側(区分、1 フレーム平均)"
    set l {}; foreach k [array names ::by] { lappend l [list $k $::by($k)] }
    foreach e [lsort -integer -decreasing -index 1 $l] { puts $f [format "%8.1f  %s" [expr {1.0*[lindex $e 1]/$::nfr}] [lindex $e 0]] }
    puts $f "== 書き込み(区分:関数、1 フレーム平均) と そのうち後で上書きされた分"
    set l {}; foreach k [array names ::tot] { lappend l [list $k $::tot($k)] }
    foreach e [lsort -integer -decreasing -index 1 $l] {
        set k [lindex $e 0]; set w [expr {[info exists ::waste($k)] ? $::waste($k) : 0}]
        puts $f [format "%8.1f  %8.1f  %s" [expr {1.0*[lindex $e 1]/$::nfr}] [expr {1.0*$w/$::nfr}] $k]
    }
    close $f
    exit
}
proc tap {row mask} { keymatrixdown $row $mask; after time 0.12 [list keymatrixup $row $mask] }
proc start_game {} {
    if {[peek8 [sym _g_scene]] == 1} return
    if {[debug read {S1990 regs} 6] == 0 && [peek8 [sym _g_scene]] == 0 && [peek16 [sym _g_tick]] > 60 && [peek16 [sym _g_tick]] < 3600} {
        if {[info exists ::env(PROF_STAGE)]} { debug write memory [expr {[sym _g_cfg] + 2}] $::env(PROF_STAGE) }
        debug write memory [expr {[sym _g_cfg] + 1}] 1
        tap 8 0x01
    }
    after time 1.0 start_game
}
debug set_bp [sym _vdp_xfer] {} on_xfer
debug set_bp [sym _env_draw] {} {ph env}
debug set_bp $::env(ALT_DRAW) {} {ph alt}
debug set_bp [sym _draw_model] {} {ph enemy}
debug set_bp [sym _trolley_draw] {} {ph trolley}
debug set_bp [sym _text_draw] {} ph_hud
debug set_bp [sym _num_draw] {} ph_hud
debug set_bp [sym _rect_fill] {} ph_hud
after time 2.0 start_game
