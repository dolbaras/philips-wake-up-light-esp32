#include "scheduler.h"
#include <cstring>
#ifdef ARDUINO
#include <Preferences.h>
static Preferences g_prefs;
#endif

namespace sched {

// ---------- чистая логика ----------
int minuteOfDay(int h, int m) { return h * 60 + m; }

int subMinutes(int mod, int delta) {
  int r = (mod - delta) % 1440;
  return r < 0 ? r + 1440 : r;
}

bool activeToday(const Alarm& a, int weekday) {
  if (a.weekdays == 0) return true;                 // каждый день
  return (a.weekdays >> weekday) & 1;
}

bool shouldFireSound(const Alarm& a, const TimeNow& n) {
  if (!a.enabled || !a.sound) return false;
  if (!activeToday(a, n.weekday)) return false;
  return minuteOfDay(n.hour, n.minute) == minuteOfDay(a.hour, a.minute);
}

bool shouldFireLight(const Alarm& a, const TimeNow& n) {
  if (!a.enabled || !a.light) return false;
  if (!activeToday(a, n.weekday)) return false;     // день проверяем по дню будильника
  int start = subMinutes(minuteOfDay(a.hour, a.minute), a.sunriseMinutes);
  return minuteOfDay(n.hour, n.minute) == start;
}

uint16_t sunriseLevelAt(const Alarm& a, int secs, uint16_t maxLevel) {
  int total = a.sunriseMinutes * 60;
  if (total <= 0) return maxLevel;
  if (secs <= 0) return 0;
  if (secs >= total) return maxLevel;
  return static_cast<uint16_t>(static_cast<uint32_t>(maxLevel) * secs / total);
}

// ---------- сериализация (фиксированная запись 8 + kSoundNameLen байт) ----------
std::vector<uint8_t> serialize(const std::vector<Alarm>& v) {
  std::vector<uint8_t> b;
  b.push_back(static_cast<uint8_t>(v.size()));
  for (const auto& a : v) {
    uint8_t flags = (a.enabled ? 1 : 0) | (a.light ? 2 : 0) | (a.sound ? 4 : 0);
    b.push_back(a.id); b.push_back(flags);
    b.push_back(a.hour); b.push_back(a.minute); b.push_back(a.weekdays);
    b.push_back(a.sunriseMinutes & 0xFF); b.push_back(a.sunriseMinutes >> 8);
    b.push_back(a.volume);
    for (int i = 0; i < kSoundNameLen; ++i) b.push_back(static_cast<uint8_t>(a.soundName[i]));
  }
  return b;
}

std::vector<Alarm> deserialize(const uint8_t* d, size_t n) {
  std::vector<Alarm> v;
  if (!d || n < 1) return v;
  const size_t rec = 8 + kSoundNameLen;
  uint8_t count = d[0];
  size_t off = 1;
  for (uint8_t i = 0; i < count && off + rec <= n; ++i) {
    Alarm a;
    a.id = d[off]; uint8_t flags = d[off + 1];
    a.enabled = flags & 1; a.light = flags & 2; a.sound = flags & 4;
    a.hour = d[off + 2]; a.minute = d[off + 3]; a.weekdays = d[off + 4];
    a.sunriseMinutes = d[off + 5] | (d[off + 6] << 8);
    a.volume = d[off + 7];
    std::memcpy(a.soundName, d + off + 8, kSoundNameLen);
    a.soundName[kSoundNameLen - 1] = '\0';
    v.push_back(a);
    off += rec;
  }
  return v;
}

NextAlarm computeNextAlarm(const std::vector<Alarm>& v, const TimeNow& now) {
  NextAlarm best;
  int bestUntil = 8 * 1440;                 // больше недели = «не найдено»
  int nowMod = minuteOfDay(now.hour, now.minute);
  for (const auto& a : v) {
    if (!a.enabled) continue;
    int target = minuteOfDay(a.hour, a.minute);
    for (int d = 0; d <= 7; ++d) {          // сегодня..через неделю
      int wd = (now.weekday + d) % 7;
      if (!activeToday(a, wd)) continue;
      int until = d * 1440 + target - nowMod;
      if (until < 0) continue;              // уже прошло сегодня
      if (until < bestUntil) {
        bestUntil = until;
        best.any = true; best.hour = a.hour; best.minute = a.minute; best.minutesUntil = until;
      }
      break;                               // ближайший активный день для этого будильника найден
    }
  }
  return best;
}

// ---------- класс ----------
int Scheduler::indexOf(uint8_t id) const {
  for (size_t i = 0; i < alarms_.size(); ++i) if (alarms_[i].id == id) return static_cast<int>(i);
  return -1;
}

uint8_t Scheduler::nextFreeId() const {
  for (uint8_t id = 1; id < 255; ++id) if (indexOf(id) < 0) return id;
  return 0;
}

void Scheduler::begin() { load(); }

void Scheduler::tick(const TimeNow& now) {
  for (size_t i = 0; i < alarms_.size() && i < static_cast<size_t>(kMaxAlarms); ++i) {
    const Alarm& a = alarms_[i];
    if (shouldFireLight(a, now) && firedLight_[i] != now.epochMinute) {
      firedLight_[i] = now.epochMinute;
      if (onFireLight) onFireLight(a);
    }
    if (shouldFireSound(a, now) && firedSound_[i] != now.epochMinute) {
      firedSound_[i] = now.epochMinute;
      if (onFireSound) onFireSound(a);
    }
  }
}

bool Scheduler::upsert(const Alarm& in) {
  Alarm a = in;
  int idx = a.id ? indexOf(a.id) : -1;
  if (idx >= 0) {
    alarms_[idx] = a;
  } else {
    if (alarms_.size() >= static_cast<size_t>(kMaxAlarms)) return false;
    if (!a.id) a.id = nextFreeId();
    alarms_.push_back(a);
  }
  save();
  return true;
}

bool Scheduler::remove(uint8_t id) {
  int idx = indexOf(id);
  if (idx < 0) return false;
  alarms_.erase(alarms_.begin() + idx);
  save();
  return true;
}

void Scheduler::save() {
#ifdef ARDUINO
  auto blob = serialize(alarms_);
  g_prefs.begin("wul", false);
  g_prefs.putBytes("alarms", blob.data(), blob.size());
  g_prefs.end();
#endif
}

void Scheduler::load() {
#ifdef ARDUINO
  g_prefs.begin("wul", true);
  size_t sz = g_prefs.getBytesLength("alarms");
  if (sz) {
    std::vector<uint8_t> blob(sz);
    g_prefs.getBytes("alarms", blob.data(), sz);
    alarms_ = deserialize(blob.data(), sz);
  }
  g_prefs.end();
#endif
}

}  // namespace sched
