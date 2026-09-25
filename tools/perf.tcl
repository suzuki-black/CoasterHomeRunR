# perf.tcl — 速度の計測(条件をそろえて比べる用)
#
#   TH_NOI=build/engine.noi TH_OUT=<出力ディレクトリ> PERF_FN="_env_draw _raster ..." \
#     openmsx -machine TH_turboR_dev -carta build/TrolleyHomeRun.rom -romtype ASCII8 -script perf.tcl
#
# ゲームを始めて何も操作せず、発進後の 8 秒間(t=12〜20)のフレーム数から平均フレーム時間を出す。
# PERF_FN の関数は、t=12 以降の 1 フレームについて呼び出し〜戻りの時間(エミュレータ時刻)を合計する。
# 結果は $TH_OUT/perf.txt、その時点の画面は $TH_OUT/perf.png。

set out_dir $::env(TH_OUT)
set ::f [open [file join $out_dir perf.txt] w]
array set ::syms {}
set fh [open $::env(TH_NOI) r]
foreach line [split [read $fh] "\n"] {
    if {[regexp {^DEF (\S+) 0x([0-9A-Fa-f]+)} $line -> name hex]} { set ::syms($name) [expr {"0x$hex"}] }
}
close $fh
proc peek16 {a} { expr {[debug read memory $a] | ([debug read memory [expr {$a + 1}]] << 8)} }
set mute on

set ::frames 0
set ::prof 0
array set ::tot {}
array set ::cnt {}
proc on_frame {} {
    incr ::frames
    if {$::prof == 1} { set ::prof 2; array unset ::tot; array unset ::cnt; set ::pt [machine_info time]; return }
    if {$::prof == 2} {
        set ::prof 3
        puts $::f [format "1 フレーム(t=12 付近): %.2f ms" [expr {([machine_info time] - $::pt) * 1000}]]
        foreach k [lsort [array names ::tot]] {
            puts $::f [format "  %-18s n=%4d 計 %7.2f ms  1 回 %7.1f us" $k $::cnt($k) [expr {$::tot($k) * 1000}] [expr {$::tot($k) * 1e6 / $::cnt($k)}]]
        }
    }
}
proc enter {name} {
    if {$::prof != 2} return
    set ret [peek16 [reg SP]]
    set id [debug set_bp $ret {} [list leave $name $ret [machine_info time]]]
    set ::bpid($ret,$name) $id
}
proc leave {name ret t0} {
    catch { debug remove_bp $::bpid($ret,$name) }
    if {![info exists ::tot($name)]} { set ::tot($name) 0; set ::cnt($name) 0 }
    set ::tot($name) [expr {$::tot($name) + [machine_info time] - $t0}]
    incr ::cnt($name)
}
debug set_bp $::syms(_vdp_xfer) {} {on_frame}
if {[info exists ::env(PERF_FN)]} {
    foreach fn $::env(PERF_FN) { debug set_bp $::syms($fn) {} [list enter $fn] }
}

after time 5.0 {keymatrixdown 8 1}
after time 5.3 {keymatrixup 8 1}
after time 12.0 { set ::f0 $::frames; set ::t0 [machine_info time]; set ::prof 1 }
after time 20.0 {
    set n [expr {$::frames - $::f0}]
    set dt [expr {[machine_info time] - $::t0}]
    puts $::f [format "平均: %.1f fps(%.2f ms/フレーム、%d フレーム / %.1f 秒)" [expr {$n / $dt}] [expr {$dt * 1000.0 / $n}] $n $dt]
    screenshot -raw -prefix {} [file join $::out_dir perf.png]
    close $::f
    exit
}
