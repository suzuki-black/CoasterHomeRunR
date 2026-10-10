# measure.tcl — build/LINEIRQ.ROM を動かし、R#19 の値ごとの結果を $TH_OUT/lineirq.txt へ書く
#   TH_OUT=<出力ディレクトリ> openmsx -machine My_FS-A1ST_512K -cart build/LINEIRQ.ROM -romtype Normal -script measure.tcl
#   1 行 = v 回数 (T1 - T0 のカウント、来なければ -)。1 カウント 3.911µs、1 行 ≒ 16.25 カウント
set out_dir $::env(TH_OUT)
set mute on
set throttle off
proc peek8 {a} { debug read memory $a }
proc peek16 {a} { expr {[peek8 $a] | ([peek8 [expr {$a + 1}]] << 8)} }
proc check {} {
    if {[peek16 0xE022] == 0x5AA5} {
        set f [open [file join $::out_dir lineirq.txt] w]
        for {set v 0} {$v < 256} {incr v} {
            set n [peek8 [expr {0xE400 + $v}]]
            set t [peek16 [expr {0xE200 + $v * 2}]]
            puts $f "$v $n [expr {$n ? $t : "-"}]"
        }
        close $f
        exit
    }
    after time 0.5 check
}
after time 3.0 check
