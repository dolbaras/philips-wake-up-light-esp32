// Коды физических кнопок часов (RC-события: cmd 0x17, prop 0x16, value=[00][group][00][code]).
// Карта снята вживую 2026-10-16 (журнал /api/events, тест пользователя): одно событие на нажатие.
#pragma once
#include <cstdint>
#include "wul_frame.h"
namespace wul {
enum RcButton : uint8_t {
  RC_SNOOZE        = 0x04,  // Снуз
  RC_LIGHT_TOGGLE  = 0x0A,  // Свет: code = новое состояние (1 вкл, 0 выкл) — лампу часы переключают сами
  RC_BRIGHT_UP     = 0x08,  // + в режиме света: code = новая яркость 1..20 (часы меняют сами)
  RC_BRIGHT_DOWN   = 0x09,  // − в режиме света
  RC_SOUND_TOGGLE  = 0x07,  // Звук: часы только сообщают (звук — забота «устройства»)
  RC_VOL_UP        = 0x05,  // + в режиме звука: code = 0, громкость ведёт «устройство»
  RC_VOL_DOWN      = 0x06,  // − в режиме звука
  RC_TEST          = 0x0B,  // удержание Свет 5 с: вход/выход режима «test» часов
};
// Разбор RC-кадра: вернуть true и заполнить group/code, если это кнопочное событие.
inline bool parseRc(const Frame& f, uint8_t& group, uint8_t& code) {
  if (f.cmd != 0x17 || f.property() != 0x16) return false;
  auto v = f.value();            // [00][group][00][code]
  if (v.size() < 4) return false;
  group = v[1]; code = v[3];
  return true;
}
}
