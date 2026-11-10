// «Мозг» вместо приложения iPad: ведёт будильники, рассвет, звонок, снуз, закат и журнал сна,
// отдавая часам те же команды, что и оригинальное приложение.
// Чистый C++17, без Arduino: время подаётся снаружи (tick), кадры уходят через колбэк send.
#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include "model.h"
#include "../wul/wul_frame.h"

namespace core {

// Локальное время в секундах «как будто UTC» (timegm от локального времени):
// день = t/86400, день недели = (день+3)%7 (0=Пн), время суток = t%86400.
using LocalTime = uint32_t;
inline int weekdayOf(LocalTime t) { return static_cast<int>((t / 86400 + 3) % 7); }
inline int minuteOfDay(LocalTime t) { return static_cast<int>(t % 86400 / 60); }

// Ближайшее срабатывание будильника строго после момента after (локальное время).
LocalTime nextOccurrence(const Alarm& a, LocalTime after);

enum class Mode : uint8_t { Idle, Sunrise, Ringing, Snoozed, Dusk };
const char* modeName(Mode m);

class Engine {
 public:
  // --- данные (сохраняются снаружи через persist*) ---
  std::vector<Alarm> alarms;
  Dusk dusk;
  Settings settings;
  std::vector<SleepEntry> log;

  // --- исходящее ---
  std::function<void(const wul::Frame&)> send;              // кадр часам (через очередь с паузами)
  std::function<void(uint8_t soundIdx, uint8_t volume)> playSound;  // звук (ЦАП); может быть пустым
  std::function<void()> stopSound;
  std::function<void()> persistAlarms, persistSettings, persistLog;
  std::function<void()> onChanged;                          // состояние изменилось (для веб/приложения)

  // --- управление (из веб-панели / приложения) ---
  bool    upsertAlarm(Alarm a, LocalTime now);              // id=0 → новый
  bool    removeAlarm(uint8_t id, LocalTime now);
  void    setAlarmEnabled(uint8_t id, bool on, LocalTime now);
  void    stopAlarm(LocalTime now);                         // Stop alarm (и во время рассвета)
  void    snoozeAlarm(LocalTime now);
  void    startDusk(LocalTime now);
  void    stopDusk(LocalTime now, bool lightOff = false);    // lightOff — ещё и погасить лампу (иначе остаётся гореть)
  void    setVolume(uint8_t v);                             // общая громкость (сразу в часы)
  void    setDisplayBrightness(uint8_t v);                  // табло 0..7 (сразу в часы)
  void    setDuskSettings(const Dusk& d);
  void    setSettings(const Settings& s);
  bool    answerMorning(int quality, int ease, const char* note);
  void    previewSound(uint8_t idx, uint8_t volume);        // «Play sound» в настройках
  void    stopPreview();

  // --- входящее от часов ---
  void onConnected(LocalTime now);                          // после хендшейка
  void onClockValue(uint8_t prop, const std::vector<uint8_t>& v);
  // Кнопка часов (cmd17 prop 16): code — сырой код, level — значение из кадра, ms — монотонные мс.
  void onButton(uint8_t code, uint8_t level, LocalTime now, uint32_t ms);
  void setSound(bool on);                                   // «Звук» вкл/выкл (как кнопка)
  // Ручной свет (найдено вживую 2026-10-16): вкл = закат 60 мин с уровня L + сразу 4C=L (кривая
  // останавливается, лампа держит уровень); уровень = 4C; выкл = 4F 00 + 4C 00 00 (как кнопка).
  void setLight(bool on, uint8_t level);
  void lightOff();                                          // погасить в любом режиме: полностью за ~3–5 с
  std::function<void()> factoryReset;                       // сочетание кнопок «сброс» (после мигания табло)

  void tick(LocalTime now);                                 // ~раз в секунду

  // --- состояние для интерфейса ---
  Mode     mode = Mode::Idle;
  uint8_t  activeAlarmId = 0;            // будильник рассвета/звонка
  LocalTime activeOcc = 0;               // его срабатывание
  LocalTime snoozeUntil = 0, duskEnd = 0, ringStart = 0;
  LocalTime questionUntil = 0;           // до когда показывать Morning question
  uint8_t  volume = 10;                  // текущая общая громкость
  uint8_t  clockLightMode = 0, clockLightLevel = 0, curveStatus = 0;  // зеркало 4C / 57
  uint8_t  lastButton = 0, lastButtonLevel = 0;
  LocalTime lastButtonAt = 0;
  struct ButtonEvent { uint32_t ms; uint8_t code; uint8_t level; };
  std::vector<ButtonEvent> buttons;      // последние kButtonLog нажатий
  bool     lightOn = false;              // лампа (кнопка Свет / 0x4C)
  uint8_t  lightLevel = 1;               // 1..20
  bool     soundOn = false;              // звук по кнопке Звук
  bool     padSound = false;             // +/− сейчас про звук (после «Звук»), иначе про свет
  bool     testMode = false;             // режим «test» часов (удержание Свет 5 с)
  bool     connected = false;

  // ближайший будильник (для табло часов и интерфейса)
  struct Next { bool any = false; uint8_t id = 0; LocalTime when = 0; };
  Next next(LocalTime now) const;

  static constexpr int kRingTimeoutSec = 30 * 60;           // звонок без реакции гаснет сам
  static constexpr size_t kButtonLog = 12;
  // Снуз ×10 за 6 с → сброс. Было ×5 за 4 с — набиралось случайно (дважды сброс к заводским, 2026-11-03)
  static constexpr int kComboPresses = 10;
  static constexpr uint32_t kComboWindowMs = 6000;
  static const char* buttonName(uint8_t code);              // "light", "up", … или nullptr

 private:
  std::vector<LocalTime> done_;          // по индексу будильника: последнее обработанное срабатывание
  LocalTime lastPushAt_ = 0, lastPollAt_ = 0, lastMusicAt_ = 0;
  std::vector<uint32_t> snoozePresses_;  // для сочетания «сброс»
  // Своя рампа яркости через 4C (кривая часов при горящей лампе сначала роняет её до минимума)
  uint8_t snoozes_ = 0;                  // снузы текущего будильника, если записи сна ещё нет
  LocalTime autoOffAt_ = 0;              // погасить свет после будильника (settings.lightOffAfterAlarm)
  struct Ramp { bool on = false; uint8_t from = 1, to = 1, last = 0; LocalTime start = 0, end = 0; } ramp_;
  void startRamp(uint8_t to, LocalTime now, LocalTime end);
  void tickRamp(LocalTime now);
  int pushedH_ = -2, pushedM_ = -2;
  Alarm* find(uint8_t id);
  LocalTime doneOf(uint8_t id) const;
  void markDone(uint8_t id, LocalTime occ);
  void pushNextAlarm(LocalTime now, bool force);
  void startSunrise(const Alarm& a, LocalTime occ, LocalTime now);
  void ring(const Alarm& a, LocalTime now);
  void finishAlarm(LocalTime now, bool dismissClock);
  void changed() { if (onChanged) onChanged(); }
  void out(const wul::Frame& f) { if (send) send(f); }
};

}  // namespace core
