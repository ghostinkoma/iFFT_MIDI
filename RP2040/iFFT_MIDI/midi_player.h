#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"

struct NoteEvent {
    uint32_t time_ms;
    uint16_t duration_ms;
    uint8_t  note;
    uint8_t  velocity;
    uint8_t  channel;
    uint8_t  program;
    bool     is_drum;
};

// ============================================================================
// MidiPlayer (streaming版)
//   ・SMFファイルを開いたまま保持し、トラックごとにオフセットを管理
//   ・テンポマップだけはロード時にスキャン (小さい、固定)
//   ・再生中は「playback_ms ± lookahead」の窓に入るイベントだけを
//     リングバッファに展開・破棄
//   ・CFG_MAX_EVENTS の制限が事実上撤廃される (SMFサイズ次第)
//   ・RAM 約 128KB → 約 12KB
// ============================================================================
class MidiPlayer {
public:
    // ---- 既存 API (シグネチャ不変) ----
    int  scan_songs();
    const char* song_name(int idx) const;
    int  song_count() const { return _count; }

    bool load(int song_idx);
    bool load_path(const char* path);
    void play();
    void stop();
    void tick(uint32_t now_ms);
    bool is_playing()  const { return _playing; }
    bool is_finished() const;

    // ---- 可視化用 (リングバッファ内のイベントを返す。フレームごとに
    //               g_vis_cursor を 0 にリセットする前提で動く) ----
    int      ev_count() const { return _ring_count; }
    uint32_t ev_time(int i) const { return _ring[(_ring_head + i) % RING_SIZE].time_ms; }
    uint16_t ev_dur(int i)  const { return _ring[(_ring_head + i) % RING_SIZE].duration_ms; }
    uint8_t  ev_note(int i) const { return _ring[(_ring_head + i) % RING_SIZE].note; }

    static const int QUEUE_SIZE = 64;
    NoteEvent        queue[QUEUE_SIZE];
    volatile uint8_t q_write = 0;
    volatile uint8_t q_read  = 0;

    volatile uint32_t playback_ms = 0;
    char current_title[32];
    char current_file [64];

private:
    static const int MAX_SONGS = CFG_MAX_SONGS;
    char _songs[MAX_SONGS][64];
    int  _count = 0;

    // ---- SMF ストリーミング状態 ----
    File _file;
    bool _file_open = false;
    uint16_t _tpb = 480;
    int      _ntracks = 0;

    // テンポマップ (ロード時に作成、再生中固定)
    struct TempoEntry { uint32_t tick; uint32_t us_per_beat; };
    static const int MAX_TEMPO = 128;
    TempoEntry _tempo[MAX_TEMPO];
    int        _tempo_n = 0;

    // ノート保留 (note-on を見たが note-off 未到来)
    struct OpenNote {
        uint32_t start_tick;
        uint8_t  note, vel, ch, prog;
    };

    // トラック状態
    struct TrackState {
        uint32_t file_offset;     // 次に読むバイト位置
        uint32_t end_offset;
        uint32_t cur_tick;
        uint8_t  running_status;
        uint8_t  ch_prog[16];
        bool     done;
        // 先読みされた次イベント
        bool     peek_valid;
        uint8_t  peek_status;
        uint8_t  peek_d1, peek_d2;
        uint32_t peek_tick;
        // 同トラックの保留ノート
        static const int MAX_OPEN = 32;
        OpenNote open[MAX_OPEN];
        int      open_n;
    };
    static const int MAX_TRACKS = 24;
    TrackState _tracks[MAX_TRACKS];

    // ---- イベントリングバッファ (time_ms 昇順) ----
    static const int RING_SIZE = 4096;
    NoteEvent _ring[RING_SIZE];
    int _ring_head = 0;    // 最古イベントの位置
    int _ring_count = 0;
    int _dispatch_idx = 0; // 次にqueue 投入するイベントの ring 相対位置

    // 再生制御
    bool     _playing = false;
    bool     _all_decoded = false;
    uint32_t _start_ms = 0;
    uint32_t _last_event_end_ms = 0;

    // 内部メソッド
    bool _open_song(const char* path);
    void _close_song();
    bool _parse_header_and_tempo();
    bool _scan_track_for_tempo(uint32_t track_offset, uint32_t track_len);
    bool _track_peek_next(TrackState& t);
    void _process_track_event(TrackState& t);
    void _refill_ring(uint32_t up_to_ms);
    void _gc_old_events(uint32_t before_ms);
    void _insert_sorted(const NoteEvent& ev);
    uint32_t _tick_to_ms(uint32_t tick) const;
};