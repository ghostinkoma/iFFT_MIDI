#pragma once
// ============================================================================
// shinonome_fonts.h — 東雲ゴシック (Public Domain) u8g2フォントの宣言
//   実体: font_shnmk16_jp.c (16x16 JIS X 0208) / font_shnm8x16_jp.c (8x16 英数+半角カナ)
// ============================================================================
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
extern const uint8_t u8g2_font_shnmk16_jp[];   // 16x16 全角(かな/漢字/記号, 第一+第二水準)
extern const uint8_t u8g2_font_shnm8x16_jp[];  // 8x16  ASCII + Latin-1 + 半角カナ
#ifdef __cplusplus
}
#endif