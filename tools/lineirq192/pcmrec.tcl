# pcmrec.tcl — build/PCMTEST.ROM で声を 3 回鳴らし、音を録り、A4h に書いた時刻と行の記録を書き出す
#   TH_OUT=<出力ディレクトリ> [DIW=<割込みを止めるカウント/フレーム>] \
#     openmsx -machine My_FS-A1ST_512K -cart build/PCMTEST.ROM -romtype Normal -script pcmrec.tcl
#   $TH_OUT/pcm.wav(録音)、$TH_OUT/pcmlog.txt(1 行 = A4h に書いた時刻(システムタイマ) とそのときの行(9.7))。
#   最後の「# main_idle / main_play」は、鳴らす前と鳴らし始めてからの各 0.4 秒にメインのループが回った回数
#   (割込みがどれだけ CPU を使うかの目安)
set out_dir $::env(TH_OUT)
set throttle off
proc peek8 {a} { debug read memory $a }
proc peek16 {a} { expr {[peek8 $a] | ([peek8 [expr {$a + 1}]] << 8)} }
proc poke16 {a v} { debug write memory $a [expr {$v & 255}]; debug write memory [expr {$a + 1}] [expr {($v >> 8) & 255}] }
proc cnt {} { peek16 0xE030 }
set ::plays 0
proc wait_init {} {
    if {[peek16 0xE020] != 0xA55A} { after time 0.2 wait_init; return }
    if {[info exists ::env(DIW)]} { poke16 0xE002 $::env(DIW) }
    record start -audioonly [file join $::out_dir pcm.wav]
    after time 0.3 idle
}
proc idle {} {
    set ::c_idle0 [cnt]
    after time 0.4 {set ::c_idle1 [cnt]}
    after time 0.5 play
}
proc play {} {
    if {$::plays == 0} {
        set ::c_play0 [cnt]
        after time 0.4 {set ::c_play1 [cnt]}
    }
    debug write memory 0xE000 1
    incr ::plays
    if {$::plays < 3} { after time 0.8 play } else { after time 1.0 finish }
}
proc finish {} {
    record stop
    set f [open [file join $::out_dir pcmlog.txt] w]
    set end [peek16 0xE010]
    for {set a 0xC000} {$a < $end} {incr a 4} { puts $f "[peek16 $a] [peek16 [expr {$a + 2}]]" }
    puts $f "# skip [peek8 0xE028]"
    puts $f "# main_idle [expr {($::c_idle1 - $::c_idle0) & 0xFFFF}] main_play [expr {($::c_play1 - $::c_play0) & 0xFFFF}]"
    close $f
    exit
}
after time 3.0 wait_init
