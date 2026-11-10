#include "bus_sniff.h"
#include <Arduino.h>
#include <deque>
#include "driver/rmt.h"
#include <vector>

namespace sniff {

static int pin_ = -1;
static HardwareSerial Bus(1);
static uint32_t baud_ = 0;
static volatile uint32_t lastEdge_ = 0, minPulse_ = 0xFFFFFFFF, edges_ = 0;
static uint32_t measureStart_ = 0;
static bool measuring_ = false;

struct Frame { uint32_t ms; std::string hex; };
static std::deque<Frame> frames_;
static std::string cur_;
static uint32_t curStart_ = 0, lastByte_ = 0;

static volatile bool capturing_ = false;
static uint32_t capUntil_ = 0;
static int level0_ = 1;


static void IRAM_ATTR onEdge() {
  uint32_t now = micros();
  uint32_t w = now - lastEdge_;
  lastEdge_ = now;
  ++edges_;
  if (w > 3 && w < minPulse_) minPulse_ = w;              // короче 3 мкс — дребезг/помеха
}

void redetect() {
  if (pin_ < 0) return;
  Bus.end();
  pinMode(pin_, INPUT);
  minPulse_ = 0xFFFFFFFF; edges_ = 0; lastEdge_ = micros();
  attachInterrupt(digitalPinToInterrupt(pin_), onEdge, CHANGE);
  measureStart_ = millis(); measuring_ = true;
}

void setBaud(uint32_t b) {
  if (pin_ < 0 || !b) return;
  if (measuring_) { detachInterrupt(digitalPinToInterrupt(pin_)); measuring_ = false; }
  baud_ = b;
  Bus.begin(baud_, SERIAL_8N1, pin_, -1);
  Serial.printf("[sniff] приём на GPIO%d, %lu бод\n", pin_, (unsigned long)baud_);
}

// Захват длительностей импульсов аппаратным RMT (1 мкс, без влияния прерываний Wi-Fi).
static constexpr rmt_channel_t kRmtCh = RMT_CHANNEL_4;
static RingbufHandle_t rmtRb_ = nullptr;
static std::vector<std::pair<uint8_t, uint16_t>> pulses_;   // (уровень, мкс); 0/0 — граница кадра

void captureEdges(uint32_t ms) {
  if (pin_ < 0) return;
  if (measuring_) { detachInterrupt(digitalPinToInterrupt(pin_)); measuring_ = false; }
  Bus.end();
  pulses_.clear();
  if (!rmtRb_) {
    rmt_config_t c = RMT_DEFAULT_CONFIG_RX(static_cast<gpio_num_t>(pin_), kRmtCh);
    c.clk_div = 80;                                          // 1 тик = 1 мкс
    c.mem_block_num = 4;
    c.rx_config.filter_en = true;
    c.rx_config.filter_ticks_thresh = 200;                   // глушить иглы < 2,5 мкс (такты APB)
    c.rx_config.idle_threshold = 2000;                       // 2 мс тишины — конец кадра
    rmt_config(&c);
    rmt_driver_install(kRmtCh, 16000, 0);
    rmt_get_ringbuf_handle(kRmtCh, &rmtRb_);
  }
  rmt_rx_start(kRmtCh, true);
  level0_ = digitalRead(pin_);
  capturing_ = true; capUntil_ = millis() + ms;
}

static void drainRmt() {
  if (!rmtRb_) return;
  size_t len = 0;
  while (rmt_item32_t* it = static_cast<rmt_item32_t*>(xRingbufferReceive(rmtRb_, &len, 0))) {
    for (size_t i = 0; i < len / sizeof(rmt_item32_t); ++i) {
      if (it[i].duration0) pulses_.push_back({static_cast<uint8_t>(it[i].level0), static_cast<uint16_t>(it[i].duration0)});
      if (it[i].duration1) pulses_.push_back({static_cast<uint8_t>(it[i].level1), static_cast<uint16_t>(it[i].duration1)});
    }
    pulses_.push_back({0, 0});                               // конец кадра
    vRingbufferReturnItem(rmtRb_, it);
    if (pulses_.size() > 6000) break;
  }
}

std::string edgesJson() {
  drainRmt();
  std::string j = "{\"capturing\":" + std::string(capturing_ ? "true" : "false") +
                  ",\"n\":" + std::to_string(pulses_.size()) + ",\"p\":[";
  for (size_t i = 0; i < pulses_.size(); ++i) {
    if (i) j += ',';
    j += "[" + std::to_string(pulses_[i].first) + "," + std::to_string(pulses_[i].second) + "]";
  }
  return j + "]}";
}

void begin(int pin) { pin_ = pin; redetect(); }

uint32_t baud() { return baud_; }

void loop() {
  if (capturing_) {
    drainRmt();
    if (static_cast<int32_t>(millis() - capUntil_) >= 0) {
      rmt_rx_stop(kRmtCh);
      drainRmt();
      capturing_ = false;
      Serial.printf("[sniff] записано импульсов: %u\n", static_cast<unsigned>(pulses_.size()));
      if (baud_) Bus.begin(baud_, SERIAL_8N1, pin_, -1);
    }
    return;
  }
  if (measuring_ && millis() - measureStart_ > 3000) {     // 3 с слушаем фронты
    detachInterrupt(digitalPinToInterrupt(pin_));
    measuring_ = false;
    if (edges_ > 20 && minPulse_ < 2000) {
      uint32_t b = 1000000UL / minPulse_;
      Serial.printf("[sniff] фронтов %lu, мин. импульс %lu мкс → ~%lu бод\n",
                    (unsigned long)edges_, (unsigned long)minPulse_, (unsigned long)b);
      setBaud(b);
    } else {
      Serial.printf("[sniff] сигнала нет (фронтов %lu) — повтор через 10 с\n", (unsigned long)edges_);
      measureStart_ = millis() + 7000;                      // ещё 3 с + ожидание
      redetect();
    }
    return;
  }
  if (!baud_) return;
  static const char* H = "0123456789ABCDEF";
  while (Bus.available()) {
    uint8_t c = Bus.read();
    uint32_t now = millis();
    if (!cur_.empty() && now - lastByte_ > 3) {             // пауза — новый кадр
      frames_.push_back({curStart_, cur_});
      if (frames_.size() > 300) frames_.pop_front();
      cur_.clear();
    }
    if (cur_.empty()) curStart_ = now;
    if (!cur_.empty()) cur_ += ' ';
    cur_ += H[c >> 4]; cur_ += H[c & 15];
    lastByte_ = now;
    if (cur_.size() > 600) { frames_.push_back({curStart_, cur_}); cur_.clear(); }
  }
  if (!cur_.empty() && millis() - lastByte_ > 3) {
    frames_.push_back({curStart_, cur_});
    if (frames_.size() > 300) frames_.pop_front();
    cur_.clear();
  }
}

std::string json(uint32_t sinceMs) {
  std::string j = "{\"baud\":" + std::to_string(baud_) + ",\"edges\":" + std::to_string(edges_) +
                  ",\"now\":" + std::to_string(millis()) + ",\"frames\":[";
  bool first = true;
  for (const auto& f : frames_) {
    if (f.ms <= sinceMs) continue;
    if (!first) j += ',';
    first = false;
    j += "{\"ms\":" + std::to_string(f.ms) + ",\"hex\":\"" + f.hex + "\"}";
  }
  return j + "]}";
}

}  // namespace sniff
