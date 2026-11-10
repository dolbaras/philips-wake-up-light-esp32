// Кодек кадра протокола Philips Wake-up Light.
// Формат (подтверждён живым захватом): [len][cmd][payload...], len = cmd+payload, без CRC.
// Чистый C++17, без зависимостей — компилируется и на ESP32, и на хосте (тесты).
#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <optional>

namespace wul {

struct Frame {
  uint8_t cmd = 0;
  std::vector<uint8_t> payload;   // всё после cmd

  // для команд работы со свойствами (10/11/12/13/14) payload[0] = id свойства
  bool hasProperty() const {
    return (cmd == 0x10 || cmd == 0x11 || cmd == 0x12 ||
            cmd == 0x13 || cmd == 0x14) && !payload.empty();
  }
  uint8_t property() const { return payload.empty() ? 0 : payload[0]; }
  std::vector<uint8_t> value() const {
    return payload.size() > 1 ? std::vector<uint8_t>(payload.begin() + 1, payload.end())
                              : std::vector<uint8_t>();
  }
};

// Сериализация кадра в байты провода: [len][cmd][payload]
std::vector<uint8_t> encode(const Frame& f);

// Разбор одного кадра из байтов. Возвращает кадр и число поглощённых байт,
// либо nullopt если данных не хватает/кадр битый.
struct ParseResult { Frame frame; size_t consumed; };
std::optional<ParseResult> decode(const uint8_t* data, size_t len);

// Инкрементальный приёмник потока UART: скармливай байты, забирай готовые кадры.
class Reader {
 public:
  void push(uint8_t b) { buf_.push_back(b); }
  void push(const uint8_t* d, size_t n) { buf_.insert(buf_.end(), d, d + n); }
  std::optional<Frame> next();   // вынуть следующий полный кадр, если есть
 private:
  std::vector<uint8_t> buf_;
};

// --- Хелперы-строители команд (app→dev) ---
Frame getProperty(uint8_t prop);                       // 12
Frame getParam(uint8_t prop);                          // 10
Frame setProperty(uint8_t prop, std::vector<uint8_t> value);  // 14
Frame getCapability();                                 // 06
Frame getName();                                       // 08
Frame appInit(uint8_t a = 0x01, uint8_t b = 0x02);     // 00

// --- Кодировки значений ---
std::vector<uint8_t> encBe16(uint16_t v);
uint16_t             decBe16(const std::vector<uint8_t>& v);
std::vector<uint8_t> encBcdHHMM(int hour, int minute); // будильник 0C
bool                 decBcdHHMM(const std::vector<uint8_t>& v, int& hour, int& minute);

// Удобные рецепты (подтверждены захватом)
Frame setAlarm(int hour, int minute);  // 04 14 0C HH MM (BCD)
Frame setAlarmOff();                   // 04 14 0C AA AA
Frame lightOn();                       // 03 14 4F EE  (это старт ЗАКАТА; нужен предварительно setDuskProfile)
Frame lightOff();                      // 03 14 4F 00  (стоп кривой)
Frame audioPlay();                     // 03 14 58 02
Frame audioStop();                     // 03 14 58 00
Frame setDisplayBrightness(uint8_t lvl); // 04 14 42 00 LL (0..7)
Frame setVolume(uint8_t v);              // 04 14 00 00 VV (0..20)

// Кривые света — как шлёт приложение. Минуты и яркость — обычные числа, не BCD.
Frame setDawnProfile(uint8_t minutesToAlarm, uint8_t intensity); // 04 14 4E MM II  (яркость 1..20)
Frame setDuskProfile(uint8_t minutes, uint8_t startBrightness);  // 04 14 49 MM BB  (приложение шлёт BB=FF)
Frame dawnStart();                     // 03 14 4F FF  (после setDawnProfile; статус 57=FF)
Frame duskStart();                     // 03 14 4F EE  (после setDuskProfile; статус 57=EE)
Frame curveStop();                     // 03 14 4F 00
Frame alarmDismiss();                  // 03 14 4F 11  (Stop alarm)
Frame snooze();                        // 03 14 56 01
Frame setAlarmVolume(uint8_t v);       // 04 14 14 00 VV  (ALMVOL, шлётся в момент звонка)

// Установка времени часам. Свойство 0B DATETIME.
// ГИПОТЕЗА формата (подтвердить захватом): SetDevPropertyValue 0B, значение = BCD YY MM DD HH MM SS.
// year — 4-значный (2026 → BCD 26). weekday пока не шлём (уточнить).
uint8_t bcd(int v);                               // 0..99 -> BCD
Frame setDateTime(int year, int month, int day, int hour, int minute, int second);

std::string hex(const std::vector<uint8_t>& b);
std::vector<uint8_t> fromHex(const std::string& s);

}  // namespace wul
