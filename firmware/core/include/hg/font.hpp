#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace hg::font {

constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
// Advance and line pitch at scale 1 (glyph plus spacing). The pitch also
// bounds the CJK glyphs, which are drawn at twice the ASCII glyph's scale
// (see Canvas::text): 16 * 2 <= 11 * 3.
constexpr int kCellWidth = 6;
constexpr int kCellHeight = 11;

// A CJK glyph spans two ASCII columns and advances by that many cells.
constexpr int kCjkColumnWidth = 2;

// Row-major glyph pixels: kGlyphWidth * kGlyphHeight chars, '#' = on.
// Non-printable / non-ASCII bytes map to '?'.
const char* glyph(char c);

// CJK bitmaps: 16x16, one bit per pixel, row-major MSB first, generated from
// Noto Sans CJK SC by firmware/core/gen_cjk_font.py (GB2312 level-1 hanzi
// plus common CJK punctuation). Returns false for codepoints outside the set.
constexpr int kCjkWidth = 16;
constexpr int kCjkHeight = 16;
constexpr int kCjkBytes = kCjkWidth * kCjkHeight / 8;
bool cjk_glyph(uint32_t codepoint, const uint8_t** out);

// Decodes one UTF-8 sequence at s[i] and advances i past it. Invalid or
// truncated bytes yield U+FFFD and advance a single byte.
uint32_t utf8_next(std::string_view s, size_t& i);

}  // namespace hg::font
