// Модель данных — как в оригинальном приложении Philips.
// Чистый C++17: компилируется и на ESP32, и на хосте (тесты).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace core {

constexpr int kMaxAlarms  = 10;   // лимит оригинала
constexpr int kLabelLen   = 24;
constexpr int kNoteLen    = 64;
constexpr int kMaxLog     = 60;   // записей журнала сна
constexpr int kSoundCount = 8;

// Звуки приложения: имя в интерфейсе → файл в бандле.
extern const char* const kSoundNames[kSoundCount];
extern const char* const kSoundFiles[kSoundCount];

// Дни недели: бит0=Пн … бит6=Вс. 0 = «Once» (один раз, потом выключается).
struct Alarm {
  uint8_t id = 0;
  bool    enabled = true;
  uint8_t hour = 7, minute = 0;
  uint8_t days = 0;
  char    label[kLabelLen] = "Alarm";
  bool    light = true;
  uint8_t sunriseMin = 30;        // 20..40, шаг 5
  uint8_t intensity = 20;         // 1..20
  bool    sound = true;
  uint8_t soundIdx = 0;           // 0..7 (Birds…Zen Garden)
  uint8_t volume = 10;            // 0..20
  uint8_t snoozeMin = 9;          // 1..20
};

struct Dusk {                     // вкладка Fall asleep
  uint8_t minutes = 15;           // 5..60, шаг 5
  bool    music = true;
  uint8_t musicIdx = 3;           // Ocean
  uint8_t volume = 8;
};

constexpr int kTzLen = 48, kRegionLen = 40;

struct Settings {
  uint8_t displayBrightness = 4;  // табло часов 0..7
  bool    trackBedtime = true;
  bool    morningQuestion = true;
  bool    resetCombo = true;      // сброс сочетанием: Снуз ×10 за 6 с
  uint8_t lightOffAfterAlarm = 0; // погасить свет через N мин после будильника; 0 — оставить гореть
  uint8_t soundIdx = 3;           // мелодия для кнопки Звук (Ocean)
  char    lang[3] = "ru";         // язык интерфейса: ru | en
  char    tz[kTzLen] = "EET-2EEST,M3.5.0/3,M10.5.0/4";   // POSIX TZ
  char    region[kRegionLen] = "Europe/Kyiv";            // подпись региона (IANA)
};

struct SleepEntry {               // Stats: засыпание (старт заката) → подъём (стоп будильника)
  uint32_t start = 0, end = 0;    // локальное unix-время, 0 = нет
  int8_t   quality = -1, ease = -1; // 0..4 (ответы Morning question), -1 = нет ответа
  uint8_t  snoozes = 0;
  char     note[kNoteLen] = "";
};

void setLabel(Alarm& a, const std::string& s);
void setText(char* dst, size_t cap, const std::string& s);   // безопасно скопировать строку в char[cap]
void setNote(SleepEntry& e, const std::string& s);
bool validate(Alarm& a);          // привести поля к допустимым диапазонам

// Двоичная сериализация для NVS (с версией формата).
std::vector<uint8_t> packAlarms(const std::vector<Alarm>& v);
std::vector<Alarm>   unpackAlarms(const uint8_t* d, size_t n);
std::vector<uint8_t> packLog(const std::vector<SleepEntry>& v);
std::vector<SleepEntry> unpackLog(const uint8_t* d, size_t n);
std::vector<uint8_t> packSettings(const Settings& s, const Dusk& d);
bool unpackSettings(const uint8_t* b, size_t n, Settings& s, Dusk& d);   // понимает форматы v1, v2, v3

}  // namespace core
