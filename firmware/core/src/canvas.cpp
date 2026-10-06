#include "hg/canvas.hpp"

#include <algorithm>

#include "hg/font.hpp"

namespace hg {

namespace {

// Column cost of one codepoint: a CJK glyph spans two ASCII cells; anything
// the CJK font does not have renders as a single '?' column.
int codepoint_cols(uint32_t cp) {
  return cp >= 0x80 && font::cjk_glyph(cp, nullptr) ? font::kCjkColumnWidth : 1;
}

// Punctuation that may not start a line; a break before these instead goes
// one codepoint earlier so the mark stays with what it belongs to.
bool closing_punct(uint32_t cp) {
  return cp == 0xFF0C || cp == 0x3002 || cp == 0x3001 || cp == 0xFF01 || cp == 0xFF1F || cp == 0xFF1A ||
         cp == 0xFF1B || cp == 0xFF09 || cp == 0x3011 || cp == 0x300D || cp == 0x300F || cp == 0x300B ||
         cp == 0x2026 || cp == 0x2019 || cp == 0x201D;
}

}  // namespace

void Canvas::set_clip_rows(int y0, int y1) {
  clip_y0_ = std::max(0, y0);
  clip_y1_ = std::min(h_, y1);
}

void Canvas::pixel(int x, int y, uint16_t color) {
  if (x < 0 || x >= w_ || y < clip_y0_ || y >= clip_y1_ || y >= h_) return;
  px_[y * stride_ + x] = encode(color);
}

void Canvas::fill_rect(int x, int y, int w, int h, uint16_t color) {
  int x0 = std::max(0, x), x1 = std::min(w_, x + w);
  int y0 = std::max(clip_y0_, y), y1 = std::min(std::min(h_, clip_y1_), y + h);
  if (x0 >= x1 || y0 >= y1) return;
  uint16_t c = encode(color);
  for (int yy = y0; yy < y1; ++yy) {
    std::fill(px_ + yy * stride_ + x0, px_ + yy * stride_ + x1, c);
  }
}

void Canvas::rect(int x, int y, int w, int h, uint16_t color) {
  fill_rect(x, y, w, 1, color);
  fill_rect(x, y + h - 1, w, 1, color);
  fill_rect(x, y, 1, h, color);
  fill_rect(x + w - 1, y, 1, h, color);
}

void Canvas::fill_round_rect(int x, int y, int w, int h, int r, uint16_t color) {
  r = std::min(r, std::min(w, h) / 2);
  if (r <= 0) {
    fill_rect(x, y, w, h, color);
    return;
  }
  fill_rect(x, y + r, w, h - 2 * r, color);
  for (int i = 0; i < r; ++i) {
    // Inset of row i from the top/bottom edge for a quarter circle of radius r.
    int dy = r - i;
    int inset = 0;
    while (inset < r && (r - inset) * (r - inset) + dy * dy > r * r) ++inset;
    fill_rect(x + inset, y + i, w - 2 * inset, 1, color);
    fill_rect(x + inset, y + h - 1 - i, w - 2 * inset, 1, color);
  }
}

void Canvas::fill_circle(int cx, int cy, int r, uint16_t color) {
  for (int dy = -r; dy <= r; ++dy) {
    int dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= r * r) ++dx;
    fill_rect(cx - dx, cy + dy, 2 * dx + 1, 1, color);
  }
}

void Canvas::ring(int cx, int cy, int r, int thickness, uint16_t color) {
  int inner = std::max(0, r - thickness);
  for (int dy = -r; dy <= r; ++dy) {
    for (int dx = -r; dx <= r; ++dx) {
      int d2 = dx * dx + dy * dy;
      if (d2 <= r * r && d2 > inner * inner) pixel(cx + dx, cy + dy, color);
    }
  }
}

void Canvas::arc(int cx, int cy, int r, int thickness, int dir, uint16_t color) {
  int inner = std::max(0, r - thickness);
  for (int dy = -r; dy <= r; ++dy) {
    int ady = dy < 0 ? -dy : dy;
    for (int dx = -r; dx <= r; ++dx) {
      int d2 = dx * dx + dy * dy;
      if (d2 > r * r || d2 <= inner * inner) continue;
      if (dir * dx * 10 < ady * 7) continue;  // outside the +-55 degree cone
      pixel(cx + dx, cy + dy, color);
    }
  }
}

void Canvas::bitmap1(int x, int y, int w, int h, const uint8_t* bits, uint16_t color, int scale) {
  if (scale < 1) scale = 1;
  const int stride = (w + 7) / 8;
  const uint16_t c = encode(color);
  for (int gy = 0; gy < h; ++gy) {
    const uint8_t* row = bits + gy * stride;
    for (int gx = 0; gx < w; ++gx) {
      if (!((row[gx >> 3] >> (7 - (gx & 7))) & 1)) continue;
      for (int dy = 0; dy < scale; ++dy) {
        const int py = y + gy * scale + dy;
        if (py < clip_y0_ || py >= std::min(h_, clip_y1_)) continue;
        for (int dx = 0; dx < scale; ++dx) {
          const int px = x + gx * scale + dx;
          if (px < 0 || px >= w_) continue;
          px_[py * stride_ + px] = c;
        }
      }
    }
  }
}

int Canvas::text(int x, int y, std::string_view s, int scale, uint16_t color) {
  if (scale < 1) scale = 1;
  const int cjk_advance = font::kCjkColumnWidth * font::kCellWidth * scale;
  const int cjk_scale = std::max(1, cjk_advance / font::kCjkWidth);
  for (size_t i = 0; i < s.size();) {
    const uint32_t cp = font::utf8_next(s, i);
    const uint8_t* bm = nullptr;
    if (cp >= 0x80 && font::cjk_glyph(cp, &bm)) {
      // Render the 16x16 glyph scaled to the 2-cell advance (2x at the usual
      // scales), centered in the cell and optically aligned with the line.
      const int cs = std::max(1, cjk_advance / font::kCjkWidth);
      const int xoff = std::max(0, (cjk_advance - font::kCjkWidth * cs) / 2);
      const int yoff = std::max(0, (font::kCellHeight * scale - font::kCjkHeight * cs) / 2);
      bitmap1(x + xoff, y + yoff, font::kCjkWidth, font::kCjkHeight, bm, color, cs);
      x += cjk_advance;
      continue;
    }
    const char* g = font::glyph(cp < 0x80 ? static_cast<char>(cp) : '?');
    for (int row = 0; row < font::kGlyphHeight; ++row) {
      for (int col = 0; col < font::kGlyphWidth; ++col) {
        if (g[row * font::kGlyphWidth + col] == '#') {
          fill_rect(x + col * scale, y + row * scale, scale, scale, color);
        }
      }
    }
    x += font::kCellWidth * scale;
  }
  return x;
}

int Canvas::text_width(std::string_view s, int scale) {
  int cols = 0;
  for (size_t i = 0; i < s.size();) {
    const uint32_t cp = font::utf8_next(s, i);
    cols += codepoint_cols(cp);
  }
  return cols > 0 ? cols * font::kCellWidth * scale - scale : 0;
}

int Canvas::line_height(int scale) { return font::kCellHeight * scale; }

void Canvas::blit(int x, int y, int w, int h, const uint16_t* src) {
  for (int row = 0; row < h; ++row) {
    int yy = y + row;
    if (yy < clip_y0_ || yy >= clip_y1_ || yy >= h_ || yy < 0) continue;
    for (int col = 0; col < w; ++col) {
      int xx = x + col;
      if (xx < 0 || xx >= w_) continue;
      px_[yy * stride_ + xx] = encode(src[row * w + col]);
    }
  }
}

std::vector<std::string> wrap_text(std::string_view text, int max_chars) {
  // Column budget: ASCII costs 1 column, a CJK glyph 2. Lines break at
  // spaces, between CJK characters (never inside a UTF-8 sequence), or —
  // when a single word exceeds the line — mid-word, as before.
  std::vector<std::string> lines;
  if (max_chars < 1) max_chars = 1;
  size_t i = 0;
  while (i <= text.size()) {
    const size_t nl = text.find('\n', i);
    std::string_view para = text.substr(i, nl == std::string_view::npos ? std::string_view::npos : nl - i);

    size_t line_start = 0, p = 0, last_space = std::string_view::npos, last_cp = std::string_view::npos;
    int cols = 0;
    auto flush = [&](size_t end) {
      std::string out(para.substr(line_start, end - line_start));
      while (!out.empty() && out.back() == ' ') out.pop_back();
      lines.push_back(std::move(out));
    };
    auto recalc = [&] {
      cols = 0;
      for (size_t k = line_start; k < p;) cols += codepoint_cols(font::utf8_next(para, k));
    };
    while (p < para.size()) {
      const size_t cp_start = p;
      const uint32_t cp = font::utf8_next(para, p);
      const int w = codepoint_cols(cp);
      if (cols + w > max_chars && cols > 0) {
        if (last_space != std::string_view::npos && last_space > line_start) {
          flush(last_space);
          line_start = last_space + 1;  // the space itself does not survive the break
        } else if (closing_punct(cp) && last_cp != std::string_view::npos && last_cp > line_start) {
          flush(last_cp);  // kinsoku: the mark stays on the line it closes
          line_start = last_cp;
        } else {
          flush(cp_start);
          line_start = cp_start;
        }
        last_space = std::string_view::npos;
        recalc();
      }
      if (cp == ' ') {
        if (cols > 0) last_space = cp_start;  // leading spaces never start a line
        cols += 1;
      } else {
        last_cp = cp_start;
        cols += w;
      }
    }
    flush(para.size());
    if (nl == std::string_view::npos) break;
    i = nl + 1;
  }
  while (!lines.empty() && lines.back().empty()) lines.pop_back();
  return lines;
}

}  // namespace hg
