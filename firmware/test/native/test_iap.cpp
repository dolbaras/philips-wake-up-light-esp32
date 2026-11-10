// Проверка слоя iAP/EA на РЕАЛЬНЫХ байтах из живого захвата.
// Сборка/запуск на хосте:
//   g++ -std=c++17 -I src/iap test/native/test_iap.cpp src/iap/iap_link.cpp -o /tmp/t && /tmp/t
#include "iap_link.h"
#include <cstdio>
#include <vector>
#include <string>
using namespace iap;
static int fails = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d  %s\n",__FILE__,__LINE__,#c); ++fails; } }while(0)
static std::string H(const std::vector<uint8_t>& v){
  std::string s; char b[4];
  for (size_t i=0;i<v.size();++i){ sprintf(b,"%02X",v[i]); if(i)s+=' '; s+=b; }
  return s;
}

int main() {
  // 1. Контрольная сумма и кадрирование: реальный data-пакет часов (DevInit внутри EA)
  std::vector<uint8_t> devinit = {0x00,0x42,0x24,0x4B,0x00,0x02,0x03,0x01,0x01,0x02};
  CHECK(iapChecksum(devinit) == 0x3C);
  auto fr = frameIap(devinit);
  CHECK(H(fr) == "FF 55 0A 00 42 24 4B 00 02 03 01 01 02 3C");

  // 2. Разбор iAP
  auto pr = parseIap(fr);
  CHECK(pr.ok && pr.valid && pr.consumed == fr.size());
  CHECK(H(pr.payload) == "00 42 24 4B 00 02 03 01 01 02");
  auto bad = fr; bad.back() ^= 0xFF;
  auto pb = parseIap(bad); CHECK(pb.ok && !pb.valid);                 // битая КС распознаётся
  std::vector<uint8_t> junk = {0x11,0x22}; auto j = parseIap(junk);
  CHECK(!j.ok && j.consumed == 1);                                    // ресинк: дропнуть 1 байт

  // 3. EA-поток: подаём data-пакет часов → Link извлекает Philips-кадр и отвечает ACK хоста
  std::vector<uint8_t> in, out; size_t rp = 0;
  Link::Transport tr;
  tr.write    = [&](const uint8_t* d, size_t n){ out.insert(out.end(), d, d+n); };
  tr.readByte = [&]() -> int { return rp < in.size() ? in[rp++] : -1; };
  Link link(tr);
  in = fr;
  std::vector<uint8_t> philips; int b;
  while ((b = link.readPhilipsByte()) >= 0) philips.push_back(static_cast<uint8_t>(b));
  CHECK(H(philips) == "03 01 01 02");                                 // извлечён DevInit-кадр
  CHECK(H(out)     == "FF 55 06 00 02 24 4B 00 42 47");               // отправлен ACK хоста (00 02 seq 00 42)

  // 4. sendPhilips: AppInit → EA host→accessory (00 43, seq=0001, sess=0002)
  out.clear();
  uint8_t appinit[] = {0x03,0x00,0x01,0x02};
  link.sendPhilips(appinit, sizeof(appinit));
  CHECK(H(out) == "FF 55 0A 00 43 00 01 00 01 03 00 01 02 AB");   // seq=0001 sess=0001

  // 5. Поток из нескольких слипшихся пакетов + мусор между ними разбирается корректно
  in.clear(); rp = 0; out.clear();
  in.push_back(0x00);                       // мусор
  in.insert(in.end(), fr.begin(), fr.end());// пакет 1 (DevInit)
  in.insert(in.end(), fr.begin(), fr.end());// пакет 2 (тот же)
  philips.clear();
  while ((b = link.readPhilipsByte()) >= 0) philips.push_back(static_cast<uint8_t>(b));
  CHECK(H(philips) == "03 01 01 02 03 01 01 02");                     // оба кадра извлечены
  CHECK(out.size() == 2 * 10);                                        // два ACK-пакета отправлены

  // 6. Attach: StartIDPS → General-ACK с эхом trans-id; OpenDataSession → сессия открыта
  {
    std::vector<uint8_t> ain, aout; size_t arp = 0;
    Link::Transport atr;
    atr.write    = [&](const uint8_t* dd, size_t nn){ aout.insert(aout.end(), dd, dd+nn); };
    atr.readByte = [&]() -> int { return arp < ain.size() ? ain[arp++] : -1; };
    Link al(atr);
    ain = {0xFF,0x55,0x04,0x00,0x38,0x00,0x00,0xC4};        // StartIDPS (trans 0x0000)
    al.pump();
    CHECK(H(aout) == "FF 55 06 00 02 00 00 00 38 C0");      // ACK(trans 0000, status 00, acked 38)
    aout.clear(); arp = 0;
    ain = frameIap({0x00,0x64,0x00,0x0A,0x00,0x02,0x00});   // OpenDataSessionForProtocol (trans 000A)
    al.pump();
    CHECK(al.sessionOpen());                                // сессия открылась
    CHECK(al.sessionId() == 0x0001);
  }

  // 7. Запрос даты/времени часами (из живого сеанса iPad):
  //    часы 03 0C 00 0B 09  →  iPad 03 0D 00 0B 09 07 EA 0A 10 05 12  (2026-10-16 05:18)
  //    часы 03 0C 00 09 03  →  iPad 03 0D 00 09 03 00
  {
    std::vector<uint8_t> tin, tout; size_t trp = 0;
    Link::Transport ttr;
    ttr.write    = [&](const uint8_t* dd, size_t nn){ tout.insert(tout.end(), dd, dd+nn); };
    ttr.readByte = [&]() -> int { return trp < tin.size() ? tin[trp++] : -1; };
    Link tl(ttr);
    tin = frameIap({0x03,0x0C,0x00,0x0B,0x09});
    tl.pump();
    CHECK(tout.empty());                                    // без источника времени — молчим
    trp = 0;
    tl.timeSource = [](Link::DateTime& t){ t = {2026, 10, 16, 5, 18}; return true; };
    tl.pump();
    CHECK(H(tout) == H(frameIap({0x03,0x0D,0x00,0x0B,0x09,0x07,0xEA,0x0A,0x10,0x05,0x12})));
    tout.clear(); trp = 0;
    tin = frameIap({0x03,0x0C,0x00,0x09,0x03});
    tl.pump();
    CHECK(H(tout) == H(frameIap({0x03,0x0D,0x00,0x09,0x03,0x00})));
  }

  printf(fails ? "\n%d CHECK(s) FAILED\n" : "\nВСЕ iAP-ПРОВЕРКИ ПРОШЛИ (EA-слой + attach совпали с захватом)\n", fails);
  return fails ? 1 : 0;
}
