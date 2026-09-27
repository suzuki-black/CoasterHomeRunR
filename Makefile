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
# コードは RAM 0x4000-0xBFFF、変数は 0xCC00-0xDFFF(仕様書 7.3)
LDFLAGS = -mz80 --no-std-crt0 --code-loc 0x4008 --data-loc 0xCC00 -Wl-j -Wl-m -Wl-u

ASM_SRCS = $(ENG)/crt0.s $(ENG)/sys.s $(ENG)/vdp.s $(ENG)/gfx.s $(ENG)/math.s $(ENG)/raster.s $(ENG)/geom.s
C_SRCS   = $(ENG)/main.c $(ENG)/game.c $(ENG)/render.c $(ENG)/rail.c $(ENG)/env.c \
           $(ENG)/hud.c $(ENG)/sound.c $(ENG)/trolley.c

# crt0 は必ず先頭にリンクする
RELS = $(BUILD)/crt0.rel \
       $(patsubst $(ENG)/%.s,$(BUILD)/%.rel,$(filter-out $(ENG)/crt0.s,$(ASM_SRCS))) \
       $(patsubst $(ENG)/%.c,$(BUILD)/%.rel,$(C_SRCS)) \
       $(BUILD)/gen_data.rel

ifdef BENCH
  C_SRCS += $(ENG)/bench.c
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
	$(CC) $(CFLAGS) -c -o $@ $<

# ヘッダを変えたら全 C を作り直す(古い構造体レイアウトの混在を防ぐ)
$(BUILD)/%.rel: $(ENG)/%.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.rel: $(ENG)/%.s $(ENG)/r800.inc Makefile | $(BUILD)
	$(AS) -g -l -I$(ENG) -o $@ $<

$(BUILD)/engine.ihx: $(RELS)
	$(CC) $(LDFLAGS) -o $@ $(RELS)

# ステージ: 読み込み先(0xB400)に合わせて単独でリンクし、ROM のバンクに置く(tools/mkrom.py --stage)
STG     = src/stage
$(BUILD)/stagehead.rel: $(STG)/stagehead.s Makefile | $(BUILD)
	$(AS) -g -o $@ $<

$(BUILD)/stage1.rel: $(STG)/stage1_cave.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/stage2.rel: $(STG)/stage2_river.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/stage3.rel: $(STG)/stage3_forest.c $(HDRS) Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/stage%.ihx: $(BUILD)/stagehead.rel $(BUILD)/stage%.rel
	$(CC) -mz80 --no-std-crt0 --code-loc 0xB402 --data-loc 0xC000 -o $@ $^

$(BUILD)/boot.rel: $(BOOT)/boot.s Makefile | $(BUILD)
	$(AS) -g -o $@ $<

$(BUILD)/boot.ihx: $(BUILD)/boot.rel
	$(CC) -mz80 --no-std-crt0 --code-loc 0x4000 --data-loc 0xC000 -o $@ $<

STAGES = $(BUILD)/stage1.ihx $(BUILD)/stage2.ihx $(BUILD)/stage3.ihx

$(ROM): $(BUILD)/boot.ihx $(BUILD)/engine.ihx $(STAGES) $(TOOLS)/mkrom.py
	$(PYTHON) $(TOOLS)/mkrom.py $(BUILD)/boot.ihx $(BUILD)/engine.ihx $(BUILD)/engine.noi $@ \
	  --stage 6 $(BUILD)/stage1.ihx --stage 8 $(BUILD)/stage2.ihx --stage 10 $(BUILD)/stage3.ihx
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
