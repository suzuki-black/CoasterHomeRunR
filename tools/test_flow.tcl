# test_flow.tcl — タイトル → オープニング → ステージ 1(中ボス・大ボス)→ 仮エンディング → タイトル を自動で遊んで確かめる
#
#   TH_NOI=build/engine.noi TH_OUT=<出力ディレクトリ> [TH_GOD=1] \
#     openmsx -machine TH_turboR_dev -carta build/TrolleyHomeRun.rom -romtype ASCII8 -script test_flow.tcl
#
# 自動操作: 当たり判定の表(hits)から一番手前の矩形の中心に照準を合わせて撃ち続け、
# 警告(攻撃の来る向き)と逆を向いていたら B で振り向く。TH_GOD=1 ならライフを減らさない(流れの確認用)。
# 結果は $TH_OUT/flow.txt、節目の画面は $TH_OUT/flow_*.png。

set out_dir $::env(TH_OUT)
set ::f [open [file join $out_dir flow.txt] w]
proc log {msg} { puts $::f $msg; flush $::f }
array set ::syms {}
set fh [open $::env(TH_NOI) r]
foreach line [split [read $fh] "\n"] {
    if {[regexp {^DEF (\S+) 0x([0-9A-Fa-f]+)} $line -> name hex]} { set ::syms($name) [expr {"0x$hex"}] }
}
close $fh
proc sym {n} { return $::syms($n) }
proc peek8 {a} { debug read memory $a }
proc peek16 {a} { expr {[peek8 $a] | ([peek8 [expr {$a + 1}]] << 8)} }
proc poke16 {a v} { debug write memory $a [expr {$v & 255}]; debug write memory [expr {$a + 1}] [expr {($v >> 8) & 255}] }
proc s16 {v} { expr {$v >= 32768 ? $v - 65536 : $v} }
proc shot {name} { screenshot -raw -prefix {} [file join $::out_dir flow_$name.png] }
set mute on
set ::god [expr {[info exists ::env(TH_GOD)] && $::env(TH_GOD)}]

set ::scene -1
set ::seen {}
set ::boss_seen 0
set ::fire 0
proc tap {row mask} { keymatrixdown $row $mask; after time 0.12 [list keymatrixup $row $mask] }

proc bot {} {
    set sc [peek8 [sym _g_scene]]
    if {$sc != $::scene} {
        log [format "t=%7.2f scene %d → %d  score=%d0 life=%d" [machine_info time] $::scene $sc [peek16 [sym _g_score]] [peek8 [sym _life]]]
        set ::scene $sc
        lappend ::seen $sc
        shot scene$sc
        if {$sc == 1} { set ::game_t [machine_info time] }
    }
    if {$sc == 1} {
        if {$::god} { debug write memory [sym _life] 8 }
        # 一番手前の矩形を狙う
        set n [peek8 [sym _n_hits]]
        set best -1; set bz 99999
        for {set i 0} {$i < $n} {incr i} {
            set a [expr {[sym _hits] + $i * 7}]
            set z [s16 [peek16 [expr {$a + 4}]]]
            if {$z < $bz} { set bz $z; set best $a }
        }
        if {$best >= 0} {
            set x [expr {([peek8 $best] + [peek8 [expr {$best + 2}]]) * 2}]
            set y [expr {([peek8 [expr {$best + 1}]] + [peek8 [expr {$best + 3}]]) * 2}]
            poke16 [sym _aim_x] $x
            poke16 [sym _aim_y] $y
            if {!$::fire} { keymatrixdown 8 0x01; set ::fire 1 }
        } elseif {$::fire} { keymatrixup 8 0x01; set ::fire 0 }
        # 警告と逆を向いていたら振り向く
        set dir [peek8 [sym _attack_dir]]
        set fc [peek8 [sym _facing]]
        if {($dir == 2 && $fc == 0) || ($dir == 1 && $fc == 1)} { tap 4 0x04 }
        set b [peek8 [sym _boss_alive]]
        if {$b && !$::boss_seen} { set ::boss_seen 1; incr ::bosses; after time 3.0 [list shot boss$::bosses] }
        if {!$b} { set ::boss_seen 0 }
    } elseif {$::fire} { keymatrixup 8 0x01; set ::fire 0 }
    if {$sc == 0 && [lsearch $::seen 3] >= 0 || $sc == 0 && [lsearch $::seen 2] >= 0} { finish; return }
    after time 0.1 [list bot]
}
set ::bosses 0
proc finish {} {
    log "scenes: $::seen  bosses=$::bosses"
    set ok [expr {[lsearch $::seen 1] >= 0 && ([lsearch $::seen 3] >= 0 || [lsearch $::seen 2] >= 0)}]
    if {$::god && [lsearch $::seen 3] < 0} { set ok 0 }
    log [expr {$ok ? "PASS" : "FAIL"}]
    close $::f
    exit
}
proc opening_shots {} {
    after time 0.8 [list shot open1_treasure]
    after time 2.6 [list shot open2_grab]
    after time 4.8 [list shot open3_grrr]
    after time 6.6 [list shot open4_escape]
    after time 8.0 [list shot open5_run]
}

after time 5.0 {tap 8 0x01; opening_shots}
after time 5.0 [list bot]
