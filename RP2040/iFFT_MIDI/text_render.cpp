// ============================================================================
// text_render.cpp — UTF-8 + グリフ単位フォントディスパッチ
// ============================================================================
#include "text_render.h"
#include <U8g2_for_Adafruit_GFX.h>

static U8G2_FOR_ADAFRUIT_GFX u8f;
static int s_ascent = 14;

// ---- 英数フォント (全言語必須・8px幅) ----
//   日本語=東雲なら英数も東雲(shnm8x16: ASCII+Latin-1+半角カナ)で統一。
#if (DISPLAY_LANG == LANG_JPN) && (JPN_FONT == JPN_FONT_SHINONOME)
  #include "shinonome_fonts.h"
  #define FONT_LATIN  u8g2_font_shnm8x16_jp
#else
  #define FONT_LATIN  u8g2_font_unifont_tr
#endif

// ---- 言語別 CJK フォントリスト ----
//   グリフが無いフォントでは drawGlyph() が 0 を返すので、
//   先頭から試して最初に描けたものを採用する。
#if DISPLAY_LANG == LANG_JPN
  #if JPN_FONT == JPN_FONT_SHINONOME
    static const uint8_t* const FONTS_CJK[] = {
        u8g2_font_shnmk16_jp,            // 16x16 ゴシック, JIS X 0208 全6879字
    };
  #else
    static const uint8_t* const FONTS_CJK[] = {
        u8g2_font_unifont_t_japanese1,   //  47,944 B かな+常用
        u8g2_font_unifont_t_japanese2,   //  90,472 B 漢字(中位)
    #if JPN_INCLUDE_LEVEL2
        u8g2_font_unifont_t_japanese3,   // 161,658 B 漢字(希少)
    #endif
    };
  #endif
static const int FONTS_CJK_N = sizeof(FONTS_CJK) / sizeof(FONTS_CJK[0]);
#endif

// ---- UTF-8 → コードポイント (BMPのみ。JIS X 0208/半角カナを完全カバー) ----
static const char* u8_next(const char* s, uint16_t* cp) {
    const uint8_t* p = (const uint8_t*)s;
    uint8_t c = p[0];
    if (c == 0)            { *cp = 0; return s; }
    if (c < 0x80)          { *cp = c; return s + 1; }
    if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        *cp = ((uint16_t)(c & 0x1F) << 6) | (p[1] & 0x3F);
        return s + 2;
    }
    if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *cp = ((uint16_t)(c & 0x0F) << 12) | ((uint16_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        return s + 3;
    }
    if ((c & 0xF8) == 0xF0) { *cp = '?'; return s + 4; }  // BMP外は非対応
    *cp = '?'; return s + 1;                               // 不正バイト
}

// Unifont 等幅: 英数/半角カナ = 8px, 全角 = 16px
static inline int cp_width(uint16_t cp) {
    if (cp < 0x80) return 8;
    if (cp >= 0xFF61 && cp <= 0xFF9F) return 8;  // 半角カタカナ
    return 16;
}

// 1グリフ描画。戻り値 = 送り幅(px)
static int draw_cp(int x, int yb, uint16_t cp) {
    int w = cp_width(cp);
    // 8px系 (ASCII / 半角カナ) は Latin フォントを優先
    //   東雲: shnm8x16 が ASCII+半角カナを持つ / Unifont: 半角カナは japanese1 側
    if (cp < 0x80 || (cp >= 0xFF61 && cp <= 0xFF9F)) {
        u8f.setFont(FONT_LATIN);
        if (u8f.drawGlyph(x, yb, cp)) return w;
    }
#if DISPLAY_LANG == LANG_JPN
    for (int i = 0; i < FONTS_CJK_N; i++) {
        u8f.setFont(FONTS_CJK[i]);
        if (u8f.drawGlyph(x, yb, cp)) return w;  // 描けたら確定
    }
#endif
    // どのフォントにも無い → '?' で代替
    u8f.setFont(FONT_LATIN);
    u8f.drawGlyph(x, yb, '?');
    return cp_width('?');
}

// ============================================================================
void text_init(Adafruit_GFX& gfx) {
    u8f.begin(gfx);
    u8f.setFontMode(1);          // transparent: 背景を塗らない
    u8f.setFontDirection(0);
    u8f.setForegroundColor(1);   // SSD1306_WHITE
    u8f.setFont(FONT_LATIN);
    int a = u8f.getFontAscent();
    s_ascent = (a > 0) ? a : 14;
}

void text_set_color(uint16_t fg) { u8f.setForegroundColor(fg); }
int  text_line_height() { return 16; }
int  text_ascent()      { return s_ascent; }

int text_draw(int x, int y_top, const char* utf8) {
    int yb = y_top + s_ascent;
    uint16_t cp; const char* s = utf8;
    for (;;) { s = u8_next(s, &cp); if (!cp) break; x += draw_cp(x, yb, cp); }
    return x;
}

int text_width(const char* utf8) {
    int w = 0; uint16_t cp; const char* s = utf8;
    for (;;) { s = u8_next(s, &cp); if (!cp) break; w += cp_width(cp); }
    return w;
}

bool text_draw_clip(int x, int y_top, const char* utf8, int max_w) {
    int yb = y_top + s_ascent, x0 = x;
    uint16_t cp; const char* s = utf8;
    for (;;) {
        s = u8_next(s, &cp); if (!cp) break;
        int w = cp_width(cp);
        if (x + w - x0 > max_w) return false;
        draw_cp(x, yb, cp); x += w;
    }
    return true;
}

int text_draw_wrap(int x0, int y_top, const char* utf8,
                   int max_w, int line_h, int max_lines) {
    int line = 0, x = x0, top = y_top;
    uint16_t cp; const char* s = utf8;
    for (;;) {
        // 明示改行: songlistに書いた "\n" (バックスラッシュ+n) または 実LF
        if (s[0] == '\\' && s[1] == 'n') {
            s += 2;
            if (++line >= max_lines) break;
            x = x0; top += line_h; continue;
        }
        const char* ns = u8_next(s, &cp);
        if (!cp) break;
        if (cp == '\n') {
            s = ns;
            if (++line >= max_lines) break;
            x = x0; top += line_h; continue;
        }
        int w = cp_width(cp);
        if (x + w - x0 > max_w) {                 // 幅で折り返し
            if (++line >= max_lines) break;
            x = x0; top += line_h;
        }
        draw_cp(x, top + s_ascent, cp); x += w;
        s = ns;
    }
    return line + 1;
}