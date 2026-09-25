# ============================================================================
#  TrolleyHomeRun — MSX turbo R 専用 3D レールシューター ビルド定義
#
#  make            ASCII8 メガロム build/TrolleyHomeRun.rom を生成
#  make run        openMSX(開発用機種 TH_turboR_dev)で起動して目視
#  make test       openMSX ヘッドレスで起動〜ゲーム進行を判定(tools/test_game.tcl)
#  make machine    開発用機種定義を ~/.openMSX/share/machines にコピー(初回のみ)
# ============================================================================

# 失敗したレシピの出力を残さない(古い生成物で計測してしまうのを防ぐ)
.DELETE_ON_ERROR:

CC      = sdcc
AS      = sdasz80
PYTHON  = python3
OPENMSX = openmsx

BUILD   = build
BOOT    = src/boot
ENG     = src/engine
TOOLS   = tools

# openMSX の実行ディレクトリ。-script のパスでプロセスを絞り込めるよう、
# セッションごとに別ディレクトリを渡せるようにしてある(pkill -f openmsx は使わない)。
RUNDIR  ?= $(BUILD)/run
TIMEOUT ?= 120
MACHINE ?= TH_turboR_dev

CFLAGS  = -mz80 --std-c11 -I$(ENG) --opt-code-speed --max-allocs-per-node 5000
# コードは RAM 0x4000-0xBFFF、変数は 0xCC00-0xDFFF(仕様書 7.3)
LDFLAGS = -mz80 --no-std-crt0 --code-loc 0x4008 --data-loc 0xCC00 -Wl-j -Wl-m

ASM_SRCS = $(ENG)/crt0.s $(ENG)/sys.s $(ENG)/vdp.s $(ENG)/gfx.s $(ENG)/math.s $(ENG)/raster.s
C_SRCS   = $(ENG)/main.c $(ENG)/game.c $(ENG)/render.c $(ENG)/rail.c $(ENG)/env.c \
           $(ENG)/hud.c $(ENG)/sound.c $(ENG)/stage_cave.c $(ENG)/trolley.c

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

.PHONY: all run test machine clean

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
	$(AS) -g -I$(ENG) -o $@ $<

$(BUILD)/engine.ihx: $(RELS)
	$(CC) $(LDFLAGS) -o $@ $(RELS)

$(BUILD)/boot.rel: $(BOOT)/boot.s Makefile | $(BUILD)
	$(AS) -g -o $@ $<

$(BUILD)/boot.ihx: $(BUILD)/boot.rel
	$(CC) -mz80 --no-std-crt0 --code-loc 0x4000 --data-loc 0xC000 -o $@ $<

$(ROM): $(BUILD)/boot.ihx $(BUILD)/engine.ihx $(TOOLS)/mkrom.py
	$(PYTHON) $(TOOLS)/mkrom.py $(BUILD)/boot.ihx $(BUILD)/engine.ihx $(BUILD)/engine.noi $@

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

clean:
	rm -rf $(BUILD)
