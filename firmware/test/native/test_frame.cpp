// Проверка кодека на РЕАЛЬНЫХ кадрах из живого захвата.
// Сборка/запуск на хосте:  g++ -std=c++17 -I src/wul test/native/test_frame.cpp src/wul/wul_frame.cpp -o /tmp/t && /tmp/t
#include "wul_frame.h"
#include <cstdio>
#include <cassert>
#include <string>

using namespace wul;
static int fails = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d  %s\n",__FILE__,__LINE__,#c); ++fails;} }while(0)

// Реальные кадры с провода и их ожидаемый разбор.
struct Case { const char* hex; uint8_t cmd; int prop; const char* valHex; };
static const Case REAL[] = {
  {"03000102",             0x00, -1,   "0102"},       // AppInit
  {"03010102",             0x01, -1,   "0102"},       // DevInit ack
  {"0106",                 0x06, -1,   ""},           // GetCapability
  {"130780081B000000000070DF1BC00000F0080400", 0x07, -1, "80081B000000000070DF1BC00000F0080400"},
  {"0E094950432F4846333535302F3031", 0x09, -1, "4950432F4846333535302F3031"}, // RetName IPC/HF3550/01
  {"021217",               0x12, 0x17, ""},           // Get FWVER_MAIN
  {"071317302E322E34",     0x13, 0x17, "302E322E34"}, // Ret FWVER "0.2.4"
  {"081100000000140001",   0x11, 0x00, "000000140001"},// Param VOL min0 max20 step1
  {"04140C0650",           0x14, 0x0C, "0650"},       // SetAlarm 06:50
  {"04140CAAAA",           0x14, 0x0C, "AAAA"},        // Alarm off
  {"03144FEE",             0x14, 0x4F, "EE"},          // light on
  {"03144F00",             0x14, 0x4F, "00"},          // light off
  {"03145802",             0x14, 0x58, "02"},          // audio play
  {"0414420007",           0x14, 0x42, "0007"},        // display brightness 7
  {"0414000000",           0x14, 0x00, "0000"},        // volume 0
};

int main() {
  // 1. Разбор реальных кадров
  for (auto& c : REAL) {
    auto bytes = fromHex(c.hex);
    auto r = decode(bytes.data(), bytes.size());
    CHECK(r.has_value());
    if (!r) continue;
    CHECK(r->consumed == bytes.size());
    CHECK(r->frame.cmd == c.cmd);
    if (c.prop >= 0) CHECK(r->frame.property() == (uint8_t)c.prop);
    CHECK(hex(c.prop < 0 ? r->frame.payload : r->frame.value()) == std::string(c.valHex));
    // 2. Round-trip: encode(decode(x)) == x
    CHECK(hex(encode(r->frame)) == std::string(c.hex));
  }

  // 3. Строители рецептов дают ровно те байты, что на проводе
  CHECK(hex(encode(setAlarm(6, 50)))          == "04140C0650");
  CHECK(hex(encode(setAlarmOff()))            == "04140CAAAA");
  CHECK(hex(encode(lightOn()))                == "03144FEE");
  CHECK(hex(encode(lightOff()))               == "03144F00");
  CHECK(hex(encode(audioPlay()))              == "03145802");
  CHECK(hex(encode(setDisplayBrightness(7)))  == "0414420007");
  CHECK(hex(encode(setVolume(0)))             == "0414000000");
  // кривые света и звонок — байты из живого сеанса приложения
  CHECK(hex(encode(setDawnProfile(0x15, 0x14))) == "04144E1514");   // 21 мин до звонка, яркость 20
  CHECK(hex(encode(setDuskProfile(5, 0xFF)))  == "04144905FF");
  CHECK(hex(encode(dawnStart()))              == "03144FFF");
  CHECK(hex(encode(duskStart()))              == "03144FEE");
  CHECK(hex(encode(curveStop()))              == "03144F00");
  CHECK(hex(encode(alarmDismiss()))           == "03144F11");
  CHECK(hex(encode(snooze()))                 == "03145601");
  CHECK(hex(encode(setAlarmVolume(10)))       == "041414000A");
  CHECK(hex(encode(setAlarm(5, 52)))          == "04140C0552");
  CHECK(hex(encode(getProperty(0x17)))        == "021217");
  CHECK(hex(encode(getCapability()))          == "0106");
  CHECK(hex(encode(appInit()))                == "03000102");

  // 3b. DATETIME (гипотеза BCD YY MM DD HH MM SS) — проверяем кодирование
  CHECK(bcd(26) == 0x26 && bcd(9) == 0x09 && bcd(59) == 0x59);
  CHECK(hex(encode(setDateTime(2026, 10, 14, 7, 5, 30))) == "08140B261014070530");

  // 4. Декодирование значений
  int h, m; CHECK(decBcdHHMM(fromHex("0650"), h, m) && h == 6 && m == 50);
  CHECK(!decBcdHHMM(fromHex("AAAA"), h, m));         // выкл
  CHECK(decBe16(fromHex("0007")) == 7);

  // 5. Потоковый ридер склеивает кадры из разбитого UART-потока
  Reader rd;
  auto s1 = fromHex("03144FEE"); auto s2 = fromHex("071317302E322E34");
  rd.push(s1.data(), s1.size());
  rd.push(s2.data(), 3);          // половина второго кадра
  auto f1 = rd.next(); CHECK(f1 && f1->cmd == 0x14 && f1->property() == 0x4F);
  CHECK(!rd.next());              // второй кадр ещё неполный
  rd.push(s2.data() + 3, s2.size() - 3);
  auto f2 = rd.next(); CHECK(f2 && f2->cmd == 0x13 && f2->property() == 0x17);

  printf(fails ? "\n%d CHECK(s) FAILED\n" : "\nВСЕ ПРОВЕРКИ ПРОШЛИ (кодек совпал с реальными кадрами)\n", fails);
  return fails ? 1 : 0;
}
