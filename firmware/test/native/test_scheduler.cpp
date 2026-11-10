// Проверка логики планировщика на хосте.
// g++ -std=c++17 -I src/scheduler test/native/test_scheduler.cpp src/scheduler/scheduler.cpp -o /tmp/ts && /tmp/ts
#include "scheduler.h"
#include <cstdio>
#include <string>
using namespace sched;
static int fails = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d  %s\n",__FILE__,__LINE__,#c); ++fails;} }while(0)

static TimeNow at(int h, int m, int wd, uint32_t em) { return TimeNow{h, m, wd, em}; }

int main() {
  Alarm a; a.id = 1; a.enabled = true; a.hour = 7; a.minute = 0;
  a.weekdays = 0;           // каждый день
  a.sunriseMinutes = 30; a.light = true; a.sound = true;

  // звук ровно в 07:00
  CHECK(shouldFireSound(a, at(7, 0, 2, 100)));
  CHECK(!shouldFireSound(a, at(7, 1, 2, 100)));
  // свет за 30 мин → 06:30
  CHECK(shouldFireLight(a, at(6, 30, 2, 100)));
  CHECK(!shouldFireLight(a, at(7, 0, 2, 100)));

  // рассвет через полночь: будильник 00:10, рассвет 30 мин → свет в 23:40
  Alarm b = a; b.hour = 0; b.minute = 10;
  CHECK(shouldFireLight(b, at(23, 40, 0, 1)));

  // дни недели: только Пн(bit0) и Ср(bit2)
  Alarm c = a; c.weekdays = (1 << 0) | (1 << 2);
  CHECK(shouldFireSound(c, at(7, 0, 0, 1)));   // Пн
  CHECK(!shouldFireSound(c, at(7, 0, 1, 1)));  // Вт
  CHECK(shouldFireSound(c, at(7, 0, 2, 1)));   // Ср

  // выключенный/без звука не срабатывает
  Alarm d = a; d.enabled = false; CHECK(!shouldFireSound(d, at(7, 0, 2, 1)));
  Alarm e = a; e.sound = false;   CHECK(!shouldFireSound(e, at(7, 0, 2, 1)));

  // рампа рассвета 0..max линейно
  CHECK(sunriseLevelAt(a, 0, 20) == 0);
  CHECK(sunriseLevelAt(a, 30 * 60, 20) == 20);
  CHECK(sunriseLevelAt(a, 15 * 60, 20) == 10);

  // антидребезг: Scheduler срабатывает один раз в минуту
  Scheduler s; int lights = 0, sounds = 0;
  s.onFireLight = [&](const Alarm&){ ++lights; };
  s.onFireSound = [&](const Alarm&){ ++sounds; };
  CHECK(s.upsert(a));
  for (int k = 0; k < 5; ++k) s.tick(at(7, 0, 2, 12345));  // та же минута 5 раз
  CHECK(sounds == 1);
  s.tick(at(7, 0, 2, 12346));                              // новая минута
  CHECK(sounds == 2);

  // round-trip сериализации
  std::vector<Alarm> v{a, c};
  auto blob = serialize(v);
  auto back = deserialize(blob.data(), blob.size());
  CHECK(back.size() == 2);
  CHECK(back[0].hour == 7 && back[0].sunriseMinutes == 30 && back[0].enabled);
  CHECK(back[1].weekdays == ((1 << 0) | (1 << 2)));
  CHECK(std::string(back[0].soundName) == "forest");

  // upsert обновляет, remove удаляет
  Scheduler s2; Alarm f = a; CHECK(s2.upsert(f)); f.hour = 9; CHECK(s2.upsert(f));
  CHECK(s2.list().size() == 1 && s2.list()[0].hour == 9);
  CHECK(s2.remove(1) && s2.list().empty());

  // computeNextAlarm: ближайшее срабатывание для отправки часам
  {
    Alarm x; x.id=1; x.enabled=true; x.hour=7; x.minute=0; x.weekdays=0;          // каждый день
    Alarm y; y.id=2; y.enabled=true; y.hour=6; y.minute=30; y.weekdays=(1<<2);    // только Ср
    std::vector<Alarm> vv{x,y};
    auto n1 = computeNextAlarm(vv, at(5,0,2,0));   // Ср 05:00 → ближе y(06:30)
    CHECK(n1.any && n1.hour==6 && n1.minute==30);
    auto n2 = computeNextAlarm(vv, at(6,45,2,0));  // Ср 06:45 → y прошёл, x 07:00
    CHECK(n2.any && n2.hour==7 && n2.minute==0);
    auto n3 = computeNextAlarm(vv, at(8,0,3,0));   // Чт 08:00 → x завтра 07:00
    CHECK(n3.any && n3.hour==7 && n3.minutesUntil==(23*60));
    Alarm off=x; off.enabled=false;
    CHECK(!computeNextAlarm({off}, at(8,0,3,0)).any);  // нет активных → any=false (шлём AAAA)
  }

  printf(fails ? "\n%d FAILED\n" : "\nПЛАНИРОВЩИК: все проверки прошли\n", fails);
  return fails ? 1 : 0;
}
