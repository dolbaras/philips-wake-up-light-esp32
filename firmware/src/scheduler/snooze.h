// Снуз на стороне ESP32 (чистая логика, тестируется на хосте).
// Будильник звенит -> нажата кнопка снуза -> стоп света/звука -> через N минут повтор.
#pragma once
#include <cstdint>

namespace sched {

enum class RingState { Idle, Ringing, Snoozed };

class SnoozeController {
 public:
  void onAlarmFired(uint32_t epochMin) { state_ = RingState::Ringing; firedAt_ = epochMin; }
  // Вернёт true, если снуз принят (будильник звенел). Выставит повтор через snoozeMinutes.
  bool onSnoozePressed(uint32_t epochMin, int snoozeMinutes) {
    if (state_ != RingState::Ringing) return false;
    state_ = RingState::Snoozed;
    refireAt_ = epochMin + (snoozeMinutes > 0 ? snoozeMinutes : 1);
    return true;
  }
  void onDismiss() { state_ = RingState::Idle; }
  // Вызывать раз в минуту: вернёт true в момент, когда снуз истёк -> звеним снова.
  bool tick(uint32_t epochMin) {
    if (state_ == RingState::Snoozed && epochMin >= refireAt_) { state_ = RingState::Ringing; return true; }
    return false;
  }
  RingState state() const { return state_; }

 private:
  RingState state_ = RingState::Idle;
  uint32_t firedAt_ = 0, refireAt_ = 0;
};

}  // namespace sched
