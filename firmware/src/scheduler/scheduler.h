// Автономный планировщик будильников/рассвета. Хранит расписание в NVS (переживает отключение).
// Это и есть "автономный модуль": ESP32 сам будит часы без телефона.
// Время: Wi-Fi + SNTP (RTC-модуль не нужен). ESP32 ставит время часам на старте
// (свойство DATETIME) и периодически ресинкает; часы сами тикают, пока под питанием.
#pragma once
#include <cstdint>
#include <vector>
#include <functional>

namespace sched {

constexpr int kMaxAlarms = 16;
constexpr int kSoundNameLen = 24;

struct Alarm {
  uint8_t  id = 0;
  bool     enabled = false;
  uint8_t  hour = 7, minute = 0;
  uint8_t  weekdays = 0;        // битовая маска: bit0=Пн..bit6=Вс; 0 = каждый день
  uint16_t sunriseMinutes = 30;// длительность рассвета (свет стартует за столько ДО времени)
  bool     light = true;
  bool     sound = true;
  uint8_t  volume = 10;        // 0..20
  char     soundName[kSoundNameLen] = "forest";
};

struct TimeNow {               // локальное время (из SNTP/localtime)
  int hour = 0, minute = 0;
  int weekday = 0;             // 0=Пн..6=Вс
  uint32_t epochMinute = 0;    // для антидребезга (срабатывание раз в минуту)
};

// --- Чистые функции (тестируются на хосте, без Arduino) ---
int  minuteOfDay(int h, int m);
int  subMinutes(int mod, int delta);                        // mod-delta с переносом через полночь
bool activeToday(const Alarm& a, int weekday);
bool shouldFireSound(const Alarm& a, const TimeNow& n);     // в момент времени будильника
bool shouldFireLight(const Alarm& a, const TimeNow& n);     // за sunriseMinutes до
uint16_t sunriseLevelAt(const Alarm& a, int secondsIntoRamp, uint16_t maxLevel);

std::vector<uint8_t> serialize(const std::vector<Alarm>& v);  // → NVS blob
std::vector<Alarm>   deserialize(const uint8_t* d, size_t n);

// Устройство хранит ТОЛЬКО время ближайшего срабатывания. Считаем его сами и шлём часам.
struct NextAlarm { bool any = false; int hour = 0, minute = 0; int minutesUntil = 0; };
NextAlarm computeNextAlarm(const std::vector<Alarm>& v, const TimeNow& now);

class Scheduler {
 public:
  void begin();                              // загрузить из NVS
  void tick(const TimeNow& now);             // вызывать ~раз в секунду
  std::vector<Alarm> list() const { return alarms_; }
  bool upsert(const Alarm& a);               // добавить/изменить + сохранить
  bool remove(uint8_t id);
  uint8_t nextFreeId() const;

  // колбэки действий — реализация дергает wul::Client (свет) и audio (звук)
  std::function<void(const Alarm&)> onFireLight;
  std::function<void(const Alarm&)> onFireSound;

 private:
  std::vector<Alarm> alarms_;
  uint32_t firedLight_[kMaxAlarms] = {0};    // epochMinute последнего срабатывания (антидребезг)
  uint32_t firedSound_[kMaxAlarms] = {0};
  int indexOf(uint8_t id) const;
  void save();
  void load();
};

}  // namespace sched
