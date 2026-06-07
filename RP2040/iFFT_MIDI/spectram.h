#pragma once
#include <string>
#include <vector>
#include <cstdint>

// FFTサイズ (抽出時)
constexpr int    ORGEL_FFT_SIZE   = 2048;
constexpr int    ORGEL_SPEC_HALF  = ORGEL_FFT_SIZE / 2 + 1;
constexpr int    ORGEL_VOICE_NH   = 64;   // 1音色あたり最大倍音数

// 抽出された1音色の解析結果
struct VoiceAnalysis {
    std::string  name;        // "piano_060" など
    float        base_hz;     // 基音周波数
    float        decay_tau_ms;// 実測減衰時定数
    int          nh;          // 有効倍音数 (≤ ORGEL_VOICE_NH)
    int16_t      harm[ORGEL_VOICE_NH+1]; // Q15振幅 (index 1..nh)
    float        snr_db;      // 倍音モデルの再構成SNR (診断用)

    // 元情報 (生成時参照用)
    int          program;     // GM Program (-1 ならドラム)
    bool         is_drum;
    int          midi_note;   // サンプルしたノート
};

// 1音色の解析実行
// pcm: モノラル float サンプル (-1..1)
// f_base_hint: 期待基音Hz (周波数推定の初期値)
// 戻り値: VoiceAnalysis (harm/decay_tau_ms 埋め込み済み)
VoiceAnalysis analyze_voice(const std::vector<float>& pcm, int rate,
                             float f_base_hint,
                             const std::string& name,
                             int program, bool is_drum, int midi_note);

// 全音色の voice_table.h / voice_table.cpp / voice_*.h を出力
void write_voice_files(const std::vector<VoiceAnalysis>& voices,
                        const std::string& out_dir);