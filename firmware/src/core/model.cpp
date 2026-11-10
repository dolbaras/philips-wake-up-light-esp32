#include "model.h"
#include <algorithm>

namespace core {

const char* const kSoundNames[kSoundCount] = {
  "Birds", "Forest", "Mountain", "Ocean", "Piano", "Rooster", "Yoga", "Zen Garden"};
const char* const kSoundFiles[kSoundCount] = {
  "chuckoo", "forest_day", "cow_in_alpes", "sea_with_seagulls",
  "sati", "rooster_and_chickens", "yoga", "buddha2"};

static void copyStr(char* dst, size_t cap, const std::string& s) {
  size_t n = std::min(s.size(), cap - 1);
  std::memcpy(dst, s.data(), n);
  dst[n] = '\0';
}
void setLabel(Alarm& a, const std::string& s) { copyStr(a.label, kLabelLen, s); }
void setText(char* dst, size_t cap, const std::string& s) { copyStr(dst, cap, s); }
void setNote(SleepEntry& e, const std::string& s) { copyStr(e.note, kNoteLen, s); }

template <typename T> static T clampT(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool validate(Alarm& a) {
  if (a.hour > 23 || a.minute > 59) return false;
  a.days &= 0x7F;
  a.sunriseMin = clampT<uint8_t>(static_cast<uint8_t>((a.sunriseMin + 2) / 5 * 5), 20, 40);
  a.intensity  = clampT<uint8_t>(a.intensity, 1, 20);
  a.soundIdx   = clampT<uint8_t>(a.soundIdx, 0, kSoundCount - 1);
  a.volume     = clampT<uint8_t>(a.volume, 0, 20);
  a.snoozeMin  = clampT<uint8_t>(a.snoozeMin, 1, 20);
  a.label[kLabelLen - 1] = '\0';
  return true;
}

// ---- сериализация: [версия][кол-во][записи фиксированной длины] ----
static const uint8_t kAlarmVer = 2, kLogVer = 1;
static const size_t  kAlarmRec = 13 + kLabelLen;
static const size_t  kLogRec   = 12 + kNoteLen;

static void put32(std::vector<uint8_t>& b, uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
static uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }

std::vector<uint8_t> packAlarms(const std::vector<Alarm>& v) {
  std::vector<uint8_t> b{kAlarmVer, static_cast<uint8_t>(v.size())};
  for (const auto& a : v) {
    b.push_back(a.id);
    b.push_back(static_cast<uint8_t>((a.enabled ? 1 : 0) | (a.light ? 2 : 0) | (a.sound ? 4 : 0)));
    for (uint8_t x : {a.hour, a.minute, a.days, a.sunriseMin, a.intensity, a.soundIdx, a.volume, a.snoozeMin})
      b.push_back(x);
    b.insert(b.end(), 3, 0);                                  // резерв
    b.insert(b.end(), a.label, a.label + kLabelLen);
  }
  return b;
}

std::vector<Alarm> unpackAlarms(const uint8_t* d, size_t n) {
  std::vector<Alarm> v;
  if (!d || n < 2 || d[0] != kAlarmVer) return v;
  size_t off = 2;
  for (uint8_t i = 0; i < d[1] && off + kAlarmRec <= n; ++i, off += kAlarmRec) {
    const uint8_t* p = d + off;
    Alarm a;
    a.id = p[0];
    a.enabled = p[1] & 1; a.light = p[1] & 2; a.sound = p[1] & 4;
    a.hour = p[2]; a.minute = p[3]; a.days = p[4]; a.sunriseMin = p[5]; a.intensity = p[6];
    a.soundIdx = p[7]; a.volume = p[8]; a.snoozeMin = p[9];
    std::memcpy(a.label, p + 13, kLabelLen);
    if (validate(a)) v.push_back(a);
  }
  return v;
}

std::vector<uint8_t> packLog(const std::vector<SleepEntry>& v) {
  std::vector<uint8_t> b{kLogVer, static_cast<uint8_t>(v.size())};
  for (const auto& e : v) {
    put32(b, e.start); put32(b, e.end);
    b.push_back(static_cast<uint8_t>(e.quality)); b.push_back(static_cast<uint8_t>(e.ease));
    b.push_back(e.snoozes); b.push_back(0);
    b.insert(b.end(), e.note, e.note + kNoteLen);
  }
  return b;
}

std::vector<SleepEntry> unpackLog(const uint8_t* d, size_t n) {
  std::vector<SleepEntry> v;
  if (!d || n < 2 || d[0] != kLogVer) return v;
  size_t off = 2;
  for (uint8_t i = 0; i < d[1] && off + kLogRec <= n; ++i, off += kLogRec) {
    const uint8_t* p = d + off;
    SleepEntry e;
    e.start = get32(p); e.end = get32(p + 4);
    e.quality = static_cast<int8_t>(p[8]); e.ease = static_cast<int8_t>(p[9]); e.snoozes = p[10];
    std::memcpy(e.note, p + 12, kNoteLen); e.note[kNoteLen - 1] = '\0';
    v.push_back(e);
  }
  return v;
}

// Настройки + закат: [ver][dusk ×4][disp][track][morning]  — v1
//                     + [resetCombo][soundIdx][lang 3][tz 48][region 40] — v2
//                     + [lightOffAfterAlarm] — v3
std::vector<uint8_t> packSettings(const Settings& s, const Dusk& d) {
  std::vector<uint8_t> b{3, d.minutes, d.music, d.musicIdx, d.volume,
                         s.displayBrightness, s.trackBedtime, s.morningQuestion, s.resetCombo, s.soundIdx};
  b.insert(b.end(), s.lang, s.lang + 3);
  b.insert(b.end(), s.tz, s.tz + kTzLen);
  b.insert(b.end(), s.region, s.region + kRegionLen);
  b.push_back(s.lightOffAfterAlarm);
  return b;
}

bool unpackSettings(const uint8_t* b, size_t n, Settings& s, Dusk& d) {
  if (n < 8 || b[0] < 1 || b[0] > 3) return false;
  d.minutes = b[1]; d.music = b[2]; d.musicIdx = b[3]; d.volume = b[4];
  s.displayBrightness = b[5]; s.trackBedtime = b[6]; s.morningQuestion = b[7];
  const size_t v2len = 10 + 3 + kTzLen + kRegionLen;
  if (b[0] >= 3 && n >= v2len + 1) s.lightOffAfterAlarm = b[v2len];
  if (b[0] >= 2 && n >= v2len) {
    s.resetCombo = b[8]; s.soundIdx = clampT<uint8_t>(b[9], 0, kSoundCount - 1);
    std::memcpy(s.lang, b + 10, 3); s.lang[2] = '\0';
    std::memcpy(s.tz, b + 13, kTzLen); s.tz[kTzLen - 1] = '\0';
    std::memcpy(s.region, b + 13 + kTzLen, kRegionLen); s.region[kRegionLen - 1] = '\0';
  }
  return true;
}

}  // namespace core
