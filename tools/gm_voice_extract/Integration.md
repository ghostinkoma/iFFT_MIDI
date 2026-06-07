# ADPCM打楽器エンジン  ファーム組み込み手順

`gm_voice_extract` で生成された `drum_data.cpp` / `voices/drum_data.h` と、
本フォルダの `drum_engine.{h,cpp}` を firmware に追加し、3か所だけ修正します。

## 1. ファイルの配置

```
iFFT_Orgel/
  drum_engine.h               <-- 新規 (このフォルダから)
  drum_engine.cpp             <-- 新規 (このフォルダから)
  drum_data.cpp               <-- 生成物 (gm_voice_extract/generated/)
  voices/
    drum_data.h               <-- 生成物 (gm_voice_extract/generated/voices/)
    voice_table.h             <-- 既存 (前回更新済み)
    voice_harm_data.h         <-- 既存
```

## 2. `iFFT_Orgel.ino` のパッチ (2か所)

### (a) ヘッダインクルードを追加 (他の include 群と並べて)
```cpp
#include "drum_engine.h"
```

### (b) `setup()` 内、`ps_init()` の直後あたりに 1行追加
```cpp
ps_init();
drum_engine_init();    // <-- 追加
```

### (c) `dispatch_events()` でドラムを別経路へ振り分け
**変更前:**
```cpp
static void dispatch_events() {
    while (g_player.q_read != g_player.q_write) {
        NoteEvent ev;
        memcpy(&ev,(const void*)&g_player.queue[g_player.q_read],sizeof(NoteEvent));
        __dmb();
        g_player.q_read = (g_player.q_read+1) % MidiPlayer::QUEUE_SIZE;

        uint16_t vi = ev.is_drum
            ? voice_idx_from_drum(ev.note)
            : voice_idx_from_note(ev.note, ev.program);

        if (ev.velocity > 0)
            ps_note_on(ev.note, ev.velocity, vi, ev.channel, millis() + (uint32_t)ev.duration_ms);
        else
            ps_note_off(ev.note, ev.channel);
    }
}
```

**変更後:**
```cpp
static void dispatch_events() {
    while (g_player.q_read != g_player.q_write) {
        NoteEvent ev;
        memcpy(&ev,(const void*)&g_player.queue[g_player.q_read],sizeof(NoteEvent));
        __dmb();
        g_player.q_read = (g_player.q_read+1) % MidiPlayer::QUEUE_SIZE;

        // ドラムは ADPCM 別エンジンへ。iFFT 側の voice_idx は使わない。
        if (ev.is_drum) {
            if (ev.velocity > 0) drum_engine_note_on(ev.note, ev.velocity);
            // ドラムは duration を見ない (自然減衰)
            continue;
        }

        uint16_t vi = voice_idx_from_note(ev.note, ev.program);
        if (ev.velocity > 0)
            ps_note_on(ev.note, ev.velocity, vi, ev.channel, millis() + (uint32_t)ev.duration_ms);
        else
            ps_note_off(ev.note, ev.channel);
    }
}
```

## 3. `palette_synth.cpp` のパッチ (`ps_render_block` 内、1か所)

iFFTのOLA加算とソフトクリップの**間**にドラムmixを差し込みます。

### 関数冒頭にヘッダを追加
```cpp
#include "drum_engine.h"   // ファイル先頭の他の include と並べる
```

### `ps_render_block` 内の OLA + softclip ループを変更

**変更前 (現行):**
```cpp
    // Hann窓 + 50% OLA → ソフトクリップ → PWM出力
    for (int n=0; n<(int)CFG_HOP_SIZE; n++) {
        int32_t wv = (int32_t)(((int64_t)work_re[n] * (int64_t)hann[n]) >> 15);
        int32_t v = wv + ola_tail[n];

        const int32_t SC = 24000;
        if      (v >  SC) v =  SC + ((v - SC) >> 3);
        else if (v < -SC) v = -SC + ((v + SC) >> 3);
        ...
```

**変更後:**
```cpp
    // ドラム ADPCM をこの hop ぶん展開しておく
    static int32_t drum_buf[CFG_HOP_SIZE];
    memset(drum_buf, 0, sizeof(drum_buf));
    drum_engine_mix(drum_buf, CFG_HOP_SIZE);

    // Hann窓 + 50% OLA + ドラム加算 → ソフトクリップ → PWM出力
    for (int n=0; n<(int)CFG_HOP_SIZE; n++) {
        int32_t wv = (int32_t)(((int64_t)work_re[n] * (int64_t)hann[n]) >> 15);
        int32_t v = wv + ola_tail[n] + drum_buf[n];   // <-- drum_buf を加算

        const int32_t SC = 24000;
        if      (v >  SC) v =  SC + ((v - SC) >> 3);
        else if (v < -SC) v = -SC + ((v + SC) >> 3);
        ...
```

`drum_buf` は `static` にして hop 間で再利用 (スタック削減)。
`CFG_HOP_SIZE * 4 = 4 KB` を BSS に取ります。

## 4. ビルド

Arduino IDE で sketch をリビルド。RAM/Flash の増分目安:
- Flash: ADPCMデータ約95KB + デコーダコード約2KB = **約97KB**
- RAM: 8音×64B + drum_buf 4KB = **約4.5KB**

## 5. 期待される動作

- ドラムが**鳴るようになる**(これまで silent placeholder だったところに音が乗る)
- 8 kHz 由来のシンバル/ハイハットは「シャリ感」がやや鈍い (Nyquist 4 kHz)
- キック/スネア/タムは8kHzでも十分認識できる音質のはず

## 6. 詰まったときの切り分け

- ドラムだけ無音 → `dispatch_events` の振り分けか `drum_engine_init` 呼び出し漏れ
- ドラムが歪む/爆音 → soft-clip 閾値 (24000) に頻繁にぶつかっている可能性。`drum_buf` 加算の前にゲイン下げ (`drum_buf[n] >> 1` など)
- ノイズだけ流れる → ADPCMデコーダのバグ。`DRUM_SAMPLE_RATE` 定義と `phase_inc` が一致しているか確認
- ドラムの一部音色だけ鳴らない → 該当 MIDI ノートが `drum_map` で `used=0`。`gm_voice_extract` のログで生成リストを確認

## 7. 音質向上 (8kHzで問題なく動いてから)

```powershell
.\build.bat -Run .\GeneralUser-GS.sf2 -Out generated
# ↓ 8kHz でとりあえず鳴らす

.\build.bat -Run .\GeneralUser-GS.sf2 -Out gen16k
# 別フォルダで 16 kHz 版も生成しておくと差し替えで A/B 比較できる
```

`gm_voice_extract` に `--drum-rate 16000` (または12000) を渡せば、レートだけ変えて
再生成できます。firmware 側の `drum_engine.cpp` は `DRUM_SAMPLE_RATE` を読んで
リサンプル係数を自動算出するので、**firmware変更ゼロでレート切り替え可能**です:

```powershell
.\build.bat -Run .\GeneralUser-GS.sf2 -Out gen16k -Programs ""    # 通常GMはそのままで
# ↑ ドラムレートを引数で指定する場合 (build.bat に未対応なら以下を直接):
.\build\Release\gm_extract.exe .\GeneralUser-GS.sf2 --out gen16k --drum-rate 16000
```

16 kHz だとシンバル/ハイハットの "シャリ感" がほぼ復活します(Nyquist 8 kHz)。
Flash増加分は約 +50 KB (94→144 KB)。残量1.7MB以上あるので問題なし。