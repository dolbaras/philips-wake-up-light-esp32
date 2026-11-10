#pragma once
#include <cstdint>
#include <cstring>

// Звучание динамика часов (стоит без короба — бас гасится, а ход диффузора на нём велик): срез баса и три полосы.
// Действует на всё, что играет модуль: музыку телефона в режиме колонки, мелодии будильника и заката, прослушивание.
// POST /api/eq {"preset":"warm"} | {"hp":150,"body":3,"presence":0,"treble":0} (любые поля → «свой»)
namespace core {

struct EqParams {
  bool on = true;
  int16_t hp = 150;                    // срез баса, Гц (4-й порядок); ≤ 50 — без среза
  int8_t body = 3;                     // «плотность»: колокол 280 Гц, дБ −6..+6
  int8_t presence = 0;                 // «чёткость»: колокол 3 кГц, дБ
  int8_t treble = 0;                   // «верха»: полка от 8 кГц, дБ
  char preset[8] = "soft";             // off | soft | warm | clear | loud | custom
};

struct EqPreset { const char* id; bool on; int16_t hp; int8_t body, presence, treble; };
inline constexpr EqPreset kEqPresets[] = {
  {"off",   false, 150, 3, 0, 0},      // без обработки
  {"soft",  true,  150, 3, 0, 0},      // мягкий (по умолчанию): бережёт динамик, звук почти как есть
  {"warm",  true,  120, 5, -1, -2},    // тёплый: больше «тела», мягче верха
  {"clear", true,  200, 1, 3, 1},      // чёткий: голос, подкасты
  {"loud",  true,  200, 4, 2, 0},      // громкий: громче без хрипа — бас срезан выше
};

inline void eqClamp(EqParams& p) {
  auto c8 = [](int v) { return static_cast<int8_t>(v < -6 ? -6 : v > 6 ? 6 : v); };
  p.hp = static_cast<int16_t>(p.hp < 50 ? 50 : p.hp > 250 ? 250 : p.hp);
  p.body = c8(p.body); p.presence = c8(p.presence); p.treble = c8(p.treble);
}

inline bool eqPreset(const char* id, EqParams& p) {
  for (const auto& e : kEqPresets)
    if (std::strcmp(e.id, id) == 0) {
      p.on = e.on; p.hp = e.hp; p.body = e.body; p.presence = e.presence; p.treble = e.treble;
      std::strncpy(p.preset, e.id, sizeof p.preset - 1); p.preset[sizeof p.preset - 1] = 0;
      return true;
    }
  return false;
}

}  // namespace core
