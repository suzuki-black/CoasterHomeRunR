# view_dump.tcl — レールの同じ位置から前と後ろを見た地形の絵を書き出す(前後のビューの矛盾を調べる用)
#
#   TH_NOI=build/engine.noi TH_GAME_NOI=build/game1.noi TH_OUT=<出力ディレクトリ> VIEW_STAGE=1 VIEW_FROM=600 VIEW_TO=680 VIEW_STEP=4 \
#     [VIEW_ROUTE=1|2] openmsx -machine TH_turboR_dev -carta build/TrolleyHomeRun.rom -romtype ASCII8 -script view_dump.tcl
#
# 1 位置(m)につき前(look = 0)、真横(0x4000)、後ろ(0x8000)の順に、敵・車両・HUD を重ねる前の描画バッファを
# $TH_OUT/views.txt へ書く。1 行 = 位置[m] 向き 詰めた絵(1536 バイト) パレット 視線のヨー。絵にするのは tools/view_sheet.py。
# ゲームの進行に触らないように、ロジックの間は cam_s をステージの始めのまま(台本を進めない)にして、描画の間だけ目的の位置にする。
# render() の先頭の timer_reset がフレームの 2 回目の呼び出しなので、そこで書き換える。坂のピッチが追いつくように 1 枚につき 10 フレーム待つ。

set out_dir $::env(TH_OUT)
array set ::syms {}
foreach nf [list $::env(TH_NOI) $::env(TH_GAME_NOI)] {
    set fh [open $nf r]
    foreach line [split [read $fh] "\n"] { if {[regexp {^DEF (\S+) 0x([0-9A-Fa-f]+)} $line -> n h]} { set ::syms($n) [expr {"0x$h"}] } }
    close $fh
}
proc sym {n} { return $::syms($n) }
proc peek8 {a} { debug read memory $a }
proc peek16 {a} { expr {[peek8 $a] | ([peek8 [expr {$a + 1}]] << 8)} }
proc poke16 {a v} { debug write memory $a [expr {$v & 255}]; debug write memory [expr {$a + 1}] [expr {($v >> 8) & 255}] }
set mute on
set throttle off
set ::f [open [file join $out_dir views.txt] w]
set ::vd_stage $::env(VIEW_STAGE)
set ::vd_dirs [expr {[info exists ::env(VIEW_DIRS)] ? $::env(VIEW_DIRS) : {0 32768}}]
set ::vd_list {}
for {set m $::env(VIEW_FROM)} {$m <= $::env(VIEW_TO)} {incr m $::env(VIEW_STEP)} {
    foreach d $::vd_dirs { lappend ::vd_list [list $m $d] }
}
set ::i 0
set ::wait 0
set ::calls 0
set ::on 0
proc tap {row mask} { keymatrixdown $row $mask; after time 0.12 [list keymatrixup $row $mask] }
proc start_game {} {
    if {[peek8 [sym _g_scene]] == 1} { set ::on 1; set ::s0 [peek16 [sym _cam_s]]; return }
    if {[debug read {S1990 regs} 6] == 0 && [peek8 [sym _g_scene]] == 0 && [peek16 [sym _g_tick]] > 60 && [peek16 [sym _g_tick]] < 3600} {
        debug write memory [expr {[sym _g_cfg] + 2}] $::vd_stage
        debug write memory [expr {[sym _g_cfg] + 1}] 1
        if {[info exists ::env(VIEW_ROUTE)]} { debug write memory [expr {[sym _g_cfg] + 5}] $::env(VIEW_ROUTE) }
        tap 8 0x01
    }
    after time 0.5 start_game
}
proc on_timer {} {
    if {!$::on} return
    incr ::calls
    if {$::calls == 1} { poke16 [sym _cam_s] $::s0; return }
    if {$::calls != 2} return
    lassign [lindex $::vd_list $::i] m d
    poke16 [sym _cam_s] [expr {$m * 64}]
    poke16 [sym _look] $d
}
proc on_env_back {} {
    if {!$::on} return
    if {$::calls != 2} return
    incr ::wait
    if {$::wait < 10} return
    set ::wait 0
    lassign [lindex $::vd_list $::i] m d
    binary scan [debug read_block memory [expr {[peek8 [sym _g_buf_hi]] << 8}] 1536] H* hex
    binary scan [debug read_block {VDP palette} 0 32] H* pal
    puts $::f "$m $d $hex $pal [peek8 [sym _cam_yaw]]"
    flush $::f
    incr ::i
    if {$::i >= [llength $::vd_list]} { close $::f; exit }
}
proc on_xfer {} { set ::calls 0 }
debug set_bp [sym _timer_reset] {} on_timer
debug set_bp [sym env_back] {} on_env_back
debug set_bp [sym _vdp_xfer] {} on_xfer
after time 2.0 start_game
