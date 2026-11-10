// Тест JSON-контракта состояния/будильников (хост).
// g++ -std=c++17 -I src -I src/api test/native/test_state.cpp src/api/app_state.cpp src/scheduler/scheduler.cpp -o /tmp/tst && /tmp/tst
#include "app_state.h"
#include <cstdio>
#include <string>
#include <cstring>
using namespace app;
static int fails = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d  %s\n",__FILE__,__LINE__,#c); ++fails;} }while(0)
static bool has(const std::string& h, const std::string& n){ return h.find(n) != std::string::npos; }

int main() {
  State s; s.fw = "0.2.4"; s.connected = true; s.lightOn = true; s.lightLevel = 10;
  s.brightness = 7; s.volume = 12; s.lastButton = 0x04;
  sched::Alarm a; a.id = 1; a.enabled = true; a.hour = 6; a.minute = 50;
  a.weekdays = (1<<0)|(1<<2); a.sunriseMinutes = 30; std::strcpy(a.soundName, "forest");
  s.alarms.push_back(a);

  std::string j = toJson(s);
  CHECK(has(j, "\"fw\":\"0.2.4\""));
  CHECK(has(j, "\"light\":{\"on\":true,\"level\":10}"));
  CHECK(has(j, "\"volume\":12"));
  CHECK(has(j, "\"weekdays\":[0,2]"));
  CHECK(has(j, "\"hour\":6") && has(j, "\"minute\":50"));
  CHECK(has(j, "\"sound_name\":\"forest\""));

  // round-trip будильника через JSON
  sched::Alarm b;
  std::string in = "{\"id\":3,\"enabled\":true,\"hour\":7,\"minute\":15,\"weekdays\":[1,3,5],"
                   "\"sunriseMinutes\":20,\"light\":true,\"sound\":false,\"volume\":8,\"sound_name\":\"ocean\"}";
  CHECK(alarmFromJson(in, b));
  CHECK(b.id == 3 && b.hour == 7 && b.minute == 15 && b.sunriseMinutes == 20);
  CHECK(b.weekdays == ((1<<1)|(1<<3)|(1<<5)));
  CHECK(b.enabled && !b.sound && b.volume == 8);
  CHECK(std::string(b.soundName) == "ocean");

  // escape
  CHECK(jesc("a\"b\\c") == "a\\\"b\\\\c");

  printf(fails ? "\n%d FAILED\n" : "\nСОСТОЯНИЕ/JSON: все проверки прошли\n", fails);
  return fails ? 1 : 0;
}
