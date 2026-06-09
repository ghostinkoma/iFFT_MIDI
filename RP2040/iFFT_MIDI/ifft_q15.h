#pragma once
// ================================================================
// ifft_q15.h — 自前 逆FFT (radix-2 DIT, 固定小数点)
//
// ・入力/作業バッファは int32 (精度確保のためスケーリングなし)
// ・ツイドル係数のみ Q15
// ・最終出力で 1/N スケール
// ・CMSIS-DSP 非依存 (ライブラリインストール不要)
//
// 使い方:
//   fft_q15_init();                  // setup時に1回 (ツイドル生成)
//   ifft_q15(work_re, work_im);      // in-place 逆変換
//   → work_re[] が時間ドメイン信号 (1/Nスケール済み)
// ================================================================
#include <stdint.h>
#include "config.h"


// setup時に呼ぶ (ツイドル/ビット反転テーブル生成)
void fft_q15_init();

// in-place 逆FFT
// re/im: 長さ FFT_N の int32 配列
// 実行後 re[] に時間信号 (im[] は ~0)
void ifft_q15(int32_t* re, int32_t* im);