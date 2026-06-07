# gm_extract — GM音源抽出ツールチェーン (C++ / CMake / Windows対応)

GeneralUser GS (または任意のGM互換 .sf2) から、iFFT_Orgel の
`voice_table.cpp` / `voices/voice_table.h` / `voices/voice_harm_data.h` を
自動生成します。問題点 #2(GM再取り込み)・#3(ピアノ音割れ)・#4(トランペット小)を
これ一本で解決します。

**外部ライブラリ依存ゼロ**(SF2レンダラ `tsf.h` を vendor、倍音抽出は Goertzel)。
Windows / PowerShell から `cmake → ビルド → 実行` だけで通ります。

---

## 1. 何が変わるか(実測値)

TimGM6mb.sf2(全128プログラム入りのGM音源)での生成結果:

| 項目 | 旧 voice_table | 新生成 (本ツール) | 効果 |
|------|---------------|---------------|------|
| 倍音データ量 | 253,988 int16 (約496 KB) | 48,094 int16 (**約94 KB**) | **約5.2倍削減** |
| ボイス数 | 約6,048 | 1,217 | 持続音を1/オクターブに集約 |
| ラウドネス | ピーク正規化(楽器ごとにバラバラ) | **RMS等ラウドネス**(全ボイス均一) | trumpet/piano が同音量 |

生成は約4〜10秒(SF2サイズによる)。`example_output/` に TimGM6mb 由来の生成例を同梱。
本番では `GeneralUser-GS.sf2` を渡して再生成してください。

---

## 2. ビルド & 実行 (Windows)

### 前提
- **CMake** が PATH にある (VS 2026 を使うなら **4.1.1+**、VS 2022 なら 3.21+ で十分)
  - VS 2026 にバンドルされている CMake は 4.1.1 なのでそのまま使えます
- **Visual Studio 2022 / 2026** (C++ デスクトップ開発) または Build Tools
  - 代替: MinGW-w64 (g++) / Ninja でも OK。CMake が自動検出します。

### A. いちばん確実: Developer PowerShell for VS 2026 から実行

スタートメニューから「**Developer PowerShell for VS 2026**」(または VS 2022)
を開き、このフォルダに `cd` してから:

```powershell
.\build.bat -Run .\GeneralUser-GS.sf2
```

このシェルは VS の cmake / MSVC / Ninja に PATH が通った状態で起動するので、
ジェネレータや環境変数で悩む必要がありません。

### B. 既存ワークフロー: VS バンドル cmake と vcpkg を明示する

以前 `wave_fft` で使っていたパターン(`$cmake .. -DCMAKE_TOOLCHAIN_FILE=...`)を
そのまま流用したい場合は `-CMakePath` / `-ToolchainFile` を渡してください。
**VS 2026 のインストールパスはバージョン番号 `18` フォルダ**(`2026` ではない)です:

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
.\build.bat -CMakePath $cmake -ToolchainFile "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" -Run .\GeneralUser-GS.sf2
```

> 本プロジェクトは外部ライブラリ依存ゼロ(`tsf.h` を vendor)なので
> `-ToolchainFile` は実は不要です。指定しても CMake が
> `Manually-specified variables were not used by the project: CMAKE_TOOLCHAIN_FILE`
> と無害な警告を出すだけで configure は成功します。
> 既存ワークフローと揃えたい場合は付けたままで構いません。

### C. 普段使いの PowerShell から (PATH に依存)

```powershell
cmake --version    # VS 2026 を使うなら 4.1.1 以上であること
.\build.bat -Run .\GeneralUser-GS.sf2
```

古い CMake が PATH の先頭にあるときは A か B で逃げてください。
詰まったら **Ninja に切り替える**のが一番確実かつ高速です:
```powershell
.\build.bat -Run .\GeneralUser-GS.sf2 -Generator Ninja
```

### スクリプトの引数一覧

> **PowerShell から呼ぶ場合は `.\build.bat` のように `.\` プレフィックスが必須**です(cmd なら `build.bat` のままで OK)。
> PowerShell は既定でカレントディレクトリのコマンドを実行しません。

| 引数 | 例 | 説明 |
|------|----|------|
| `-Run` | `.\GeneralUser-GS.sf2` | ビルド後そのまま抽出を実行 |
| `-Out` | `gen` | 出力ディレクトリ(default: `generated`) |
| `-Programs` | `0,16,56` | 一部のGMプログラムだけ抽出(デバッグ用) |
| `-Config` | `Release` / `Debug` | ビルドコンフィグ |
| `-Generator` | `"Ninja"` / `"Visual Studio 18 2026"` | CMakeジェネレータ強制 |
| `-CMakePath` | `"C:\...\cmake.exe"` | cmake.exe のフルパス |
| `-ToolchainFile` | `"C:\vcpkg\...\vcpkg.cmake"` | `-DCMAKE_TOOLCHAIN_FILE` |
| `-Clean` |  | `build\` を消して再構成 |

`build.ps1` も同じ引数です。

### PowerShell スクリプト (`build.ps1`) を直接使う場合

Windows の既定の実行ポリシー (`Restricted` / `AllSigned`) では署名なしの `.ps1` を
弾くため、下記いずれかが必要です。

**(a) 1回だけバイパス** (システム設定を変えない):
```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Run .\GeneralUser-GS.sf2
```

**(b) 現在のユーザに恒久設定** (1回だけやれば以後 `.\build.ps1` がそのまま走る):
```powershell
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned
Unblock-File .\build.ps1   # zip 展開時に付くダウンロード元ブロックを外す
```

**(c) 何もしたくない** → 上の `build.bat` を使ってください。

### CMake を直接叩く場合
```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\gm_extract.exe .\GeneralUser-GS.sf2 --out generated
```

---

## 3. 動作原理

1. `tsf.h` (vendored TinySoundFont) で各プログラムの必要な音階を直接レンダリング
2. 立ち上がり(約60ms)を飛ばした定常部から、**指定周波数 h×f0 で Goertzel** により倍音振幅を抽出
3. RMSエンベロープから **持続音/減衰音を自動判定**、減衰時定数 τ を最小二乗で推定
4. 各ボイスを **再構成波形のRMSが一定**(`SIGNAL_RMS_TARGET`)になるよう正規化 = 等ラウドネス
5. 現行firmwareと同形式の C++ を出力

ピッチは実行時にMIDIノートから計算されるため(`palette_synth.cpp` の
`fbase = 440*2^((midi-69)/12)`)、1/オクターブのサンプルでもピッチずれは発生しません。
倍音「比率」だけをオクターブ内で流用します。

## 4. サンプル密度ポリシー

| 楽器タイプ | stride | 1プログラムあたり |
|----------|--------|----------------|
| ピアノ系 (GM prog 0-7) | 1 (音階ごと) | A0..C8 の88ボイス(prog 0-7で共有) |
| 減衰音(自動判定) | 6半音 | 約13サンプル |
| 持続音(自動判定) | 12半音(1/oct) | 約7サンプル |

`--decay-stride` / `--sustain-stride` で調整可能。打楽器(ch10)は生成しません
——別途 ADPCM サンプルエンジンで扱うためです(silent プレースホルダのみ出力)。

## 5. ピアノ音割れ・トランペット音量の解決原理

旧データはピーク正規化だったため、エネルギーが多数の倍音に分散する金管
(trumpet は第3倍音が最大で基音は弱い)が小さく聞こえ、逆にピアノは
`CFG_GAIN_PIANO=3.5` で無理に持ち上げてクリップしていました。

新データは全ボイスを **同じ知覚音量(RMS一定)** に揃えるので、
カテゴリ別ゲインはすべて 1.0 付近で済み、ピアノを持ち上げる必要が消えます。

検証(C++ 生成出力、中央C = MIDI 60):

```
piano_060  reconstructed-RMS=4200.2  peak=4256  h1=3826
p056_060   reconstructed-RMS=4200.1  peak=3230  h1=1763   <- trumpet (h3 が最大)
p048_060   reconstructed-RMS=4199.9  peak=5170  h1=5170
p016_060   reconstructed-RMS=4200.0  peak=4839  h1=4839
p073_060   reconstructed-RMS=4200.0  peak=4415  h1=3934
```

trumpet は h1 が弱くピーク倍音は h3 ですが、再構成RMSは piano と一致 → 同音量に聞こえます。

---

## 6. firmware への組み込み

1. 生成された3ファイルを `iFFT_Orgel/` 以下にそれぞれ上書きコピー:
   - `voice_table.cpp` → `iFFT_Orgel/voice_table.cpp`
   - `voices/voice_table.h`、`voices/voice_harm_data.h` → `iFFT_Orgel/voices/`
2. **`voice_table.h` の `MultiSample` 構造体に `stride` フィールドが増えています。**
   `voice_idx_from_note()` も新しい lookup に差し替わっているので、生成版の
   ヘッダをそのまま使えば対応完了です(firmware 側の他ファイル変更は不要)。
3. `config.h` のゲインを下記に変更(等ラウドネス化に合わせる)。

### config.h の推奨変更
```c
// 等ラウドネス化したので、カテゴリゲインは原則 1.0。微調整は好みで±20%程度。
#define CFG_GAIN_PIANO       1.0f   // 3.5 → 1.0 (クリップ解消)
#define CFG_GAIN_CHROMATIC   1.0f
#define CFG_GAIN_ORGAN       1.0f
#define CFG_GAIN_GUITAR      1.0f
#define CFG_GAIN_BASS        1.0f
#define CFG_GAIN_STRINGS     1.0f
#define CFG_GAIN_ENSEMBLE    1.0f
#define CFG_GAIN_BRASS       1.0f   // 2.8 不要に(trumpet も等音量)
#define CFG_GAIN_REED        1.0f
#define CFG_GAIN_PIPE        1.0f
#define CFG_GAIN_SYNTH_LEAD  1.0f
#define CFG_GAIN_SYNTH_PAD   1.0f
#define CFG_GAIN_SYNTH_FX    1.0f
#define CFG_GAIN_ETHNIC      1.0f
#define CFG_GAIN_PERC        1.0f
#define CFG_GAIN_SFX         1.0f
#define CFG_GAIN_DRUM        1.0f   // ADPCMエンジン導入までは無音

// 全体音量はここ一箇所で調整。新データはピーク値が旧 piano の約半分なので、
// 最初は少し上げて、シリアルログで softclip に当たらない範囲を探る。
#define CFG_MASTER_GAIN     14000   // 8000 から上げて実測調整
```

---

## 7. 調整パラメータ (`src/main.cpp` 冒頭)

| 定数 | 既定 | 意味 |
|------|------|------|
| `SR` | 22050 | 解析サンプルレート (firmware と一致させること) |
| `HARM_MAX` | 64 | 1ボイスあたり最大倍音 (`VOICE_HARM_MAX` と一致) |
| `ANALYSIS_WIN` | 8192 | 倍音抽出窓長 (大きいほど低音の倍音分離↑) |
| `ATTACK_SKIP_S` | 0.06 | アタックを飛ばす秒数 |
| `SIGNAL_RMS_TARGET` | 4200 | 等ラウドネス目標RMS (上げると全体↑、`CFG_MASTER_GAIN` と連動) |
| `HARM_CLIP` | 20000 | 単一倍音の上限クランプ |

## 8. プロジェクト構成

```
gm_voice_extract/
  CMakeLists.txt
  build.bat           # cmd 用 (PowerShell実行ポリシー回避、推奨)
  build.ps1           # PowerShell 用 (同一引数)
  README.md
  src/
    main.cpp
    third_party/
      tsf.h           # TinySoundFont (single header, MIT, vendored)
  example_output/      # TimGM6mb で生成した参考出力
    voice_table.cpp
    voices/voice_table.h
    voices/voice_harm_data.h
```

## 9. 既知の制約

- TinySoundFont は SF2 **モジュレータ未実装**。GeneralUser GS はモジュレータを多用しているので、
  完全準拠シンセ (FluidSynth等) と比べて音色が微妙に異なる可能性があります。
  ただし倍音「比率」と RMS 等ラウドネスは保たれるので、実用上問題は出にくいです。
  必要なら別途「FluidSynth で WAV を書き出して読み込む」入力モードを追加できます。
- 現在の等ラウドネスは純RMS。明るい音(高域エネルギー大)はやや大きく感じる場合があるため、
  A特性重み付けに差し替えるとより自然(必要なら対応します)。
- 減衰音の τ は単一指数フィット。ピアノ低音の長い余韻はおおむね拾えていますが、
  二段減衰の楽器は近似になります。
- ドラムは本ツールでは生成しません(加算合成では原理的に無理)。
  次の「ADPCM打楽器エンジン」で別経路合成します。
