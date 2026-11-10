#include "lamp_i2c.h"
#include <Arduino.h>

namespace lampi2c {

static int scl_ = -1, sda_ = -1;
static const uint8_t kAddr = 0x51;
static const uint32_t kHalfUs = 50;     // полупериод: ~10 кГц, как у самих часов

static void lo(int p) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
static void rel(int p) { pinMode(p, INPUT); }
static bool hi(int p) { return digitalRead(p) == HIGH; }
static void dly() { delayMicroseconds(kHalfUs); }

static bool releaseScl() {               // отпустить SCL и дождаться, пока ведомый перестанет его держать
  rel(scl_);
  for (uint32_t t = micros(); !hi(scl_); ) if (micros() - t > 2000) return false;
  return true;
}

static bool idle(uint32_t ms) {          // обе линии в «1» непрерывно ms миллисекунд (ждём не дольше 300 мс)
  uint32_t start = millis(), quiet = millis();
  while (millis() - start < 300) {
    if (!hi(scl_) || !hi(sda_)) quiet = millis();
    else if (millis() - quiet >= ms) return true;
    delayMicroseconds(50);
  }
  return false;
}

static void stop() {
  lo(sda_); dly();
  releaseScl(); dly();
  rel(sda_); dly();
}

// Передать байт; true — ведомый ответил ACK. lost — на SDA «0», когда мы её отпускали (чужой обмен).
static bool txByte(uint8_t b, bool& lost) {
  for (int i = 7; i >= 0; --i) {
    if (b & (1 << i)) rel(sda_); else lo(sda_);
    dly();
    if (!releaseScl()) { lost = true; return false; }
    if ((b & (1 << i)) && !hi(sda_)) { lost = true; return false; }
    dly();
    lo(scl_);
  }
  rel(sda_); dly();
  if (!releaseScl()) { lost = true; return false; }
  bool ack = !hi(sda_);
  dly();
  lo(scl_);
  return ack;
}

static uint8_t rxByte(bool ack) {
  uint8_t b = 0;
  rel(sda_);
  for (int i = 0; i < 8; ++i) {
    dly();
    releaseScl();
    b = (b << 1) | (hi(sda_) ? 1 : 0);
    dly();
    lo(scl_);
  }
  if (ack) lo(sda_); else rel(sda_);
  dly(); releaseScl(); dly(); lo(scl_);
  rel(sda_);
  return b;
}

static bool start() {                    // START из свободной шины
  if (!hi(scl_) || !hi(sda_)) return false;
  lo(sda_); dly();
  lo(scl_); dly();
  return true;
}

static bool restart() {
  rel(sda_); dly();
  if (!releaseScl()) return false;
  dly();
  lo(sda_); dly();
  lo(scl_); dly();
  return true;
}

void begin(int scl, int sda) { scl_ = scl; sda_ = sda; rel(scl_); rel(sda_); }

// Один обмен: адрес+рег, затем запись data или (повторный старт) чтение n байт. Прерывания не запрещаем:
// ведущий — мы, растянутый Wi-Fi такт I2C не ломает.
static std::string xfer(uint16_t reg, const std::vector<uint8_t>* data, uint8_t n, std::vector<uint8_t>* out) {
  if (scl_ < 0) return "не настроено";
  if (!idle(30)) return "шина занята";
  bool lost = false;
  if (!start()) return "шина занята";
  auto fail = [&](const char* why) { stop(); return std::string(lost ? "чужой обмен" : why); };
  if (!txByte(kAddr << 1, lost)) return fail("нет ACK на адрес");
  if (!txByte(reg >> 8, lost) || !txByte(reg & 0xFF, lost)) return fail("нет ACK на регистр");
  if (data) {
    for (uint8_t b : *data) if (!txByte(b, lost)) return fail("нет ACK на данные");
  } else {
    if (!restart()) return fail("повторный старт");
    if (!txByte((kAddr << 1) | 1, lost)) return fail("нет ACK на чтение");
    out->clear();
    for (uint8_t i = 0; i < n; ++i) out->push_back(rxByte(i + 1 < n));
  }
  stop();
  return "";
}

std::string read(uint16_t reg, uint8_t n, std::vector<uint8_t>& out) {
  return xfer(reg, nullptr, n ? n : 1, &out);
}

std::string write(uint16_t reg, const std::vector<uint8_t>& data) { return xfer(reg, &data, 0, nullptr); }

std::string probe() {
  char buf[96];
  int s = scl_ < 0 ? -1 : hi(scl_), d = sda_ < 0 ? -1 : hi(sda_);
  bool ack = false;
  std::string err;
  if (scl_ >= 0 && idle(30) && start()) {
    bool lost = false;
    ack = txByte(kAddr << 1, lost);
    stop();
    if (lost) err = "чужой обмен";
  } else err = "шина занята или не подключена";
  snprintf(buf, sizeof buf, "{\"scl\":%d,\"sda\":%d,\"ack\":%s,\"error\":\"%s\"}", s, d, ack ? "true" : "false",
           err.c_str());
  return buf;
}

std::string scanJson(uint16_t from, uint16_t to) {
  if (to < from) to = from;
  if (to - from > 31) to = from + 31;
  std::string r = "{\"r\":{", e = "\"err\":{";
  bool fr = true, fe = true;
  char k[8], v[4];
  for (uint32_t reg = from; reg <= to; ++reg) {
    std::vector<uint8_t> out;
    std::string err = read(reg, 1, out);
    if (err == "шина занята" || err == "не настроено") { e += std::string(fe ? "" : ",") + "\"stop\":\"" + err + "\""; fe = false; break; }
    snprintf(k, sizeof k, "%04X", (unsigned)reg);
    if (err.empty()) {
      snprintf(v, sizeof v, "%02X", out[0]);
      r += std::string(fr ? "" : ",") + "\"" + k + "\":\"" + v + "\""; fr = false;
    } else {
      e += std::string(fe ? "" : ",") + "\"" + k + "\":\"" + err + "\""; fe = false;
    }
    yield();
  }
  return r + "}," + e + "}}";
}

}  // namespace lampi2c
