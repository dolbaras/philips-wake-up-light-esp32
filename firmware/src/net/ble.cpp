#include "ble.h"
#ifndef WUL_NO_BLE
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <deque>
#include <mutex>
#include "api.h"

namespace ble {

static const char* kService = "6e7a0001-77b1-4c8e-9d1a-57a4e0c0b1a0";
static const char* kRx      = "6e7a0002-77b1-4c8e-9d1a-57a4e0c0b1a0";
static const char* kTx      = "6e7a0003-77b1-4c8e-9d1a-57a4e0c0b1a0";

static NimBLECharacteristic* tx = nullptr;
static volatile bool linked = false;
static volatile uint16_t mtu = 23;
static std::string partial;                    // недособранный запрос (колбэки NimBLE — в своей задаче)
static std::deque<std::string> inbox;          // готовые запросы → основной цикл
static std::mutex mux;                         // не portMUX: внутри строки выделяют память

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*) override { linked = true; }
  void onDisconnect(NimBLEServer*) override {
    linked = false; mtu = 23;
    std::lock_guard<std::mutex> g(mux); partial.clear();
    NimBLEDevice::startAdvertising();
  }
  void onMTUChange(uint16_t m, ble_gap_conn_desc*) override { mtu = m; }
};

class RxCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    std::string v = c->getValue();
    std::lock_guard<std::mutex> g(mux);
    for (char ch : v) {
      if (ch == '\n') { if (!partial.empty() && inbox.size() < 8) inbox.push_back(partial); partial.clear(); }
      else if (partial.size() < 4096) partial += ch;
    }
  }
};

void begin(const std::string& name) {
  NimBLEDevice::init(name);
  NimBLEDevice::setMTU(247);
  NimBLEServer* srv = NimBLEDevice::createServer();
  srv->setCallbacks(new ServerCb());
  NimBLEService* svc = srv->createService(kService);
  NimBLECharacteristic* rx = svc->createCharacteristic(kRx, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(new RxCb());
  tx = svc->createCharacteristic(kTx, NIMBLE_PROPERTY::NOTIFY);
  svc->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(kService);
  adv->setScanResponse(true);
  adv->start();
  Serial.printf("[ble] реклама: %s\n", name.c_str());
}

bool connected() { return linked; }

static void send(const std::string& s) {
  const size_t chunk = mtu > 3 ? mtu - 3 : 20;
  for (size_t i = 0; i < s.size() && linked; i += chunk) {
    tx->setValue(reinterpret_cast<const uint8_t*>(s.data() + i), std::min(chunk, s.size() - i));
    tx->notify();
    delay(4);                                  // не переполнить очередь уведомлений стека
  }
}

void loop() {
  std::string req;
  {
    std::lock_guard<std::mutex> g(mux);
    if (!inbox.empty()) { req = inbox.front(); inbox.pop_front(); }
  }
  if (req.empty()) return;

  send(api::handleLine(req));
}

}  // namespace ble
#else   // пробная сборка без BLE (место во флеш под A2DP): режим связи «ble» недоступен
#include <Arduino.h>
namespace ble {
void begin(const std::string&) { Serial.println("[ble] в этой сборке BLE нет"); }
void loop() {}
bool connected() { return false; }
}  // namespace ble
#endif
