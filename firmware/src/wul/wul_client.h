// Клиент протокола: хендшейк + get/set поверх произвольного байтового транспорта (UART к MCU часов).
#pragma once
#include "wul_frame.h"
#include "wul_properties.h"   // генерируется tools/gen-properties.py (enum Cmd, PROP_*)
#include <functional>

namespace wul {

// Транспорт абстрагирован, чтобы ядро тестировалось на хосте.
struct Transport {
  std::function<void(const uint8_t*, size_t)> write;  // отправить байты в UART
  std::function<int()> readByte;                       // -1 если нет данных
};

class Client {
 public:
  explicit Client(Transport t) : t_(std::move(t)) {}

  void send(const Frame& f) { auto b = encode(f); t_.write(b.data(), b.size()); }
  void poll();                                   // вызывать в loop(): читает UART, собирает кадры
  std::function<void(const Frame&)> onFrame;     // колбэк на каждый принятый кадр от часов

  // Высокоуровневое (ответ прилетит в onFrame; матчинг по prop делает слой выше)
  bool handshake(uint32_t timeoutMs);            // AppInit → ждём DevInit; затем читаем capability/имя
  void getValue(uint8_t prop) { send(getProperty(prop)); }
  void setValue(uint8_t prop, std::vector<uint8_t> v) { send(setProperty(prop, std::move(v))); }

  bool handshakeDone() const { return handshakeDone_; }
  const std::string& deviceName() const { return name_; }
  const std::string& fwVersion() const { return fw_; }

 private:
  Transport t_;
  Reader rd_;
  bool handshakeDone_ = false;
  std::string name_, fw_;
  uint32_t nowMs();                              // платформенное время (Arduino millis / тест)
};

}  // namespace wul
