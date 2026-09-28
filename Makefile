# ============================================================================
#  TrolleyHomeRun — MSX turbo R 専用 3D レールシューター ビルド定義
#
#  make            ASCII8 メガロム build/TrolleyHomeRun.rom を生成(WebMSX 用に "… [ASCII8].rom" も)
#  make fpsrom     計測用 ROM build/fps/TrolleyHomeRun_FPS [ASCII8].rom(fps 表示、タイトルに CPU の状態)
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

CFLAGS  = -mz80 --std-c11 -I$(ENG) --opt-code-speed --max-allocs-per-node 5000 $(EXTRA_CFLAGS)
# WebMSX 用のコピーの名前(マッパーの種類をファイル名で指定する)
ROMNAME ?= TrolleyHomeRun
# 常駐部のコードは RAM ページ1 0x4000-0x7FFF、変数は 0xCC00-0xDFFF(仕様書 7.3)。
# ページ2(0x8000-0xBFFF)はモジュール(オーバーレイ): 地形(env.c)とステージごとのゲーム(game.c・models.c・ステージのデータ)
# 定数の表(gen_data.c・models.c)とタイトル・隠しコンフィグ(menu.c)はエリア _PAGE0 としてページ0の RAM 0x2C80- に置く
# (ROM のバンク 1 から起動時に読み込む)
LDFLAGS = -mz80 --no-std-crt0 --code-loc 0x4008 --data-loc 0xCC00 -Wl-b_PAGE0=0x2C80 -Wl-j -Wl-m -Wl-u
P0FLAGS = --constseg PAGE0

ASM_SRCS = $(ENG)/crt0.s $(ENG)/sys.s $(ENG)/vdp.s $(ENG)/gfx.s $(ENG)/math.s $(ENG)/raster.s $(ENG)/geom.s
C_SRCS   = $(ENG)/main.c $(ENG)/render.c $(ENG)/rail.c $(ENG)/shared.c \
           $(ENG)/hud.c $(ENG)/sound.c $(ENG)/trolley.c $(ENG)/menu.c
# モジュールのリンク: 0x8000 に先頭(modhead)、変数は 0xBC00-0xBFFF。常駐部のシンボルは ressyms(engine.noi から生成)で渡す
MODFLAGS = -mz80 --no-std-crt0 --data-loc 0xBC00 -Wl-j -Wl-m

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


HDRS = $(wildcard $(ENG)/*.h)

ROM = $(BUILD)/TrolleyHomeRun.rom

.PHONY: all fpsrom run test flow slotcheck perf pcprof machine clean

all: $(ROM)

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/gen_data.c: $(TOOLS)/gen_data.py | $(BUILD)
	$(PYTHON) $< $@

$(BUILD)/gen_data.rel: $(BUILD)/gen_data.c $(HDRS) Makefile
	$(CC) $(CFLAGS) $(P0FLAGS) -c -o $@ $<

$(BUILD)/menu.rel: $(ENG)/menu.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) $(P0FLAGS) --codeseg PAGE0 -c -o $@ $<

# ヘッダを変えたら全 C を作り直す(古い構造体レイアウトの混在を防ぐ)
$(BUILD)/%.rel: $(ENG)/%.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

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
	$(AS) -o $@ $<

# 地形モジュール(マッパーのセグメント 5 に置く)
$(BUILD)/envmod.ihx: $(BUILD)/modhead_env.rel $(BUILD)/env.rel $(BUILD)/ressyms.rel
	$(CC) $(MODFLAGS) --code-loc 0x8006 -o $@ $^

# ステージごとのゲームモジュール(ページ2 = セグメント 1 に、ステージの始めに読み込む)
STG     = src/stage
$(BUILD)/game%.ihx: $(BUILD)/modhead_game.rel $(BUILD)/game.rel $(BUILD)/models.rel $(BUILD)/stage%.rel $(BUILD)/ressyms.rel
	$(CC) $(MODFLAGS) --code-loc 0x8005 -o $@ $^

$(BUILD)/stage1.rel: $(STG)/stage1_cave.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/stage2.rel: $(STG)/stage2_lake.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/stage3.rel: $(STG)/stage3_forest.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/stage4.rel: $(STG)/stage4_town.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/stage5.rel: $(STG)/stage5_home.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/boot.rel: $(BOOT)/boot.s Makefile | $(BUILD)
	$(AS) -g -o $@ $<

$(BUILD)/boot.ihx: $(BUILD)/boot.rel
	$(CC) -mz80 --no-std-crt0 --code-loc 0x4000 --data-loc 0xC000 -o $@ $<

GAMES = $(foreach n,1 2 3 4 5,$(BUILD)/game$(n).ihx)

$(ROM): $(BUILD)/boot.ihx $(BUILD)/engine.ihx $(BUILD)/envmod.ihx $(GAMES) $(TOOLS)/mkrom.py
	$(PYTHON) $(TOOLS)/mkrom.py $(BUILD)/boot.ihx $(BUILD)/engine.ihx $(BUILD)/engine.noi $@ \
	  $(BUILD)/envmod.ihx $(BUILD)/envmod.noi \
	  $(foreach n,1 2 3 4 5,$(BUILD)/game$(n).ihx $(BUILD)/game$(n).noi)
	cp $@ "$(BUILD)/$(ROMNAME) [ASCII8].rom"

# 計測用 ROM(fps 表示、タイトルに CPU の状態)。オブジェクトが混ざらないよう build/fps に分けてビルドする
fpsrom:
	$(MAKE) BUILD=build/fps EXTRA_CFLAGS=-DDEBUG_FPS ROMNAME=TrolleyHomeRun_FPS

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
	$(PYTHON) $(TOOLS)/prof_syms.py $(BUILD)/engine.noi $(BUILD) $(BUILD)/prof_syms.txt
	cp $(TOOLS)/pcprof.tcl $(RUNDIR)/
	TH_NOI=$(abspath $(BUILD)/engine.noi) TH_OUT=$(abspath $(RUNDIR)) PROF_SYMS=$(abspath $(BUILD)/prof_syms.txt) \
	  timeout $(TIMEOUT) $(OPENMSX_ROM) -script $(abspath $(RUNDIR))/pcprof.tcl >/dev/null 2>&1 || true
	@head -40 $(RUNDIR)/pcprof.txt

clean:
	rm -rf $(BUILD)
