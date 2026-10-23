# ============================================================================
#  TrolleyHomeRun — MSX turbo R 専用 3D レールシューター ビルド定義
#
#  make            ASCII8 メガロム build/TrolleyHomeRun.rom を生成(WebMSX 用に "… [ASCII8].rom" も)
#  make fpsrom     計測用 ROM build/fps/TrolleyHomeRun_FPS [ASCII8].rom(fps 表示、タイトルに CPU の状態)
#  make shotrom    景色を確かめる ROM build/shot/TrolleyHomeRun_SHOT [ASCII8].rom(敵を出さない)
#  make vdpbench   VDP への書き込みの速さを測る ROM build/vdpb/TrolleyHomeRun_VDPBENCH [ASCII8].rom
#  make run        openMSX(開発用機種 TH_turboR_dev)で起動して目視
#  make test       openMSX で起動〜ゲーム進行を判定(tools/test_game.tcl)
#  make flow       自動で遊んで流れを判定(tools/test_flow.tcl、GOD=1 でライフを減らさない)
#  make slotcheck  ROM 上で動くコードが無いか・DRAM モードかの確認(tools/slotcheck.tcl)
#  make perf       速度の計測(tools/perf.tcl)
#  make pcprof     PC サンプリングの統計プロファイル(tools/pcprof.tcl)
#  make machine    開発用機種定義を ~/.openMSX/share/machines にコピー(初回のみ)
# ============================================================================

# 失敗したレシピの出力を残さない(古い生成物で計測してしまうのを防ぐ)
.DELETE_ON_ERROR:
# 途中の .rel などを消さない(モジュールのリンクで何度も使う)
.SECONDARY:

CC      = sdcc
AS      = sdasz80
PYTHON  = python3
OPENMSX = openmsx

BUILD  ?= build
BOOT    = src/boot
ENG     = src/engine
TOOLS   = tools

# openMSX の実行ディレクトリ。-script のパスでプロセスを絞り込めるよう、
# セッションごとに別ディレクトリを渡せるようにしてある(pkill -f openmsx は使わない)。
RUNDIR  ?= $(BUILD)/run
TIMEOUT ?= 120
MACHINE ?= TH_turboR_dev

# C は R800 向け(-mr800: 掛け算に MULUB / MULUW を使う)。--max-allocs-per-node を上げるとレジスタの割り当てが良くなり、
# 速く小さくなる(5000 → 100000 で 1 フレーム約 −2ms。全体のビルドは約 3.5 分。docs/SDCCとR800の調査.md)。
# Z80 で動く C(Z80 転送の間に割り込み処理から呼ぶ音の処理 sound.c)に R800 専用の命令が入らないよう、ビルドで検査する
# (下の sound.rel。Z80 は MULUB / MULUW を実行できず、黙って結果が壊れる。Z80 向けと R800 向けのオブジェクトは
# SDCC のリンカが混ぜられないので、Z80 向けにコンパイルする手は取れない)
CPORT   = -mr800
CFLAGS  = $(CPORT) --std-c11 -I$(ENG) -I$(BUILD) --opt-code-speed --max-allocs-per-node 100000 $(EXTRA_CFLAGS)
# WebMSX 用のコピーの名前(マッパーの種類をファイル名で指定する)
ROMNAME ?= TrolleyHomeRun
# 常駐部のコードは RAM ページ1 0x4000-0x7FFF、変数は 0xCC00-0xDFFF(仕様書 7.3)。
# ページ2(0x8000-0xBFFF)はモジュール(オーバーレイ): 地形(env.c)とステージごとのゲーム(game.c・models.c・ステージのデータ)
# 定数の表(gen_data.c)とタイトル・隠しコンフィグ(menu.c)はエリア _PAGE0 としてページ0の RAM 0x1A00- に置く
# (ROM のバンク 1 から起動時に読み込む)
LDFLAGS = -mr800 --no-std-crt0 --code-loc 0x4008 --data-loc 0xCC00 -Wl-b_PAGE0=0x1A00 -Wl-j -Wl-m -Wl-u
P0FLAGS = --constseg PAGE0

ASM_SRCS = $(ENG)/crt0.s $(ENG)/sys.s $(ENG)/vdp.s $(ENG)/gfx.s $(ENG)/math.s $(ENG)/raster.s $(ENG)/geom.s $(ENG)/pcm.s
C_SRCS   = $(ENG)/main.c $(ENG)/render.c $(ENG)/rail.c $(ENG)/shared.c \
           $(ENG)/hud.c $(ENG)/sound.c $(ENG)/trolley.c $(ENG)/menu.c $(ENG)/fx.c $(ENG)/vdpbench.c
# モジュールのリンク: 0x8000 に先頭(modhead)、変数は 0xBC00-0xBFFF。常駐部のシンボルは ressyms(engine.noi から生成)で渡す
MODFLAGS = -mr800 --no-std-crt0 --data-loc 0xBC00 -Wl-j -Wl-m -Wl-u

# crt0 は必ず先頭にリンクする
RELS = $(BUILD)/crt0.rel \
       $(patsubst $(ENG)/%.s,$(BUILD)/%.rel,$(filter-out $(ENG)/crt0.s,$(ASM_SRCS))) \
       $(patsubst $(ENG)/%.c,$(BUILD)/%.rel,$(C_SRCS)) \
       $(BUILD)/gen_data.rel

ifdef BENCH
  C_SRCS += $(ENG)/bench.c
  RELS   += $(BUILD)/bench_s.rel
  CFLAGS += -DBENCH
endif


HDRS = $(wildcard $(ENG)/*.h) $(BUILD)/songs.h $(BUILD)/title.h

ROM = $(BUILD)/TrolleyHomeRun.rom

.PHONY: all fpsrom shotrom vdpbench run test flow slotcheck perf pcprof overdraw machine clean

all: $(ROM)

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/gen_data.c: $(TOOLS)/gen_data.py | $(BUILD)
	$(PYTHON) $< $@

# FM 音源の曲(src/sound/songs.mml)→ 音のモジュールの曲の部分(0x8C00-)と曲の番号(songs.h)
$(BUILD)/songs.bin $(BUILD)/songs.h: src/sound/songs.mml $(TOOLS)/gen_music.py | $(BUILD)
	$(PYTHON) $(TOOLS)/gen_music.py src/sound/songs.mml $(BUILD)/songs.bin $(BUILD)/songs.h

$(BUILD)/gen_data.rel: $(BUILD)/gen_data.c $(HDRS) Makefile
	$(CC) $(CFLAGS) $(P0FLAGS) -c -o $@ $<

$(BUILD)/menu.rel: $(ENG)/menu.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) $(P0FLAGS) --codeseg PAGE0 -c -o $@ $<

# ヘッダを変えたら全 C を作り直す(古い構造体レイアウトの混在を防ぐ)
$(BUILD)/%.rel: $(ENG)/%.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

# Z80 でも動く C: R800 専用の命令(multu / multuw)と、Z80 では非公式の命令(IX・IY の半分のレジスタ)が無いことを確かめる
Z80_SAFE_C = sound sndtick
$(patsubst %,$(BUILD)/%.rel,$(Z80_SAFE_C)): $(BUILD)/%.rel: $(ENG)/%.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<
	@if grep -qiE '\b(multu|multuw|ixh|ixl|iyh|iyl)\b' $(BUILD)/$*.asm; then \
	  echo "$<: Z80 でも動くコードに R800 専用・非公式の命令が入った(Makefile の Z80_SAFE_C)"; rm -f $@; exit 1; fi

$(BUILD)/%.rel: $(ENG)/%.s $(ENG)/r800.inc Makefile | $(BUILD)
	$(AS) -g -l -I$(ENG) -o $@ $<

# 計測用(BENCH=1)の asm。gfx.s から分けて、通常のビルドのコード領域を使わない
$(BUILD)/bench_s.rel: $(ENG)/bench.s Makefile | $(BUILD)
	$(AS) -g -l -I$(ENG) -o $@ $<

$(BUILD)/engine.ihx: $(RELS)
	$(CC) $(LDFLAGS) -o $@ $(RELS)

# 常駐部のシンボル(モジュールのリンク用)
$(BUILD)/ressyms.s: $(BUILD)/engine.ihx $(TOOLS)/ressyms.py
	$(PYTHON) $(TOOLS)/ressyms.py $(BUILD)/engine.noi $@

$(BUILD)/ressyms.rel: $(BUILD)/ressyms.s
	$(AS) -l -o $@ $<

# 地形モジュール(マッパーのセグメント 5 に、ステージの始めに読み込む)。ステージごとに、その面の地形の処理だけを入れる
# (env.c を ENV_S1〜ENV_S6 でコンパイルする。鉄橋のトラスと海の asm(truss.s)はステージ 4 だけ)
$(BUILD)/env%.ihx: $(BUILD)/modhead_env.rel $(BUILD)/env_s%.rel $(BUILD)/ressyms.rel
	$(CC) $(MODFLAGS) --code-loc 0x8006 -o $@ $^

$(BUILD)/env4.ihx: $(BUILD)/modhead_env.rel $(BUILD)/env_s4.rel $(BUILD)/truss.rel $(BUILD)/ressyms.rel
	$(CC) $(MODFLAGS) --code-loc 0x8006 -o $@ $^

$(BUILD)/env_s%.rel: $(ENG)/env.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -DENV_S$* -c -o $@ $<

# 場面のモジュール(マッパーのセグメント 10 に置く。エンディング・ゲームオーバー・隠しコンフィグ・SCREEN 3 の作り直し)
$(BUILD)/scenemod.ihx: $(BUILD)/modhead_scene.rel $(BUILD)/scene.rel $(BUILD)/ressyms.rel
	$(CC) $(MODFLAGS) --code-loc 0x800C -o $@ $^

# 音のモジュール(マッパーのセグメント 8 に置く。効果音・ドラムのノイズ・PSG のパート・FM の曲のドライバ。曲のデータは 0x8C00- に
# tools/mkrom.py が合わせる)。割り込み(Z80 転送の間も)から動くので、C は Z80_SAFE_C で確かめる
$(BUILD)/sndmod.ihx: $(BUILD)/modhead_snd.rel $(BUILD)/sndtick.rel $(BUILD)/sndfm.rel $(BUILD)/ressyms.rel
	$(CC) $(MODFLAGS) --code-loc 0x8009 -o $@ $^

# ステージごとのゲームモジュール(ページ2 = セグメント 1 に、ステージの始めに読み込む)
STG     = src/stage
$(BUILD)/game%.ihx: $(BUILD)/modhead_game.rel $(BUILD)/game.rel $(BUILD)/models.rel $(BUILD)/stage%.rel $(BUILD)/aimhit.rel $(BUILD)/ressyms.rel
	$(CC) $(MODFLAGS) --code-loc 0x8005 -o $@ $^

# 海の面(ステージ 4 の鉄橋)だけの敵(半魚人・大ダコ人とその足)は、GAME_SEA を付けてコンパイルした game.c・models.c に入れる
# (ゲームモジュールは game.c をステージごとにリンクするので、ほかの面のモジュールの空きを使わない。ステージ 1 は残り 120 B ほど)
$(BUILD)/game4.ihx: $(BUILD)/modhead_game.rel $(BUILD)/game_sea.rel $(BUILD)/models_sea.rel $(BUILD)/stage4.rel $(BUILD)/aimhit.rel $(BUILD)/ressyms.rel
	$(CC) $(MODFLAGS) --code-loc 0x8005 -o $@ $^

$(BUILD)/%_sea.rel: $(ENG)/%.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -DGAME_SEA -c -o $@ $<

# ステージ 1 のモジュールは置き場が一番きついので、ステージ 1 に出ない敵の絵を外した models.c をリンクする(GAME_S1)
$(BUILD)/game1.ihx: $(BUILD)/modhead_game.rel $(BUILD)/game.rel $(BUILD)/models_s1.rel $(BUILD)/stage1.rel $(BUILD)/aimhit.rel $(BUILD)/ressyms.rel
	$(CC) $(MODFLAGS) --code-loc 0x8005 -o $@ $^

$(BUILD)/%_s1.rel: $(ENG)/%.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -DGAME_S1 -c -o $@ $<

$(BUILD)/stage1.rel: $(STG)/stage1_cave.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/stage2.rel: $(STG)/stage2_lake.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/stage3.rel: $(STG)/stage3_forest.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/stage4.rel: $(STG)/stage4_bridge.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/stage5.rel: $(STG)/stage5_town.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/stage6.rel: $(STG)/stage6_home.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/boot.rel: $(BOOT)/boot.s Makefile | $(BUILD)
	$(AS) -g -o $@ $<

$(BUILD)/boot.ihx: $(BUILD)/boot.rel
	$(CC) -mz80 --no-std-crt0 --code-loc 0x4000 --data-loc 0xC000 -o $@ $<

# タイトルの絵(SCREEN 11。data/title_art.webp を YJK + YAE に。文字は tools/gen_title.py が重ねる)
$(BUILD)/title.bin $(BUILD)/title.h &: data/title_art.webp $(TOOLS)/gen_title.py | $(BUILD)
	$(PYTHON) $(TOOLS)/gen_title.py data/title_art.webp $(BUILD)/title.bin $(BUILD)/title.h

GAMES = $(foreach n,1 2 3 4 5 6,$(BUILD)/game$(n).ihx)
ENVS  = $(foreach n,1 2 3 4 5 6,$(BUILD)/env$(n).ihx)

$(ROM): $(BUILD)/boot.ihx $(BUILD)/engine.ihx $(ENVS) $(GAMES) $(BUILD)/sndmod.ihx $(BUILD)/songs.bin src/sound/voicemod.bin $(BUILD)/title.bin $(BUILD)/scenemod.ihx $(TOOLS)/mkrom.py
	$(PYTHON) $(TOOLS)/mkrom.py $(BUILD)/boot.ihx $(BUILD)/engine.ihx $(BUILD)/engine.noi $@ \
	  $(foreach n,1 2 3 4 5 6,$(BUILD)/env$(n).ihx $(BUILD)/env$(n).noi) \
	  $(foreach n,1 2 3 4 5 6,$(BUILD)/game$(n).ihx $(BUILD)/game$(n).noi) $(BUILD)/sndmod.ihx $(BUILD)/sndmod.noi $(BUILD)/songs.bin src/sound/voicemod.bin $(BUILD)/title.bin \
	  $(BUILD)/scenemod.ihx $(BUILD)/scenemod.noi
	cp $@ "$(BUILD)/$(ROMNAME) [ASCII8].rom"

# 計測用 ROM(fps 表示、タイトルに CPU の状態)。オブジェクトが混ざらないよう build/fps に分けてビルドする
fpsrom:
	$(MAKE) BUILD=build/fps EXTRA_CFLAGS=-DDEBUG_FPS ROMNAME=TrolleyHomeRun_FPS

# 景色を確かめる ROM(敵を出さない。SHOT_NO_FOES)。オブジェクトが混ざらないよう build/shot に分けてビルドする
shotrom:
	$(MAKE) BUILD=build/shot EXTRA_CFLAGS=-DSHOT_NO_FOES ROMNAME=TrolleyHomeRun_SHOT

# VDP への書き込みの速さ(表示中・垂直帰線中・画面オフ・50Hz)を測って画面に出す ROM(src/engine/vdpbench.c)
vdpbench:
	$(MAKE) BUILD=build/vdpb EXTRA_CFLAGS=-DVDPBENCH ROMNAME=TrolleyHomeRun_VDPBENCH

machine:
	cp $(TOOLS)/openmsx/TH_turboR_dev.xml $(HOME)/.openMSX/share/machines/

OPENMSX_ROM = $(OPENMSX) -machine $(MACHINE) -carta $(abspath $(ROM)) -romtype ASCII8

run: $(ROM)
	$(OPENMSX_ROM)

# ヘッドレスの判定。結果は $(RUNDIR)/result.txt、スクリーンショットは $(RUNDIR)/*.png
test: $(ROM)
	@mkdir -p $(RUNDIR)
	cp $(TOOLS)/test_game.tcl $(RUNDIR)/
	TH_NOI=$(abspath $(BUILD)/engine.noi) TH_OUT=$(abspath $(RUNDIR)) \
	  timeout $(TIMEOUT) $(OPENMSX_ROM) -script $(abspath $(RUNDIR))/test_game.tcl >/dev/null 2>&1 || true
	@cat $(RUNDIR)/result.txt

# 自動で遊んでタイトル〜オープニング〜ボス〜エンディングの流れを確かめる(tools/test_flow.tcl)。GOD=1 でライフを減らさない
flow: $(ROM)
	@mkdir -p $(RUNDIR)
	cp $(TOOLS)/test_flow.tcl $(RUNDIR)/
	TH_NOI=$(abspath $(BUILD)/engine.noi) TH_OUT=$(abspath $(RUNDIR)) TH_GOD=$(GOD) \
	  timeout $(TIMEOUT) $(OPENMSX_ROM) -script $(abspath $(RUNDIR))/test_flow.tcl >/dev/null 2>&1 || true
	@cat $(RUNDIR)/flow.txt

# 実行中のコードがどのスロットにあるか(ROM 上で動く処理が無いか)と DRAM モードの確認(tools/slotcheck.tcl)
slotcheck: $(ROM)
	@mkdir -p $(RUNDIR)
	cp $(TOOLS)/slotcheck.tcl $(RUNDIR)/
	TH_NOI=$(abspath $(BUILD)/engine.noi) TH_OUT=$(abspath $(RUNDIR)) \
	  timeout $(TIMEOUT) $(OPENMSX_ROM) -script $(abspath $(RUNDIR))/slotcheck.tcl >/dev/null 2>&1 || true
	@cat $(RUNDIR)/slotcheck.txt

# 速度の計測(tools/perf.tcl)。PERF_FN="_env_draw _raster" のように関数を渡すと区間ごとの時間も出す
perf: $(ROM)
	@mkdir -p $(RUNDIR)
	cp $(TOOLS)/perf.tcl $(RUNDIR)/
	TH_NOI=$(abspath $(BUILD)/engine.noi) TH_OUT=$(abspath $(RUNDIR)) PERF_FN="$(PERF_FN)" \
	  timeout $(TIMEOUT) $(OPENMSX_ROM) -script $(abspath $(RUNDIR))/perf.tcl >/dev/null 2>&1 || true
	@cat $(RUNDIR)/perf.txt

# PC のサンプリングによる統計プロファイル(tools/pcprof.tcl)。関数・ローカルラベル単位の比率
pcprof: $(ROM)
	@mkdir -p $(RUNDIR)
	$(PYTHON) $(TOOLS)/prof_syms.py $(BUILD) $(BUILD)/prof_syms.txt $(or $(PROF_STAGE),1)
	cp $(TOOLS)/pcprof.tcl $(RUNDIR)/
	TH_NOI=$(abspath $(BUILD)/engine.noi) TH_OUT=$(abspath $(RUNDIR)) PROF_SYMS=$(abspath $(BUILD)/prof_syms.txt) \
	  timeout $(TIMEOUT) $(OPENMSX_ROM) -script $(abspath $(RUNDIR))/pcprof.tcl >/dev/null 2>&1 || true
	@head -40 $(RUNDIR)/pcprof.txt

# 重ね塗りの計測(tools/overdraw.tcl)。STAGE=3 のように面を選ぶ
STAGE ?= 3
overdraw: $(ROM)
	@mkdir -p $(RUNDIR)
	$(PYTHON) $(TOOLS)/prof_syms.py $(BUILD) $(BUILD)/prof_syms.txt $(STAGE)
	cp $(TOOLS)/overdraw.tcl $(RUNDIR)/
	TH_NOI=$(abspath $(BUILD)/engine.noi) TH_OUT=$(abspath $(RUNDIR)) PROF_SYMS=$(abspath $(BUILD)/prof_syms.txt) \
	  PROF_STAGE=$(STAGE) OD_FRAMES="60 900 20" ALT_DRAW=$$(awk '/DEF _alt_draw/ {print $$3}' $(BUILD)/game$(STAGE).noi) \
	  timeout $(TIMEOUT) $(OPENMSX_ROM) -script $(abspath $(RUNDIR))/overdraw.tcl >/dev/null 2>&1 || true
	@head -20 $(RUNDIR)/od.txt

clean:
	rm -rf $(BUILD)
