#include "iap_link.h"

namespace iap {

// ---- Кодек одного iAP-кадра ----
uint8_t iapChecksum(const std::vector<uint8_t>& payload) {
  unsigned s = payload.size();                  // len
  for (uint8_t b : payload) s += b;
  return static_cast<uint8_t>((0x100 - (s & 0xFF)) & 0xFF);
}
std::vector<uint8_t> frameIap(const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> out;
  out.reserve(payload.size() + 4);
  out.push_back(0xFF); out.push_back(0x55);
  out.push_back(static_cast<uint8_t>(payload.size()));   // малый формат (<256) — все наши исходящие
  out.insert(out.end(), payload.begin(), payload.end());
  out.push_back(iapChecksum(payload));
  return out;
}

IapParse parseIap(const std::vector<uint8_t>& b) {
  IapParse r{{}, 0, false, false};
  if (b.size() < 2) return r;                            // ждём ещё
  if (b[0] != 0xFF || b[1] != 0x55) { r.consumed = 1; return r; }  // ресинк: выкинуть 1 байт
  if (b.size() < 3) return r;
  size_t len = b[2];
  if (len != 0) {                                        // малый формат
    size_t total = 3 + len + 1;
    if (b.size() < total) return r;
    r.payload.assign(b.begin() + 3, b.begin() + 3 + len);
    unsigned s = len; for (uint8_t x : r.payload) s += x; s += b[3 + len];
    r.valid = ((s & 0xFF) == 0); r.ok = true; r.consumed = total;
  } else {                                               // большой формат: 00 lenHi lenLo payload cks
    if (b.size() < 5) return r;
    size_t L = (static_cast<size_t>(b[3]) << 8) | b[4];
    size_t total = 5 + L + 1;
    if (b.size() < total) return r;
    r.payload.assign(b.begin() + 5, b.begin() + 5 + L);
    unsigned s = b[3] + b[4]; for (uint8_t x : r.payload) s += x; s += b[5 + L];
    r.valid = ((s & 0xFF) == 0); r.ok = true; r.consumed = total;
  }
  return r;
}

// ---- Link ----
void Link::sendIap(std::vector<uint8_t> payload) {
  if (!(payload.size() >= 5 && payload[0] == 0x03 && payload[1] == 0x0D && payload[4] == 0x09)) {   // ответы со временем не копим
    static const char* H = "0123456789ABCDEF";
    std::string h = ">"; for (size_t i = 0; i < payload.size() && i < 20; ++i) { h += H[payload[i] >> 4]; h += H[payload[i] & 15]; }
    if (last_.size() >= 80) last_.erase(last_.begin());
    last_.push_back(h);
  }
  auto pkt = frameIap(payload);
  if (uart_.write) uart_.write(pkt.data(), pkt.size());
}

void Link::sendAccessoryDataAck(uint16_t seq) {
  // хост квитирует data-пакет аксессуара: 00 02 <seq16> 00 42
  sendIap({0x00, 0x02, static_cast<uint8_t>(seq >> 8), static_cast<uint8_t>(seq & 0xFF), 0x00, 0x42});
}

void Link::ackGen(uint16_t trans, uint8_t ackedCmd) {
  // General-ACK устройства: 00 02 <trans16> <status=00> <ackedCmd>
  sendIap({0x00, 0x02, static_cast<uint8_t>(trans >> 8), static_cast<uint8_t>(trans & 0xFF), 0x00, ackedCmd});
}

void Link::sendPhilips(const uint8_t* frame, size_t n) {
  // EA data host->accessory: 00 43 <seq16> <sessID16> <philips...>
  std::vector<uint8_t> p{0x00, 0x43,
    static_cast<uint8_t>(txSeq_ >> 8), static_cast<uint8_t>(txSeq_ & 0xFF),
    static_cast<uint8_t>(sessID_ >> 8), static_cast<uint8_t>(sessID_ & 0xFF)};
  p.insert(p.end(), frame, frame + n);
  sendIap(std::move(p));
  ++txSeq_;
}

void Link::pump() {
  if (uart_.readByte) { int b; while ((b = uart_.readByte()) >= 0) { rx_.push_back(static_cast<uint8_t>(b)); ++rxBytes_; } }
  for (;;) {
    IapParse r = parseIap(rx_);
    if (!r.ok) {
      if (r.consumed == 1) { rx_.erase(rx_.begin()); continue; }  // ресинк
      break;                                                       // нужно больше данных
    }
    if (r.valid) {
      ++rxCount_;
      static const char* H = "0123456789ABCDEF";
      std::string h = "<"; for (size_t i = 0; i < r.payload.size() && i < 20; ++i) { h += H[r.payload[i] >> 4]; h += H[r.payload[i] & 15]; }
      if (!(r.payload.size() >= 5 && r.payload[0] == 0x03 && r.payload[1] == 0x0C && r.payload[4] == 0x09)) {   // запросы времени не копим
        if (last_.size() >= 80) last_.erase(last_.begin());
        last_.push_back(h);
      }
      handlePayload(r.payload);
    } else ++rxBad_;
    rx_.erase(rx_.begin(), rx_.begin() + r.consumed);
  }
  if (rx_.size() > 4096) rx_.erase(rx_.begin(), rx_.end() - 512);   // защита от мусора
}

int Link::readPhilipsByte() {
  if (philipsOut_.empty()) pump();
  if (philipsOut_.empty()) return -1;
  int b = philipsOut_.front(); philipsOut_.pop_front(); return b;
}

void Link::handlePayload(const std::vector<uint8_t>& p) {
  if (p.size() < 2) return;
  uint8_t lingo = p[0], cmd = p[1];
  const uint8_t* d = p.data() + 2; size_t dn = p.size() - 2;

  if (lingo == 0x00 && cmd == 0x42) {                    // данные аксессуар->хост
    if (p.size() >= 6) {
      uint16_t seq = (static_cast<uint16_t>(p[2]) << 8) | p[3];
      // Часы продолжают уже открытую сессию (модуль перезагрузился, а «извлечения» они не заметили):
      // подхватываем её id — иначе ждали бы OpenDataSession, которого не будет.
      if (!sessionOpen_) { sessionOpen_ = attachDone_ = true; logf("iap: resume session"); }   // id у часов всегда 0001
      for (size_t i = 6; i < p.size(); ++i) philipsOut_.push_back(p[i]);  // Philips-кадр
      sendAccessoryDataAck(seq);
    }
    return;
  }
  if (lingo == 0x00 && cmd == 0x41) return;              // ACK нашего data-пакета — игнор
  if (lingo == 0x03) {                                   // lingo 0x03
    if (cmd == 0x08 && dn >= 2)                          // обмен версией lingo в attach → ответить
      sendIap({0x03, 0x00, d[0], d[1], 0x00, 0x08});     // (наблюдённый ответ устройства)
    else if (cmd == 0x0C && dn >= 3) {                   // GetiPodStateInfo: 03 0C <trans16> <тип>
      uint8_t tH = d[0], tL = d[1], type = d[2];
      if (type == 0x09) {                                // дата/время: 03 0D <trans> 09 YYYY MM DD hh mm
        DateTime t;
        if (timeSource && timeSource(t))
          sendIap({0x03, 0x0D, tH, tL, 0x09,
                   static_cast<uint8_t>(t.year >> 8), static_cast<uint8_t>(t.year & 0xFF),
                   static_cast<uint8_t>(t.month), static_cast<uint8_t>(t.day),
                   static_cast<uint8_t>(t.hour), static_cast<uint8_t>(t.minute)});
      } else {
        sendIap({0x03, 0x0D, tH, tL, type, 0x00});       // прочие типы: iPad отвечал 00
      }
    }
    return;
  }
  if (lingo == 0x00) handleAttach(cmd, d, dn);           // identify/auth/служебное
  if (onControl) onControl(lingo, cmd, d, dn);
}

std::string Link::lastRx() const { std::string j; for (auto& s : last_) j += (j.empty() ? "\"" : ",\"") + s + "\""; return "[" + j + "]"; }

void Link::notifyDateTime(const DateTime& t) {
  static uint16_t trans = 0x0100;
  ++trans;
  sendIap({0x03, 0x09, static_cast<uint8_t>(trans >> 8), static_cast<uint8_t>(trans & 0xFF), 0x09,
           static_cast<uint8_t>(t.year >> 8), static_cast<uint8_t>(t.year & 0xFF),
           static_cast<uint8_t>(t.month), static_cast<uint8_t>(t.day),
           static_cast<uint8_t>(t.hour), static_cast<uint8_t>(t.minute)});
}

// Attach-автомат: точный replay стороны устройства, снятый вживую.
// Крипто НЕ нужно: сертификат/подпись аксессуара принимаем БЕЗ проверки.
// Каждая General-команда несёт 2-байтный trans-id (первые 2 байта data), ответы его возвращают эхом.
// ВАЛИДИРОВАТЬ вживую при bring-up: сверить выход ESP32 с живым захватом.
void Link::handleAttach(uint8_t cmd, const uint8_t* d, size_t n) {
  uint16_t trans = (n >= 2) ? ((static_cast<uint16_t>(d[0]) << 8) | d[1]) : 0;
  uint8_t tH = static_cast<uint8_t>(trans >> 8), tL = static_cast<uint8_t>(trans & 0xFF);
  switch (cmd) {
    case 0x38:  // StartIDPS
      ackGen(trans, 0x38);
      tokenAcks_ = 0; authPhase_ = 0;
      break;
    case 0x4B:  // RequestTransportMaxPayloadSize(?) → наблюдённый ответ cmd4C
      sendIap({0x00, 0x4C, tH, tL, 0x00, 0x00, 0x00, 0x06, 0x3D, 0xEF, 0x73, 0xFF});
      break;
    case 0x39:  // SetFIDTokenValues → RetFIDTokenValueACKs (1-й и 2-й блок разные, приняты все токены)
      if (tokenAcks_++ == 0)
        sendIap({0x00,0x3A, tH,tL, 0x0A,0x03,0x00,0x00,0x00,0x03,0x00,0x01,0x00,0x04,0x00,0x03,0x00,0x03,
                 0x04,0x00,0x02,0x00,0x01,0x04,0x00,0x02,0x00,0x04,0x04,0x00,0x02,0x00,0x05,0x04,0x00,0x02,
                 0x00,0x06,0x04,0x00,0x02,0x00,0x07,0x04,0x00,0x02,0x00,0x09,0x04,0x00,0x02,0x00,0x0C});
      else
        sendIap({0x00,0x3A, tH,tL, 0x02,0x04,0x00,0x04,0x00,0x01,0x03,0x00,0x05,0x00});
      break;
    case 0x11:  // опрос в IDPS → наблюдённый ответ cmd12
      sendIap({0x00, 0x12, tH, tL, 0x01, 0xFA});
      break;
    case 0x3B:  // EndIDPS → cmd3C (status ok)
      sendIap({0x00, 0x3C, tH, tL, 0x00});
      break;
    case 0x2B:  // две фазы аутентификации аксессуара
      if (authPhase_ == 0) {                              // перед сертификатом
        sendIap({0x00, 0x14, 0x00, 0x01});               // GetAccessoryAuthenticationInfo (auth-trans 0001)
        ackGen(trans, 0x2B);
        authPhase_ = 1;
      } else {                                            // перед подписью
        sendIap({0x00, 0x16, 0x00, 0x01, 0x00});         // AckAccessoryAuthenticationInfo = ok
        std::vector<uint8_t> ch{0x00, 0x17, 0x00, 0x01}; // GetAccessoryAuthenticationSignature + 20б челлендж
        for (int i = 0; i < 20; ++i) ch.push_back(static_cast<uint8_t>(0xA5 ^ (i * 7)));
        sendIap(std::move(ch));
        ackGen(trans, 0x2B);
      }
      break;
    case 0x15:  // RetAccessoryAuthenticationInfo (кусок сертификата) → ACK (не проверяем)
      ackGen(0x0001, 0x15);                               // auth-trans 0001
      break;
    case 0x18:  // RetAccessoryAuthenticationSignature → AckStatus=success (не проверяем)
      sendIap({0x00, 0x19, 0x00, 0x01, 0x00});
      break;
    case 0x64:  // OpenDataSessionForProtocol → открыть data-сессию
      sendIap({0x03, 0x09, 0x00, 0x02, 0x03, 0x00});      // наблюдённый lingo03/cmd09
      ackGen(trans, 0x64);
      sendIap({0x00, 0x3F, 0x00, 0x03, 0x00, 0x01, 0x01}); // старт data-сессии (наблюдённый)
      sessID_ = 0x0001; sessionOpen_ = true; attachDone_ = true;
      logf("iap: data session open");
      break;
    default:
      break;  // прочие служебные — игнор (добавим по живому захвату, если понадобится)
  }
}

void Link::tick() {
  // Ничего не шлём: «keepalive» 03 0C/0D оказался запросом даты/времени (см. handlePayload).
}

}  // namespace iap
