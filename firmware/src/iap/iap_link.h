// Слой Apple iAP + External-Accessory поверх UART к MCU часов.
// Заворачивает/разворачивает Philips-кадры в iAP-пакеты (FF 55 len payload cks),
// ведёт EA data-сессию (seq/sessID, ACK) и attach-автомат (identify/auth без крипто).
// Формат разобран живым захватом. Чистый C++17 (тестируется на хосте).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <functional>

namespace iap {

// --- Кодек одного iAP-кадра (малый формат) ---
uint8_t  iapChecksum(const std::vector<uint8_t>& payload);           // (len+Σpayload+cks)&0xFF==0
std::vector<uint8_t> frameIap(const std::vector<uint8_t>& payload);  // -> FF 55 len payload cks

// Разбор первого iAP-пакета из начала buf. ok=false — не хватает данных/нет синхры.
struct IapParse { std::vector<uint8_t> payload; size_t consumed; bool ok; bool valid; };
IapParse parseIap(const std::vector<uint8_t>& buf);

class Link {
 public:
  struct Transport {                                    // реальный UART к часам
    std::function<void(const uint8_t*, size_t)> write;
    std::function<int()> readByte;                      // -1 если нет данных
  };
  explicit Link(Transport uart) : uart_(std::move(uart)) {}

  // Отправить аксессуару готовый Philips-кадр ([len][cmd][payload]) — завернётся в EA 00 43.
  void sendPhilips(const uint8_t* frame, size_t n);

  // Прокачать входящий UART: разобрать iAP, обработать EA-данные/ACK/keepalive/attach.
  // Извлечённые Philips-байты копятся во внутренней очереди.
  void pump();

  // Следующий байт извлечённого Philips-потока (-1 если нет). Внутри дёргает pump().
  int readPhilipsByte();

  // Оставлен для совместимости; хост сам ничего не шлёт (03 0D — только ответ на запрос часов).
  void tick();

  bool     sessionOpen() const { return sessionOpen_; }
  uint16_t sessionId()   const { return sessID_; }
  uint32_t rxCount()     const { return rxCount_; }     // принятых валидных iAP-пакетов (детект «часы живы»)
  uint32_t rxBytes()     const { return rxBytes_; }     // всего байт с линии (диагностика)
  uint32_t rxBad()       const { return rxBad_; }       // пакетов с неверной КС
  std::string lastRx() const;                             // последние принятые пакеты (hex), диагностика

  // Забыть сессию (часы обесточили/переподключаемся): следующий attach начнётся с нуля.
  void reset() {
    rx_.clear(); philipsOut_.clear();
    txSeq_ = 0x0001; sessID_ = 0x0001;
    sessionOpen_ = attachDone_ = false; tokenAcks_ = authPhase_ = 0;
  }

  // Часы периодически спрашивают дату/время (03 0C <trans> 09) и показывают ответ на табло.
  // Источник времени задаёт прошивка (NTP/RTC); без него на запрос времени не отвечаем.
  struct DateTime { int year, month, day, hour, minute; };
  std::function<bool(DateTime&)> timeSource;
  // Display Remote RemoteEventNotification «дата/время изменились» (03 09 <trans> 09 YYYY MM DD hh mm) —
  // не дожидаясь запроса часов. Значения не проверяются (для опытов с табло).
  void notifyDateTime(const DateTime& t);

  // Хуки для логов/отладки (необязательны).
  std::function<void(uint8_t lingo, uint8_t cmd, const uint8_t* d, size_t n)> onControl;
  std::function<void(const char*)> log;

 private:
  void handlePayload(const std::vector<uint8_t>& p);     // диспетчер по lingo/cmd
  void handleAttach(uint8_t cmd, const uint8_t* d, size_t n);
  void sendIap(std::vector<uint8_t> payload);            // кадрирует и пишет в UART
  void sendAccessoryDataAck(uint16_t seq);               // 00 02 <seq> 00 42
  void ackGen(uint16_t trans, uint8_t ackedCmd);         // General-ACK: 00 02 <trans> 00 <cmd>
  void logf(const char* s) { if (log) log(s); }

  Transport uart_;
  std::vector<uint8_t> rx_;            // буфер входящих UART-байт (для фреймера)
  std::deque<uint8_t>  philipsOut_;    // извлечённые Philips-байты (FIFO к wul::Client)
  uint16_t txSeq_ = 0x0001;           // наш счётчик отправленных data-пакетов
  uint16_t sessID_ = 0x0001;          // id data-сессии (устройство назначает при OpenDataSession)
  bool     sessionOpen_ = false;
  bool     attachDone_  = false;
  uint8_t  tokenAcks_ = 0;            // сколько SetFIDTokenValues уже подтвердили (1-й/2-й блок разные)
  uint8_t  authPhase_ = 0;            // фаза auth: 0=до инфо, 1=инфо получено (cmd2B → челлендж)
  uint32_t rxCount_ = 0, rxBytes_ = 0, rxBad_ = 0;
  std::vector<std::string> last_;
};

}  // namespace iap
