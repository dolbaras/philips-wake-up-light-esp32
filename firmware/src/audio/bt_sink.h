// Bluetooth-колонка (A2DP-приёмник). Телефон подключается к модулю как к обычной колонке; звук идёт в
// audio:: (тот же ЦАП, смещение и «мост»), а громкость телефона (AVRCP, 0..127) ведёт громкость часов.
// Только в режиме «колонка» (Wi-Fi выключен — обоим не хватает памяти); управление тогда — по SPP (net/spp.h).
#pragma once
#include <cstdint>
#include <string>

namespace btsink {

void begin(const char* name);   // поднять приёмник (повторный вызов после end() — снова включить)
void end();                     // отключить телефон и выключить приёмник
bool enabled();
bool connected();               // телефон подключён
bool streaming();               // телефон шлёт звук (A2DP «играет»)
std::string peer();             // имя телефона
bool takeVolume(int& vol127);   // телефон сменил громкость — забрать (из основного цикла)
void reportVolume(int vol127);  // громкость сменили на часах — сообщить телефону
void pause();                   // попросить телефон поставить на паузу (будильник)
std::string title();            // что играет — телефон присылает по AVRCP (может быть пусто)
std::string artist();
extern void (*memProbe)();      // диагностика: вызывается между запуском контроллера и Bluedroid

}  // namespace btsink
