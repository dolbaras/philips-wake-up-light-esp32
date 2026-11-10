// Движок «вместо приложения»: команды часам должны совпадать с тем, что слало оригинальное приложение
// (живой лог sniff_log.txt).
#include "engine.h"
#include <cstdio>
#include <string>
#include <vector>
using namespace core;
static int fails = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d  %s\n",__FILE__,__LINE__,#c); ++fails; } }while(0)

static std::string hex(const wul::Frame& f) {
  std::string s; char b[3];
  for (uint8_t x : wul::encode(f)) { snprintf(b, 3, "%02X", x); s += b; }
  return s;
}
// локальное время «как UTC» (алгоритм days_from_civil)
static LocalTime T(int y, int m, int d, int h, int mi, int s = 0) {
  y -= m <= 2;
  int era = y / 400, yoe = y - era * 400;
  int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097L + doe - 719468;
  return static_cast<LocalTime>(days * 86400 + h * 3600 + mi * 60 + s);
}
static bool has(const std::vector<std::string>& v, const std::string& x) {
  for (auto& s : v) if (s == x) return true;
  return false;
}

int main() {
  // 2026-10-16 — пятница
  CHECK(weekdayOf(T(2026, 10, 16, 6, 0)) == 4);

  // nextOccurrence: Once и по дням недели
  Alarm once; once.hour = 7; once.minute = 0;
  CHECK(nextOccurrence(once, T(2026, 10, 16, 6, 0)) == T(2026, 10, 16, 7, 0));
  CHECK(nextOccurrence(once, T(2026, 10, 16, 7, 0)) == T(2026, 10, 17, 7, 0));
  Alarm wk = once; wk.days = 0x1F;                       // Пн–Пт
  CHECK(nextOccurrence(wk, T(2026, 10, 16, 8, 0)) == T(2026, 10, 19, 7, 0));   // пт после 7 → пн

  // ---- полный цикл будильника ----
  std::vector<std::string> out;
  int played = 0, stopped = 0;
  Engine e;
  e.send = [&](const wul::Frame& f) { out.push_back(hex(f)); };
  e.playSound = [&](uint8_t, uint8_t) { ++played; };
  e.stopSound = [&]() { ++stopped; };

  Alarm a; a.hour = 7; a.minute = 0; a.sunriseMin = 30; a.intensity = 20; a.volume = 10; a.snoozeMin = 9;
  CHECK(e.upsertAlarm(a, T(2026, 10, 16, 6, 0)));
  CHECK(e.alarms.size() == 1 && e.alarms[0].id == 1);

  e.onConnected(T(2026, 10, 16, 6, 0));
  CHECK(has(out, "04140C0700"));                          // часам — ближайший будильник
  out.clear();

  e.tick(T(2026, 10, 16, 6, 29, 30));
  CHECK(e.mode == Mode::Idle);
  e.tick(T(2026, 10, 16, 6, 30));                         // старт рассвета за 30 мин
  CHECK(e.mode == Mode::Sunrise);
  CHECK(has(out, "04144E1E14"));                          // 4E = 30 мин, яркость 20
  CHECK(has(out, "03144FFF"));                            // 4F FF
  out.clear();
  e.tick(T(2026, 10, 16, 6, 45));
  CHECK(!has(out, "03144FFF"));                           // рассвет не перезапускается

  e.tick(T(2026, 10, 16, 7, 0, 1));                       // звонок
  CHECK(e.mode == Mode::Ringing);
  CHECK(has(out, "03145801") && has(out, "041414000A"));  // аудиотракт + ALMVOL 10
  CHECK(played == 1);
  out.clear();

  e.snoozeAlarm(T(2026, 10, 16, 7, 1));
  CHECK(e.mode == Mode::Snoozed && has(out, "03145601"));
  e.tick(T(2026, 10, 16, 7, 9));
  CHECK(e.mode == Mode::Snoozed);
  e.tick(T(2026, 10, 16, 7, 10));                         // через 9 мин — снова звонок
  CHECK(e.mode == Mode::Ringing && played == 2);
  out.clear();

  e.stopAlarm(T(2026, 10, 16, 7, 12));
  CHECK(e.mode == Mode::Idle);
  CHECK(has(out, "03144F11"));                            // Stop alarm
  CHECK(!e.alarms[0].enabled);                            // Once выключился
  CHECK(has(out, "04140CAAAA"));                          // будильников больше нет
  CHECK(e.log.size() == 1 && e.log[0].end == T(2026, 10, 16, 7, 12) && e.log[0].start == 0);
  CHECK(e.log[0].snoozes == 1);                            // снуз без записи заката тоже посчитан
  CHECK(e.questionUntil == T(2026, 10, 16, 7, 42));
  CHECK(e.answerMorning(3, 2, "ok") && e.log[0].quality == 3 && e.questionUntil == 0);

  // повторный проход того же утра не должен звенеть снова
  e.setAlarmEnabled(1, true, T(2026, 10, 16, 7, 13));
  out.clear();
  e.tick(T(2026, 10, 16, 7, 14));
  CHECK(e.mode == Mode::Idle && has(out, "04140C0700"));  // теперь ближайший — завтра 07:00

  // ---- закат ----
  e.dusk.minutes = 15; e.dusk.music = true; e.dusk.volume = 8;
  out.clear();
  e.startDusk(T(2026, 10, 16, 22, 0));
  CHECK(e.mode == Mode::Dusk);
  CHECK(has(out, "0414490FFF") && has(out, "03144FEE") && has(out, "03145802") && has(out, "0414000008"));
  CHECK(e.log.back().start == T(2026, 10, 16, 22, 0));
  e.tick(T(2026, 10, 16, 22, 15));
  CHECK(e.mode == Mode::Idle && has(out, "03145800"));
  // утром стоп будильника закрывает ту же запись сна
  e.tick(T(2026, 10, 17, 6, 30));
  CHECK(e.mode == Mode::Sunrise);
  e.tick(T(2026, 10, 17, 7, 0));
  e.snoozeAlarm(T(2026, 10, 17, 7, 1));
  e.stopAlarm(T(2026, 10, 17, 7, 5));
  CHECK(e.log.back().start == T(2026, 10, 16, 22, 0) && e.log.back().end == T(2026, 10, 17, 7, 5));
  CHECK(e.log.back().snoozes == 1);                       // раньше затирался нулём при стопе

  // остановка рассвета до звонка — звонка не будет
  e.setAlarmEnabled(1, true, T(2026, 10, 17, 8, 0));
  e.tick(T(2026, 10, 18, 6, 30));
  CHECK(e.mode == Mode::Sunrise);
  e.stopAlarm(T(2026, 10, 18, 6, 40));
  e.tick(T(2026, 10, 18, 7, 0));
  CHECK(e.mode == Mode::Idle);

  // сериализация
  auto blob = packAlarms(e.alarms);
  auto back = unpackAlarms(blob.data(), blob.size());
  CHECK(back.size() == 1 && back[0].hour == 7 && back[0].intensity == 20 && std::string(back[0].label) == "Alarm");
  auto lb = packLog(e.log);
  auto lback = unpackLog(lb.data(), lb.size());
  CHECK(lback.size() == e.log.size() && lback[0].quality == 3 && std::string(lback[0].note) == "ok");

  // ---- кнопки часов (карта снята вживую 2026-10-16) ----
  {
    Engine b;
    std::vector<std::string> bo;
    int resets = 0, snd = 0;
    b.send = [&](const wul::Frame& f) { bo.push_back(hex(f)); };
    b.playSound = [&](uint8_t idx, uint8_t) { snd = idx + 1; };
    b.factoryReset = [&]() { ++resets; };
    LocalTime t0 = T(2026, 10, 16, 12, 0);
    b.volume = 10;
    b.onButton(0x0A, 0, t0, 1000);  CHECK(!b.lightOn);                       // Свет: выкл
    b.onButton(0x0A, 1, t0, 2000);  CHECK(b.lightOn);                        // Свет: вкл
    b.onButton(0x08, 4, t0, 3000);  CHECK(b.lightLevel == 4);                // яркость +
    b.onButton(0x05, 0, t0, 4000);  CHECK(b.volume == 11 && has(bo, "041400000B"));   // громкость + (level 0 не громкость!)
    b.onButton(0x06, 0, t0, 5000);  CHECK(b.volume == 10);
    bo.clear();
    b.onButton(0x05, 14, t0, 5500); CHECK(b.volume == 14 && bo.empty());          // звук идёт: часы сами выставили 14
    b.onButton(0x07, 0, t0, 6000);  CHECK(b.soundOn && snd == 4);            // Звук: мелодия Ocean
    b.onButton(0x07, 0, t0, 7000);  CHECK(!b.soundOn);
    b.onButton(0x0B, 0, t0, 8000);  CHECK(b.testMode);                       // test вкл/выкл
    b.onButton(0x0B, 0, t0, 9000);  CHECK(!b.testMode);
    b.testMode = true; b.onConnected(t0); CHECK(!b.testMode);              // вышли из test передёргиванием питания
    CHECK(b.buttons.size() == 10 && std::string(Engine::buttonName(b.buttons.back().code)) == "test");
    std::vector<uint8_t> raw{0x00, 0x01, 0x00, 0x07};
    b.onClockValue(0x4C, raw);      CHECK(b.lightOn && b.lightLevel == 7);
    std::vector<uint8_t> dark{0x00, 0x01, 0x00, 0x00};
    b.onClockValue(0x4C, dark);     CHECK(!b.lightOn && b.lightLevel == 7);   // после 4C 00 00 часы отвечают «01, уровень 0»
    b.onClockValue(0x4C, raw);
    // Снуз ×10 за 6 с: медленно — нет, ×5 быстро (прежнее сочетание) — нет, ×10 быстро — сброс с миганием табло
    for (int i = 0; i < 10; ++i) b.onButton(0x04, 0, t0, 20000 + i * 1000);
    CHECK(resets == 0);
    for (int i = 0; i < 5; ++i) b.onButton(0x04, 0, t0, 32000 + i * 300);
    CHECK(resets == 0);
    bo.clear();
    for (int i = 0; i < 10; ++i) b.onButton(0x04, 0, t0, 40000 + i * 500);
    CHECK(resets == 1 && has(bo, "0414420000") && has(bo, "0414420007"));
    b.settings.resetCombo = false;
    for (int i = 0; i < 10; ++i) b.onButton(0x04, 0, t0, 60000 + i * 300);
    CHECK(resets == 1);
    // во время звонка: Снуз = повтор, Звук = выключить
    Alarm r; r.hour = 12; r.minute = 30; r.light = false;
    b.upsertAlarm(r, t0);
    b.tick(T(2026, 10, 16, 12, 30, 1));
    CHECK(b.mode == Mode::Ringing);
    b.onButton(0x04, 0, T(2026, 10, 16, 12, 31), 70000);
    CHECK(b.mode == Mode::Snoozed);
    b.tick(T(2026, 10, 16, 12, 40));
    CHECK(b.mode == Mode::Ringing);
    b.onButton(0x07, 0, T(2026, 10, 16, 12, 41), 80000);
    CHECK(b.mode == Mode::Idle && !b.soundOn);
    // рассвет при уже горящей лампе: без кривой часов (она роняет лампу до минимума), своя рампа 4C
    {
      Engine r2;
      std::vector<std::string> ro;
      r2.send = [&](const wul::Frame& f) { ro.push_back(hex(f)); };
      r2.connected = true;
      std::vector<uint8_t> lit{0x00, 0x05, 0x00, 0x05};
      r2.onClockValue(0x4C, lit);                                  // лампа горит на 5
      Alarm w; w.hour = 7; w.minute = 0; w.sunriseMin = 30; w.intensity = 20;
      r2.upsertAlarm(w, T(2026, 10, 18, 6, 0));
      ro.clear();
      r2.tick(T(2026, 10, 18, 6, 30));
      CHECK(r2.mode == Mode::Sunrise && !has(ro, "03144FFF"));      // кривая часов не запускалась
      r2.tick(T(2026, 10, 18, 6, 45));
      CHECK(has(ro, "04144C000C"));                                 // на полпути: 5 → 12
      r2.tick(T(2026, 10, 18, 7, 0));
      CHECK(has(ro, "04144C0014"));                                 // к звонку — 20
      Engine r3;                                                    // лампа ярче нужного — не трогаем
      std::vector<std::string> ro3;
      r3.send = [&](const wul::Frame& f) { ro3.push_back(hex(f)); };
      r3.connected = true;
      std::vector<uint8_t> bright{0x00, 0x14, 0x00, 0x14};
      r3.onClockValue(0x4C, bright);
      Alarm w3 = w; w3.intensity = 10;
      r3.upsertAlarm(w3, T(2026, 10, 18, 6, 0));
      ro3.clear();
      r3.tick(T(2026, 10, 18, 6, 30));
      r3.tick(T(2026, 10, 18, 6, 50));
      bool any4C = false; for (auto& x : ro3) if (x.rfind("04144C", 0) == 0) any4C = true;
      CHECK(r3.mode == Mode::Sunrise && !has(ro3, "03144FFF") && !any4C);
    }
    // погасить свет через N минут после будильника; ручное касание света отменяет
    {
      Engine a4;
      std::vector<std::string> ao;
      a4.send = [&](const wul::Frame& f) { ao.push_back(hex(f)); };
      a4.connected = true;
      a4.settings.lightOffAfterAlarm = 10;
      Alarm w; w.hour = 7; w.minute = 0; w.light = false; w.sound = true;
      a4.upsertAlarm(w, T(2026, 10, 18, 6, 0));
      a4.tick(T(2026, 10, 18, 7, 0, 1));
      CHECK(a4.mode == Mode::Ringing);
      std::vector<uint8_t> lit{0x00, 0x14, 0x00, 0x14};
      a4.onClockValue(0x4C, lit);
      a4.stopAlarm(T(2026, 10, 18, 7, 2));
      ao.clear();
      a4.tick(T(2026, 10, 18, 7, 11));
      CHECK(!has(ao, "0414490101"));                                // рано
      a4.tick(T(2026, 10, 18, 7, 12));
      CHECK(has(ao, "03144F00") && has(ao, "04144C0000") && !a4.lightOn);       // гасим как физкнопка
      // закат: «Остановить» — свет остаётся, «Погасить» — гаснет
      Engine d;
      std::vector<std::string> dout;
      d.send = [&](const wul::Frame& f) { dout.push_back(hex(f)); };
      d.startDusk(T(2026, 10, 18, 22, 0));
      dout.clear();
      d.stopDusk(T(2026, 10, 18, 22, 3));
      CHECK(d.mode == Mode::Idle && has(dout, "03144F00") && !has(dout, "04144C0000"));
      d.startDusk(T(2026, 10, 18, 22, 5));
      dout.clear();
      d.stopDusk(T(2026, 10, 18, 22, 6), true);
      CHECK(has(dout, "03144F00") && has(dout, "04144C0000"));
      Settings s4; s4.lightOffAfterAlarm = 30; Dusk d4;
      auto b4 = packSettings(s4, d4); Settings s5; Dusk d5;
      CHECK(unpackSettings(b4.data(), b4.size(), s5, d5) && s5.lightOffAfterAlarm == 30);
    }
    // настройки v2 туда-обратно, и чтение старого v1
    Settings s; s.resetCombo = false; s.soundIdx = 5; setText(s.lang, 3, "en"); setText(s.tz, kTzLen, "CET-1CEST");
    Dusk d; d.minutes = 25;
    auto sb = packSettings(s, d);
    Settings s2; Dusk d2;
    CHECK(unpackSettings(sb.data(), sb.size(), s2, d2));
    CHECK(!s2.resetCombo && s2.soundIdx == 5 && std::string(s2.lang) == "en" && std::string(s2.tz) == "CET-1CEST" && d2.minutes == 25);
    std::vector<uint8_t> v1{1, 30, 1, 2, 9, 6, 0, 1};
    Settings s3; Dusk d3;
    CHECK(unpackSettings(v1.data(), v1.size(), s3, d3) && d3.minutes == 30 && s3.displayBrightness == 6 && std::string(s3.lang) == "ru");
  }

  printf(fails ? "\n%d CHECK(s) FAILED\n" : "\nДВИЖОК: все сценарии совпали с поведением приложения\n", fails);
  return fails ? 1 : 0;
}
