// Сеть модуля: Wi-Fi-клиент / точка настройки (мастер, captive portal) / Bluetooth LE.
// Настройки сети — в NVS «net». Правила — API.md («Первичная настройка»).
#pragma once
#include <string>
#include <vector>
#include "api.h"

namespace netmgr {

constexpr const char* kApPass = "wakeuplight";

// seedSsid/seedPass — сеть из config.h: записывается один раз, если в NVS пусто и сброса не было.
void begin(const char* seedSsid, const char* seedPass);
// Режим колонки: только прочитать настройки (Wi-Fi, точку, mDNS, OTA не поднимать — памяти нет). Иначе состояние
// говорило бы «не настроен» (приложение показывало мастер настройки вместо экрана колонки, 2026-11-03).
void beginOffline();
void loop();                         // в режиме колонки — только отложенная перезагрузка (сброс, смена сети)

api::NetInfo info();
std::vector<api::WifiNet> scan();
void save(const std::string& ssid, const std::string* pass, const std::string& link, bool setupDone);
void factoryReset(const char* src = "?");                 // стереть все настройки модуля, перезагрузиться в мастер
void restartIn(uint32_t ms, const char* why = nullptr);
bool wifiUp();                       // есть подключение к домашней сети (для NTP)
std::string deviceName();            // WakeUpLight-XXXX (точка настройки и BLE)
// Откат прошивки: новая (после OTA) загружается «на проверке». Подтверждаем, когда она 45 с проработала и подняла
// сеть (Wi-Fi, точку, BLE или режим колонки), перед приёмом следующей OTA и перед намеренной перезагрузкой.
// Упала или перезагрузилась раньше — загрузчик сам вернёт прошлую прошивку.
void markGood();
bool otaPending();                   // текущая прошивка ещё не подтверждена

}  // namespace netmgr
