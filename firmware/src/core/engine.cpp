#include "engine.h"
#include <algorithm>

namespace core {

LocalTime nextOccurrence(const Alarm& a, LocalTime after) {
  const uint32_t day = after / 86400;
  const uint32_t tod = a.hour * 3600u + a.minute * 60u;
  for (uint32_t d = 0; d <= 8; ++d) {
    LocalTime t = (day + d) * 86400u + tod;
    if (t <= after) continue;
    int wd = static_cast<int>((day + d + 3) % 7);
    if (a.days == 0 || ((a.days >> wd) & 1)) return t;
  }
  return 0;
}

const char* modeName(Mode m) {
  switch (m) {
    case Mode::Idle:    return "idle";
    case Mode::Sunrise: return "sunrise";
    case Mode::Ringing: return "ringing";
    case Mode::Snoozed: return "snoozed";
    case Mode::Dusk:    return "dusk";
  }
  return "idle";
}

// ---------- служебное ----------
Alarm* Engine::find(uint8_t id) {
  for (auto& a : alarms) if (a.id == id) return &a;
  return nullptr;
}

LocalTime Engine::doneOf(uint8_t id) const {
  for (size_t i = 0; i + 1 < done_.size(); i += 2) if (done_[i] == id) return done_[i + 1];
  return 0;
}

void Engine::markDone(uint8_t id, LocalTime occ) {
  for (size_t i = 0; i + 1 < done_.size(); i += 2) if (done_[i] == id) { done_[i + 1] = occ; return; }
  done_.push_back(id); done_.push_back(occ);
}

Engine::Next Engine::next(LocalTime now) const {
  Next best;
  for (const auto& a : alarms) {
    if (!a.enabled) continue;
    LocalTime after = std::max<LocalTime>(doneOf(a.id), now >= 60 ? now - 60 : 0);
    LocalTime t = nextOccurrence(a, after);
    if (t && (!best.any || t < best.when)) { best.any = true; best.id = a.id; best.when = t; }
  }
  return best;
}

void Engine::pushNextAlarm(LocalTime now, bool force) {
  if (!connected) return;
  Next n = next(now);
  int h = n.any ? minuteOfDay(n.when) / 60 : -1, m = n.any ? minuteOfDay(n.when) % 60 : -1;
  if (!force && h == pushedH_ && m == pushedM_ && now - lastPushAt_ < 60) return;
  out(n.any ? wul::setAlarm(h, m) : wul::setAlarmOff());    // часам — только ближайший (как приложение)
  pushedH_ = h; pushedM_ = m; lastPushAt_ = now;
}

// ---------- рассвет / звонок / стоп ----------
void Engine::startRamp(uint8_t to, LocalTime now, LocalTime end) {
  ramp_.on = true; ramp_.from = std::max<uint8_t>(1, lightLevel); ramp_.to = to;
  ramp_.start = now; ramp_.end = std::max(end, now + 1); ramp_.last = ramp_.from;
}

void Engine::tickRamp(LocalTime now) {
  if (!ramp_.on) return;
  const int span = static_cast<int>(ramp_.end - ramp_.start), done = static_cast<int>(now - ramp_.start);
  int lvl = ramp_.from + (static_cast<int>(ramp_.to) - ramp_.from) * std::min(done, span) / span;
  if (lvl != ramp_.last) {
    out(wul::setProperty(0x4C, {0x00, static_cast<uint8_t>(lvl)}));   // 4C 00 LL — часы сами сглаживают шаг
    ramp_.last = static_cast<uint8_t>(lvl); lightLevel = ramp_.last;
  }
  if (now >= ramp_.end) ramp_.on = false;
}

void Engine::startSunrise(const Alarm& a, LocalTime occ, LocalTime now) {
  if (mode == Mode::Dusk) stopDusk(now);
  if (lightOn) {
    // Лампа уже горит: кривая часов сначала уронила бы её до минимума (проверено вживую 2026-10-17).
    // Ведём сами от текущей яркости до заданной к моменту звонка; ярче нужного — не трогаем.
    if (lightLevel < a.intensity) startRamp(a.intensity, now, occ);
  } else {
    uint32_t left = (occ - now + 59) / 60;
    left = std::max<uint32_t>(1, std::min<uint32_t>(left, 255));
    out(wul::setDawnProfile(static_cast<uint8_t>(left), a.intensity)); // 4E [мин до звонка, яркость]
    out(wul::dawnStart());                                              // 4F FF
  }
  out(wul::setAlarm(a.hour, a.minute));                                 // 0C — рассвет привязан к будильнику
  pushedH_ = a.hour; pushedM_ = a.minute; lastPushAt_ = now;
  mode = Mode::Sunrise; activeAlarmId = a.id; activeOcc = occ;
  changed();
}

void Engine::ring(const Alarm& a, LocalTime now) {
  if (mode == Mode::Dusk) stopDusk(now);
  if (a.light && mode != Mode::Sunrise && mode != Mode::Snoozed) {     // рассвета не было (перезагрузка и т.п.)
    if (lightOn) { if (lightLevel < a.intensity) startRamp(a.intensity, now, now + 60); }
    else { out(wul::setDawnProfile(1, a.intensity)); out(wul::dawnStart()); }
  }
  if (a.sound) {
    out(wul::setProperty(0x58, {0x01}));                                // аудиотракт, как приложение
    out(wul::setAlarmVolume(a.volume));                                 // 0x14 ALMVOL
    if (playSound) playSound(a.soundIdx, a.volume);
  }
  mode = Mode::Ringing; activeAlarmId = a.id; ringStart = now;
  changed();
}

void Engine::finishAlarm(LocalTime now, bool dismissClock) {
  ramp_.on = false;                                                     // своя рампа — замереть на текущей яркости
  if (dismissClock) out(wul::alarmDismiss());                           // 4F 11 = пауза кривой: лампа остаётся гореть
  if (stopSound) stopSound();
  if (Alarm* a = find(activeAlarmId)) {
    markDone(a->id, activeOcc);
    if (a->days == 0) { a->enabled = false; if (persistAlarms) persistAlarms(); }   // Once
  }
  if (settings.trackBedtime && (mode == Mode::Ringing || mode == Mode::Snoozed)) {
    SleepEntry* open = nullptr;
    if (!log.empty() && log.back().end == 0 && log.back().start && now - log.back().start < 24 * 3600)
      open = &log.back();
    if (!open) { log.push_back(SleepEntry{}); open = &log.back(); open->snoozes = snoozes_; }
    open->end = now;
    if (log.size() > static_cast<size_t>(kMaxLog)) log.erase(log.begin());
    if (persistLog) persistLog();
  }
  if (settings.morningQuestion && (mode == Mode::Ringing || mode == Mode::Snoozed)) questionUntil = now + 30 * 60;
  if (settings.lightOffAfterAlarm) autoOffAt_ = now + settings.lightOffAfterAlarm * 60u;   // потом погасить свет
  mode = Mode::Idle; activeAlarmId = 0; activeOcc = 0; snoozeUntil = 0; snoozes_ = 0;
  pushNextAlarm(now, true);
  changed();
}

void Engine::stopAlarm(LocalTime now) {
  if (mode == Mode::Sunrise || mode == Mode::Ringing || mode == Mode::Snoozed) finishAlarm(now, true);
}

void Engine::snoozeAlarm(LocalTime now) {
  if (mode != Mode::Ringing) return;
  out(wul::snooze());                                                   // 56 01
  if (stopSound) stopSound();
  const Alarm* a = find(activeAlarmId);
  snoozeUntil = now + (a ? a->snoozeMin : 9) * 60u;
  if (settings.trackBedtime && !log.empty() && log.back().end == 0) log.back().snoozes++;
  else ++snoozes_;                                                      // записи сна нет (не было заката) — посчитаем сами
  mode = Mode::Snoozed;
  changed();
}

// ---------- закат ----------
void Engine::startDusk(LocalTime now) {
  if (mode != Mode::Idle) return;
  out(wul::setDuskProfile(dusk.minutes, 0xFF));                         // 49 [мин, FF]
  out(wul::duskStart());                                                // 4F EE
  if (dusk.music) {
    out(wul::audioPlay());                                              // 58 02
    out(wul::setVolume(dusk.volume));
    volume = dusk.volume;
    if (playSound) playSound(dusk.musicIdx, dusk.volume);
  }
  mode = Mode::Dusk; duskEnd = now + dusk.minutes * 60u; lastMusicAt_ = now;
  if (settings.trackBedtime) {
    SleepEntry e; e.start = now;
    log.push_back(e);
    if (log.size() > static_cast<size_t>(kMaxLog)) log.erase(log.begin());
    if (persistLog) persistLog();
  }
  changed();
}

void Engine::stopDusk(LocalTime /*now*/, bool lightOffToo) {
  if (mode != Mode::Dusk) return;
  out(wul::curveStop());                                                // 4F 00
  if (dusk.music) {
    out(wul::audioStop());                                              // 58 00
    out(wul::setVolume(0));
    volume = 0;
    if (stopSound) stopSound();
  }
  mode = Mode::Idle; duskEnd = 0;
  if (lightOffToo) lightOff();                                          // «Погасить»: иначе лампа замирает на текущем уровне
  changed();
}

// ---------- настройки / будильники ----------
void Engine::setVolume(uint8_t v) {
  volume = std::min<uint8_t>(v, 20);
  out(wul::setVolume(volume));
  changed();
}

void Engine::setDisplayBrightness(uint8_t v) {
  settings.displayBrightness = std::min<uint8_t>(v, 7);
  out(wul::setDisplayBrightness(settings.displayBrightness));
  if (persistSettings) persistSettings();
  changed();
}

void Engine::setDuskSettings(const Dusk& d) {
  dusk = d;
  dusk.minutes = std::max<uint8_t>(5, std::min<uint8_t>(60, static_cast<uint8_t>((d.minutes + 2) / 5 * 5)));
  dusk.musicIdx = std::min<uint8_t>(d.musicIdx, kSoundCount - 1);
  dusk.volume = std::min<uint8_t>(d.volume, 20);
  if (persistSettings) persistSettings();
  changed();
}

void Engine::setSettings(const Settings& s) {
  bool bright = s.displayBrightness != settings.displayBrightness;
  settings = s;
  settings.displayBrightness = std::min<uint8_t>(s.displayBrightness, 7);
  if (bright) out(wul::setDisplayBrightness(settings.displayBrightness));
  if (persistSettings) persistSettings();
  changed();
}

void Engine::previewSound(uint8_t idx, uint8_t vol) {
  out(wul::setProperty(0x58, {0x01}));
  out(wul::setVolume(std::min<uint8_t>(vol, 20)));
  if (playSound) playSound(std::min<uint8_t>(idx, kSoundCount - 1), vol);
}

void Engine::stopPreview() {
  out(wul::audioStop());
  if (stopSound) stopSound();
}

bool Engine::upsertAlarm(Alarm a, LocalTime now) {
  if (!validate(a)) return false;
  if (a.id == 0 || !find(a.id)) {
    if (alarms.size() >= static_cast<size_t>(kMaxAlarms)) return false;
    if (a.id == 0) { uint8_t id = 1; while (find(id)) ++id; a.id = id; }
    alarms.push_back(a);
  } else {
    *find(a.id) = a;
    markDone(a.id, 0);                       // изменили — заново считаем ближайшее срабатывание
  }
  if (persistAlarms) persistAlarms();
  pushNextAlarm(now, true);
  changed();
  return true;
}

bool Engine::removeAlarm(uint8_t id, LocalTime now) {
  if (!find(id)) return false;
  if (activeAlarmId == id && mode != Mode::Idle && mode != Mode::Dusk) finishAlarm(now, true);
  alarms.erase(std::remove_if(alarms.begin(), alarms.end(), [&](const Alarm& a) { return a.id == id; }),
               alarms.end());
  if (persistAlarms) persistAlarms();
  pushNextAlarm(now, true);
  changed();
  return true;
}

void Engine::setAlarmEnabled(uint8_t id, bool on, LocalTime now) {
  Alarm* a = find(id);
  if (!a) return;
  if (!on && activeAlarmId == id && mode != Mode::Idle && mode != Mode::Dusk) finishAlarm(now, true);
  a->enabled = on;
  markDone(id, 0);
  if (persistAlarms) persistAlarms();
  pushNextAlarm(now, true);
  changed();
}

bool Engine::answerMorning(int quality, int ease, const char* note) {
  for (auto it = log.rbegin(); it != log.rend(); ++it) {
    if (!it->end) continue;
    it->quality = static_cast<int8_t>(std::max(-1, std::min(4, quality)));
    it->ease = static_cast<int8_t>(std::max(-1, std::min(4, ease)));
    if (note) setNote(*it, note);
    questionUntil = 0;
    if (persistLog) persistLog();
    changed();
    return true;
  }
  return false;
}

// ---------- входящее от часов ----------
void Engine::onConnected(LocalTime now) {
  connected = true; testMode = false;                                   // из test часы отвечают только после выхода
  pushedH_ = pushedM_ = -2;
  out(wul::setDisplayBrightness(settings.displayBrightness));
  out(wul::getProperty(0x00));
  pushNextAlarm(now, true);
  changed();
}

void Engine::onClockValue(uint8_t prop, const std::vector<uint8_t>& v) {
  if (prop == 0x4C && v.size() >= 4) {
    clockLightMode = v[1]; clockLightLevel = v[3];
    lightOn = v[1] != 0 && v[3] != 0;                                   // 00 выкл, 01 горит, 13 звонит; 01 с уровнем 0 — погашена (4C 00 00)
    if (v[3] >= 1 && v[3] <= 20) lightLevel = v[3];
  }
  else if (prop == 0x57 && !v.empty()) curveStatus = v.back();
  else if (prop == 0x00 && v.size() >= 2) volume = v[1];
  else return;
  changed();
}

// Карта снята вживую 2026-10-16 (журнал /api/events): одно событие на нажатие, без «отпустил».
const char* Engine::buttonName(uint8_t code) {
  switch (code) {
    case 0x0A: return "light";   case 0x08: return "up";      case 0x09: return "down";
    case 0x07: return "sound";   case 0x05: return "volUp";   case 0x06: return "volDown";
    case 0x04: return "snooze";  case 0x0B: return "test";
    default:   return nullptr;
  }
}

void Engine::setSound(bool on) {
  padSound = true;
  if (on == soundOn) { changed(); return; }
  soundOn = on;
  if (on) {
    if (volume == 0) volume = 8;
    out(wul::setProperty(0x58, {0x01}));                                // тракт звука дока
    out(wul::setVolume(volume));
    if (playSound) playSound(settings.soundIdx, volume);
  } else {
    out(wul::audioStop());
    if (stopSound) stopSound();
  }
  changed();
}

void Engine::lightOff() {
  // Как физкнопка: часы пишут лампе (I2C 0x51) уровень 0 + «перейти сейчас» — гаснет полностью за ~3 с.
  // Кривую (рассвет/закат/ручной EE) сначала останавливаем, иначе она продолжит вести уровень.
  ramp_.on = false; autoOffAt_ = 0;
  out(wul::curveStop());                                                // 4F 00
  out(wul::setProperty(0x4C, {0x00, 0x00}));                            // 4C 00 00
  lightOn = false;
  changed();
}

void Engine::setLight(bool on, uint8_t level) {
  level = std::max<uint8_t>(1, std::min<uint8_t>(level ? level : lightLevel, 20));
  if (mode != Mode::Idle) return;                                       // во время рассвета/заката свет ведёт кривая
  ramp_.on = false; autoOffAt_ = 0;
  padSound = false;
  if (on) {
    if (!lightOn) {
      out(wul::setDuskProfile(60, level));                              // 49 3C LL — зажечь на уровне LL
      out(wul::duskStart());                                            // 4F EE
    }
    out(wul::setProperty(0x4C, {0x00, level}));                         // 4C 00 LL — держать уровень
    lightOn = true; lightLevel = level;
  } else if (lightOn) {
    lightOff();
    return;
  }
  changed();
}

void Engine::onButton(uint8_t code, uint8_t level, LocalTime now, uint32_t ms) {
  lastButton = code; lastButtonLevel = level; lastButtonAt = now;
  buttons.push_back({ms, code, level});
  if (buttons.size() > kButtonLog) buttons.erase(buttons.begin());
  const bool alarm = mode == Mode::Sunrise || mode == Mode::Ringing || mode == Mode::Snoozed;

  switch (code) {
    case 0x0A:                                                          // Свет: часы переключили лампу сами
      lightOn = level != 0; padSound = false; autoOffAt_ = 0;
      if (alarm) stopAlarm(now);                                        // проснулся — будильник выключаем
      else if (mode == Mode::Dusk) stopDusk(now);
      break;
    case 0x08: case 0x09:                                               // +/− яркости: level = новая яркость
      if (level >= 1 && level <= 20) lightLevel = level;
      lightOn = true; padSound = false; autoOffAt_ = 0;
      break;
    case 0x07:                                                          // Звук: часы только сообщают
      padSound = true;
      if (alarm) stopAlarm(now);
      else if (mode == Mode::Dusk) {
        if (dusk.music) { out(wul::audioStop()); if (stopSound) stopSound(); dusk.music = false; }
      } else setSound(!soundOn);
      break;
    case 0x05: case 0x06:
      padSound = true;
      // звук идёт — часы меняют громкость сами и присылают новую (1..20): только запоминаем, иначе спорим с ними;
      // звука нет — level 0, громкость ведём мы
      if (level >= 1 && level <= 20) { volume = level; changed(); }
      else setVolume(code == 0x05 ? std::min(20, volume + 1) : std::max(0, volume - 1));
      break;
    case 0x04:                                                          // Снуз
      if (mode == Mode::Ringing) { snoozeAlarm(now); break; }
      if (alarm || !settings.resetCombo) break;
      snoozePresses_.push_back(ms);
      while (!snoozePresses_.empty() && ms - snoozePresses_.front() > kComboWindowMs)
        snoozePresses_.erase(snoozePresses_.begin());
      if (snoozePresses_.size() >= static_cast<size_t>(kComboPresses)) {
        snoozePresses_.clear();
        for (int i = 0; i < 3; ++i) {                                   // табло мигает 3 раза — «понял»
          out(wul::setDisplayBrightness(0));
          out(wul::setDisplayBrightness(7));
        }
        out(wul::setDisplayBrightness(settings.displayBrightness));
        if (factoryReset) factoryReset();
      }
      break;
    case 0x0B:                                                          // часы вошли/вышли из «test»
      testMode = !testMode;
      break;
    default: break;
  }
  changed();
}

// ---------- такт ----------
void Engine::tick(LocalTime now) {
  tickRamp(now);
  if (autoOffAt_ && now >= autoOffAt_) {
    autoOffAt_ = 0;
    if (mode == Mode::Idle && lightOn) lightOff();
  }
  if (connected && now - lastPollAt_ >= 30) {                          // фоновый опрос, как приложение
    lastPollAt_ = now;
    out(wul::getProperty(0x4C));
    out(wul::getProperty(0x57));
  }
  if (questionUntil && now >= questionUntil) { questionUntil = 0; changed(); }

  switch (mode) {
    case Mode::Ringing:
      if (now - ringStart >= static_cast<LocalTime>(kRingTimeoutSec)) finishAlarm(now, true);
      return;
    case Mode::Snoozed:
      if (now >= snoozeUntil) { if (const Alarm* a = find(activeAlarmId)) ring(*a, now); else finishAlarm(now, true); }
      return;
    case Mode::Dusk:
      if (dusk.music && now - lastMusicAt_ >= 1) { lastMusicAt_ = now; out(wul::audioPlay()); }  // держим звук, как приложение
      if (now >= duskEnd) {
        if (dusk.music) { out(wul::audioStop()); if (stopSound) stopSound(); }
        mode = Mode::Idle; duskEnd = 0;
        changed();
      }
      break;
    default: break;
  }

  Next n = next(now);
  if (n.any && mode != Mode::Ringing && mode != Mode::Snoozed) {
    const Alarm* a = find(n.id);
    if (a && now >= n.when) {
      if (a->light || a->sound) { activeAlarmId = a->id; activeOcc = n.when; ring(*a, now); }
      else markDone(a->id, n.when);
      return;
    }
    if (a && a->light && !(mode == Mode::Sunrise && activeOcc == n.when) &&
        now + a->sunriseMin * 60u >= n.when)
      startSunrise(*a, n.when, now);
  }
  pushNextAlarm(now, false);
}

}  // namespace core
