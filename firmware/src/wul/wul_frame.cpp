#include "wul_frame.h"
#include <cstdio>

namespace wul {

std::vector<uint8_t> encode(const Frame& f) {
  std::vector<uint8_t> out;
  uint8_t len = static_cast<uint8_t>(1 + f.payload.size());  // cmd + payload
  out.push_back(len);
  out.push_back(f.cmd);
  out.insert(out.end(), f.payload.begin(), f.payload.end());
  return out;
}

std::optional<ParseResult> decode(const uint8_t* data, size_t len) {
  if (len < 2) return std::nullopt;            // нужен хотя бы [len][cmd]
  uint8_t flen = data[0];
  if (flen < 1) return std::nullopt;           // len включает минимум cmd
  size_t total = 1 + flen;                     // байт длины + тело
  if (len < total) return std::nullopt;        // тело ещё не пришло
  Frame f;
  f.cmd = data[1];
  f.payload.assign(data + 2, data + total);
  return ParseResult{std::move(f), total};
}

std::optional<Frame> Reader::next() {
  auto r = decode(buf_.data(), buf_.size());
  if (!r) return std::nullopt;
  buf_.erase(buf_.begin(), buf_.begin() + r->consumed);
  return r->frame;
}

Frame getProperty(uint8_t prop) { return Frame{0x12, {prop}}; }
Frame getParam(uint8_t prop)    { return Frame{0x10, {prop}}; }
Frame setProperty(uint8_t prop, std::vector<uint8_t> value) {
  std::vector<uint8_t> p{prop};
  p.insert(p.end(), value.begin(), value.end());
  return Frame{0x14, std::move(p)};
}
Frame getCapability() { return Frame{0x06, {}}; }
Frame getName()       { return Frame{0x08, {}}; }
Frame appInit(uint8_t a, uint8_t b) { return Frame{0x00, {a, b}}; }

std::vector<uint8_t> encBe16(uint16_t v) {
  return { static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v & 0xFF) };
}
uint16_t decBe16(const std::vector<uint8_t>& v) {
  if (v.size() < 2) return v.empty() ? 0 : v[0];
  return (static_cast<uint16_t>(v[0]) << 8) | v[1];
}
std::vector<uint8_t> encBcdHHMM(int hour, int minute) {
  auto bcd = [](int x) { return static_cast<uint8_t>(((x / 10) << 4) | (x % 10)); };
  return { bcd(hour), bcd(minute) };
}
bool decBcdHHMM(const std::vector<uint8_t>& v, int& hour, int& minute) {
  if (v.size() < 2) return false;
  if (v[0] == 0xAA && v[1] == 0xAA) return false;  // будильник выключен
  auto un = [](uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); };
  hour = un(v[0]); minute = un(v[1]);
  return true;
}

Frame setAlarm(int h, int m)         { return setProperty(0x0C, encBcdHHMM(h, m)); }
Frame setAlarmOff()                  { return setProperty(0x0C, {0xAA, 0xAA}); }
Frame lightOn()                      { return setProperty(0x4F, {0xEE}); }
Frame lightOff()                     { return setProperty(0x4F, {0x00}); }
Frame audioPlay()                    { return setProperty(0x58, {0x02}); }
Frame audioStop()                    { return setProperty(0x58, {0x00}); }
Frame setDisplayBrightness(uint8_t l){ return setProperty(0x42, encBe16(l)); }
Frame setVolume(uint8_t v)           { return setProperty(0x00, encBe16(v)); }
Frame setDawnProfile(uint8_t m, uint8_t i) { return setProperty(0x4E, {m, i}); }
Frame setDuskProfile(uint8_t m, uint8_t b) { return setProperty(0x49, {m, b}); }
Frame dawnStart()                    { return setProperty(0x4F, {0xFF}); }
Frame duskStart()                    { return setProperty(0x4F, {0xEE}); }
Frame curveStop()                    { return setProperty(0x4F, {0x00}); }
Frame alarmDismiss()                 { return setProperty(0x4F, {0x11}); }
Frame snooze()                       { return setProperty(0x56, {0x01}); }
Frame setAlarmVolume(uint8_t v)      { return setProperty(0x14, encBe16(v)); }

uint8_t bcd(int v) { v %= 100; return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }
Frame setDateTime(int year, int month, int day, int hour, int minute, int second) {
  // ГИПОТЕЗА: 14 0B YY MM DD HH MM SS (BCD). Подтвердить живым захватом (prop 0B DATETIME).
  std::vector<uint8_t> v{ bcd(year % 100), bcd(month), bcd(day), bcd(hour), bcd(minute), bcd(second) };
  return setProperty(0x0B, v);
}

std::string hex(const std::vector<uint8_t>& b) {
  static const char* H = "0123456789ABCDEF";
  std::string s; s.reserve(b.size() * 2);
  for (uint8_t x : b) { s += H[x >> 4]; s += H[x & 0xF]; }
  return s;
}
std::vector<uint8_t> fromHex(const std::string& s) {
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
  };
  std::vector<uint8_t> b;
  for (size_t i = 0; i + 1 < s.size(); i += 2)
    b.push_back(static_cast<uint8_t>((nib(s[i]) << 4) | nib(s[i + 1])));
  return b;
}

}  // namespace wul
