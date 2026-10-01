# fillbench_replay.tcl — 記録した塗りの呼び出し(fillbench_log.tcl の spans.txt)を、make fillbench の ROM で
#   いまの fill と、詰めたバッファへ塗る pfill の両方で再生して時間を比べる(src/engine/fillbench.c)
#
#   TH_NOI=build/fillb/engine.noi TH_OUT=<spans.txt のあるディレクトリ> \
#     openmsx -machine TH_turboR_dev -carta "build/fillb/TrolleyHomeRun_FILLBENCH [ASCII8].rom" -romtype ASCII8 \
#     -script tools/fillbench_replay.tcl
#
# 出力 $TH_OUT/replay.txt: 窓ごとの 1 フレームあたりの時間と、両方の絵の食い違い(0 であること)
set out $::env(TH_OUT)
array set ::syms {}
set fh [open $::env(TH_NOI) r]
foreach line [split [read $fh] "\n"] { if {[regexp {^DEF (\S+) 0x([0-9A-Fa-f]+)} $line -> n h]} { set ::syms($n) [expr {"0x$h"}] } }
close $fh
proc sym {n} { return $::syms($n) }
proc peek8 {a} { debug read memory $a }
proc peek16 {a} { expr {[peek8 $a] | ([peek8 [expr {$a + 1}]] << 8)} }
proc poke16 {a v} { debug write memory $a [expr {$v & 255}]; debug write memory [expr {$a + 1}] [expr {($v >> 8) & 255}] }
set mute on
set throttle off
# 窓ごとの呼び出しの列
set ::lists {}
set cur {}; set ncur 0; set nfr 0; set win ""
set fh [open [file join $out spans.txt] r]
foreach line [split [read $fh] "\n"] {
    set t [split $line " "]
    if {[lindex $t 0] eq "W"} { if {$ncur} { lappend ::lists [list $win $ncur $nfr $cur] }; set win [lindex $t 1]; set cur {}; set ncur 0; set nfr 0; continue }
    if {[lindex $t 0] eq "F"} { incr nfr; continue }
    if {[lindex $t 0] eq "P"} {
        set n [lindex $t 2]
        lappend cur [lindex $t 1] $n [lindex $t 3]
        for {set i 0} {$i < 2*$n} {incr i} { lappend cur [lindex $t [expr {4 + $i}]] }
        incr ncur
    }
}
if {$ncur} { lappend ::lists [list $win $ncur $nfr $cur] }
close $fh
set ::rf [open [file join $out replay.txt] w]
set ::k 0
proc next {} {
    if {$::k >= [llength $::lists]} { close $::rf; exit }
    lassign [lindex $::lists $::k] win n nfr bytes
    set a 0x9000
    foreach b $bytes { debug write memory $a $b; incr a }
    poke16 [sym _fb_n] $n
    debug write memory [sym _fb_state] 1
    after time 0.05 [list wait $win $n $nfr]
}
proc wait {win n nfr} {
    if {[peek8 [sym _fb_state]] != 2} { after time 0.05 [list wait $win $n $nfr]; return }
    set r0 [peek16 [sym _fb_res]]; set r1 [peek16 [expr {[sym _fb_res]+2}]]; set r2 [peek16 [expr {[sym _fb_res]+4}]]
    set fr [expr {$nfr - 1}]
    puts $::rf [format "win %s calls %d frames %d  base %.0fus  fill %.0fus (%.2fms/frame)  pfill %.0fus (%.2fms/frame)  mismatch %d" $win $n $fr [expr {$r0*3.911}] [expr {($r1-$r0)*3.911}] [expr {($r1-$r0)*3.911/1000/$fr}] [expr {($r2-$r0)*3.911}] [expr {($r2-$r0)*3.911/1000/$fr}] [peek16 [sym _fb_mis]]]
    flush $::rf
    incr ::k
    debug write memory [sym _fb_state] 0
    after time 0.05 next
}
after time 8 next
