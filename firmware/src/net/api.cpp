#include "api.h"
#include <ArduinoJson.h>
#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace api {

static Context ctx;
static uint32_t rev_ = 1;

void begin(const Context& c) { ctx = c; }
void bump() { ++rev_; }
uint32_t rev() { return rev_; }

static core::LocalTime now() { return ctx.now ? ctx.now() : 0; }

static Response json(JsonDocument& d, int code = 200) {
  Response r; r.code = code;
  r.body.reserve(measureJson(d));   // один блок нужного размера, без удвоений строки (в колонке ОЗУ мало)
  serializeJson(d, r.body);
  return r;
}
static Response ok() { JsonDocument d; d["ok"] = true; d["rev"] = rev_; return json(d); }
static Response fail(int code, const char* why) { JsonDocument d; d["ok"] = false; d["error"] = why; return json(d, code); }
// Режим колонки: памяти впритык, декодер mp3 по запросу не запускаем (будильник — отдельно, через ядро)
static bool speakerMode() { return ctx.speakerInfo && ctx.speakerInfo().bt; }
static const char* kNoMp3 = "в режиме колонки недоступно";

// ---- модель ↔ JSON ----
static void alarmToJson(const core::Alarm& a, JsonObject o) {
  o["id"] = a.id; o["enabled"] = a.enabled;
  o["hour"] = a.hour; o["minute"] = a.minute; o["days"] = a.days; o["label"] = a.label;
  o["light"] = a.light; o["sunriseMin"] = a.sunriseMin; o["intensity"] = a.intensity;
  o["sound"] = a.sound; o["soundIdx"] = a.soundIdx; o["volume"] = a.volume; o["snoozeMin"] = a.snoozeMin;
}

static core::Alarm alarmFromJson(JsonObjectConst o, core::Alarm a) {
  a.id = o["id"] | a.id;
  a.enabled = o["enabled"] | a.enabled;
  a.hour = o["hour"] | a.hour; a.minute = o["minute"] | a.minute; a.days = o["days"] | a.days;
  if (o["label"].is<const char*>()) core::setLabel(a, o["label"].as<const char*>());
  a.light = o["light"] | a.light; a.sunriseMin = o["sunriseMin"] | a.sunriseMin; a.intensity = o["intensity"] | a.intensity;
  a.sound = o["sound"] | a.sound; a.soundIdx = o["soundIdx"] | a.soundIdx;
  a.volume = o["volume"] | a.volume; a.snoozeMin = o["snoozeMin"] | a.snoozeMin;
  return a;
}

static Response state(const Request& r) {
  if (!r.rev.empty() && static_cast<uint32_t>(std::strtoul(r.rev.c_str(), nullptr, 10)) == rev_) return {304, ""};
  core::Engine& e = *ctx.engine;
  core::LocalTime t = now();
  JsonDocument d;
  d["rev"] = rev_;
  d["time"] = t;                                       // локальное время «как UTC», 0 = нет времени
  JsonObject dev = d["device"].to<JsonObject>();
  dev["name"] = ctx.deviceName ? ctx.deviceName() : "";
  dev["fw"] = ctx.firmware ? ctx.firmware() : "";
  dev["connected"] = e.connected;
  if (ctx.dock && *ctx.dock()) dev["dock"] = ctx.dock();   // часы ведёт iPhone/iPad из дока — модуль не мешает
  dev["esp"] = "esp32-wul 2.0";
  d["mode"] = core::modeName(e.mode);
  d["activeAlarm"] = e.activeAlarmId;
  d["snoozeUntil"] = e.snoozeUntil;
  d["duskEnd"] = e.duskEnd;
  d["question"] = e.questionUntil > 0;
  d["volume"] = e.volume;

  JsonObject clk = d["clock"].to<JsonObject>();
  clk["lightOn"] = e.lightOn; clk["lightLevel"] = e.lightLevel;
  clk["soundOn"] = e.soundOn; clk["test"] = e.testMode; clk["pad"] = e.padSound ? "sound" : "light";
  clk["lightMode"] = e.clockLightMode; clk["lightLevelRaw"] = e.clockLightLevel; clk["curve"] = e.curveStatus;
  JsonArray btn = d["buttons"].to<JsonArray>();
  for (const auto& b : e.buttons) {
    const char* id = core::Engine::buttonName(b.code);
    JsonObject o = btn.add<JsonObject>();
    o["ms"] = b.ms; o["id"] = id ? id : "unknown"; o["level"] = b.level;
  }

  auto nx = e.next(t);
  if (nx.any) { JsonObject n = d["next"].to<JsonObject>(); n["id"] = nx.id; n["when"] = nx.when; }
  JsonArray al = d["alarms"].to<JsonArray>();
  for (const auto& a : e.alarms) alarmToJson(a, al.add<JsonObject>());
  JsonObject du = d["dusk"].to<JsonObject>();
  du["minutes"] = e.dusk.minutes; du["music"] = e.dusk.music; du["musicIdx"] = e.dusk.musicIdx; du["volume"] = e.dusk.volume;
  JsonObject st = d["settings"].to<JsonObject>();
  st["displayBrightness"] = e.settings.displayBrightness;
  st["trackBedtime"] = e.settings.trackBedtime; st["morningQuestion"] = e.settings.morningQuestion;
  st["lang"] = e.settings.lang; st["tz"] = e.settings.tz; st["region"] = e.settings.region;
  st["resetCombo"] = e.settings.resetCombo; st["soundIdx"] = e.settings.soundIdx;
  st["lightOffAfterAlarm"] = e.settings.lightOffAfterAlarm;
  if (ctx.boardLedGet) st["boardLed"] = ctx.boardLedGet();
  JsonArray snd = d["sounds"].to<JsonArray>();
  for (int i = 0; i < core::kSoundCount; ++i) snd.add(core::kSoundNames[i]);

  NetInfo ni = ctx.netInfo ? ctx.netInfo() : NetInfo{};
  JsonObject net = d["net"].to<JsonObject>();
  net["link"] = ni.link; net["ssid"] = ni.ssid; net["ip"] = ni.ip; net["rssi"] = ni.rssi;
  net["ap"] = ni.ap; net["ble"] = ni.ble;
  d["setup"] = ni.setup;
  if (ctx.displayInfo) {
    DisplayInfo di = ctx.displayInfo();
    JsonObject dp = d["display"].to<JsonObject>();
    dp["mode"] = di.mode; dp["text"] = di.text; dp["secondsLeft"] = di.secondsLeft;
  }
  if (ctx.speakerInfo) {
    SpeakerInfo si = ctx.speakerInfo();
    JsonObject sp = d["speaker"].to<JsonObject>();
    sp["available"] = si.available; sp["bt"] = si.bt; sp["on"] = si.on; sp["connected"] = si.connected;
    sp["playing"] = si.playing; sp["showEq"] = si.showEq; sp["lamp"] = si.lamp; sp["name"] = si.name; sp["peer"] = si.peer;
    if (!si.title.empty()) sp["title"] = si.title;
    if (!si.artist.empty()) sp["artist"] = si.artist;
  }
  if (ctx.eqGet) {
    const core::EqParams q = ctx.eqGet();
    JsonObject eq = d["eq"].to<JsonObject>();
    eq["preset"] = q.preset; eq["on"] = q.on; eq["hp"] = q.hp; eq["body"] = q.body; eq["presence"] = q.presence; eq["treble"] = q.treble;
  }
  return json(d);
}

static Response log() {
  JsonDocument d;
  JsonArray arr = d.to<JsonArray>();
  for (const auto& s : ctx.engine->log) {
    JsonObject o = arr.add<JsonObject>();
    o["start"] = s.start; o["end"] = s.end; o["quality"] = s.quality; o["ease"] = s.ease;
    o["snoozes"] = s.snoozes; o["note"] = s.note;
  }
  return json(d);
}

static Response scan() {
  JsonDocument d;
  JsonArray arr = d.to<JsonArray>();
  if (ctx.scan)
    for (const auto& n : ctx.scan()) {
      JsonObject o = arr.add<JsonObject>();
      o["ssid"] = n.ssid; o["rssi"] = n.rssi; o["secure"] = n.secure;
    }
  return json(d);
}

// Применить поля настроек из JSON (общая часть /api/settings и /api/setup).
static void applySettings(JsonObjectConst b) {
  core::Settings s = ctx.engine->settings;
  s.displayBrightness = b["displayBrightness"] | s.displayBrightness;
  s.trackBedtime = b["trackBedtime"] | s.trackBedtime;
  s.morningQuestion = b["morningQuestion"] | s.morningQuestion;
  s.resetCombo = b["resetCombo"] | s.resetCombo;
  s.soundIdx = b["soundIdx"] | s.soundIdx;
  s.lightOffAfterAlarm = std::min(240, static_cast<int>(b["lightOffAfterAlarm"] | s.lightOffAfterAlarm));
  if (s.soundIdx >= core::kSoundCount) s.soundIdx = core::kSoundCount - 1;
  if (b["lang"].is<const char*>()) {
    std::string l = b["lang"].as<const char*>();
    if (l == "ru" || l == "en") core::setText(s.lang, 3, l);
  }
  if (b["tz"].is<const char*>() && *b["tz"].as<const char*>()) core::setText(s.tz, core::kTzLen, b["tz"].as<const char*>());
  if (b["region"].is<const char*>()) core::setText(s.region, core::kRegionLen, b["region"].as<const char*>());
  if (b["boardLed"].is<bool>() && ctx.boardLedSet) ctx.boardLedSet(b["boardLed"].as<bool>());
  ctx.engine->setSettings(s);
  if (ctx.settingsChanged) ctx.settingsChanged();
}

// POST-команды: имя → обработчик(тело). Время нужно не всем.
static Response post(const std::string& p, JsonObjectConst b) {
  core::Engine& e = *ctx.engine;
  const bool hasTime = now() != 0;
  auto needTime = [&]() { return fail(503, "время ещё не синхронизировано"); };

  if (p == "/api/alarm") {
    if (!hasTime) return needTime();
    core::Alarm base;
    uint8_t id = b["id"] | 0;
    for (const auto& a : e.alarms) if (a.id == id) base = a;
    if (!e.upsertAlarm(alarmFromJson(b, base), now())) return fail(400, "не сохранить (лимит 10 или плохие поля)");
    return ok();
  }
  if (p == "/api/alarm/delete") {
    if (!hasTime) return needTime();
    if (!e.removeAlarm(b["id"] | 0, now())) return fail(404, "нет такого будильника");
    return ok();
  }
  if (p == "/api/alarm/enable") { if (!hasTime) return needTime(); e.setAlarmEnabled(b["id"] | 0, b["on"] | true, now()); return ok(); }
  if (p == "/api/alarm/stop")   { if (!hasTime) return needTime(); e.stopAlarm(now()); return ok(); }
  if (p == "/api/alarm/snooze") { if (!hasTime) return needTime(); e.snoozeAlarm(now()); return ok(); }
  if (p == "/api/dusk") {
    core::Dusk d = e.dusk;
    d.minutes = b["minutes"] | d.minutes; d.music = b["music"] | d.music;
    d.musicIdx = b["musicIdx"] | d.musicIdx; d.volume = b["volume"] | d.volume;
    e.setDuskSettings(d); return ok();
  }
  if (p == "/api/dusk/start") { if (!hasTime) return needTime(); e.startDusk(now()); return ok(); }
  if (p == "/api/dusk/stop")  { e.stopDusk(now(), b["off"] | false); return ok(); }
  if (p == "/api/light/off")  { e.lightOff(); return ok(); }   // погасить в любом режиме (плавно)
  if (p == "/api/settings")   { applySettings(b); return ok(); }
  if (p == "/api/volume")     { e.setVolume(b["v"] | 10); return ok(); }
  if (p == "/api/brightness") { e.setDisplayBrightness(b["v"] | 4); return ok(); }
  if (p == "/api/light") {
    if (e.mode != core::Mode::Idle) return fail(409, "сейчас светом управляет рассвет/закат");
    e.setLight(b["on"] | (b["level"].is<int>() ? true : !e.lightOn), b["level"] | e.lightLevel);
    return ok();
  }
  if (p == "/api/sound")      { e.setSound(b["on"] | !e.soundOn); return ok(); }
  if (p == "/api/preview")    { if (speakerMode()) return fail(409, kNoMp3); e.previewSound(b["idx"] | 0, b["volume"] | 10); return ok(); }
  if (p == "/api/preview/stop") { e.stopPreview(); return ok(); }
  if (p == "/api/morning") {
    if (!e.answerMorning(b["quality"] | -1, b["ease"] | -1, b["note"] | "")) return fail(404, "нет записи сна");
    return ok();
  }
  if (p == "/api/time") {
    uint32_t t = b["t"] | 0;
    if (t < 1700000000u) return fail(400, "плохое время");
    if (ctx.setTime) ctx.setTime(t);
    return ok();
  }
  if (p == "/api/network" || p == "/api/setup") {
    const bool setup = p == "/api/setup";
    if (setup) {
      applySettings(b);
      uint32_t t = b["t"] | 0;
      if (t >= 1700000000u && ctx.setTime) ctx.setTime(t);
    }
    std::string link = b["link"] | "wifi";
    if (link != "wifi" && link != "ble") return fail(400, "link: wifi или ble");
    std::string ssid = b["ssid"] | "";
    if (link == "wifi" && ssid.empty()) return fail(400, "нужна сеть Wi-Fi");
    std::string pass;
    const bool hasPass = b["pass"].is<const char*>();
    if (hasPass) pass = b["pass"].as<const char*>();
    if (!ctx.saveNetwork) return fail(500, "нет менеджера сети");
    ctx.saveNetwork(ssid, hasPass ? &pass : nullptr, link, true);
    return ok();
  }
  if (p == "/api/eq") {   // {"preset":"warm"} | {"hp":150,"body":3,"presence":0,"treble":0} — любые поля → «свой»
    if (!ctx.eqGet || !ctx.eqSet) return fail(500, "нет эквалайзера");
    core::EqParams q = ctx.eqGet();
    if (b["preset"].is<const char*>()) {
      if (!core::eqPreset(b["preset"] | "", q)) return fail(400, "нет такого пресета");
    } else {
      bool any = false;
      auto take = [&](const char* k, auto& v) { if (b[k].is<int>()) { v = static_cast<std::remove_reference_t<decltype(v)>>(b[k] | 0); any = true; } };
      take("hp", q.hp); take("body", q.body); take("presence", q.presence); take("treble", q.treble);
      if (!any) return fail(400, "нужно preset или hp/body/presence/treble");
      q.on = true; std::strncpy(q.preset, "custom", sizeof q.preset);
      core::eqClamp(q);
    }
    ctx.eqSet(q);
    bump();
    return ok();
  }
  if (p == "/api/speaker") {   // {"bt":true,"showEq":true,"lamp":true} — любые поля
    if (!ctx.speakerSet) return fail(500, "нет колонки");
    auto tri = [&](const char* k) { return b[k].isNull() ? -1 : (b[k] | false) ? 1 : 0; };
    ctx.speakerSet(tri("bt"), tri("showEq"), tri("lamp"));
    bump();
    return ok();
  }
  if (p == "/api/display") {   // {"text":"BOSS","sec":10} | {"eq":true,"fps":10,"sec":0} | {"clock":true} | {"h":..,"m":..}
    if (b["clock"] | false) { if (ctx.displayClock) ctx.displayClock(); return ok(); }
    if (b["eq"] | false) { if (ctx.displayEq) ctx.displayEq(b["fps"] | 12, b["sec"] | 0); return ok(); }
    if (b["text"].is<const char*>()) {
      std::string err = ctx.displayText ? ctx.displayText(b["text"].as<const char*>(), b["sec"] | 0) : "нет табло";
      if (!err.empty()) return fail(400, err.c_str());
      return ok();
    }
    if (b["h"].is<int>() && ctx.displayRaw) { ctx.displayRaw(b["h"] | 0, b["m"] | 0); return ok(); }
    return fail(400, "text | eq | clock");
  }
  if (p == "/api/bus/capture") { if (ctx.busCapture) ctx.busCapture(b["ms"] | 3000); return ok(); }
  if (p == "/api/bus") { if (ctx.busCmd) ctx.busCmd(b["baud"] | 0); return ok(); }   // {"baud":N} | {} — измерить
  if (p == "/api/melodies") {   // {} — список | {"format":true,"confirm":true} — очистить каталог
    if (!ctx.melodies) return fail(500, "нет флеш");
    if (b["format"] | false) {
      if (!(b["confirm"] | false)) return fail(400, "нужно {\"confirm\":true}");
      ctx.melodiesFormat();
    }
    if (b["crc"].is<const char*>() && ctx.melodyCrc) {
      JsonDocument d; d["name"] = b["crc"].as<const char*>(); d["crc"] = ctx.melodyCrc(b["crc"].as<const char*>()); return json(d);
    }
    return {200, ctx.melodies()};
  }
  if (p == "/api/play") {   // {"name":"sati","vol":0.4,"loop":false} | {"idx":3} | {"stop":true}
    if (!ctx.play) return fail(500, "нет звука");
    if (b["stop"] | false) { if (ctx.toneStop) ctx.toneStop(); if (ctx.holdAudio) ctx.holdAudio(false); return ok(); }
    if (speakerMode()) return fail(409, kNoMp3);
    if (b["drive"].is<float>() && ctx.drive) ctx.drive(b["drive"] | 1.f);
    if (b["keep"].is<bool>() && ctx.keepAlive) ctx.keepAlive(b["keep"] | true);
    std::string name = b["name"] | "";
    if (name.empty() && !b["idx"].is<int>() && (b["drive"].is<float>() || b["keep"].is<bool>())) return ok();   // только настройки
    int idx = b["idx"] | -1;
    if (name.empty() && idx >= 0 && idx < core::kSoundCount) name = core::kSoundFiles[idx];
    if (!ctx.play(name, b["vol"] | 0.4f, b["loop"] | false)) return fail(404, "нет такой мелодии во флеш");
    if (e.send) e.send(wul::setProperty(0x58, {0x01}));   // открыть аудиотракт часов
    if (ctx.holdAudio) ctx.holdAudio(true);
    return ok();
  }
  if (p == "/api/link") {   // {} — переподключиться к часам | {"passive":true} — отпустить линии | {"passive":false} — вернуться
    // {"auto":false} — не уступать часы iPhone/iPad в доке (по умолчанию уступает сам), {"auto":true} — снова уступать
    if (b["auto"].is<bool>() && ctx.dockAuto) { ctx.dockAuto(b["auto"] | true); return ok(); }
    if (b["passive"].is<bool>() && ctx.passive) { ctx.passive(b["passive"] | false); return ok(); }
    if (ctx.relink) ctx.relink();
    return ok();
  }
  if (p == "/api/diag/shorts") {   // {"confirm":true} — прогон и перезагрузка модуля
    if (!(b["confirm"] | false) || !ctx.shortTest) return fail(400, "нужно {\"confirm\":true}");
    return {200, ctx.shortTest()};
  }
  if (p == "/api/flash") {   // {} — идентификатор чипа | {"test":true} — запись и чтение последнего сектора
    if (!ctx.flash) return fail(500, "нет флеш");
    return {200, ctx.flash(b["test"] | false)};
  }
  if (p == "/api/tone") {   // {"hz":440,"vol":0.3,"sec":10,"ch":"left|right|both","beep":false,"cvol":10} | {"stop":true}
    if (!ctx.tone) return fail(500, "нет звука");
    bool cfg = false;                                    // {"dc":1.5} — постоянка на выходе ЦАП; {"bridge":true} — R = −моно
    if (!b["dc"].isNull() && ctx.toneDc) { ctx.toneDc(b["dc"] | 0.f); cfg = true; }
    if (!b["bridge"].isNull() && ctx.toneBridge) { ctx.toneBridge(b["bridge"] | false); cfg = true; }
    if (cfg && b["hz"].isNull()) return ok();
    if (b["stop"] | false) { ctx.toneStop(); return ok(); }
    if (b["bits"].is<int>() && ctx.toneBits) ctx.toneBits(b["bits"] | 32);
    if (b["pins"].is<JsonArrayConst>() && ctx.toneRepin) ctx.toneRepin(b["pins"][0] | 4, b["pins"][1] | 21, b["pins"][2] | 22);
    if (b["pintest"].is<JsonArrayConst>()) {   // диагностика проводки: [bck, lrck, din] → 1/0 на выводах IO4/IO21/IO22
      const int p[3] = {4, 21, 22};
#ifdef ARDUINO
      for (int i = 0; i < 3; ++i) { pinMode(p[i], OUTPUT); digitalWrite(p[i], (b["pintest"][i] | 0) ? HIGH : LOW); }
#endif
      (void)p;
      return ok();
    }
    if (b["status"] | false) { JsonDocument d; d["ok"] = true; d["audio"] = ctx.toneStatus ? ctx.toneStatus() : ""; return json(d); }
    std::string ch = b["ch"] | "both";
    ctx.tone(b["hz"] | 440, b["vol"] | 0.3f, b["sec"] | 10, ch == "left" ? 1 : ch == "right" ? 2 : 0, b["beep"] | false, b["hz2"] | 0);
    if (ctx.holdAudio) ctx.holdAudio(true);
    if (e.send) {                            // открыть аудиотракт часов (58 01) и громкость, как при прослушивании
      e.send(wul::setProperty(0x58, {0x01}));
      int cv = b["cvol"] | 10;
      e.send(wul::setProperty(0x00, {0x00, static_cast<uint8_t>(cv < 0 ? 0 : cv > 20 ? 20 : cv)}));
    }
    return ok();
  }
  if (p == "/api/lamp") {   // {"probe":true} | {"read":5,"n":1} | {"scan":[0,31]} | {"write":2,"hex":"00","confirm":true}
    if (!ctx.lampProbe) return fail(500, "нет I2C лампы");
    if (b["probe"] | false) return {200, ctx.lampProbe()};
    if (b["read"].is<int>()) return {200, ctx.lampRead(b["read"] | 0, b["n"] | 1)};
    if (b["scan"].is<JsonArrayConst>()) return {200, ctx.lampScan(b["scan"][0] | 0, b["scan"][1] | 0)};
    if (b["write"].is<int>()) {
      if (!(b["confirm"] | false)) return fail(400, "запись — только с \"confirm\":true");
      std::string h = b["hex"] | "";
      std::vector<uint8_t> v;
      for (size_t i = 0; i + 1 < h.size(); i += 2) v.push_back(static_cast<uint8_t>(std::strtoul(h.substr(i, 2).c_str(), nullptr, 16)));
      if (v.empty()) return fail(400, "hex: байты");
      return {200, ctx.lampWrite(b["write"] | 0, v)};
    }
    return fail(400, "probe | read | scan | write");
  }
  if (p == "/api/raw") {                     // диагностика: {"hex":"144C000A"} = cmd + payload (без длины)
    std::string h = b["hex"] | "";
    std::vector<uint8_t> v;
    for (size_t i = 0; i + 1 < h.size(); ) {
      if (h[i] == ' ') { ++i; continue; }
      v.push_back(static_cast<uint8_t>(std::strtoul(h.substr(i, 2).c_str(), nullptr, 16)));
      i += 2;
    }
    if (v.empty() || !e.send) return fail(400, "hex: cmd payload…");
    wul::Frame f; f.cmd = v[0]; f.payload.assign(v.begin() + 1, v.end());
    e.send(f);
    return ok();
  }
  if (p == "/api/reset") {
    if (!(b["confirm"] | false)) return fail(400, "нужно {\"confirm\":true}");
    if (ctx.factoryReset) ctx.factoryReset();
    return ok();
  }
  return fail(404, "нет такого пути");
}

Response handle(const Request& r) {
  if (r.method == "GET") {
    if (r.path == "/api/state")     return state(r);
    if (r.path == "/api/log")       return log();
    if (r.path == "/api/wifi/scan") return scan();
    if (r.path == "/api/events")    return {200, ctx.events ? ctx.events() : "[]"};
    if (r.path == "/api/diag/coredump") return {200, ctx.coredump ? ctx.coredump() : "{}"};
    if (r.path == "/api/link")      return {200, ctx.linkStats ? ctx.linkStats() : "{}"};
    if (r.path == "/api/link/lines") return {200, ctx.detectLines ? ctx.detectLines() : "{}"};
    if (r.path == "/api/link/tap")  return {200, ctx.tap ? ctx.tap(std::strtoul(r.rev.c_str(), nullptr, 10)) : "{}"};
    if (r.path == "/api/bus/edges") return {200, ctx.busEdges ? ctx.busEdges() : "{}"};
    if (r.path == "/api/bus")       return {200, ctx.bus ? ctx.bus(std::strtoul(r.rev.c_str(), nullptr, 10)) : "{}"};
    return fail(404, "нет такого пути");
  }
  if (r.method == "POST") {
    JsonDocument b;
    if (!r.body.empty() && deserializeJson(b, r.body)) return fail(400, "плохой JSON");
    if (b.isNull()) b.to<JsonObject>();
    return post(r.path, b.as<JsonObjectConst>());
  }
  return fail(405, "метод не поддерживается");
}

std::string handleLine(const std::string& req) {
  JsonDocument d;
  if (deserializeJson(d, req)) return "{\"id\":0,\"code\":400}\n";
  Request r;
  r.method = d["m"] | "GET";
  std::string p = d["p"] | "";
  const size_t q = p.find('?');
  if (q != std::string::npos) {
    const std::string query = p.substr(q + 1);
    p.resize(q);
    if (query.rfind("rev=", 0) == 0) r.rev = query.substr(4);
  }
  r.path = p;
  if (!d["b"].isNull()) serializeJson(d["b"], r.body);
  const int id = d["id"] | 0;
  d.clear();
  Response res = handle(r);
  std::string out;                                    // сразу нужного размера: в колонке ОЗУ мало, без удвоений строки
  out.reserve(res.body.size() + 48);
  out += "{\"id\":"; out += std::to_string(id); out += ",\"code\":"; out += std::to_string(res.code);
  if (!res.body.empty()) { out += ",\"body\":"; out += res.body; }
  out += "}\n";
  return out;
}

}  // namespace api
