#include "wul_client.h"
#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace wul {

uint32_t Client::nowMs() {
#ifdef ARDUINO
  return millis();
#else
  return 0;  // в хост-тестах время не нужно
#endif
}

void Client::poll() {
  int b;
  while ((b = t_.readByte()) >= 0) {
    rd_.push(static_cast<uint8_t>(b));
    while (auto f = rd_.next()) {
      // перехват служебных ответов для статуса
      if (f->cmd == CMD_DEV_INIT) handshakeDone_ = true;
      if (f->cmd == CMD_RET_NAME) name_.assign(f->payload.begin(), f->payload.end());
      if (f->cmd == CMD_RET_VALUE && f->property() == 0x17) {  // FWVER_MAIN
        auto v = f->value(); fw_.assign(v.begin(), v.end());
      }
      if (onFrame) onFrame(*f);
    }
  }
}

bool Client::handshake(uint32_t timeoutMs) {
  handshakeDone_ = false;
  send(appInit());                 // 03 00 01 02
  uint32_t start = nowMs();
  while (!handshakeDone_) {
    poll();
    if (nowMs() - start > timeoutMs) return false;
#ifdef ARDUINO
    delay(2);
#else
    break;  // в тесте без реального транспорта не крутим вечно
#endif
  }
  if (handshakeDone_) {            // как реальное приложение: Capability -> Name -> FW
    auto drain = [&](uint32_t ms){ uint32_t s=nowMs(); while(nowMs()-s<ms){ poll();
#ifdef ARDUINO
      delay(2);
#else
      break;
#endif
    } };
    send(Frame{0x05, {0x00}});     // 02 05 00  (обязательный init после DevInit — из захвата!)
    drain(300);
    send(getCapability());         // 01 06
    drain(300);
    send(getName());               // 01 08
    drain(300);
    send(getProperty(0x17));       // 02 12 17  (FWVER_MAIN)
    drain(300);
  }
  return handshakeDone_;
}

}  // namespace wul
