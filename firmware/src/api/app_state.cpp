#include "app_state.h"
#include <cstdio>
#include <cstring>

namespace app {

std::string jesc(const std::string& s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += c;
    }
  }
  return o;
}

static std::string wd(uint8_t m) {           // маска дней → [0,2,...]
  std::string o = "[";
  bool first = true;
  for (int i = 0; i < 7; ++i) if ((m >> i) & 1) { if (!first) o += ","; o += std::to_string(i); first = false; }
  return o + "]";
}

std::string alarmToJson(const sched::Alarm& a) {
  char buf[256];
  snprintf(buf, sizeof buf,
    "{\"id\":%u,\"enabled\":%s,\"hour\":%u,\"minute\":%u,\"weekdays\":%s,"
    "\"sunriseMinutes\":%u,\"light\":%s,\"sound\":%s,\"volume\":%u,\"sound_name\":\"%s\"}",
    a.id, a.enabled ? "true" : "false", a.hour, a.minute, wd(a.weekdays).c_str(),
    a.sunriseMinutes, a.light ? "true" : "false", a.sound ? "true" : "false",
    a.volume, jesc(a.soundName).c_str());
  return buf;
}

std::string toJson(const State& s) {
  std::string o = "{";
  o += "\"device\":{\"name\":\"" + jesc(s.deviceName) + "\",\"fw\":\"" + jesc(s.fw) + "\"},";
  o += std::string("\"connected\":") + (s.connected ? "true" : "false") + ",";
  o += std::string("\"light\":{\"on\":") + (s.lightOn ? "true" : "false") +
       ",\"level\":" + std::to_string(s.lightLevel) + "},";
  o += "\"brightness\":" + std::to_string(s.brightness) + ",";
  o += "\"volume\":" + std::to_string(s.volume) + ",";
  o += "\"almVolume\":" + std::to_string(s.almVolume) + ",";
  o += "\"lastButton\":{\"group\":" + std::to_string(s.lastButton) +
       ",\"level\":" + std::to_string(s.lastButtonLevel) + "},";
  o += "\"alarms\":[";
  for (size_t i = 0; i < s.alarms.size(); ++i) { if (i) o += ","; o += alarmToJson(s.alarms[i]); }
  o += "]}";
  return o;
}

// --- минимальный парсер полей будильника (без внешних зависимостей) ---
static bool findInt(const std::string& j, const char* key, long& out) {
  std::string k = std::string("\"") + key + "\"";
  size_t p = j.find(k);
  if (p == std::string::npos) return false;
  p = j.find(':', p + k.size());
  if (p == std::string::npos) return false;
  ++p;
  while (p < j.size() && (j[p] == ' ' || j[p] == '\t')) ++p;
  bool neg = false;
  if (p < j.size() && (j[p] == '-')) { neg = true; ++p; }
  if (p >= j.size() || !isdigit((unsigned char)j[p])) return false;
  long v = 0; while (p < j.size() && isdigit((unsigned char)j[p])) v = v * 10 + (j[p++] - '0');
  out = neg ? -v : v;
  return true;
}
static bool findBool(const std::string& j, const char* key, bool& out) {
  std::string k = std::string("\"") + key + "\"";
  size_t p = j.find(k);
  if (p == std::string::npos) return false;
  p = j.find(':', p + k.size());
  if (p == std::string::npos) return false;
  out = j.compare(j.find_first_not_of(" \t", p + 1), 4, "true") == 0;
  return true;
}

bool alarmFromJson(const std::string& j, sched::Alarm& a) {
  long v;
  if (findInt(j, "id", v)) a.id = (uint8_t)v;
  if (findInt(j, "hour", v)) a.hour = (uint8_t)v; else return false;
  if (findInt(j, "minute", v)) a.minute = (uint8_t)v; else return false;
  if (findInt(j, "sunriseMinutes", v)) a.sunriseMinutes = (uint16_t)v;
  if (findInt(j, "volume", v)) a.volume = (uint8_t)v;
  bool b;
  if (findBool(j, "enabled", b)) a.enabled = b;
  if (findBool(j, "light", b)) a.light = b;
  if (findBool(j, "sound", b)) a.sound = b;
  // weekdays: массив индексов [0,2,..] → маска
  size_t wp = j.find("\"weekdays\"");
  if (wp != std::string::npos) {
    size_t lb = j.find('[', wp), rb = j.find(']', wp);
    if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
      a.weekdays = 0;
      for (size_t i = lb + 1; i < rb; ++i)
        if (isdigit((unsigned char)j[i])) a.weekdays |= (1 << (j[i] - '0'));
    }
  }
  // sound_name
  size_t sp = j.find("\"sound_name\"");
  if (sp != std::string::npos) {
    size_t q1 = j.find('"', j.find(':', sp) + 1);
    size_t q2 = (q1 != std::string::npos) ? j.find('"', q1 + 1) : std::string::npos;
    if (q1 != std::string::npos && q2 != std::string::npos) {
      std::string name = j.substr(q1 + 1, q2 - q1 - 1);
      std::strncpy(a.soundName, name.c_str(), sched::kSoundNameLen - 1);
      a.soundName[sched::kSoundNameLen - 1] = '\0';
    }
  }
  return true;
}

}  // namespace app
