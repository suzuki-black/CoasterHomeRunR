# pcprof.tcl — PC のサンプリングによる統計プロファイラ
#
#   TH_NOI=build/engine.noi TH_OUT=<出力ディレクトリ> PROF_SYMS=build/prof_syms.txt \
#     openmsx -machine TH_turboR_dev -carta build/TrolleyHomeRun.rom -romtype ASCII8 -script pcprof.tcl
#
# ゲームを始めて何もせず、t=12〜14 の 2 秒間、エミュレータ時刻で 10µs ごとに PC を読み、
# 直前のシンボル(PROF_SYMS: "アドレス 名前" の行。make pcprof が .noi と .map から作る)ごとに数える。
# 結果は $TH_OUT/pcprof.txt(多い順)。関数の中の重い場所も、ローカルラベル単位で見える。

set out_dir $::env(TH_OUT)
set mute on
set ::addrs {}
set fh [open $::env(PROF_SYMS) r]
foreach line [split [read $fh] "\n"] {
    if {[regexp {^([0-9A-Fa-f]+) (\S+)} $line -> hex name]} { lappend ::addrs [list [expr {"0x$hex"}] $name] }
}
close $fh
set ::addrs [lsort -integer -index 0 $::addrs]

proc sym_of {pc} {
    set lo 0
    set hi [expr {[llength $::addrs] - 1}]
    if {$pc < [lindex $::addrs 0 0]} { return [format "(%04X)" $pc] }
    while {$lo < $hi} {
        set mid [expr {($lo + $hi + 1) / 2}]
        if {[lindex $::addrs $mid 0] <= $pc} { set lo $mid } else { set hi [expr {$mid - 1}] }
    }
    return [lindex $::addrs $lo 1]
}

array set ::hist {}
set ::n 0
proc sample {} {
    set pc [reg PC]
    if {$pc < 0x4000} { set s "(BIOS/ISR)" } elseif {$pc >= 0xC000} { set s [format "(page3 %04X)" $pc] } else { set s [sym_of $pc] }
    if {[info exists ::hist($s)]} { incr ::hist($s) } else { set ::hist($s) 1 }
    incr ::n
    if {$::n < 200000} { after time 0.00001 sample } else { report }
}
proc report {} {
    set f [open [file join $::out_dir pcprof.txt] w]
    set l {}
    foreach k [array names ::hist] { lappend l [list $k $::hist($k)] }
    foreach e [lsort -integer -decreasing -index 1 $l] {
        puts $f [format "%6.2f%%  %s" [expr {100.0 * [lindex $e 1] / $::n}] [lindex $e 0]]
    }
    close $f
    exit
}

after time 5.0 {keymatrixdown 8 1}
after time 5.3 {keymatrixup 8 1}
after time 12.0 {sample}
