// ============================================================================
// midi_player.cpp ─ v7 Note Pool Architecture
//   ・各 Note は独立スロットに固定常駐 (ring buffer 廃止)
//   ・寿命管理: in_use → on_fired → off_fired → released
//   ・release は「両方発火済 AND end_ms + margin < playback」のみ
//   ・eviction なし → ノートが勝手に消える現象が構造的にあり得ない
// ============================================================================
#include "midi_player.h"
#include <algorithm>
#include <cstring>

// ============================================================================
// 補助: バイナリ読み込み
// ============================================================================
static uint32_t read_be32(File& f) {
    uint8_t b[4]; f.read(b, 4);
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}
static uint16_t read_be16(File& f) {
    uint8_t b[2]; f.read(b, 2);
    return ((uint16_t)b[0] << 8) | b[1];
}
static uint32_t read_varlen(File& f) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        int c = f.read();
        if (c < 0) return v;
        v = (v << 7) | (c & 0x7F);
        if (!(c & 0x80)) break;
    }
    return v;
}

// ============================================================================
// ファイルオープン / ヘッダ + テンポマップ
// ============================================================================
bool MidiPlayer::_open_song(const char* path) {
    _close_song();
    _file = FatFS.open(path, "r");
    if (!_file) return false;
    _file_open = true;
    return true;
}

void MidiPlayer::_close_song() {
    if (_file_open) { _file.close(); _file_open = false; }
}

bool MidiPlayer::_parse_header_and_tempo() {
    _file.seek(0);
    uint8_t hdr[4]; _file.read(hdr, 4);
    if (memcmp(hdr, "MThd", 4) != 0) return false;
    uint32_t hlen = read_be32(_file);
    uint16_t fmt  = read_be16(_file); (void)fmt;
    uint16_t ntrk = read_be16(_file);
    _tpb = read_be16(_file);
    if (_tpb == 0) _tpb = 96;
    if (ntrk > MAX_TRACKS) ntrk = MAX_TRACKS;
    _ntracks = ntrk;
    _file.seek(8 + hlen);

    // 各 track の file offset を記録
    for (int i = 0; i < _ntracks; i++) {
        uint8_t th[4]; _file.read(th, 4);
        if (memcmp(th, "MTrk", 4) != 0) return false;
        uint32_t tlen = read_be32(_file);
        _tracks[i].file_offset = _file.position();
        _tracks[i].end_offset  = _tracks[i].file_offset + tlen;
        _file.seek(_tracks[i].end_offset);
    }

    // テンポマップ全 track 走査
    _tempo_n = 0;
    _tempo[_tempo_n++] = { 0, 500000 };   // デフォルト 120 BPM
    for (int i = 0; i < _ntracks; i++) {
        _file.seek(_tracks[i].file_offset);
        uint32_t tick = 0;
        uint8_t  running = 0;
        while (_file.position() < _tracks[i].end_offset) {
            tick += read_varlen(_file);
            int c = _file.read();
            if (c < 0) break;
            uint8_t status;
            if (c & 0x80) { status = c; running = c; }
            else { status = running; _file.seek(_file.position() - 1); }
            if (status == 0xFF) {
                uint8_t type = _file.read();
                uint32_t len = read_varlen(_file);
                if (type == 0x51 && len == 3) {
                    uint8_t b[3]; _file.read(b, 3);
                    uint32_t us = ((uint32_t)b[0]<<16)|((uint32_t)b[1]<<8)|b[2];
                    if (_tempo_n < MAX_TEMPO_CHANGES) {
                        _tempo[_tempo_n++] = { tick, us };
                    }
                } else {
                    _file.seek(_file.position() + len);
                }
            } else if (status == 0xF0 || status == 0xF7) {
                uint32_t len = read_varlen(_file);
                _file.seek(_file.position() + len);
            } else {
                uint8_t type = status & 0xF0;
                if (type == 0xC0 || type == 0xD0) _file.read();   // 1byte
                else _file.seek(_file.position() + 2);             // 2byte
            }
        }
    }
    // sort by tick
    for (int i = 1; i < _tempo_n; i++) {
        TempoChange t = _tempo[i];
        int j = i - 1;
        while (j >= 0 && _tempo[j].tick > t.tick) { _tempo[j+1] = _tempo[j]; j--; }
        _tempo[j+1] = t;
    }
    return true;
}

// ============================================================================
// tick → ms 変換 (テンポマップ参照)
// ============================================================================
uint32_t MidiPlayer::_tick_to_ms(uint32_t tick) const {
    uint64_t us = 0;
    uint32_t prev_tick = 0;
    uint32_t cur_us = 500000;
    for (int i = 0; i < _tempo_n; i++) {
        if (_tempo[i].tick > tick) break;
        us += (uint64_t)(_tempo[i].tick - prev_tick) * cur_us / _tpb;
        prev_tick = _tempo[i].tick;
        cur_us = _tempo[i].us_per_qn;
    }
    us += (uint64_t)(tick - prev_tick) * cur_us / _tpb;
    return (uint32_t)(us / 1000);
}

// ============================================================================
// load_path
// ============================================================================
bool MidiPlayer::load_path(const char* path) {
    if (!_open_song(path)) return false;
    snprintf(current_file, sizeof(current_file), "%s", path);
    if (!_parse_header_and_tempo()) { _close_song(); return false; }
    DBG_PRINTF("[midi] tempo changes: %d\n", _tempo_n);

    // pool 全リセット
    for (int i = 0; i < POOL_SIZE; i++) _pool[i].flags = 0;
    _in_use_count = 0;
    _alloc_cursor = 0;
    q_read = q_write = 0;
    _all_decoded = false;
    _last_processed_ms = 0;
    _last_event_end_ms = 0;
    playback_ms = 0;

    // track 初期化 + 最初のイベント peek
    for (int i = 0; i < _ntracks; i++) {
        TrackState& t = _tracks[i];
        t.cur_tick = 0;
        t.running_status = 0;
        t.done = false;
        t.open_n = 0;
        memset(t.ch_prog, 0, sizeof(t.ch_prog));
        _file.seek(t.file_offset);
        t.peek_valid = false;
        _track_peek_next(t);
    }

    DBG_PRINTF("[midi] pool ready, tracks=%d tpb=%d\n", _ntracks, _tpb);
    return true;
}

void MidiPlayer::play() { _start_ms = millis(); _playing = true; }
void MidiPlayer::stop() { _playing = false; _close_song(); }
bool MidiPlayer::is_finished() const {
    if (!_all_decoded) return false;
    // dispatch 待ちが残ってる?
    for (int i = 0; i < POOL_SIZE; i++) {
        const Note& n = _pool[i];
        if (!(n.flags & NOTE_FLAG_IN_USE)) continue;
        if (!(n.flags & NOTE_FLAG_ON_FIRED)) return false;
        if (n.is_drum) continue;
        if (n.duration_ms == NOTE_DURATION_ACTIVE) continue;
        if (!(n.flags & NOTE_FLAG_OFF_FIRED)) return false;
    }
    return playback_ms >= _last_event_end_ms;
}

// ============================================================================
// peek の取得 (track の次イベントを decode してメンバに格納)
// ============================================================================
bool MidiPlayer::_track_peek_next(TrackState& t) {
    if (t.done) return false;
    // 上は不要だが念のため: 実際には _refill 内で seek 制御
    while (true) {
        if (_file.position() >= t.end_offset) { t.done = true; t.peek_valid = false; return false; }
        uint32_t delta = read_varlen(_file);
        t.cur_tick += delta;
        int c = _file.read();
        if (c < 0) { t.done = true; t.peek_valid = false; return false; }
        uint8_t status;
        if (c & 0x80) { status = c; t.running_status = c; }
        else { status = t.running_status; _file.seek(_file.position() - 1); }

        if (status == 0xFF) {
            uint8_t type = _file.read();
            uint32_t len = read_varlen(_file);
            if (type == 0x2F) { t.done = true; t.peek_valid = false; return false; }
            _file.seek(_file.position() + len);
            // テンポチェンジは load 時に処理済。スキップ。
        } else if (status == 0xF0 || status == 0xF7) {
            uint32_t len = read_varlen(_file);
            _file.seek(_file.position() + len);
        } else {
            uint8_t type = status & 0xF0;
            t.peek_status = status;
            t.peek_tick = t.cur_tick;
            if (type == 0xC0 || type == 0xD0) {
                t.peek_d1 = _file.read();
                t.peek_d2 = 0;
            } else {
                t.peek_d1 = _file.read();
                t.peek_d2 = _file.read();
            }
            // 残った file_offset は次回 peek 用に更新
            t.file_offset = _file.position();
            t.peek_valid = true;
            return true;
        }
    }
}

// ============================================================================
// pool スロット確保
//   1. 未使用スロットを探す
//   2. 見つからなければ「完全に終わった」スロットを再利用
// ============================================================================
int MidiPlayer::_alloc_slot() {
    // ラウンドロビン (member ベース、load 時にリセット)
    for (int k = 0; k < POOL_SIZE; k++) {
        int i = (_alloc_cursor + k) % POOL_SIZE;
        if (!(_pool[i].flags & NOTE_FLAG_IN_USE)) {
            _alloc_cursor = (i + 1) % POOL_SIZE;
            return i;
        }
    }
    // 全部使用中 → release 試行
    _release_done_slots();
    for (int k = 0; k < POOL_SIZE; k++) {
        int i = (_alloc_cursor + k) % POOL_SIZE;
        if (!(_pool[i].flags & NOTE_FLAG_IN_USE)) {
            _alloc_cursor = (i + 1) % POOL_SIZE;
            return i;
        }
    }
    return -1;   // 真に満杯 (放棄)
}

// ============================================================================
// 同名 active note を start_ms 一致で厳密検索 (ID 代わり)
// ============================================================================
int MidiPlayer::_find_active(uint8_t note, uint8_t ch, uint32_t start_ms) {
    for (int i = 0; i < POOL_SIZE; i++) {
        const Note& n = _pool[i];
        if (!(n.flags & NOTE_FLAG_IN_USE)) continue;
        if (n.duration_ms != NOTE_DURATION_ACTIVE) continue;
        if (n.note != note || n.channel != ch) continue;
        if (n.start_ms != start_ms) continue;
        return i;
    }
    return -1;
}

// ============================================================================
// dispatch queue へ Note を積む
// ============================================================================
bool MidiPlayer::_enqueue_dispatch(const Note& n, bool is_off) {
    int next = (q_write + 1) % QUEUE_SIZE;
    if (next == q_read) return false;   // ★ queue 満杯: caller がリトライできるよう false 返す
    NoteEvent& ev = queue[q_write];
    if (is_off) {
        ev.time_ms     = n.start_ms + n.duration_ms;   // 実 note-off 時刻
        ev.duration_ms = 0;
        ev.velocity    = 0;
    } else {
        ev.time_ms     = n.start_ms;
        ev.duration_ms = n.duration_ms;
        ev.velocity    = n.velocity;
    }
    ev.note        = n.note;
    ev.channel     = n.channel;
    ev.program     = n.program;
    ev.is_drum     = n.is_drum != 0;
    q_write = next;
    if (_dispatch_cb) _dispatch_cb(ev);
    return true;
}

// ============================================================================
// MIDI トラックの 1 イベントを処理 → pool に登録 or 既存 active 更新
// ============================================================================
void MidiPlayer::_process_track_event(TrackState& t) {
    if (!t.peek_valid) return;
    {
        uint32_t ev_ms = _tick_to_ms(t.peek_tick);
        if (ev_ms > _last_processed_ms) _last_processed_ms = ev_ms;
    }
    uint8_t status = t.peek_status;
    uint8_t type   = status & 0xF0;
    uint8_t ch     = status & 0x0F;

    if (type == 0x90 && t.peek_d2 > 0) {
        // ----- note-on: pool に新規スロット確保 -----
        int idx = _alloc_slot();
        if (idx >= 0) {
            _in_use_count++;
            Note& n = _pool[idx];
            n.start_ms   = _tick_to_ms(t.peek_tick);
            n.note       = t.peek_d1;
            n.velocity   = t.peek_d2;
            n.channel    = ch;
            n.program    = t.ch_prog[ch];
            n.is_drum    = (ch == 9) ? 1 : 0;
            // ★ ドラムは note-off 不要 → 固定 duration & off_fired 自動セット
            //   ・視覚化用に 200ms の小バー
            //   ・release 条件を即満たすので slot は速やかに解放
            if (n.is_drum) {
                n.duration_ms = 200;
                n.flags       = NOTE_FLAG_IN_USE | NOTE_FLAG_OFF_FIRED;
                if (n.start_ms + 200 > _last_event_end_ms)
                    _last_event_end_ms = n.start_ms + 200;
            } else {
                n.duration_ms = NOTE_DURATION_ACTIVE;
                n.flags       = NOTE_FLAG_IN_USE;
            }
        }
        // ドラムは note-off 不要、トラックの open[] に積まない
        if (ch != 9) {
            // ★ open[] フル時は最古をリーク救済: その pool slot を強制 finalize
            if (t.open_n >= TrackState::MAX_OPEN) {
                OpenNote& oldest = t.open[0];
                uint32_t old_start_ms = _tick_to_ms(oldest.start_tick);
                uint32_t now_ms_ev    = _tick_to_ms(t.peek_tick);
                uint32_t dur = (now_ms_ev > old_start_ms) ? now_ms_ev - old_start_ms : 50;
                if (!dur) dur = 50;
                uint16_t dc  = (uint16_t)std::min(dur, (uint32_t)(NOTE_DURATION_ACTIVE - 1));
                int op = _find_active(oldest.note, oldest.ch, old_start_ms);
                if (op >= 0) {
                    _pool[op].duration_ms = dc;
                    if (old_start_ms + dc > _last_event_end_ms)
                        _last_event_end_ms = old_start_ms + dc;
                }
                // スライド (一番古いを捨てる)
                for (int j = 0; j < TrackState::MAX_OPEN - 1; j++) t.open[j] = t.open[j+1];
                t.open_n = TrackState::MAX_OPEN - 1;
            }
            OpenNote& on = t.open[t.open_n++];
            on.start_tick = t.peek_tick;
            on.note = t.peek_d1;
            on.vel  = t.peek_d2;
            on.ch   = ch;
            on.prog = t.ch_prog[ch];
        }
    } else if (type == 0x80 || (type == 0x90 && t.peek_d2 == 0)) {
        // ----- note-off: 最新の同名 open を引き当て、pool の duration を確定 -----
        for (int i = t.open_n - 1; i >= 0; i--) {
            if (t.open[i].ch == ch && t.open[i].note == t.peek_d1) {
                uint32_t start_ms = _tick_to_ms(t.open[i].start_tick);
                uint32_t off_ms   = _tick_to_ms(t.peek_tick);
                uint32_t dur      = (off_ms > start_ms) ? off_ms - start_ms : 50;
                if (!dur) dur = 50;
                uint16_t dur_clamped = (uint16_t)std::min(dur, (uint32_t)(NOTE_DURATION_ACTIVE - 1));

                int pidx = _find_active(t.peek_d1, ch, start_ms);
                if (pidx >= 0) {
                    _pool[pidx].duration_ms = dur_clamped;
                    if (start_ms + dur_clamped > _last_event_end_ms)
                        _last_event_end_ms = start_ms + dur_clamped;
                }
                t.open[i] = t.open[--t.open_n];
                break;
            }
        }
    } else if (type == 0xC0) {
        t.ch_prog[ch] = t.peek_d1;
    }

    _track_peek_next(t);
}

// ============================================================================
// イベント先読み (lookahead 範囲まで decode)
//   ・pool 残量が少なくなったら自動停止 → 次 tick で再開
// ============================================================================
void MidiPlayer::_refill(uint32_t up_to_ms) {
    while (true) {
        // pool 残量チェック (counter で O(1) 判定)
        if (_in_use_count >= POOL_SIZE - 16) break;

        // 最古 (tick 最小) の peek を選ぶ
        int earliest = -1;
        uint32_t et = UINT32_MAX;
        for (int i = 0; i < _ntracks; i++) {
            if (_tracks[i].peek_valid && _tracks[i].peek_tick < et) {
                et = _tracks[i].peek_tick;
                earliest = i;
            }
        }
        if (earliest < 0) {
            if (!_all_decoded) {
                _emit_orphan_offs();
            }
            _all_decoded = true;
            break;
        }
        // lookahead 範囲を超えたら停止
        uint32_t ev_ms = _tick_to_ms(et);
        if (ev_ms > up_to_ms) break;

        // file_offset を該当 track に戻して 1 イベント処理
        _file.seek(_tracks[earliest].file_offset);
        _process_track_event(_tracks[earliest]);
    }
}

// ============================================================================
// 孤立 note-on (note-off 来ず decode 終了) を曲末で release
// ============================================================================
void MidiPlayer::_emit_orphan_offs() {
    uint32_t end_ms = _last_processed_ms;
    for (int ti = 0; ti < _ntracks; ti++) {
        TrackState& t = _tracks[ti];
        for (int oi = 0; oi < t.open_n; oi++) {
            OpenNote& on = t.open[oi];
            uint32_t start_ms = _tick_to_ms(on.start_tick);
            uint32_t dur = (end_ms > start_ms) ? end_ms - start_ms : 50;
            if (!dur) dur = 50;
            uint16_t dc = (uint16_t)std::min(dur, (uint32_t)(NOTE_DURATION_ACTIVE - 1));
            int pidx = _find_active(on.note, on.ch, start_ms);
            if (pidx >= 0) {
                _pool[pidx].duration_ms = dc;
                if (start_ms + dc > _last_event_end_ms)
                    _last_event_end_ms = start_ms + dc;
            }
        }
        t.open_n = 0;
    }
}

// ============================================================================
// dispatch: pool 全走査して発火可能な Note を queue へ
//   ・note-on : !on_fired AND start_ms ≤ playback + LEAD
//   ・note-off: on_fired AND !off_fired AND duration 確定 AND end ≤ playback + LEAD
// ============================================================================
void MidiPlayer::_dispatch_due() {
    uint32_t now_eff = playback_ms + DISPATCH_LEAD_MS;
    for (int i = 0; i < POOL_SIZE; i++) {
        Note& n = _pool[i];
        if (!(n.flags & NOTE_FLAG_IN_USE)) continue;
        // note-on (★ enqueue 成功時のみフラグ set。queue 満杯時は次 tick で再試行)
        if (!(n.flags & NOTE_FLAG_ON_FIRED) && n.start_ms <= now_eff) {
            if (_enqueue_dispatch(n, false)) {
                n.flags |= NOTE_FLAG_ON_FIRED;
            }
        }
        // note-off (drum は skip — drum は ring-out 任せ)
        if ((n.flags & NOTE_FLAG_ON_FIRED) && !(n.flags & NOTE_FLAG_OFF_FIRED) && !n.is_drum) {
            if (n.duration_ms != NOTE_DURATION_ACTIVE) {
                uint32_t end_ms = n.start_ms + n.duration_ms;
                if (end_ms <= now_eff) {
                    if (_enqueue_dispatch(n, true)) {     // ★ 同様にリトライ可能
                        n.flags |= NOTE_FLAG_OFF_FIRED;
                    }
                }
            } else {
                // ACTIVE のまま長時間経過 = note-off 取りこぼしの救済
                uint32_t age = playback_ms - n.start_ms;
                if (age > 60000) {       // 60 秒 (LOOKAHEAD=20s でも legitimate 長音は < 60s)
                    n.duration_ms = (uint16_t)std::min(age, (uint32_t)(NOTE_DURATION_ACTIVE - 1));
                    if (n.start_ms + n.duration_ms > _last_event_end_ms)
                        _last_event_end_ms = n.start_ms + n.duration_ms;
                    if (_enqueue_dispatch(n, true)) {
                        n.flags |= NOTE_FLAG_OFF_FIRED;
                    }
                }
            }
        }
    }
}

// ============================================================================
// release: 完全終了したスロットを解放
//   ・on_fired AND (off_fired OR is_drum) AND end_ms + KEEP_PAST < playback
//   ・active (placeholder) は release されない
// ============================================================================
void MidiPlayer::_release_done_slots() {
    uint32_t margin = (playback_ms > MIDI_KEEP_PAST_MS) ? playback_ms - MIDI_KEEP_PAST_MS : 0;
    for (int i = 0; i < POOL_SIZE; i++) {
        Note& n = _pool[i];
        if (!(n.flags & NOTE_FLAG_IN_USE)) continue;
        if (!(n.flags & NOTE_FLAG_ON_FIRED)) continue;
        if (n.duration_ms == NOTE_DURATION_ACTIVE) continue;   // active 護持
        if (!n.is_drum && !(n.flags & NOTE_FLAG_OFF_FIRED)) continue;
        uint32_t end_ms = n.start_ms + n.duration_ms;
        if (end_ms >= margin) continue;   // まだ視覚化中
        // OK: スロット解放
        n.flags = 0;
        _in_use_count--;   // ★ カウンタも必ず減らす (これが抜けると pool 永久満杯のフリ)
    }
}

// ============================================================================
// tick (毎メインループ呼び出し)
// ============================================================================
void MidiPlayer::tick(uint32_t now_ms) {
    if (!_playing) return;
    playback_ms = now_ms - _start_ms;

    if (!_all_decoded) {
        _refill(playback_ms + MIDI_LOOKAHEAD_MS);
    }
    _release_done_slots();
    _dispatch_due();
}