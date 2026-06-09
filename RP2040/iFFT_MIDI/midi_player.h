#pragma once
// ============================================================================
// midi_player.h  ─ v7 Note Pool Architecture
//
// [BUGFIX v7.1]
// - DISPATCH_LEAD_MS: 50ms → 100ms
//   draw_notes()がフレームレート依存(33ms)のため、50msでは不足する場合がある。
//   100msにすることでフレーム遅れによるノーツ取りこぼしを防ぐ。
// ============================================================================
#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"

#ifndef MIDI_LOOKAHEAD_MS
#define MIDI_LOOKAHEAD_MS 8000
#endif

#ifndef MIDI_KEEP_PAST_MS
#define MIDI_KEEP_PAST_MS 500
#endif

struct Note {
    uint32_t start_ms;
    uint16_t duration_ms;
    uint8_t  note;
    uint8_t  velocity;
    uint8_t  channel;
    uint8_t  program;
    uint8_t  flags;
    uint8_t  is_drum;
};

static constexpr uint8_t NOTE_FLAG_IN_USE   = 0x01;
static constexpr uint8_t NOTE_FLAG_ON_FIRED = 0x02;
static constexpr uint8_t NOTE_FLAG_OFF_FIRED= 0x04;

static constexpr uint16_t NOTE_DURATION_ACTIVE = 65535;

struct NoteEvent {
    uint32_t time_ms;
    uint16_t duration_ms;
    uint8_t  note;
    uint8_t  velocity;
    uint8_t  channel;
    uint8_t  program;
    bool     is_drum;
};

class MidiPlayer {
public:
    int      ev_count() const { return POOL_SIZE; }
    bool     ev_in_use(int i) const { return (_pool[i].flags & NOTE_FLAG_IN_USE) != 0; }
    uint32_t ev_start_ms(int i) const { return _pool[i].start_ms; }
    uint32_t ev_end_ms(int i) const {
        if (_pool[i].duration_ms == NOTE_DURATION_ACTIVE) return (uint32_t)-1;
        return _pool[i].start_ms + _pool[i].duration_ms;
    }
    bool     ev_active(int i) const { return _pool[i].duration_ms == NOTE_DURATION_ACTIVE; }
    bool     ev_on_fired(int i) const { return (_pool[i].flags & NOTE_FLAG_ON_FIRED) != 0; }
    bool     ev_off_fired(int i)const { return (_pool[i].flags & NOTE_FLAG_OFF_FIRED) != 0; }
    uint8_t  ev_note(int i) const { return _pool[i].note; }
    uint8_t  ev_velocity(int i) const { return _pool[i].velocity; }
    uint8_t  ev_channel(int i) const { return _pool[i].channel; }
    uint8_t  ev_program(int i) const { return _pool[i].program; }
    bool     ev_is_drum(int i) const { return _pool[i].is_drum != 0; }
    uint32_t display_ms() const { return playback_ms + DISPATCH_LEAD_MS; }

    // [BUGFIX] DISPATCH_LEAD_MS: 50ms → 100ms
    // draw_notes()のOLEDフレームレートは33ms。
    // 50msでは1フレーム遅れでLEADを追い越す可能性がある。
    // 100msにすることでマージンを確保。
    static constexpr uint32_t DISPATCH_LEAD_MS = 100;

    uint16_t ev_dur(int i) const { return _pool[i].duration_ms; }
    uint32_t ev_time(int i) const { return _pool[i].start_ms; }

    bool load_path(const char* path);
    bool load(const char* path) { return load_path(path); }
    void play();
    void stop();
    bool is_finished() const;
    bool is_playing() const { return _playing; }
    void tick(uint32_t now_ms);

    int      pool_in_use() const { return _in_use_count; }
    int      pool_active() const {
        int c = 0;
        for (int i = 0; i < POOL_SIZE; i++) {
            if ((_pool[i].flags & NOTE_FLAG_IN_USE) && _pool[i].duration_ms == NOTE_DURATION_ACTIVE) c++;
        }
        return c;
    }
    void     dump_stats() const {
        int in_use = _in_use_count, active = 0, ancient = 0, future = 0;
        for (int i = 0; i < POOL_SIZE; i++) {
            const Note& n = _pool[i];
            if (!(n.flags & NOTE_FLAG_IN_USE)) continue;
            if (n.duration_ms == NOTE_DURATION_ACTIVE) active++;
            if (playback_ms > n.start_ms + 30000) ancient++;
            if (n.start_ms > playback_ms + 100) future++;
        }
        DBG_PRINTF("[midi] t=%lu in_use=%d act=%d anc=%d fut=%d ev_end=%lu\n",
                   (unsigned long)playback_ms, in_use, active, ancient, future,
                   (unsigned long)_last_event_end_ms);
    }

    using DispatchCb = void (*)(const NoteEvent& ev);
    void set_dispatch_cb(DispatchCb cb) { _dispatch_cb = cb; }

    char current_file[64] = "";
    char current_title[32] = "";
    volatile uint32_t playback_ms = 0;

private:
    static constexpr int POOL_SIZE = 4096;
    static constexpr int MAX_TRACKS = 24;
    static constexpr int MAX_TEMPO_CHANGES = 1024;

    Note _pool[POOL_SIZE];

public:
    static constexpr int QUEUE_SIZE = 256;
    NoteEvent queue[QUEUE_SIZE];
    volatile int q_write = 0;
    volatile int q_read  = 0;
    int q_pop(NoteEvent& out) {
        if (q_read == q_write) return 0;
        out = queue[q_read];
        q_read = (q_read + 1) % QUEUE_SIZE;
        return 1;
    }
private:
    DispatchCb _dispatch_cb = nullptr;

    File _file;
    bool _file_open = false;
    bool _playing = false;
    uint32_t _start_ms = 0;
    int _ntracks = 0;
    uint16_t _tpb = 96;

    struct OpenNote {
        uint32_t start_tick;
        uint8_t  note, vel, ch, prog;
    };
    struct TrackState {
        uint32_t cur_tick = 0;
        uint8_t  running_status = 0;
        bool     done = false;
        uint32_t file_offset = 0;
        uint32_t end_offset = 0;
        bool     peek_valid = false;
        uint32_t peek_tick = 0;
        uint8_t  peek_status = 0;
        uint8_t  peek_d1 = 0;
        uint8_t  peek_d2 = 0;
        uint8_t  ch_prog[16];
        static constexpr int MAX_OPEN = 32;
        OpenNote open[MAX_OPEN];
        int      open_n = 0;
    };
    TrackState _tracks[MAX_TRACKS];

    struct TempoChange { uint32_t tick; uint32_t us_per_qn; };
    TempoChange _tempo[MAX_TEMPO_CHANGES];
    int _tempo_n = 0;

    bool _all_decoded = false;
    uint32_t _last_processed_ms = 0;
    uint32_t _last_event_end_ms = 0;
    int      _in_use_count = 0;
    int      _alloc_cursor = 0;

    bool _open_song(const char* path);
    void _close_song();
    bool _parse_header_and_tempo();
    bool _track_peek_next(TrackState& t);
    void _process_track_event(TrackState& t);
    void _refill(uint32_t up_to_ms);
    void _dispatch_due();
    void _release_done_slots();
    void _emit_orphan_offs();

    int      _alloc_slot();
    int      _find_active(uint8_t note, uint8_t ch, uint32_t start_ms);
    bool     _enqueue_dispatch(const Note& n, bool is_off);

    uint32_t _tick_to_ms(uint32_t tick) const;
};