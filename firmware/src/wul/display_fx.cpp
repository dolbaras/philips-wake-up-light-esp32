#include "display_fx.h"

namespace wul {

static int glyph(char c) {                        // знак → цифра, -1 = пусто, -2 = нельзя
  if (c >= '0' && c <= '9') return c - '0';
  switch (c) {
    case 'O': case 'o': case 'D': return 0;
    case 'I': case 'i': case 'l': return 1;
    case 'Z': case 'z': return 2;
    case 'S': case 's': return 5;
    case 'b': case 'G': return 6;
    case 'B': return 8;
    case 'g': case 'q': return 9;
    case ' ': case '_': return -1;
    default: return -2;
  }
}

bool textToPairs(const std::string& text, DisplayPair& out, std::string* error) {
  auto fail = [&](const char* why) { if (error) *error = why; return false; };
  if (text.size() != 4) return fail("нужно ровно 4 знака");
  int g[4];
  for (int i = 0; i < 4; ++i) {
    g[i] = glyph(text[i]);
    if (g[i] == -2) return fail("недопустимый знак: можно 0-9, O I Z S b G B g и пробел");
  }
  uint8_t* dst[2] = {&out.h, &out.m};
  for (int p = 0; p < 2; ++p) {
    int a = g[2 * p], b = g[2 * p + 1];
    if (a == -1 && b == -1) { *dst[p] = kBlankPair; continue; }
    if (a == -1 || b == -1) return fail("пробел гасит пару целиком: обе цифры половинки должны быть пробелами");
    *dst[p] = static_cast<uint8_t>(a * 10 + b);
  }
  return true;
}

uint8_t eqGlyph(uint8_t level) {
  static const uint8_t k[] = {1, 7, 4, 3, 9, 8};  // 2,3,4,5,6,7 горящих сегментов
  return k[level > 5 ? 5 : level];
}

DisplayPair eqPairs(const uint8_t levels[4]) {
  DisplayPair p;
  p.h = static_cast<uint8_t>(eqGlyph(levels[0]) * 10 + eqGlyph(levels[1]));
  p.m = static_cast<uint8_t>(eqGlyph(levels[2]) * 10 + eqGlyph(levels[3]));
  return p;
}

}  // namespace wul
