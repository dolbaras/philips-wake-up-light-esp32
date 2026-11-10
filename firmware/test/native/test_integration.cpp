// Интеграционный тест: wul::Client против «виртуальных часов», отвечающих по протоколу.
// Проверяет сквозной цикл: хендшейк, чтение имени/версии, round-trip свойства.
// g++ -std=c++17 -I src/wul test/native/test_integration.cpp src/wul/wul_frame.cpp src/wul/wul_client.cpp -o /tmp/ti && /tmp/ti
#include "wul_client.h"
#include <deque>
#include <string>
#include <cstdio>
using namespace wul;
static int fails=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++fails;} }while(0)

// Модель устройства HF3550: отвечает так же, как реальные часы в захвате.
struct VirtualClock {
  std::deque<uint8_t> out;
  int lastSetProp=-1; std::vector<uint8_t> lastSetVal;
  void receive(const uint8_t* d, size_t n){ Reader rd; rd.push(d,n);
    while(auto f=rd.next()) respond(*f); }
  void emit(const Frame& r){ auto b=encode(r); for(auto x:b) out.push_back(x); }
  void respond(const Frame& f){
    switch(f.cmd){
      case 0x00: emit(Frame{0x01, f.payload}); break;                 // AppInit -> DevInit (эхо версии)
      case 0x08: { std::string nm="IPC/HF3550/01";                    // GetName -> RetName
        emit(Frame{0x09, std::vector<uint8_t>(nm.begin(),nm.end())}); } break;
      case 0x06: emit(Frame{0x07, fromHex("80081B000000000070DF1BC00000F0080400")}); break; // GetCap->RetCap
      case 0x12: { uint8_t p=f.property(); std::vector<uint8_t> val;   // GetValue -> RetValue
        if(p==0x17){ std::string v="0.2.4"; val.assign(v.begin(),v.end()); }
        else if(p==0x00) val={0x00,0x00};
        std::vector<uint8_t> pl{p}; pl.insert(pl.end(),val.begin(),val.end());
        emit(Frame{0x13, pl}); } break;
      case 0x14: lastSetProp=f.property(); lastSetVal=f.value(); break; // Set: запоминаем, без ответа
    }
  }
};

int main(){
  VirtualClock vc;
  Transport tr;
  tr.write    = [&](const uint8_t* d, size_t n){ vc.receive(d,n); };
  tr.readByte = [&]()->int{ if(vc.out.empty()) return -1; int b=vc.out.front(); vc.out.pop_front(); return b; };
  Client c(tr);

  // 1. Хендшейк: AppInit -> DevInit
  CHECK(c.handshake(1000));
  CHECK(c.handshakeDone());

  // 2. Хендшейк дослал GetName + GetProperty(FWVER) — обрабатываем ответы
  c.poll();
  CHECK(c.deviceName()=="IPC/HF3550/01");
  CHECK(c.fwVersion()=="0.2.4");

  // 3. Запись свойства доходит до «часов» корректно (свет вкл)
  c.send(lightOn()); CHECK(vc.lastSetProp==0x4F && vc.lastSetVal==std::vector<uint8_t>{0xEE});

  // 4. Будильник на 06:50 кодируется и доходит как 0C -> 06 50
  c.send(setAlarm(6,50)); CHECK(vc.lastSetProp==0x0C && vc.lastSetVal==(std::vector<uint8_t>{0x06,0x50}));

  // 5. Чтение значения: запрос -> ответ -> onFrame видит значение
  int gotProp=-1; std::vector<uint8_t> gotVal;
  c.onFrame=[&](const Frame& f){ if(f.cmd==0x13){ gotProp=f.property(); gotVal=f.value(); } };
  c.getValue(0x00); c.poll();
  CHECK(gotProp==0x00 && gotVal==(std::vector<uint8_t>{0x00,0x00}));

  printf(fails?"\n%d FAILED\n":"\nИНТЕГРАЦИЯ: сквозной цикл Client↔устройство прошёл\n",fails);
  return fails?1:0;
}
