// API модуля, не зависящий от транспорта: HTTP (web.cpp) и Bluetooth LE (ble.cpp) вызывают
// один и тот же handle(). Контракт — API.md.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "../core/engine.h"
#include "../core/eq.h"

namespace api {

struct Request {
  std::string method;          // "GET" | "POST"
  std::string path;            // "/api/state" (без query)
  std::string rev;             // значение ?rev= (пусто — нет)
  std::string body;            // JSON для POST
};

struct Response {
  int code = 200;
  std::string body;            // JSON (пусто для 304)
};

struct WifiNet { std::string ssid; int rssi; bool secure; };

struct NetInfo {
  std::string link = "wifi";   // "wifi" | "ble"
  std::string ssid, ip, ble;
  int rssi = 0;
  bool ap = false;             // поднята точка настройки
  bool setup = false;          // первичная настройка не пройдена
};

struct DisplayInfo { std::string mode = "clock", text; int secondsLeft = 0; };
// Bluetooth-колонка: available — можно включить (связь Wi-Fi), bt/showEq/lamp — настройки
struct SpeakerInfo { bool available = false, bt = false, on = false, connected = false, playing = false, showEq = true, lamp = true;
                     std::string name, peer, title, artist; };

struct Context {
  core::Engine* engine = nullptr;
  std::function<core::LocalTime()> now;                      // 0 = времени ещё нет
  std::function<std::string()> deviceName, firmware, events;
  std::function<NetInfo()> netInfo;
  std::function<std::vector<WifiNet>()> scan;
  // сохранить сеть/связь и перезагрузиться; pass == nullptr — оставить прежний
  std::function<void(const std::string& ssid, const std::string* pass, const std::string& link, bool setupDone)> saveNetwork;
  std::function<void()> factoryReset;                         // стереть всё и перезагрузиться
  std::function<void(core::LocalTime)> setTime;               // время от телефона/браузера
  std::function<void()> settingsChanged;                      // применить tz и т.п.
  // Табло: надпись из 4 знаков (ошибка — непустая строка), эквалайзер, вернуть часы, сырые значения
  std::function<std::string(const std::string& text, int sec)> displayText;
  std::function<void(int fps, int sec)> displayEq;
  std::function<void()> displayClock;
  std::function<void(int h, int m)> displayRaw;
  std::function<struct DisplayInfo()> displayInfo;
  std::function<SpeakerInfo()> speakerInfo;
  std::function<void(int bt, int showEq, int lamp)> speakerSet;   // −1 — не менять
  std::function<core::EqParams()> eqGet;                          // звучание динамика (core/eq.h)
  std::function<void(const core::EqParams&)> eqSet;               // применить и сохранить
  std::function<bool()> boardLedGet;                              // синий светодиод на плате модуля (settings.boardLed)
  std::function<void(bool)> boardLedSet;
  std::function<std::string(uint32_t sinceMs)> bus;          // прослушка второй шины (диагностика)
  std::function<void(uint32_t baud)> busCmd;                  // 0 — измерить скорость заново
  std::function<void(uint32_t ms)> busCapture;                // записать сырые фронты на ms
  std::function<std::string()> busEdges;
  // контроллер лампы по I2C (диагностика): probe / read(reg,n) / write(reg,hex) / scan(from,to) → JSON
  // звук (ЦАП): тестовый тон; ch 0 — оба канала, 1 — левый, 2 — правый
  std::function<void(int hz, float vol, int sec, int ch, bool beep, int hz2)> tone;
  std::function<void()> toneStop;
  std::function<std::string()> toneStatus;
  std::function<void(int bck, int lrck, int din)> toneRepin;
  std::function<void(int bits)> toneBits;
  std::function<void(float volts)> toneDc;
  std::function<void(bool on)> toneBridge;
  std::function<std::string(bool test)> flash;
  std::function<std::string()> melodies;                      // каталог мелодий во флеш
  std::function<void()> melodiesFormat;
  std::function<uint32_t(const std::string& name)> melodyCrc;
  std::function<bool(const std::string& name, float vol, bool loop)> play;
  std::function<void(float drive)> drive;
  std::function<void(bool on)> keepAlive;
  std::function<void(bool on)> holdAudio;                     // держать аудиотракт часов (58 02 раз в 0,5 с), пока играет                // внешняя флеш: id, test — проверка записи
  std::function<std::string()> shortTest;
  std::function<std::string()> coredump;                     // сводка последнего падения (дамп во флеш)
  std::function<std::string()> linkStats;                     // счётчики линии с часами
  std::function<void()> relink;                               // пересоздать связь с часами
  std::function<std::string(uint32_t since)> tap;             // журнал прослушки iPad↔часы в пассивном режиме
  std::function<void(bool on)> passive;                       // отпустить линии часов (iPad вместо модуля) / вернуться
  std::function<void(bool on)> dockAuto;                      // уступать часы живому iPhone/iPad в доке сам
  std::function<std::string()> detectLines;                   // диагностика линий detect (уровни, подтяжки, спад)
  std::function<const char*()> dock;                          // "" — часы ведёт модуль, "device" — iPhone/iPad в доке, "passive" — отпущены вручную
  std::function<std::string()> lampProbe;
  std::function<std::string(uint16_t reg, uint8_t n)> lampRead;
  std::function<std::string(uint16_t reg, const std::vector<uint8_t>& data)> lampWrite;
  std::function<std::string(uint16_t from, uint16_t to)> lampScan;
};

void begin(const Context& c);
Response handle(const Request& r);
// Строковый транспорт (Bluetooth LE, Bluetooth SPP в режиме колонки): запрос {"id","m","p","b"} одной строкой →
// ответ {"id","code","body"} + '\n'. Вызывать из основного цикла.
std::string handleLine(const std::string& req);
void bump();                   // состояние изменилось (rev++)
uint32_t rev();

}  // namespace api
