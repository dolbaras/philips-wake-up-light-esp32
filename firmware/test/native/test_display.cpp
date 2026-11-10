// Табло: текст → пары «часы/минуты», эквалайзер → цифры (правила сняты вживую 2026-10-17).
#include "display_fx.h"
#include <cstdio>
#include <string>
using namespace wul;
static int fails = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d  %s\n",__FILE__,__LINE__,#c); ++fails; } }while(0)

int main() {
  DisplayPair p;
  std::string err;
  CHECK(textToPairs("1234", p) && p.h == 12 && p.m == 34);
  CHECK(textToPairs("5OS ", p, &err) == false && !err.empty());   // пробел только парой
  CHECK(textToPairs("BOSS", p) && p.h == 80 && p.m == 55);
  CHECK(textToPairs("  12", p) && p.h == kBlankPair && p.m == 12);
  CHECK(textToPairs("Zg  ", p) && p.h == 29 && p.m == kBlankPair);
  CHECK(!textToPairs("HELO", p, &err));                          // букв H/E нет
  CHECK(!textToPairs("123", p));                                 // не 4 знака
  uint8_t lv[4] = {0, 5, 2, 3};
  DisplayPair e = eqPairs(lv);
  CHECK(e.h == 18 && e.m == 43);
  CHECK(eqGlyph(9) == 8);
  printf(fails ? "\n%d CHECK(s) FAILED\n" : "\nТАБЛО: текст и эквалайзер — ок\n", fails);
  return fails ? 1 : 0;
}
