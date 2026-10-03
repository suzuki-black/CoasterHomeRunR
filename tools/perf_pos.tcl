# perf_pos.tcl — レール上の位置ごとの 1 フレームの時間(変更の前後を同じ場所どうしで比べる用)
#
#   TH_NOI=build/engine.noi TH_OUT=<出力ディレクトリ> [PERF_STAGE=1] \
#     openmsx -machine TH_turboR_dev -carta build/TrolleyHomeRun.rom -romtype ASCII8 -script perf_pos.tcl
#
# 速くすると同じ時刻でも違う場所を描くので、時刻ではなく位置(cam_s)で突き合わせる。
# (ゲームの進みはフレーム単位で決まっていて、変更の前後で各フレームの位置は一致する)
# タイトルが出たらゲームを始め(無敵、撃たない)、そのステージが終わるか、位置が 10 秒進まなくなる
# (撃たないのでボスで止まる)まで、フレームごとに
#   位置(m) 1 フレームの時間(ms) 地形の描画 env_draw の時間(ms)
# を $TH_OUT/perf_pos.txt に書く。比べるのは tools/perf_pos_cmp.py。

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
set ::f [open [file join $out_dir perf_pos.txt] w]
set ::on 0
set ::env_t 0
proc tap {row mask} { keymatrixdown $row $mask; after time 0.12 [list keymatrixup $row $mask] }
proc start_game {} {
    if {[peek8 [sym _g_scene]] == 1} { set ::on 1; set ::stage [peek8 [sym _g_stage]]; return }
    if {[debug read {S1990 regs} 6] == 0 && [peek8 [sym _g_scene]] == 0 && [peek16 [sym _g_tick]] > 60 && [peek16 [sym _g_tick]] < 3600} {
        # 始めるステージ(隠しコンフィグの g_cfg.stage = 3 バイト目)
        if {[info exists ::env(PERF_STAGE)]} { debug write memory [expr {[sym _g_cfg] + 2}] $::env(PERF_STAGE) }
        # 無敵(隠しコンフィグの NO DAMAGE = g_cfg の 2 バイト目)。ライフを戻すやり方だと被弾のたびに宝の山の表を作り直し、
        # 実際のプレイに無い時間が約 3ms 入っていた(docs/SDCCとR800の調査.md)
        debug write memory [expr {[sym _g_cfg] + 1}] 1
        tap 8 0x01
    }
    after time 1.0 start_game
}
proc on_env {} { set ::env_in [machine_info time] }
proc on_env_back {} { if {[info exists ::env_in]} { set ::env_t [expr {$::env_t + [machine_info time] - $::env_in}]; unset ::env_in } }
proc on_xfer {} {
    set now [machine_info time]
    if {$::on && [info exists ::last]} {
        if {[peek8 [sym _g_stage]] != $::stage || [peek8 [sym _g_scene]] != 1} { close $::f; exit }
        # 撃たないのでボスで止まる: 位置が 10 秒進まなければ終わる
        set s [peek16 [sym _cam_s]]
        if {![info exists ::ls] || $s != $::ls} { set ::ls $s; set ::lt $now } elseif {$now - $::lt > 10} { close $::f; exit }
        puts $::f [format "%.2f %.3f %.3f" [expr {[peek16 [sym _cam_s]] / 64.0}] [expr {($now - $::last) * 1000}] [expr {$::env_t * 1000}]]
    }
    set ::last $now
    set ::env_t 0
}
debug set_bp [sym _env_draw] {} on_env
debug set_bp [sym env_back] {} on_env_back
debug set_bp [sym _vdp_xfer] {} on_xfer
after time 2.0 start_game
