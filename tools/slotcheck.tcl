# slotcheck.tcl — 実行中のコードがどのスロットにあるかを調べる(ROM 上で動いている処理が無いかの確認)
#
#   TH_NOI=build/engine.noi TH_OUT=<出力ディレクトリ> \
#     openmsx -machine TH_turboR_dev -carta build/TrolleyHomeRun.rom -romtype ASCII8 -script slotcheck.tcl
#
# タイトル画面(t=5.5〜6.5)とゲーム中(t=9〜12)で、エミュレータ時刻 10µs ごとに PC を読み、
# PC のあるページにいま選ばれているスロットで分類して数える:
#   カートリッジ(スロット 1)/ RAM(スロット 3-0)/ BIOS(C-BIOS、スロット 0 と 3-1)。
# あわせて S1990 のレジスタ 6(bit5 = 0 で R800、bit6 = 0 で DRAM モード)を記録する。
# カートリッジ上の実行があれば、その PC とシンボルも出す。結果は $TH_OUT/slotcheck.txt。

set out_dir $::env(TH_OUT)
set mute on
array set ::syms {}
set fh [open $::env(TH_NOI) r]
foreach line [split [read $fh] "\n"] {
    if {[regexp {^DEF (\S+) 0x([0-9A-Fa-f]+)} $line -> name hex]} { set ::syms($name) [expr {"0x$hex"}] }
}
close $fh

proc slot_of_pc {} {
    foreach {ps ss name} {1 X cart 3 0 ram 0 X bios 3 1 bios_sub} {
        if {$ss eq "X"} { set r [pc_in_slot $ps] } else { set r [pc_in_slot $ps $ss] }
        if {$r} { return $name }
    }
    return other
}

array set ::cnt {}
array set ::cartpc {}
set ::n 0
proc sample {tag limit} {
    set k [slot_of_pc]
    if {[info exists ::cnt($tag,$k)]} { incr ::cnt($tag,$k) } else { set ::cnt($tag,$k) 1 }
    if {$k eq "cart"} {
        set pc [format %04X [reg PC]]
        if {[info exists ::cartpc($pc)]} { incr ::cartpc($pc) } else { set ::cartpc($pc) 1 }
    }
    incr ::n
    if {$::n < $limit} { after time 0.00001 [list sample $tag $limit] } else { set ::n 0; $::next }
}
proc report {} {
    set f [open [file join $::out_dir slotcheck.txt] w]
    puts $f "S1990 reg6 = [format %02X [debug read {S1990 regs} 6]] (bit5: 0=R800 / bit6: 0=DRAM モード)"
    foreach tag {title game} {
        set tot 0
        foreach k {cart ram bios bios_sub other} { if {[info exists ::cnt($tag,$k)]} { incr tot $::cnt($tag,$k) } }
        set line "$tag:"
        foreach k {cart ram bios bios_sub other} {
            set c [expr {[info exists ::cnt($tag,$k)] ? $::cnt($tag,$k) : 0}]
            append line [format "  %s %.2f%%" $k [expr {100.0 * $c / $tot}]]
        }
        puts $f $line
    }
    foreach pc [lsort [array names ::cartpc]] { puts $f "カートリッジ上の実行: PC=$pc 回数=$::cartpc($pc)" }
    close $f
    exit
}
after time 5.5 {set ::next {after time 0.1 {keymatrixdown 8 1; after time 0.2 {keymatrixup 8 1}}; }; sample title 100000}
after time 9.0 {set ::next report; sample game 300000}
