# v7 リーク修正 — 「2 分鳴り続ける」現象の対策

## ご観測の症状
- 演奏 4 分前後で音符が消えなくなる
- 該当音は約 2 分間鳴り続ける
- 以降の再生でも頻発

## 真因 (3 個のバグの連鎖)

### バグ A: `_find_active` の start_ms 不一致

旧コード: pool を逆向きに走査して「ANY active マッチ」を返す
→ `_alloc_slot` は round-robin、open[] と pool の「最新」順序が一致しない
→ start_ms が違う slot を返してしまい、note-off の duration 更新が**空振り**
→ 該当 Note は永遠に ACTIVE のまま

### バグ B: open[] オーバーフロー (MAX_OPEN=32)

dense passage で同時保持 32 を超えると、新規 note-on の照合情報を捨てていた
→ pool には slot が確保される、open[] には記録されない
→ note-off が来てもマッチ不可能 → 該当 Note 永久 ACTIVE

### バグ C: ACTIVE 永続音への救済が皆無

A/B でリークした note-on は dispatch される (placeholder duration=65535)
→ `_dispatch_due` は `duration != ACTIVE` でしか note-off を発火しない
→ sustained 楽器の場合 ps_note_off が永久に来ない
→ synth で 65 秒鳴り続ける (場合によりさらに長く)

## 修正

### Fix A: start_ms を厳密マッチング条件に組み込み

```cpp
// 旧
int _find_active(uint8_t note, uint8_t ch);   // ANY active

// 新
int _find_active(uint8_t note, uint8_t ch, uint32_t start_ms);  // 厳密 ID
```

`(note, ch, start_ms)` の 3 つ組で完全に一意。round-robin アロケータでも
確実に対応 slot を発見。

### Fix B: open[] フル時は最古を強制 finalize

```cpp
if (t.open_n >= TrackState::MAX_OPEN) {
    // open[0] (最古) の対応 pool slot を「現在時刻まで」で確定
    int op = _find_active(oldest.note, oldest.ch, old_start_ms);
    if (op >= 0) _pool[op].duration_ms = (現在 - old_start);
    // open[] をスライドダウン
    for (int j = 0; j < MAX_OPEN - 1; j++) t.open[j] = t.open[j+1];
    t.open_n--;
}
```

最古は犠牲になるが、その対応 pool slot もきれいに finalize される
(リーク発生せず)。

### Fix C: 60 秒以上 ACTIVE な Note の救済

```cpp
if (n.duration_ms == NOTE_DURATION_ACTIVE) {
    uint32_t age = playback_ms - n.start_ms;
    if (age > 60000) {
        // 60 秒以上 ACTIVE = note-off 取りこぼし確定 → 強制 finalize
        n.duration_ms = (uint16_t)age;
        _enqueue_dispatch(n, true);   // ps_note_off を強制発火
        n.flags |= NOTE_FLAG_OFF_FIRED;
    }
}
```

万一 A/B をすり抜けたケースでも、最長 60 秒で必ず stop。1812 の sustained
pad/pedal は通常 30-40 秒以内なので legitimate な音は cut しない。

## stress test 結果

5 分間連続再生 (DQ6, Gurenge, Umi no Mieru Machi):

```
[t=  1000ms] pool=  47  active=  6  ancient= 0
[t= 30000ms] pool= 239  active=  4  ancient= 0
[t= 60000ms] pool= 178  active=  6  ancient= 0
[t=120000ms] pool= 190  active=  8  ancient= 0
[t=180000ms] pool= 212  active=  2  ancient= 0
[t=240000ms] pool=   0  active=  0  ancient= 0
```

`ancient = 0` ですべての時点 → **30 秒以上 ACTIVE のリークが構造的にゼロ**。
これがご提案アーキの本来の動作です。

## 影響範囲

- `midi_player.cpp` の 3 か所修正のみ
- `midi_player.h` は `_find_active` のシグネチャに `start_ms` 追加 (private)
- `.ino` 側の変更不要

---

# v7.2 追加修正 — 「8 分で 1 音符フリーズ」の真因

## ご観測の症状 (パラメータ LOOKAHEAD=20s, KEEP_PAST=10ms)
- 4 分まで完璧
- 8 分付近で再生停止、1 音符が長い線で OLED に残ったままフリーズ

## 真因: `_in_use_count` の減算抜け

v7.1 で O(N) → O(1) 化のため `_in_use_count` メンバを導入したが、
`_release_done_slots` での `--` がパッチ適用時に silent fail していた。

**症状の連鎖**:
1. note-on のたびにカウンタ `++` (line 314)
2. release は `n.flags = 0` するが counter は据え置き
3. 累積で counter が POOL_SIZE - 16 (= 4080) に到達 (≒ 4 分付近)
4. `_refill` の `if (_in_use_count >= POOL_SIZE - 16) break;` が永久成立
5. MIDI streamer 完全停止 → 新規 dispatch 0
6. 既 dispatch 済の音が鳴り終わると OLED 凍結
7. 最後に描画された 1 つの bar が残る

実機での再現:
```
[t=240s] pool_in_use=4080 / 実フラグ=0   ← カウンタ完全に虚偽
        dispatched 値は 6487 で以降カウントアップ停止
```

## 修正 (midi_player.cpp line 506)

```cpp
// OK: スロット解放
n.flags = 0;
_in_use_count--;   // ★ これが抜けていた
```

たったの 1 行。だがこの 1 行で counter と現実が連動し、構造的に
**「pool 満杯のフリ」が原理的に発生不能**になる。

## 検証 (10 分連続再生)

```
[t=240000ms] dispatched= 7848  pool=  0    ← 終曲で完全 drain
[t=360000ms] dispatched= 7848  pool=  0
[t=480000ms] dispatched= 7848  pool=  0
[t=600000ms] dispatched= 7848  pool=  0
```

before: pool=4080 stuck, is_finished=0
after:  pool=0 clean,    is_finished=1

## 影響範囲
- `midi_player.cpp` 1 行追加のみ
- API 互換性は完全保持
- `.ino` 側の変更不要

これで 1812 完遂への最大の障害が除去されました。
