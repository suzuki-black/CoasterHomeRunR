# TROLLEY HOME RUN（試作版）

MSX turbo R 専用の 3D レールシューター。SCREEN 3（マルチカラー 64×48）/ ASCII8 メガロム 128KB / R800 DRAM モード。
仕様は [docs/TrolleyHomeRun_Spec.md](docs/TrolleyHomeRun_Spec.md)、計測と経緯は [docs/検証記録.md](docs/検証記録.md)。

## いまできること

- 起動 → R800 DRAM モード → ROM からエンジンを RAM へ写して実行
- 洞窟ステージ（六角形のトンネル・木の枠・カーブと起伏のあるレール）を 3D で走る
- 照準・射撃・前後の振り向き、猿人（E01）とコウモリ人（E02）、ボスの大猿人（B01）
- 画面下部のトロッコの縁。ライフが減るごとに壊れていく
- ライフゲージ、警告表示、スコア、効果音（PSG ch C）、ゲームオーバー → タイトル

未実装: 洞窟以外のステージ、E03〜E05・敵弾・救急箱の配置、B02〜B04、BGM、SCREEN 1 のエンディング、マウス（TODO）。
フレームレートは openMSX で 12〜16fps（目標 30fps に未達）。

## 操作

| 操作 | キーボード | ジョイスティック（ポート1） |
|---|---|---|
| 照準 | カーソルキー | 方向 |
| 射撃 | スペース | A ボタン |
| 前後切り替え | M または B | B ボタン |

## ビルドと実行

必要なもの: SDCC 4.6（sdcc / sdasz80）、Python 3、openMSX。

```bash
make machine
```

開発用機種 `TH_turboR_dev` を `~/.openMSX/share/machines/` へコピーする（初回のみ）。
C-BIOS を turbo R のハードで動かす機種で、RAM は FS-A1ST と同じスロット 3-0 にある
（3-2 だと openMSX の R800 が 2〜3 倍遅く測れる）。

```bash
make
```

`build/TrolleyHomeRun.rom`（ASCII8、128KB）ができる。

```bash
make run
```

openMSX で起動して遊ぶ。

```bash
make test RUNDIR=/path/to/scratch
```

ヘッドレスではなくウィンドウありで約 1 分走らせ、タイトル〜ゲームオーバー〜タイトルを判定する（結果と画面は RUNDIR）。

## 構成

```
src/boot/boot.s       ROM bank0: 起動・スロット検出・CPU 切替・ローダー
src/engine/           RAM 実行部(0x4000-0xBFFF)
  crt0.s              エンジンの入口
  sys.s               H.TIMI 割り込み・入力・PSG・タイマ
  vdp.s               SCREEN 3 の転送(パック変換つき)
  raster.s            投影と凸多角形の塗り(ホットパス)
  gfx.s math.s        バッファ塗り・矩形、固定小数点演算(MULUW)
  render.c env.c      カメラ・ニアクリップ、トンネル
  rail.c              レール表
  game.c              プレイヤー・敵・当たり判定・スクリプト
  trolley.c hud.c     トロッコの縁、文字・ゲージ
  sound.c             効果音ドライバ
  stage_cave.c        洞窟ステージのデータ
tools/gen_data.py     sin 表・フォント・スプライト・逆数表・トロッコの絵
tools/mkrom.py        ROM の組み立て(配置の検査つき)
tools/test_game.tcl   openMSX の自動テスト
tools/openmsx/        開発用機種定義
```
