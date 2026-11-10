#include "bootlog.h"
#include <Arduino.h>
#include <esp_attr.h>
#include <cstdio>
#include <cstring>

namespace bootlog {

struct Rec { uint32_t no, up; uint8_t reset, bt; char why[64]; };   // why — до ~30 букв кириллицей
static const uint32_t kMagic = 0xB0010603u;
static const uint32_t kN = 6;
RTC_NOINIT_ATTR static uint32_t magic_, n_;                // n_ — сколько записей всего (кольцо по kN)
RTC_NOINIT_ATTR static Rec cur_, hist_[kN];

void why(const char* r) {
  size_t n = strnlen(r, sizeof cur_.why - 1);
  while (n > 0 && (static_cast<uint8_t>(r[n]) & 0xC0) == 0x80) --n;   // не резать букву UTF-8 посередине
  memcpy(cur_.why, r, n); cur_.why[n] = 0;
}
void tick(bool speaker) { cur_.up = millis() / 1000; cur_.bt = speaker; }

static const char* resetText(int r) {
  switch (r) {
    case 1:  return "питание";
    case 3:  return "перезагрузка";
    case 4:  return "ПАДЕНИЕ";
    case 5: case 6: return "СТОРОЖ: завис";
    case 7:  return "сторож/включение";
    case 9:  return "просадка питания";
    default: return "сброс";
  }
}

std::string begin(uint32_t no, int reset, bool cold) {
  if (magic_ != kMagic || cold || n_ > 1000000) { magic_ = kMagic; n_ = 0; }
  else if (cur_.no) {                                        // прошлая загрузка закончилась ЭТИМ сбросом
    Rec r = cur_; r.reset = static_cast<uint8_t>(reset); r.why[sizeof r.why - 1] = 0;
    hist_[n_ % kN] = r; ++n_;
  }
  memset(&cur_, 0, sizeof cur_); cur_.no = no;
  std::string s = "прошлые загрузки:";
  if (!n_) return s + " нет (было обесточено)";
  char b[128];
  for (uint32_t i = 0; i < n_ && i < kN; ++i) {             // с последней
    const Rec& r = hist_[(n_ - 1 - i) % kN];
    const char* how = r.why[0] ? r.why : resetText(r.reset);
    const bool odd = r.why[0] && r.reset != 3;             // хотели перезагрузиться, а вышло иначе
    snprintf(b, sizeof b, " #%u %s %u:%02u → %s%s%s;", (unsigned)r.no, r.bt ? "колонка" : "Wi-Fi",
             (unsigned)(r.up / 60), (unsigned)(r.up % 60), how, odd ? ", но " : "", odd ? resetText(r.reset) : "");
    s += b;
  }
  return s;
}

}  // namespace bootlog
