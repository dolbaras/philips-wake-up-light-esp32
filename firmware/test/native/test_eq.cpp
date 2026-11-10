// Звучание динамика (core/eq.h): пресеты и пределы значений
#include "eq.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
  core::EqParams p;
  assert(p.on && p.hp == 150 && p.body == 3 && p.presence == 0 && p.treble == 0 && !std::strcmp(p.preset, "soft"));
  assert(core::eqPreset("warm", p) && p.on && p.hp == 120 && p.body == 5 && p.presence == -1 && p.treble == -2);
  assert(!std::strcmp(p.preset, "warm"));
  assert(core::eqPreset("off", p) && !p.on && !std::strcmp(p.preset, "off"));
  assert(!core::eqPreset("nope", p) && !std::strcmp(p.preset, "off"));       // неизвестный — ничего не меняет
  for (const auto& e : core::kEqPresets) {                                   // все пресеты — в пределах ручек
    core::EqParams q; assert(core::eqPreset(e.id, q));
    core::EqParams c = q; core::eqClamp(c);
    assert(c.hp == q.hp && c.body == q.body && c.presence == q.presence && c.treble == q.treble);
    assert(std::strlen(e.id) < sizeof q.preset);
  }
  p.hp = 10; p.body = 9; p.presence = -9; p.treble = 6; core::eqClamp(p);
  assert(p.hp == 50 && p.body == 6 && p.presence == -6 && p.treble == 6);
  p.hp = 999; core::eqClamp(p); assert(p.hp == 250);
  std::puts("ЭКВАЛАЙЗЕР: пресеты и пределы — ок");
}
