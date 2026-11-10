#include "spp.h"
#include <Arduino.h>
#include <esp_spp_api.h>
#include <deque>
#include <mutex>
#include <new>
#include <cstdlib>
#include "api.h"

namespace spp {

static char name_[32] = "";
static volatile uint32_t handle_ = 0;            // 0 — никто не подключён
static volatile bool txBusy_ = false, txCong_ = false;
static std::deque<std::string> inbox_;           // готовые строки запросов → основной цикл
static std::string partial_;                     // недособранная строка (задача Bluetooth)
static std::mutex mux_;                          // строки выделяют память — не portMUX
static volatile uint32_t opens_ = 0, reqs_ = 0, lost_ = 0, oom_ = 0;
static const size_t kMaxLine = 2048, kMaxQueue = 4, kChunk = 512;

static void onEvent(esp_spp_cb_event_t e, esp_spp_cb_param_t* p) {   // задача Bluetooth
  switch (e) {
    case ESP_SPP_INIT_EVT:
      // AUTHENTICATE — только спаренный телефон (тот же, что играет музыку); канал выбирает стек
      esp_spp_start_srv(ESP_SPP_SEC_AUTHENTICATE, ESP_SPP_ROLE_SLAVE, 0, name_);
      break;
    case ESP_SPP_SRV_OPEN_EVT: {
      std::lock_guard<std::mutex> g(mux_);
      partial_.clear(); inbox_.clear();
      handle_ = p->srv_open.handle; txBusy_ = txCong_ = false; ++opens_;
      Serial.println("[spp] приложение подключилось");
      break;
    }
    case ESP_SPP_CLOSE_EVT:
      if (p->close.handle == handle_) { handle_ = 0; txBusy_ = txCong_ = false; Serial.println("[spp] приложение отключилось"); }
      break;
    case ESP_SPP_DATA_IND_EVT: {
      std::lock_guard<std::mutex> g(mux_);
      for (uint16_t i = 0; i < p->data_ind.len; ++i) {
        const char c = static_cast<char>(p->data_ind.data[i]);
        if (c == '\n') {
          if (!partial_.empty() && inbox_.size() < kMaxQueue) inbox_.push_back(std::move(partial_));
          partial_.clear();
        } else if (partial_.size() < kMaxLine) partial_ += c;
      }
      break;
    }
    case ESP_SPP_WRITE_EVT: txBusy_ = false; txCong_ = p->write.cong; break;
    case ESP_SPP_CONG_EVT: txCong_ = p->cong.cong; break;
    default: break;
  }
}

void begin(const char* name) {
  strncpy(name_, name, sizeof name_ - 1);
  esp_err_t e1 = esp_spp_register_callback(onEvent);
  esp_err_t e2 = esp_spp_init(ESP_SPP_MODE_CB);
  Serial.printf("[spp] сервер «%s»: callback=%d init=%d, свободно %u\n", name_, e1, e2, (unsigned)ESP.getFreeHeap());
}

bool connected() { return handle_ != 0; }

// Ответ уходит кусками по 512 байт, по одному за проход основного цикла — без ожидания. Раньше send() ждал
// подтверждения каждого куска до 1,5 с: пока играет музыка, канал занят, и основной цикл стоял — «58 02» часам
// опаздывало больше 1,5 с, часы глушили вход дока (паузы в музыке, 2026-11-03).
static std::string out_;                          // недоотправленный ответ
static size_t outPos_ = 0;
static uint32_t outAt_ = 0;                       // millis() последнего отправленного куска
static void dropOut() { std::string().swap(out_); outPos_ = 0; }   // память — сразу обратно (в колонке её мало)

static void pump() {
  if (outPos_ >= out_.size()) return;
  if (txBusy_ || txCong_) {                       // стек ещё не взял прошлый кусок
    if (millis() - outAt_ > 3000) { ++lost_; dropOut(); }   // затор — приложение повторит запрос
    return;
  }
  const size_t n = std::min(kChunk, out_.size() - outPos_);
  txBusy_ = true;
  if (esp_spp_write(handle_, static_cast<int>(n), reinterpret_cast<uint8_t*>(&out_[outPos_])) != ESP_OK) {
    txBusy_ = false; ++lost_; dropOut(); return;
  }
  outPos_ += n; outAt_ = millis();
  if (outPos_ >= out_.size()) dropOut();
}

void loop() {
  if (!handle_) { if (!out_.empty()) dropOut(); return; }
  if (outPos_ < out_.size()) { pump(); return; }  // следующий запрос — когда прошлый ответ ушёл целиком
  std::string req;
  {
    std::lock_guard<std::mutex> g(mux_);
    if (inbox_.empty()) return;
    req = std::move(inbox_.front()); inbox_.pop_front();
  }
  ++reqs_;
  try {
    out_ = api::handleLine(req);
  } catch (const std::bad_alloc&) {                // не хватило ОЗУ — ответить ошибкой, а не уронить колонку
    ++oom_;
    const size_t at = req.find("\"id\":");
    const unsigned long id = at == std::string::npos ? 0 : std::strtoul(req.c_str() + at + 5, nullptr, 10);
    char b[96];
    snprintf(b, sizeof b, "{\"id\":%lu,\"code\":503,\"body\":{\"ok\":false,\"error\":\"no memory\"}}\n", id);
    out_ = b;
  }
  outPos_ = 0; outAt_ = millis();
  pump();
}

const char* stats() {
  static char b[160];                            // кириллица — 2 байта на букву
  snprintf(b, sizeof b, "SPP: подключений %u, запросов %u, потеряно ответов %u, без памяти %u", (unsigned)opens_, (unsigned)reqs_,
           (unsigned)lost_, (unsigned)oom_);
  return b;
}

}  // namespace spp
