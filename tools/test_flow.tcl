# test_flow.tcl — タイトル → オープニング → ステージ 1(中ボス・大ボス)→ ステージ 2(地底湖・湖の主)
#                  → ステージ 3(森・森の番人)→ ステージ 4(海の鉄橋・大ダコ人)→ ステージ 5(町・祭りの山車)
#                  → ステージ 6(村の入り口・亜人の王)
#                  → エンディング(SCREEN 1)→ タイトル を自動で遊んで確かめる
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
set ::god [expr {[info exists ::env(TH_GOD)] && $::env(TH_GOD) ne "" && $::env(TH_GOD)}]   ;# make は GOD を付けないと空で渡す

set ::scene -1
set ::stage 0
set ::tsum 0
set ::tn 0
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
        if {$sc == 3} { after time 3.0 [list shot ending] }
    }
    set st [peek8 [sym _g_stage]]
    if {$st != $::stage} {
        fps_log
        log [format "t=%7.2f stage %d → %d  score=%d0 life=%d" [machine_info time] $::stage $st [peek16 [sym _g_score]] [peek8 [sym _life]]]
        set ::stage $st
        if {$st >= 2} { stage_shots $st }
    }
    if {$sc == 1} {
        if {$::god} { debug write memory [sym _life] 8 }
        incr ::tsum [peek8 [sym _g_perf_ticks]]
        incr ::tn
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
        # (A を押したままの B は誘導弾なので、A を離して押す)
        if {($dir == 2 && $fc == 0) || ($dir == 1 && $fc == 1)} { keymatrixup 8 0x01; set ::fire 0; tap 4 0x04 }
        set b [peek8 [sym _boss_alive]]
        if {$b && !$::boss_seen} { set ::boss_seen 1; incr ::bosses; after time 3.0 [list shot boss$::bosses] }
        if {!$b} { set ::boss_seen 0 }
    } elseif {$::fire} { keymatrixup 8 0x01; set ::fire 0 }
    if {$sc == 0 && [lsearch $::seen 3] >= 0 || $sc == 0 && [lsearch $::seen 2] >= 0} { finish; return }
    after time 0.1 [list bot]
}
set ::bosses 0
# ステージごとの平均 fps(直前のフレームのティック数を 0.1 秒ごとに見た平均から)
proc fps_log {} {
    if {$::tn} { log [format "  stage %d: 平均 %.1f fps" $::stage [expr {60.0 * $::tn / $::tsum}]] }
    set ::tsum 0; set ::tn 0
}
proc finish {} {
    fps_log
    log "scenes: $::seen  bosses=$::bosses"
    set ok [expr {[lsearch $::seen 1] >= 0 && ([lsearch $::seen 3] >= 0 || [lsearch $::seen 2] >= 0)}]
    if {$::god && ([lsearch $::seen 3] < 0 || $::bosses < 7)} { set ok 0 }
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

# ステージ 2・3 の節目の画面(ステージの名前の表示から数えた秒)
proc stage_shots {st} {
    foreach t {2.5 4.5 6.5 9.0 11.0 13.0 16.0 22.0 30.0 40.0} {
        after time $t [list shot st${st}_[string map {. _} $t]]
    }
}

# タイトルが出るまでの時間は機種で違う(実機の BIOS はロゴを出す)ので、エンジンが動き出して(g_tick が進んで)から
# スペースを押し、ゲームが始まるまで 1 秒ごとに押し直す
proc start_game {} {
    if {[lsearch $::seen 1] >= 0} return
    # エンジンが動き出したか: R800 DRAM モードに入り、タイトルで、g_tick が進んでいる(起動直後の RAM の 0xFF を除く)
    if {[debug read {S1990 regs} 6] == 0 && [peek8 [sym _g_scene]] == 0 && [peek16 [sym _g_tick]] > 60 && [peek16 [sym _g_tick]] < 3600} {
        tap 8 0x01
        if {![info exists ::open_shots]} { set ::open_shots 1; opening_shots }
    }
    after time 1.0 [list start_game]
}
after time 2.0 [list start_game]
after time 2.0 [list bot]
