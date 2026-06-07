#pragma once
// ================================================================
// song_list.h
// SongList.txt フォーマット:
//   ; コメント
//   FilenameHash=filename.mid
//   Title="曲名"        (省略時: ファイル名)
//   Author="作曲者"     (省略/NULL時: "Unknown")
//   Arranger="編曲者"   (省略/NULL時: 表示しない)
// ================================================================
#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"

struct SongInfo {
    char midi_path [64];
    char title     [48];
    char author    [32];
    char arranger  [32];
    bool has_title;
    bool has_arranger;
};

class SongList {
public:
    static const int MAX = CFG_MAX_SONGS;

    int             load(const char* midi_dir);
    int             count()        const { return _count; }
    const SongInfo& get(int idx)   const { return _songs[idx]; }

    // OLED表示 (Adafruit_SSD1306を直接受け取る)
    void show_on_oled(void* disp_ptr, int idx) const;

private:
    SongInfo _songs[MAX];
    int      _count = 0;

    static void _unquote_trim(const char* src, char* dst, int max_len);
    static bool _is_null_or_empty(const char* s);
};