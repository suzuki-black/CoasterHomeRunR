# test_game.tcl — openMSX で起動〜タイトル〜ゲーム〜ゲームオーバー〜タイトルを判定する
#
#   TH_NOI=build/engine.noi TH_OUT=<出力ディレクトリ> \
#     openmsx -machine TH_turboR_dev -carta build/TrolleyHomeRun.rom -romtype ASCII8 -script test_game.tcl
#
# 結果は $TH_OUT/result.txt、スクリーンショットは $TH_OUT/*.png。最後に PASS / FAIL を書く。
# ★after time は相対時間。周期処理は [list proc args] で登録する(二重引用符は登録時に評価される)。
# ★static 変数はシンボルに出ない(参照すると Tcl が黙って止まる)。読む変数は game.c 側で static を外してある。

set out_dir $::env(TH_OUT)
set ::f [open [file join $out_dir result.txt] w]
proc log {msg} { puts $::f $msg; flush $::f }

array set ::syms {}
set fh [open $::env(TH_NOI) r]
foreach line [split [read $fh] "\n"] {
    if {[regexp {^DEF (\S+) 0x([0-9A-Fa-f]+)} $line -> name hex]} {
        set ::syms($name) [expr {"0x$hex"}]
    }
}
close $fh
proc sym {name} { return $::syms($name) }
proc peek8  {addr} { debug read memory $addr }
proc peek16 {addr} { expr {[peek8 $addr] | ([peek8 [expr {$addr + 1}]] << 8)} }

set mute on

# フレーム数(表示の切替 = vdp_xfer の呼び出し)を数えて fps を出す
set ::frames 0
debug set_bp [sym _vdp_xfer] {} {incr ::frames}

set ::seen_scene {}
set ::fail {}
set ::last_frames 0
set ::max_score 0
set ::min_life 8

proc state {tag} {
    set s [peek16 [sym _cam_s]]
    set sc [peek8 [sym _g_scene]]
    set life [peek8 [sym _life]]
    set score [peek16 [sym _g_score]]
    set fps [expr {$::frames - $::last_frames}]
    set ::last_frames $::frames
    if {[lsearch $::seen_scene $sc] < 0} { lappend ::seen_scene $sc }
    if {$sc == 1 && $life < $::min_life} { set ::min_life $life }
    if {$score > $::max_score} { set ::max_score $score }
    log [format "%-8s t=%6.2f scene=%d life=%d score=%d0 rail=%.1fm facing=%d dir=%d boss=%d fps=%d cpu=%02X" \
        $tag [machine_info time] $sc $life $score [expr {$s / 64.0}] \
        [peek8 [sym _facing]] [peek8 [sym _attack_dir]] [peek8 [sym _boss_alive]] $fps \
        [debug read {S1990 regs} 6]]
}

proc shot {name} { screenshot -raw -prefix {} [file join $::out_dir $name.png] }
proc tap {row mask} {
    keymatrixdown $row $mask
    after time 0.15 [list keymatrixup $row $mask]
}

# 振り向く(A を押したままの B は誘導弾なので、A を離して押し、また押す)
proc turn {} {
    keymatrixup 8 0x01
    tap 4 0x04
    after time 0.3 [list keymatrixdown 8 0x01]
}

proc tick_log {} {
    state tick
    after time 1.0 [list tick_log]
}

# 照準を左右に振りながら撃ち続ける(当たるかは運)
set ::sweep 0
proc sweep {} {
    incr ::sweep
    if {($::sweep / 6) % 2} { keymatrixup 8 0x10; keymatrixdown 8 0x80 } else { keymatrixup 8 0x80; keymatrixdown 8 0x10 }
    after time 0.25 [list sweep]
}

proc step_title {} {
    state title
    shot 01_title
    tap 8 0x01
    after time 3.0 [list step_run]
}
proc step_run {} {
    state run
    shot 02_run
    keymatrixdown 8 0x01
    sweep
    after time 10.0 [list step_turn]
}
proc step_turn {} {
    shot 03_front
    turn
    after time 0.13 [list shot 04_turning]
    after time 1.0 [list shot 05_back]
    after time 12.0 [list step_turn2]
}
proc step_turn2 {} {
    shot 06_back_later
    turn
    after time 1.0 [list shot 07_front_again]
    after time 60.0 [list step_end]
}
proc watch_over {} {
    set sc [peek8 [sym _g_scene]]
    if {$sc == 2 && ![info exists ::over_shot]} { set ::over_shot 1; shot 08_gameover }
    if {$sc == 0 && [info exists ::over_shot] && ![info exists ::back_title]} {
        set ::back_title 1
        state back_title
        shot 09_title_again
        after time 0.5 [list step_end]
    }
    after time 0.2 [list watch_over]
}
proc step_end {} {
    state end
    if {[lsearch $::seen_scene 1] < 0} { lappend ::fail "ゲームが始まらない" }
    if {$::min_life >= 8} { lappend ::fail "被弾しない(敵の攻撃が届いていない)" }
    if {$::max_score == 0} { lappend ::fail "一度も撃破できていない" }
    if {[debug read {S1990 regs} 6] != 0} { lappend ::fail "R800 DRAM モードになっていない" }
    log "scenes seen: $::seen_scene  min_life=$::min_life  max_score=$::max_score"
    if {[llength $::fail]} { log "FAIL: [join $::fail {, }]" } else { log "PASS" }
    close $::f
    exit
}

# タイトルが出るまでの時間は機種で違う(実機の BIOS はロゴを出す)ので、エンジンが動き出して(g_tick が進んで)から始める
proc wait_title {} {
    # エンジンが動き出したか: R800 DRAM モードに入り、タイトルで、g_tick が進んでいる(起動直後の RAM の 0xFF を除く)
    if {[debug read {S1990 regs} 6] == 0 && [peek8 [sym _g_scene]] == 0 && [peek16 [sym _g_tick]] > 60 && [peek16 [sym _g_tick]] < 3600} { step_title } else { after time 0.5 [list wait_title] }
}
after time 2.0 [list wait_title]
after time 6.0 [list tick_log]
after time 6.0 [list watch_over]
