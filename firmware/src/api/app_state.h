// Единая модель состояния: кэш значений часов + расписание. Контракт для веб-пульта,
// BLE и Android. JSON-сериализация — чистый C++ (тестируется на хосте).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../scheduler/scheduler.h"

namespace app {

struct State {
  std::string deviceName = "Wake Up Light";
  std::string fw;
  bool     connected = false;
  bool     lightOn = false;
  uint8_t  lightLevel = 0;       // 4C LIGHTLVL / статус кривой
  uint16_t brightness = 0;       // 42 DISPBRIGHT 1..7
  uint16_t volume = 0;           // 00 VOL 0..20
  uint16_t almVolume = 0;        // 14 ALMVOL 0..20
  uint8_t  lastButton = 0;       // последняя RC-кнопка (group)
  uint8_t  lastButtonLevel = 0;
  std::vector<sched::Alarm> alarms;
};

std::string toJson(const State& s);                 // полное состояние → JSON
std::string alarmToJson(const sched::Alarm& a);
// Разбор входящего JSON будильника (из веб-пульта/приложения) — минимальный парсер.
bool alarmFromJson(const std::string& json, sched::Alarm& out);

// утилиты
std::string jesc(const std::string& s);             // JSON-escape строки

}  // namespace app
