# iFFT MIDI 

RP2040上で動作する、GM互換MIDI再生システムです。  
SoundFont（GeneralUser GS）から生成した倍音データを用い、iFFTベースの音源合成を行います。

---

## 概要

本プロジェクトは以下の構成で動作します。

- General MIDI (GM128) 対応再生エンジン
- SoundFont（GeneralUser GS）からの音色解析ツールチェーン
- 倍音抽出（Goertzel法）
- iFFTによる再合成音源
- ドラム用IMA-ADPCM再生エンジン
- RP2040単体動作（外付けRAMなし）
- サウンド出力分解能１２ビット
- 開発環境 Arduino IDE2.3系
  
---

## ビルド方法

- 本リポジトリをクローンまたはzipファイルをダウンロード後、Arduino IDEのビルドと書き込みを行ってください。
- ビルドの際以下の２点を設定してください。
- ボードマネージャーからRaspberry pi picoを選択。（ボードの導入がまだの方はインストールを行ってから選択）
- ツール→Flash Sizeの項目で Scketch 1MB FS 1Mを選択
- 基本的にはRP2040へ他のサンプルスケッチを書き込みする要領と全く同じです。
  
---

## MIDIファイルアップロード
- little FSを利用しています導入がまだの方は以下のリポジトリからインストールを行ってください
- https://github.com/earlephilhower/arduino-littlefs-upload
- ctrl+shif+pで littlefsのアップロードの項目を選択
- プロジェクトファイル内の /data にあるファイルがターゲットにアップロードされます。
- midiファイルは　/data/midi　に格納してください。
- /data　フォルダは全体で!MBを超えないようにしてください。
  
---



## 主な特徴

### ■ 音源構成
- 128ボイス対応（ポリフォニック再生）
- ピアノは88鍵単位で個別ボイス化
- その他GM音色はオクターブ/半音ストライドでサンプリング
- Sustain / Decayの自動判定
- 最大同時発生数　128音＋パーカッション系42Voice
- 音源のサンプリング元はGeneralUser GSを利用させてもらっています。
- https://github.com/mrbumpy409/GeneralUser-GS

### ■ 音響処理
- FFT窓サイズ：2048
- 倍音抽出：Goertzel法による周波数解析
- RMS正規化によるラウドネス均一化
- iFFTによる時間波形再構成
- 26KspsでiFFを行う。
  
### ■ ドラム処理
- IMA-ADPCM 4bit圧縮
- 標準GMドラムキット対応（bank 128）
- ストリーミング再生方式
-最大同時発生は42迄
---

## 音源データ生成

別途ツール `gm_extract` により生成されます。

処理フロー：

1. SF2（GeneralUser GS）読み込み
2. 各プログラム・ノートをレンダリング
3. アタック部を除外し定常成分を解析
4. 倍音成分を抽出
5. RMS正規化
6. C++ヘッダへ変換

出力：

- voice_table.cpp
- voice_table.h
- voice_harm_data.h
- drum_data.cpp

---
## テストベンチ用ファイル
- 1812Overture.mid
- Bond.mid
- dq6-theme.mid
- GraxyExpress999.mid
- la-campanella-Franz-liszt-paganini.mid
- Umi no Mieru Machi.mid
- xi - FREEDOM DiVE↓.mid
- いずれも音切れなく再生可能。
---

## ハードウェア要件

- RP2040マイコン
- PWM出力または簡易DAC
- BTL出力
- スピーカー（小型可）
- カップリングコンデンサ程度の外付け部品
- カップリングコンデンサは絶対に省略しないでください。最悪マイコンのGPIOが壊れる可能性があります。
- SSD1306
- ピンアサインはconfg.hで変更可能です。

※外付けRAMは不要

---

##　日本語表示対応

- SongList.txtに曲名・作者、midiファイル名を記載することで、曲の冒頭にOLEDへ表示可能。
- 漢字JIS1及び2水準、記号。すべてプロジェクトに配備済みです。
- 東雲フォント（１６ドット）を利用させてもらっています。
- 視認性重視の為表示できる文字数の制限があります。
- 詳しい記述方法およびサンプルはSongList.txtを参照してください。

---

## 制約

- SF2モジュレーション完全再現ではない（簡略化レンダリング）
- 音色はGeneralUser GS依存
- ドラムはADPCMベースの簡易波形再生
- いくつかのスピーカーで試験しましたが、スピーカーによっては十分な音圧が得られないことがあります。
- その場合は外部アンプへ接続することをおすすめしますが、外部アンプへ接続する場合は
- #define CFG_MASTER_GAIN
- この値を下げるかアッテネート抵抗をいれてください。
- いずれの場合でもカップリングコンデンサは必須です。

##注意事項
 -本プログラムは商用利用可能としますが、一切の責任を取りません。
 -SFから中ちゅつした場合、SFのライセンス規定に従ってください。

---
 
##謝辞
 - GeneralUser GS作者　Christian Collins様
 - https://github.com/mrbumpy409/GeneralUser-GS
 - 東雲フォント作者　code4fukui様
 - https://github.com/code4fukui/shinonome-font
 - littlefs-upload作者　Earle F. Philhower III様
 - https://github.com/earlephilhower/arduino-littlefs-upload

---



