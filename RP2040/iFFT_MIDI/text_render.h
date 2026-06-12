#pragma once
// ============================================================================
// text_render.h — 言語非依存 UTF-8 テキスト描画 (U8g2_for_Adafruit_GFX ブリッジ)
//
//  ・既存の Adafruit_SSD1306 バッファにそのまま描画 (2枚目のFB不要 = RAM増分ほぼ0)
//  ・入力は UTF-8。グリフ単位で複数フォントへディスパッチし、
//    u8g2 の japanese1/2/3 分割を呼び出し側から隠蔽する。
//  ・言語は config.h の DISPLAY_LANG で切替。英数(8x16)は全言語で常時リンク。
//
//  必要ライブラリ: "U8g2_for_Adafruit_GFX" (Arduino Library Manager / olikraus)
//                  日本語フォントを同梱。依存は Adafruit_GFX のみ。
// ============================================================================
#include <Adafruit_GFX.h>
#include "config.h"

// 一度だけ呼ぶ (display.begin() の後)
void text_init(Adafruit_GFX& gfx);

// 前景色 (SSD1306 は 1=白 / 0=黒)
void text_set_color(uint16_t fg);

int  text_line_height();   // 16
int  text_ascent();        // ベースラインまでの上げ幅 (~14)

// (x, y_top) を左上として1行描画。戻り値 = 描画後の x。
int  text_draw(int x, int y_top, const char* utf8);

// 描画せず合計幅(px)。Unifont は等幅(英数/半角カナ=8, 全角=16)なので厳密。
int  text_width(const char* utf8);

// max_w[px] に収まる範囲だけ描画。全部入れば true。
bool text_draw_clip(int x, int y_top, const char* utf8, int max_w);

// max_w で折り返して最大 max_lines 行まで描画。戻り値 = 使用行数。
int  text_draw_wrap(int x, int y_top, const char* utf8,
                    int max_w, int line_h, int max_lines);