#include "midi_player.h"
#include <cstring>
#include <algorithm>

// ============================================================================
// SMF ストリーミング MidiPlayer
//   ・全イベントを一度に RAM に展開せず、ファイルからオンデマンドで decode
//   ・テンポマップだけはロード時に固定
//   ・現在位置 ± lookahead の窓に入るイベントだけリングバッファに保持
// ============================================================================

#ifndef MIDI_LOOKAHEAD_MS
#define MIDI_LOOKAHEAD_MS 8000   // 何ms先のイベントまでバッファに入れるか
#endif

#ifndef MIDI_KEEP_PAST_MS
#define MIDI_KEEP_PAST_MS 8000    // バッファに残しておく過去ms(可視化のため)
#endif

// ----- big-endian read helpers (受け取った buf 用) -----
static inline uint32_t ru32be(const uint8_t* p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static inline uint16_t ru16be(const uint8_t* p) {
    return ((uint16_t)p[0]<<8)|p[1];
}

// ============================================================================
// scan_songs / song_name (既存と同じ)
// ============================================================================
int MidiPlayer::scan_songs() {
    _count = 0;
    Dir dir = LittleFS.openDir(CFG_MIDI_DIR);
    while (dir.next() && _count < MAX_SONGS) {
        String name = dir.fileName();
        if (name.endsWith(".mid") || name.endsWith(".MID")) {
            snprintf(_songs[_count], 64, "%s%s%s",
                CFG_MIDI_DIR,
                (CFG_MIDI_DIR[strlen(CFG_MIDI_DIR)-1]=='/') ? "" : "/",
                name.c_str());
            _count++;
        }
    }
    for (int i=0;i<_count-1;i++)
        for (int j=i+1;j<_count;j++)
            if (strcmp(_songs[i],_songs[j])>0) {
                char t[64]; memcpy(t,_songs[i],64);
                memcpy(_songs[i],_songs[j],64); memcpy(_songs[j],t,64);
            }
    DBG_PRINTF("[midi] %d songs\n", _count);
    return _count;
}

const char* MidiPlayer::song_name(int idx) const {
    if (idx<0||idx>=_count) return "";
    const char* p = strrchr(_songs[idx],'/');
    return p ? p+1 : _songs[idx];
}

// ============================================================================
// ロード処理
// ============================================================================
bool MidiPlayer::load(int idx) {
    if (idx<0||idx>=_count) return false;
    stop();
    strncpy(current_file, _songs[idx], 63);
    strncpy(current_title, song_name(idx), 31);
    char* dot=strrchr(current_title,'.'); if(dot)*dot='\0';
    return _open_song(current_file);
}

bool MidiPlayer::load_path(const char* path) {
    stop();
    strncpy(current_file, path, 63);
    const char* base=strrchr(path,'/');
    base=base?base+1:path;
    strncpy(current_title, base, 31);
    char* dot=strrchr(current_title,'.'); if(dot)*dot='\0';
    return _open_song(current_file);
}

bool MidiPlayer::_open_song(const char* path) {
    _close_song();
    _file = LittleFS.open(path, "r");
    if (!_file) {
        DBG_PRINTF("[midi] cannot open: %s\n", path);
        return false;
    }
    _file_open = true;

    // ---- ヘッダ + 全テンポイベントを事前スキャン ----
    if (!_parse_header_and_tempo()) {
        _close_song();
        return false;
    }

    // ---- リングバッファ・トラック状態の初期化 ----
    _ring_head = 0;
    _ring_count = 0;
    _dispatch_idx = 0;
    _all_decoded = false;
    _last_event_end_ms = 0;

    for (int i = 0; i < _ntracks; i++) {
        TrackState& t = _tracks[i];
        t.cur_tick = 0;
        t.running_status = 0;
        memset(t.ch_prog, 0, 16);
        t.done = false;
        t.peek_valid = false;
        t.open_n = 0;
        // file_offset / end_offset は _parse_header_and_tempo で既に設定済み
        // 最初のイベントを先読み
        _track_peek_next(t);
    }

    DBG_PRINTF("[midi] streaming ready, tpb=%d tracks=%d tempo_n=%d\n",
               _tpb, _ntracks, _tempo_n);
    return true;
}

void MidiPlayer::_close_song() {
    if (_file_open) { _file.close(); _file_open = false; }
    _ring_count = 0;
    _ring_head = 0;
    _dispatch_idx = 0;
    _ntracks = 0;
    _all_decoded = false;
}

// ============================================================================
// ヘッダ読み込み + テンポマップ構築
//
// File API で sequential 読み (seek + read). track の MTrk ヘッダを順に拾い、
// トラックごとに「テンポイベントだけ」を 1パスで集める。
// ============================================================================
bool MidiPlayer::_parse_header_and_tempo() {
    _tempo_n = 0;
    _tempo[0] = {0, 500000}; _tempo_n = 1;   // 既定 120BPM

    uint8_t hdr[14];
    _file.seek(0);
    if (_file.read(hdr, 14) != 14) return false;
    if (memcmp(hdr, "MThd", 4) != 0) return false;
    uint32_t hdr_len = ru32be(hdr + 4);
    uint16_t fmt    = ru16be(hdr + 8);
    uint16_t ntrk   = ru16be(hdr + 10);
    _tpb            = ru16be(hdr + 12);
    if (fmt > 1) return false;
    uint32_t pos = 8 + hdr_len;   // chunk-style 長さ + 8 byte ヘッダ

    if (ntrk > MAX_TRACKS) {
        DBG_PRINTF("[midi] WARNING: %d tracks > MAX_TRACKS %d (truncating)\n",
                   ntrk, MAX_TRACKS);
        ntrk = MAX_TRACKS;
    }
    _ntracks = ntrk;

    for (int t = 0; t < ntrk; t++) {
        uint8_t mtrk[8];
        _file.seek(pos);
        if (_file.read(mtrk, 8) != 8) return false;
        if (memcmp(mtrk, "MTrk", 4) != 0) return false;
        uint32_t trk_len = ru32be(mtrk + 4);
        uint32_t track_data_pos = pos + 8;
        _tracks[t].file_offset = track_data_pos;
        _tracks[t].end_offset  = track_data_pos + trk_len;
        // テンポイベントだけスキャン
        _scan_track_for_tempo(track_data_pos, trk_len);
        pos = track_data_pos + trk_len;
    }

    // テンポを tick 順にソート
    for (int i=0;i<_tempo_n-1;i++)
        for (int j=i+1;j<_tempo_n;j++)
            if (_tempo[j].tick < _tempo[i].tick) {
                TempoEntry tt=_tempo[i]; _tempo[i]=_tempo[j]; _tempo[j]=tt;
            }
    DBG_PRINTF("[midi] tempo changes: %d\n", _tempo_n);
    return true;
}

// テンポメタイベントだけ拾うトラックスキャン (1回読みするためバッファ使用)
bool MidiPlayer::_scan_track_for_tempo(uint32_t track_offset, uint32_t track_len) {
    // 小さいバッファでチャンク読み (LittleFS のシングルリードオーバヘッド削減)
    const int BUFSZ = 256;
    uint8_t  buf[BUFSZ];
    uint32_t file_pos = track_offset;
    uint32_t end      = track_offset + track_len;
    int      bp = 0, bn = 0;
    auto refill = [&](void) -> bool {
        if (file_pos >= end) return false;
        uint32_t want = end - file_pos;
        if (want > (uint32_t)BUFSZ) want = BUFSZ;
        _file.seek(file_pos);
        int got = _file.read(buf, want);
        if (got <= 0) return false;
        bp = 0; bn = got; file_pos += got;
        return true;
    };
    auto rb = [&](int& out) -> bool {
        if (bp >= bn) { if (!refill()) return false; }
        out = buf[bp++];
        return true;
    };

    uint32_t abs_tick = 0;
    uint8_t  running = 0;

    while (true) {
        // delta
        uint32_t delta = 0;
        int b;
        while (true) {
            if (!rb(b)) return true;
            delta = (delta<<7) | (b & 0x7F);
            if (!(b & 0x80)) break;
        }
        abs_tick += delta;
        if (!rb(b)) return true;
        uint8_t status;
        if (b & 0x80) { status = (uint8_t)b; running = status; }
        else          { status = running; bp--; if (bp < 0) bp = 0; }

        if (status == 0xFF) {
            int meta; if (!rb(meta)) return true;
            uint32_t mlen = 0;
            while (true) {
                if (!rb(b)) return true;
                mlen = (mlen<<7) | (b & 0x7F);
                if (!(b & 0x80)) break;
            }
            if (meta == 0x51 && mlen == 3 && _tempo_n < MAX_TEMPO) {
                int b0,b1,b2;
                if (!rb(b0)||!rb(b1)||!rb(b2)) return true;
                uint32_t us = ((uint32_t)b0<<16)|((uint32_t)b1<<8)|b2;
                _tempo[_tempo_n++] = {abs_tick, us};
                mlen -= 3;
            }
            // skip remaining mlen bytes
            while (mlen-- > 0) { if (!rb(b)) return true; }
            if (meta == 0x2F) return true;   // end of track
        } else if (status == 0xF0 || status == 0xF7) {
            uint32_t slen = 0;
            while (true) {
                if (!rb(b)) return true;
                slen = (slen<<7) | (b & 0x7F);
                if (!(b & 0x80)) break;
            }
            while (slen-- > 0) { if (!rb(b)) return true; }
        } else {
            uint8_t type = status & 0xF0;
            int needed = (type==0xC0 || type==0xD0) ? 1 : 2;
            while (needed-- > 0) { if (!rb(b)) return true; }
        }
    }
}

uint32_t MidiPlayer::_tick_to_ms(uint32_t tick) const {
    uint64_t us = 0;
    uint32_t prev_tick = 0;
    uint32_t tempo = 500000;
    for (int i = 0; i < _tempo_n; i++) {
        if (_tempo[i].tick >= tick) break;
        us += (uint64_t)(_tempo[i].tick - prev_tick) * tempo / _tpb;
        prev_tick = _tempo[i].tick;
        tempo = _tempo[i].us_per_beat;
    }
    us += (uint64_t)(tick - prev_tick) * tempo / _tpb;
    return (uint32_t)(us / 1000);
}

// ============================================================================
// トラックの次の channel-event を先読み
//   meta / sysex はスキップ。done になったら peek_valid=false
// ============================================================================
bool MidiPlayer::_track_peek_next(TrackState& t) {
    t.peek_valid = false;
    if (t.done) return false;
    if (t.file_offset >= t.end_offset) { t.done = true; return false; }
    _file.seek(t.file_offset);

    while (true) {
        // delta
        uint32_t delta = 0;
        while (true) {
            int b = _file.read();
            if (b < 0) { t.done = true; return false; }
            delta = (delta<<7) | (b & 0x7F);
            if (!(b & 0x80)) break;
        }
        t.cur_tick += delta;
        int sb = _file.read();
        if (sb < 0) { t.done = true; return false; }
        uint8_t status;
        if (sb & 0x80) { status = (uint8_t)sb; t.running_status = status; }
        else           { status = t.running_status;
                         // running status: 1バイト戻る
                         _file.seek(_file.position() - 1);
                       }

        if (status == 0xFF) {
            int meta = _file.read();
            if (meta < 0) { t.done = true; return false; }
            uint32_t mlen = 0;
            while (true) {
                int b = _file.read();
                if (b < 0) { t.done = true; return false; }
                mlen = (mlen<<7) | (b & 0x7F);
                if (!(b & 0x80)) break;
            }
            _file.seek(_file.position() + mlen);
            if (meta == 0x2F) { t.done = true; t.file_offset = _file.position(); return false; }
            t.file_offset = _file.position();
            if (t.file_offset >= t.end_offset) { t.done = true; return false; }
            continue;
        } else if (status == 0xF0 || status == 0xF7) {
            uint32_t slen = 0;
            while (true) {
                int b = _file.read();
                if (b < 0) { t.done = true; return false; }
                slen = (slen<<7) | (b & 0x7F);
                if (!(b & 0x80)) break;
            }
            _file.seek(_file.position() + slen);
            t.file_offset = _file.position();
            if (t.file_offset >= t.end_offset) { t.done = true; return false; }
            continue;
        } else {
            // channel event
            uint8_t type = status & 0xF0;
            int d1 = _file.read();
            if (d1 < 0) { t.done = true; return false; }
            int d2 = 0;
            if (type != 0xC0 && type != 0xD0) {
                d2 = _file.read();
                if (d2 < 0) { t.done = true; return false; }
            }
            t.peek_status = status;
            t.peek_d1 = (uint8_t)d1;
            t.peek_d2 = (uint8_t)d2;
            t.peek_tick = t.cur_tick;
            t.peek_valid = true;
            t.file_offset = _file.position();
            return true;
        }
    }
}

// ============================================================================
// 1つの先読みイベントを処理して、次の peek へ進む
// ============================================================================
void MidiPlayer::_process_track_event(TrackState& t) {
    if (!t.peek_valid) return;
    uint8_t status = t.peek_status;
    uint8_t type   = status & 0xF0;
    uint8_t ch     = status & 0x0F;

    if (type == 0x90 && t.peek_d2 > 0) {
        // ----- note-on: 即時にイベント発行 (プレースホルダ duration) -----
        // これにより長い持続音でも OLED に早く現れる。
        // 65535ms は note-off が来なかった場合の安全上限 (上限 ~65s 後に自動 off)。
        NoteEvent ev;
        ev.time_ms     = _tick_to_ms(t.peek_tick);
        ev.duration_ms = 65535;
        ev.note        = t.peek_d1;
        ev.velocity    = t.peek_d2;
        ev.channel     = ch;
        ev.program     = t.ch_prog[ch];
        ev.is_drum     = (ch == 9);
        _insert_sorted(ev);
        if (ev.time_ms + ev.duration_ms > _last_event_end_ms)
            _last_event_end_ms = ev.time_ms + ev.duration_ms;
        // 保留テーブルに登録 (note-off で照合)
        if (t.open_n < TrackState::MAX_OPEN) {
            OpenNote& on = t.open[t.open_n++];
            on.start_tick = t.peek_tick;
            on.note = t.peek_d1;
            on.vel  = t.peek_d2;
            on.ch   = ch;
            on.prog = t.ch_prog[ch];
        }
    } else if (type == 0x80 || (type == 0x90 && t.peek_d2 == 0)) {
        // ----- note-off: 最後の同名 note-on を引き当てて duration を上書き -----
        for (int i = t.open_n - 1; i >= 0; i--) {
            if (t.open[i].ch == ch && t.open[i].note == t.peek_d1) {
                uint32_t start_ms = _tick_to_ms(t.open[i].start_tick);
                uint32_t off_ms   = _tick_to_ms(t.peek_tick);
                uint32_t dur      = (off_ms > start_ms) ? off_ms - start_ms : 50;
                if (!dur) dur = 50;
                uint16_t dur_clamped = (uint16_t)std::min(dur, (uint32_t)65535);

                // リング内の対応する note-on イベントを探して上書き
                // (time_ms + note + ch + velocity>0 + 元プレースホルダの組み合わせで一意特定)
                for (int j = 0; j < _ring_count; j++) {
                    int ri = (_ring_head + j) % RING_SIZE;
                    if (_ring[ri].time_ms == start_ms &&
                        _ring[ri].note    == t.peek_d1 &&
                        _ring[ri].channel == ch &&
                        _ring[ri].velocity > 0 &&
                        _ring[ri].duration_ms == 65535) {
                        _ring[ri].duration_ms = dur_clamped;
                        if (_ring[ri].time_ms + dur_clamped > _last_event_end_ms)
                            _last_event_end_ms = _ring[ri].time_ms + dur_clamped;
                        break;
                    }
                }
                t.open[i] = t.open[--t.open_n];
                break;
            }
        }
    } else if (type == 0xC0) {
        t.ch_prog[ch] = t.peek_d1;
    }
    // CC / Pitch bend / etc. は無視

    _track_peek_next(t);
}

// ============================================================================
// リングバッファへの昇順挿入 (時系列を維持)
// ============================================================================
void MidiPlayer::_insert_sorted(const NoteEvent& ev) {
    if (_ring_count >= RING_SIZE - 1) return;   // フル
    int pos = _ring_count;
    while (pos > 0) {
        int prev_i = (_ring_head + pos - 1) % RING_SIZE;
        if (_ring[prev_i].time_ms <= ev.time_ms) break;
        int cur_i = (_ring_head + pos) % RING_SIZE;
        _ring[cur_i] = _ring[prev_i];
        pos--;
    }
    int ins_i = (_ring_head + pos) % RING_SIZE;
    _ring[ins_i] = ev;
    _ring_count++;
    // 過去に挿入された場合は dispatch_idx を維持できないので再計算
    // (実用上、過去への挿入は dispatch_idx より後でも前でもありうる)
    if (pos < _dispatch_idx) _dispatch_idx++;
}

// ============================================================================
// 先読みでリングバッファを満たす
//   up_to_ms 以下の time_ms を持つイベントが揃うまで decode し続ける。
//   ただし、保留(note-on)が多いと note-off まで decode が進むので、
//   実際は up_to_ms より少し先まで decode することがある (それで正しい)。
// ============================================================================
void MidiPlayer::_refill_ring(uint32_t up_to_ms) {
    // 全トラックの中で最も小さい peek_tick (ms換算) を持つトラックを進める
    while (_ring_count < RING_SIZE - 8) {
        int     earliest = -1;
        uint32_t earliest_tick = 0xFFFFFFFFu;
        for (int i = 0; i < _ntracks; i++) {
            TrackState& t = _tracks[i];
            if (t.done || !t.peek_valid) continue;
            if (t.peek_tick < earliest_tick) {
                earliest = i;
                earliest_tick = t.peek_tick;
            }
        }
        if (earliest < 0) {
            _all_decoded = true;
            break;
        }
        uint32_t earliest_ms = _tick_to_ms(earliest_tick);
        if (earliest_ms > up_to_ms) break;
        _process_track_event(_tracks[earliest]);
    }
}

// ============================================================================
// 過去のイベントを破棄してリングを空ける
// ============================================================================
void MidiPlayer::_gc_old_events(uint32_t before_ms) {
    while (_ring_count > 0) {
        NoteEvent& head = _ring[_ring_head];
        uint32_t end_ms = head.time_ms + head.duration_ms;
        if (end_ms >= before_ms) break;
        _ring_head = (_ring_head + 1) % RING_SIZE;
        _ring_count--;
        if (_dispatch_idx > 0) _dispatch_idx--;
    }
}

// ============================================================================
// 再生制御
// ============================================================================
void MidiPlayer::play() {
    if (!_file_open) return;
    _start_ms = millis();
    playback_ms = 0;
    _playing = true;
    q_write = q_read = 0;
    _dispatch_idx = 0;
}

void MidiPlayer::stop() {
    _playing = false;
    playback_ms = 0;
}

bool MidiPlayer::is_finished() const {
    if (!_playing) return false;
    if (!_all_decoded) return false;
    // 全イベント発火済み (queueも空) かつ余韻終了
    if (_ring_count > _dispatch_idx) return false;
    return playback_ms >= _last_event_end_ms;
}

// ============================================================================
// tick: 主要メインループから毎フレーム呼ばれる
//   ・playback_ms 更新
//   ・必要なら decode して ring をふくらませる
//   ・古いイベントを GC
//   ・送出 (queue に push)
// ============================================================================
void MidiPlayer::tick(uint32_t now_ms) {
    if (!_playing) return;
    uint32_t pos_ms = now_ms - _start_ms;
    playback_ms = pos_ms;

    // 先読み (現在 + 2000ms までを buffer に)
    if (!_all_decoded) {
        _refill_ring(pos_ms + MIDI_LOOKAHEAD_MS);
    }

    // GC (現在 - 200ms より古いイベントを捨てる)
    uint32_t before = (pos_ms > MIDI_KEEP_PAST_MS) ? pos_ms - MIDI_KEEP_PAST_MS : 0;
    _gc_old_events(before);

    // 送出 (現在 + 50ms 以内のイベントを queue に)
    while (_dispatch_idx < _ring_count) {
        int idx = (_ring_head + _dispatch_idx) % RING_SIZE;
        const NoteEvent& ev = _ring[idx];
        if (ev.time_ms > pos_ms + 50) break;
        uint8_t next_w = (q_write + 1) % QUEUE_SIZE;
        if (next_w == q_read) break;   // queue full
        memcpy((void*)&queue[q_write], &ev, sizeof(NoteEvent));
        __dmb();
        q_write = next_w;
        _dispatch_idx++;
    }
}