// Прошивка ESP32 вместо iPad для Philips Wake-up Light HF3550.
// ESP32 сам становится «устройством в доке»: detect-линии → iAP-attach → Philips-хендшейк,
// а дальше ведёт будильники/рассвет/закат так же, как оригинальное приложение (core::Engine).
// Управление — веб-панель и Android-приложение по HTTP API (net/web.h).
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_core_dump.h>
#include <deque>
#include <ctime>
#include "config.h"
#include "iap/iap_link.h"
#include "wul/wul_client.h"
#include "wul/wul_buttons.h"
#include "wul/display_fx.h"
#include "core/engine.h"
#include "net/api.h"
#include "net/web.h"
#include "net/netmgr.h"
#include "diag/bus_sniff.h"
#include "diag/lamp_i2c.h"
#include "diag/bootlog.h"
#include "core/eq.h"
#include "audio/audio.h"
#include "net/spp.h"
#include "audio/bt_sink.h"
#include "storage/ext_flash.h"
#include "storage/melodies.h"
#include <driver/gpio.h>
#include <soc/gpio_struct.h>
#include <esp_timer.h>

// --- UART к часам (via5/via1 на межплатном разъёме контроллера) ---
static const int PIN_CLK_RX = 16;            // ← TX часов (зелёный)
static const int PIN_CLK_TX = 17;            // → RX часов (фиолетовый)
static const uint32_t CLK_BAUD = 19200;
// --- detect «устройство в доке»: B11/T12 → HIGH, B12 → LOW (нужен ФРОНТ после загрузки часов) ---
static const int PIN_DET_B11 = 25, PIN_DET_T12 = 26, PIN_DET_B12 = 27;
// --- кнопка BOOT на плате: удерживать 10 с → сброс к заводским ---
static const int PIN_BOOT = 0;
// --- прослушка второй шины межплатного разъёма (верхний вывод 5), только вход ---
static const int PIN_SNIFF = 34;
// --- синий светодиод платы: состояние без USB и сети (см. ledTick) ---
static const int PIN_LED = 2;
// Синий светодиод на плате: по умолчанию НЕ светит — корпус часов прозрачный, мигание мешает (2026-11-10).
// Включить для отладки: «Индикатор на плате» в приложении или POST /api/settings {"boardLed":true}. NVS «ui/led».
static bool boardLed = false;
static const int PIN_FL_SCK = 18, PIN_FL_MISO = 19, PIN_FL_MOSI = 23, PIN_FL_CS = 5;   // W25Q64, мелодии
static const int PIN_I2S_BCK = 4, PIN_I2S_LRCK = 21, PIN_I2S_DIN = 22;   // PCM5102A
static const int PIN_LAMP_SCL = 32, PIN_LAMP_SDA = 13;   // WUL_SCL / WUL_SDA через преобразователь уровней
static const uint32_t kBootHoldMs = 10000;

HardwareSerial ClkSerial(2);
HardwareSerial PadSerial(1);                 // только в пассивном режиме: второй приёмник прослушки
static iap::Link*   link_ = nullptr;
static wul::Client* client = nullptr;
static core::Engine engine;
static Preferences prefs;
static std::deque<wul::Frame> txq;
static uint32_t audioGapMax = 0;                // самый долгий перерыв между «58 01/02», мс (часы глушат после ~1,5 с)
static uint32_t audioGapAt = 0, audioGapsLong = 0;   // когда был самый долгий (с от старта); сколько раз дольше 1 с
static uint32_t loopGapMax = 0, loopGapAt = 0;    // самый долгий оборот loop(), мс, и когда (с) — модуль «замирал»?
static bool holdAudio = false;                 // ручное прослушивание (/api/play, /api/tone)
static bool keepAlive = true;                  // слать «58 02» раз в 0,5 с, пока играет (проверяем, нужно ли)
static const float kLineDc = 1.5f;             // постоянка на выходе ЦАП, В: вход дока (C506/C505) без неё молчит
static const float kLineGain = 0.48f;          // размах ±1,43 В вокруг 1,5 В — громко как iPad и без хрипа (проверено на слух)
static uint8_t alarmVol = 0;                   // громкость будильника 0..20; часы доводят до неё свою громкость за 90 с           // кадры часам с паузами (как приложение)

// Журнал кадров Philips (диагностика кнопок/протокола): Serial + GET /api/events
struct RawEvent { uint32_t ms; bool rx; std::string hex; };
static std::deque<RawEvent> events;
// Самое старое событие, кроме записей загрузки (ms = 0: BOOT, итоги сеанса колонки) — их кадры не вытесняют
static void dropOldEvent() {
  for (auto it = events.begin(); it != events.end(); ++it)
    if (it->ms) { events.erase(it); return; }
  events.pop_front();
}
// Необычные кадры от часов (без ответов на наши опросы 4C/57 и без имени 09): журнал кадров живёт в ОЗУ и
// пропадает при перезагрузке в Wi-Fi — а что часы слали во время колонки, нужно видеть в её итогах (2026-11-03).
static std::deque<std::string> rxOdd;
static void logFrame(bool rx, const wul::Frame& f) {
  static const char* H = "0123456789ABCDEF";
  std::string hex{H[f.cmd >> 4], H[f.cmd & 15]};
  for (uint8_t b : f.payload) { hex += ' '; hex += H[b >> 4]; hex += H[b & 15]; }
  if (rx && f.cmd != 0x09 && !(f.cmd == 0x13 && !f.payload.empty() && (f.payload[0] == 0x4C || f.payload[0] == 0x57))) {
    if (rxOdd.size() >= 12) rxOdd.pop_front();
    rxOdd.push_back(std::to_string(millis() / 1000) + " " + hex.substr(0, 29));
  }
  if (events.size() >= 100) dropOldEvent();
  events.push_back({millis(), rx, hex});
  Serial.printf("%10lu %s %s\n", (unsigned long)millis(), rx ? "<<" : ">>", hex.c_str());
}

// ---------- время ----------
static long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static bool localTm(struct tm& lt) {
  time_t t = time(nullptr);
  if (t < 1700000000) return false;          // NTP ещё не пришёл
  localtime_r(&t, &lt);
  return true;
}

static core::LocalTime localNow() {
  struct tm lt;
  if (!localTm(lt)) return 0;
  return static_cast<core::LocalTime>(daysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) * 86400L +
                                      lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);
}

// ---------- detect ----------
// Те же три линии выставляются сами, когда в доке живой iPhone/iPad (B11=1, T12=1, B12=0). Модуль держит их
// в режиме «вход+выход» (видно настоящий уровень, можно на миг отпустить — probeLine).
// Замеры /api/link/lines с iPad в доке и без (2026-11-03):
//  - B12 — контакт «устройство в доке»: устройство сажает его на свою землю намертво (выход ESP в 1 не поднимает,
//    прозвонка обесточенных часов с iPad в доке «звонит» на GND); без устройства часы сами тянут его в 1 (сильно).
//    По нему модуль и узнаёт, что в доке iPhone/iPad. Отпущенный — с подтяжкой ESP вверх (как у часов).
//  - T12 — высокоомный узел (≈0,9 В / 3,5 В с устройством): подтяжка ESP 45 кОм его прижимает — часы тогда не
//    видят «вставили» и не зовут устройство. Отпускаем БЕЗ подтяжки. B11 стоит в 1 сам — тоже без подтяжки.
enum class Det : uint8_t { Released, Removed, Asserted };
static Det det = Det::Released;
static void detPin(int pin, int level, bool pullUp, bool pullDown) {   // level < 0 — отпустить
  const gpio_num_t g = static_cast<gpio_num_t>(pin);
  if (level >= 0) gpio_set_level(g, level);           // уровень — до включения выхода, без иголки
  gpio_config_t c = {};
  c.pin_bit_mask = 1ULL << pin;
  c.mode = level < 0 ? GPIO_MODE_INPUT : GPIO_MODE_INPUT_OUTPUT;
  c.pull_up_en = level < 0 && pullUp ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
  c.pull_down_en = level < 0 && pullDown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
  c.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&c);
}
static void detectRelease() {
  detPin(PIN_DET_B11, -1, false, false); detPin(PIN_DET_T12, -1, false, false); detPin(PIN_DET_B12, -1, true, false);
  det = Det::Released;
}
static uint32_t b12Stuck = 0;                   // раз B12 не поднялся в 1 — не боремся выходом с землёй устройства
// B12 в 1: если через 50 мкс на выводе всё ещё 0 — его держит земля устройства в доке: отпускаем с подтяжкой
// вверх, чтобы не гнать ток выхода в замыкание секундами
static void b12High() {
  detPin(PIN_DET_B12, HIGH, false, false);
  delayMicroseconds(50);
  if (gpio_get_level(static_cast<gpio_num_t>(PIN_DET_B12)) == 0) { detPin(PIN_DET_B12, -1, true, false); ++b12Stuck; }
}
// «Устройство вынуто» явно: B11/T12 → 0, B12 → 1. Просто отпущенные линии часы иногда не замечают и держат старую сессию.
static void detectRemoved() {
  detPin(PIN_DET_B11, LOW, false, false); detPin(PIN_DET_T12, LOW, false, false); b12High();
  det = Det::Removed;
}
static void detectAssert() {
  detPin(PIN_DET_B11, HIGH, false, false); detPin(PIN_DET_T12, HIGH, false, false); detPin(PIN_DET_B12, LOW, false, false);
  det = Det::Asserted;
}
// Отпущенные линии говорят «в доке устройство»: B12 в 0 против подтяжек часов и ESP вверх
static bool detectSaysDevice() { return digitalRead(PIN_DET_B12) == LOW; }

// Проба, пока модуль сам держит «устройство есть»: на миг отпустить B12 с подтяжкой к «нет». Без устройства
// часы сразу поднимают его в 1 — тут же держим снова (часам такая иголка не видна); с устройством он на его земле
// всё окно. Возвращает, сколько мкс линия продержалась (kProbeUs — всё окно).
static const uint32_t kProbeUs = 100;              // без устройства B12 встаёт в 1 сразу (< 1 мкс)
static portMUX_TYPE probeMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t probeLine(int pin, int level) {
  const gpio_num_t g = static_cast<gpio_num_t>(pin);
  gpio_set_pull_mode(g, level ? GPIO_PULLDOWN_ONLY : GPIO_PULLUP_ONLY);
  const uint32_t mask = 1u << pin;
  uint32_t us = kProbeUs;
  portENTER_CRITICAL(&probeMux);
  GPIO.enable_w1tc = mask;                             // выход выключен — вход включён всегда (режим «вход+выход»)
  const int64_t t0 = esp_timer_get_time();
  for (int64_t dt = 0; dt < kProbeUs; dt = esp_timer_get_time() - t0)
    if (((GPIO.in >> pin) & 1u) != static_cast<uint32_t>(level)) { us = static_cast<uint32_t>(dt); break; }
  GPIO.enable_w1ts = mask;                             // снова держим прежний уровень (регистр out не трогали)
  portEXIT_CRITICAL(&probeMux);
  gpio_set_pull_mode(g, GPIO_FLOATING);
  return us;
}

// ---------- хранение (NVS) ----------
static void saveBlob(const char* key, const std::vector<uint8_t>& b) {
  prefs.begin("wul2", false); prefs.putBytes(key, b.data(), b.size()); prefs.end();
}
static std::vector<uint8_t> loadBlob(const char* key) {
  prefs.begin("wul2", true);
  std::vector<uint8_t> b(prefs.getBytesLength(key));
  if (!b.empty()) prefs.getBytes(key, b.data(), b.size());
  prefs.end();
  return b;
}
static void saveSettings() { saveBlob("settings", core::packSettings(engine.settings, engine.dusk)); }
static void loadAll() {
  auto a = loadBlob("alarms"); engine.alarms = core::unpackAlarms(a.data(), a.size());
  auto l = loadBlob("log");    engine.log = core::unpackLog(l.data(), l.size());
  auto s = loadBlob("settings");
  if (!s.empty()) core::unpackSettings(s.data(), s.size(), engine.settings, engine.dusk);
}

// Часовой пояс из настроек (POSIX TZ) — сразу для localtime
static void applyTz() {
  setenv("TZ", engine.settings.tz, 1);
  tzset();
}

// Время от телефона/браузера («локальное как UTC») — когда нет NTP (Bluetooth, точка настройки)
static void setLocalTime(core::LocalTime t) {
  time_t tt = t;
  struct tm lt;
  gmtime_r(&tt, &lt);
  lt.tm_isdst = -1;
  struct timeval tv{mktime(&lt), 0};
  settimeofday(&tv, nullptr);
  Serial.printf("[time] время от клиента: %ld\n", (long)tv.tv_sec);
  api::bump();
}

static uint32_t resetAt = 0;

// Защита от петли падений: два аварийных перезапуска подряд (паника, сторож, просадка) раньше, чем модуль
// проработал минуту, — грузимся без Bluetooth-колонки, чтобы остались Wi-Fi и обновление по воздуху.
RTC_NOINIT_ATTR static uint32_t rtcMagic, rtcCrashes;
RTC_NOINIT_ATTR static uint32_t rtcHeap[6];       // память перед запуском Bluetooth: своб./крупнейший до и после контроллера
static bool safeMode = false;
static uint32_t modeSwitchAt = 0;                // переключение Wi-Fi ↔ колонка: перезагрузка в millis()
static void memMark(const char* what) {             // диагностика памяти: «MEM этап своб./крупнейший» в /api/events
  char b[64];
  snprintf(b, sizeof b, "MEM %s %u/%u", what, (unsigned)ESP.getFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  events.push_back({millis(), false, b});
}                     // колонку запустили до Wi-Fi — следим, поднялся ли Wi-Fi

// Табло (wul/display_fx.h): вместо времени — своя надпись или эквалайзер. Часы спрашивают время ~раз в
// секунду, а уведомление iAP меняет табло сразу (до 20 раз/с проверено).
enum class DispMode : uint8_t { Clock, Text, Eq };
static struct {
  DispMode mode = DispMode::Clock;
  wul::DisplayPair pair;
  std::string text;
  uint32_t until = 0;                                  // millis() окончания; 0 — пока не вернут часы
  uint32_t periodMs = 100, last = 0;
  uint8_t lvl[4] = {0, 0, 0, 0};
  bool live = false;                                   // уровни эквалайзера даёт светомузыка, а не демо
} disp;

static void dispPush(bool bump = true) {               // отдать часам текущее содержимое сразу
  iap::Link::DateTime t;
  if (link_ && link_->timeSource && link_->timeSource(t)) link_->notifyDateTime(t);
  if (bump) api::bump();                               // живой эквалайзер (15 кадров/с) интерфейс не дёргает
}

static void dispClock() { disp.mode = DispMode::Clock; disp.text.clear(); disp.until = 0; disp.live = false; dispPush(); }

static void dispTick() {
  if (disp.mode == DispMode::Clock) return;
  if (disp.until && static_cast<int32_t>(millis() - disp.until) >= 0) { dispClock(); return; }
  if (disp.mode != DispMode::Eq || disp.live || millis() - disp.last < disp.periodMs) return;
  disp.last = millis();
  for (auto& l : disp.lvl) {                            // демо: случайное блуждание; со звуком — уровни полос
    int v = l + static_cast<int>(esp_random() % 5) - 2;
    l = static_cast<uint8_t>(v < 0 ? 0 : v > 5 ? 5 : v);
  }
  disp.pair = wul::eqPairs(disp.lvl);
  dispPush();
}   // сброс сочетанием кнопок: после мигания табло

// Откат прошивки (загрузчик собран с CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE): Arduino по умолчанию подтверждает новую
// прошивку сразу при старте — откат не срабатывал никогда. Подтверждаем сами (netmgr::markGood): проработала 45 с и
// подняла сеть. Упала раньше — загрузчик вернёт прошлую, а OTA по Wi-Fi остаётся доступна (часы собраны, USB нет).
extern "C" bool verifyRollbackLater() { return true; }

// ---------- прослушка в пассивном режиме: обе линии iPad↔часы → GET /api/link/tap ----------
static std::deque<std::string> tapLog;          // "номер мс C|P hex" (C — часы, P — iPad)
static uint32_t tapSeq = 0;
struct TapDir { char who; HardwareSerial& port; std::string hex; uint32_t at; };
static TapDir tapDirs[2] = {{'C', ClkSerial, {}, 0}, {'P', PadSerial, {}, 0}};
static uint32_t tapErr = 0;                     // available() > 0, а read() = -1
static uint32_t tapPadAt = 0;                   // последний байт от iPad — без него пассивный режим снимаем
static bool tapQuiet = false;                   // авто-стыковка: только следим за активностью, журнал не пишем

static void tapPoll() {
  const uint32_t now = millis();
  for (auto& d : tapDirs) {
    for (int budget = 512; budget > 0 && d.port.available(); --budget) {   // наводки на висящей линии не должны съесть цикл
      const int v = d.port.read();
      if (v < 0) { ++tapErr; break; }
      if (v == 0xFF && d.hex == "FF") { d.at = now; continue; }   // iPad перед кадром шлёт сотни FF (синхро) — держим один
      char b[3]; snprintf(b, sizeof b, "%02X", v);
      if (d.hex.size() < 240) d.hex += b;
      d.at = now;
      if (d.who == 'P') tapPadAt = now;
    }
    if (d.hex.empty() || now - d.at < 6) continue;               // кадр закончился: пауза в линии > 6 мс
    // одиночные запросы/ответы времени (iAP 03 0C / 03 0D) идут раз в секунду — в журнал не пишем
    const bool timeOnly = d.hex.rfind("FF55", 0) == 0 && d.hex.size() >= 10 &&
                          (d.hex.compare(6, 4, "030C") == 0 || d.hex.compare(6, 4, "030D") == 0) &&
                          d.hex.size() == (std::strtoul(d.hex.substr(4, 2).c_str(), nullptr, 16) + 4) * 2;
    // Журнал — только в ручном пассивном режиме (диагностика): при авто-стыковке он рос бы до ~85 КБ (300 кадров),
    // а в режиме колонки свободно ~10–15 КБ — падение по памяти и петля перезагрузок, пока устройство в доке
    if (!timeOnly && d.hex != "FF" && !tapQuiet) {
      char h[24]; snprintf(h, sizeof h, "%u %u %c ", (unsigned)++tapSeq, (unsigned)d.at, d.who);
      tapLog.push_back(h + d.hex);
      if (tapLog.size() > 300) tapLog.pop_front();
    }
    d.hex.clear();
  }
}

// ---------- соединение с часами ----------
enum class Link { Release, Attach, Online, Passive };   // Passive — линии отпущены (к часам подключён iPad)
static Link state = Link::Release;
static uint32_t stateAt = 0, lastRxCount = 0, lastRxAt = 0, lastTxAt = 0;
static uint32_t lastPhilipsAt = 0;             // последний кадр Philips от часов (ответ на опрос и т.п.)
static uint8_t failCount = 0;                  // неудачных подключений подряд — после первой «вынимаем» устройство явно
static bool needOnConnected = false;

static void goState(Link s) { state = s; stateAt = millis(); }

// Пересоздать связь: часы должны увидеть «вынули — вставили». Со второй попытки — явное «вынуто» на 4 с.
static void relink(const char* why) {
  Serial.printf("[link] %s — переподключение (попытка %u)\n", why, failCount + 1);
  engine.connected = false; api::bump();
  if (failCount++ > 0) detectRemoved(); else detectRelease();
  goState(Link::Release);
}

// ---------- живой iPhone/iPad в доке: модуль уступает ему часы («по-старому») и забирает их, когда его вынут ----------
static bool dockAuto = true;                    // NVS link/auto — уступать сам (выключается через API)
static bool dockOn = false;                     // пассив включён сам: в доке устройство; выход — когда его вынут
static uint8_t dockHits = 0, dockPulses = 0;    // проб «устройство есть» подряд; фронтов для часов за эту стыковку
static uint32_t dockProbeAt = 0, dockPulseUntil = 0, dockQuietAt = 0, dockGoneAt = 0, dockCount = 0;
struct ProbeStat { uint32_t n = 0, held = 0, maxUs = 0, lastUs = 0; };   // maxUs — дольше всего уходила без устройства
static ProbeStat probeB12;
static std::string dockLast;                    // последнее событие дока — в /api/link

static void dockNote(const char* what) {
  dockLast = what;
  Serial.printf("[dock] %s\n", what);
  if (events.size() >= 100) dropOldEvent();
  events.push_back({millis(), false, std::string("док: ") + what});
}

static void probeCount(ProbeStat& s, uint32_t us) {
  ++s.n; s.lastUs = us;
  if (us >= kProbeUs) ++s.held; else if (us > s.maxUs) s.maxUs = us;
}

// Пассивный режим: модуль отпускает UART и detect и слушает обе линии (tapPoll). Вручную (API, переживает
// перезагрузку, выход — минута тишины) или сам (manual = false): в доке живое устройство, выход — когда его вынут.
static void passiveOn(bool manual) {
  audio::stop(); engine.connected = false; api::bump();
  detectRelease(); ClkSerial.end(); pinMode(PIN_CLK_TX, INPUT); txq.clear();
  ClkSerial.begin(CLK_BAUD, SERIAL_8N1, PIN_CLK_RX, -1);     // только приём: часы→iPad (IO16)
  PadSerial.setRxBufferSize(1024);
  PadSerial.begin(CLK_BAUD, SERIAL_8N1, PIN_CLK_TX, -1);     // только приём: iPad→часы (IO17); UART1 делим со sniff — он на паузе
  gpio_pullup_en(static_cast<gpio_num_t>(PIN_CLK_TX));          // без iPad линия висит — подтяжка гасит наводки
  gpio_pullup_en(static_cast<gpio_num_t>(PIN_CLK_RX));
  for (auto& d : tapDirs) d.hex.clear();
  if (manual) { Preferences pr; pr.begin("link", false); pr.putBool("passive", true); pr.end(); }   // переживает перезагрузку
  tapQuiet = !manual;
  dockOn = !manual; dockPulses = 0; dockPulseUntil = 0; dockGoneAt = 0; dockQuietAt = millis();
  // ЦАП сидит на том же звуковом узле дока: в простое держим 0 В, как при замерах с iPad (звук iPad шёл громко),
  // а не 1,5 В смещения для нашего звука
  audio::setDc(0);
  goState(Link::Passive);
}

// Фронт для часов: 3 с «вынуто», потом отпускаем — аудиоплата держит «есть», часы видят вставку и зовут устройство
static void dockPulse() {
  detectRemoved(); dockPulseUntil = millis() + 3000; ++dockPulses;
}

static void passiveOff() {
  if (state != Link::Passive) return;
  PadSerial.end(); ClkSerial.end(); sniff::redetect();       // UART1 возвращаем сниффу шины
  tapLog.clear(); tapLog.shrink_to_fit();
  for (auto& d : tapDirs) { d.hex.clear(); d.hex.shrink_to_fit(); }
  if (!dockOn) { Preferences pr; pr.begin("link", false); pr.putBool("passive", false); pr.end(); }
  dockOn = false; dockPulseUntil = 0; dockHits = 0; dockProbeAt = millis();
  audio::setDc(kLineDc);
  ClkSerial.begin(CLK_BAUD, SERIAL_8N1, PIN_CLK_RX, PIN_CLK_TX);
  failCount = 1; detectRemoved(); goState(Link::Release);
}

// Раз в секунду (после попадания — чаще): не появилось ли в доке живое устройство. Пока модуль держит «есть» —
// пробой (probeLine), пока линии отпущены — просто читаем. Четыре раза подряд «есть» — уступаем часы.
static void dockWatch() {
  if (state == Link::Passive || millis() - dockProbeAt < (dockHits ? 300u : 1000u)) return;
  dockProbeAt = millis();
  bool seen = false;
  if (det == Det::Asserted) {
    const uint32_t a = probeLine(PIN_DET_B12, LOW);
    probeCount(probeB12, a);
    seen = a >= kProbeUs;
  } else if (det == Det::Released && millis() - stateAt > 300) {
    seen = detectSaysDevice();
  }
  dockHits = seen ? dockHits + 1 : 0;
  // Решаем: 4 раза подряд (≈2 с). Не раньше 15 с после старта — пока часы грузятся (первые 2–3 с после включения
  // питания), линии detect у них пляшут (2026-11-03: ложная «стыковка» на 3,6 с). Будильник звонит — не отдаём часы.
  if (dockHits < 4 || !dockAuto || millis() < 15000 || engine.mode == core::Mode::Ringing) return;
  dockHits = 0; ++dockCount;
  const bool ours = det == Det::Asserted;               // часы сейчас говорят с модулем — им нужен новый фронт
  passiveOn(false);
  if (ours) { dockPulse(); dockNote("в доке iPhone/iPad — отпускаю линии, часы переподключаются к нему"); }
  else dockNote("в доке iPhone/iPad — линии не трогаю");
}

// Диагностика (GET /api/link/lines): каждую линию detect по очереди — отпустить без подтяжки / с подтяжкой вниз / вверх
// (уровень через 2 мс и мкс до смены уровня), держать 1 / 0 (что читается на самом выводе). Остальные две не трогаем.
// Часы видят иголки по 2 мс — сессия может переподключиться.
static std::string detectLines() {
  struct L { const char* n; int pin; } ls[] = {{"b11", PIN_DET_B11}, {"t12", PIN_DET_T12}, {"b12", PIN_DET_B12}};
  const Det was = det;
  std::string r = "{";
  for (auto& l : ls) {
    const gpio_num_t g = static_cast<gpio_num_t>(l.pin);
    const int held = gpio_get_level(g);                  // что было до (режим «вход+выход» — уровень вывода)
    char b[200]; int fall[3] = {}, lvl[3] = {};
    const gpio_pull_mode_t pulls[3] = {GPIO_FLOATING, GPIO_PULLDOWN_ONLY, GPIO_PULLUP_ONLY};
    for (int k = 0; k < 3; ++k) {
      gpio_set_pull_mode(g, pulls[k]);
      const int start = gpio_get_level(g);
      portENTER_CRITICAL(&probeMux);
      GPIO.enable_w1tc = 1u << l.pin;
      const int64_t t0 = esp_timer_get_time();
      int64_t dt = 0;
      while ((dt = esp_timer_get_time() - t0) < 2000 && gpio_get_level(g) == start) {}
      portEXIT_CRITICAL(&probeMux);
      fall[k] = static_cast<int>(dt);
      delay(2);
      lvl[k] = gpio_get_level(g);
      if (det != Det::Released) GPIO.enable_w1ts = 1u << l.pin;   // снова держим (отпущенные так и оставляем)
    }
    gpio_set_pull_mode(g, GPIO_FLOATING);
    int drv[2];
    for (int v = 1; v >= 0; --v) { detPin(l.pin, v, false, false); delayMicroseconds(200); drv[v] = gpio_get_level(g); }
    snprintf(b, sizeof b, "%s\"%s\":{\"was\":%d,\"free\":[%d,%d],\"down\":[%d,%d],\"up\":[%d,%d],\"drive1\":%d,\"drive0\":%d}",
             r.size() > 1 ? "," : "", l.n, held, lvl[0], fall[0], lvl[1], fall[1], lvl[2], fall[2], drv[1], drv[0]);
    r += b;
  }
  if (was == Det::Asserted) detectAssert(); else if (was == Det::Removed) detectRemoved(); else detectRelease();
  return r + "}";
}

static void serviceLink() {
  dockWatch();
  switch (state) {
    case Link::Release:                                  // «устройства нет» — часы сбрасывают состояние
      if (millis() - stateAt > (failCount > 1 ? 4000u : 2500u)) {
        link_->reset();
        txq.clear();
        detectAssert();                                  // фронт «вставили» → часы шлют StartIDPS
        Serial.println("[link] detect → жду attach");
        goState(Link::Attach);
      }
      break;
    case Link::Attach:
      if (link_->sessionOpen()) {
        Serial.println("[link] iAP-сессия открыта, Philips-хендшейк");
        if (client->handshake(6000)) {
          Serial.printf("[link] на связи: %s fw=%s\n", client->deviceName().c_str(), client->fwVersion().c_str());
          needOnConnected = true;
          lastRxCount = link_->rxCount(); lastRxAt = millis(); lastPhilipsAt = millis();
          failCount = 0;
          goState(Link::Online);
        } else relink("хендшейк не прошёл");
      } else if (millis() - stateAt > 15000) relink("attach не пришёл за 15 с");
      break;
    case Link::Passive: {
      if (dockPulseUntil) {                              // держим «вынуто», затем отпускаем — фронт «вставили»
        if (static_cast<int32_t>(millis() - dockPulseUntil) < 0) break;
        dockPulseUntil = 0; detectRelease(); dockQuietAt = millis(); dockGoneAt = 0;
        break;
      }
      if (!dockOn) {                                     // вручную: iPad отвечает часам раз в секунду; минута тишины — вынули
        if (millis() - std::max(stateAt, tapPadAt) > 60000) { Serial.println("[link] iPad молчит минуту — возвращаюсь на линию"); passiveOff(); }
        break;
      }
      if (millis() - dockQuietAt < 300) break;           // линии только что отпущены — пусть установятся
      if (detectSaysDevice()) dockGoneAt = 0;
      else if (!dockGoneAt) dockGoneAt = millis();
      else if (millis() - dockGoneAt > 2000) { dockNote("устройство вынули — модуль снова ведёт часы"); passiveOff(); break; }
      // Устройство в доке, но с часами не говорит (часы его не позвали) — ещё фронт: через 8 с после стыковки
      // (если её застали с отпущенными линиями), дальше раз в 20 с, не больше трёх за стыковку
      if (!dockGoneAt && dockPulses < 3 && millis() - std::max(dockQuietAt, tapPadAt) > (dockPulses ? 20000u : 8000u)) {
        dockNote(dockPulses ? "устройство молчит — ещё фронт для часов" : "устройство молчит — фронт для часов");
        dockPulse();
      }
      break;
    }
    case Link::Online:
      if (link_->rxCount() != lastRxCount) { lastRxCount = link_->rxCount(); lastRxAt = millis(); }
      if (millis() - lastRxAt > 15000) relink("часы молчат 15 с");               // обесточены или линия оборвана
      // часы живы (спрашивают время), но не отвечают на наши запросы — Philips-уровень завис
      else if (engine.connected && localNow() && millis() - lastPhilipsAt > 90000) relink("90 с без ответов на запросы");   // опрос идёт раз в 30 с
      break;
  }
}

// Светодиод (если включён в настройках): часто (5 Гц) — прошивка идёт, Wi-Fi нет; двойная вспышка — точка настройки;
// 1 Гц — сеть (Wi-Fi или Bluetooth) есть, часов нет; короткая вспышка раз в 3 с — всё на связи.
static void ledTick() {
  if (!boardLed) { digitalWrite(PIN_LED, LOW); return; }
  uint32_t t = millis();
  bool on;
  const bool net = netmgr::wifiUp() || netmgr::info().link == "ble";   // в режиме Bluetooth Wi-Fi нет — так и задумано
  if (state == Link::Online && net) on = t % 3000 < 80;
  else if (net) on = t % 1000 < 500;
  else if (netmgr::info().ap) { uint32_t p = t % 2000; on = p < 100 || (p >= 250 && p < 350); }
  else on = t % 200 < 100;
  digitalWrite(PIN_LED, on ? HIGH : LOW);
}

// ---------- Bluetooth-колонка и светомузыка ----------
// Два режима, переключение — перезагрузкой (Bluetooth A2DP и Wi-Fi вместе не помещаются в ОЗУ ESP32-WROOM):
//  «Wi-Fi» — как всегда: приложение, веб, обновление по воздуху; Bluetooth не запускается.
//  «Колонка» — Wi-Fi выключен, модуль — Bluetooth-колонка со светомузыкой; будильники и кнопки часов работают.
// Переключить: POST /api/speaker {"bt":true|false} (приложение/веб) или Звук ×3 за 3 с на часах (туда и обратно).
// Из колонки модуль сам возвращается в Wi-Fi через 10 мин без подключённого телефона; после падения — тоже.
// Телефон играет через модуль (btsink → audio:: → ЦАП → вход дока). Пока идёт поток: аудиотракт часов открыт
// (58 01, дальше 58 02 раз в 0,5 с — общий код ниже), громкость телефона = громкость часов, а светомузыка
// ведёт табло (4 полосы) и лампу (уровень по громкости: тихо — красный, громко — белый).
static struct {
  bool bt = false, showEq = true, lamp = true;     // bt — режим «колонка» (NVS «spk/mode»), остальное — светомузыка
  bool active = false;                             // поток идёт и аудиотракт часов открыт нами
  bool lm = false;                                 // светомузыка сейчас работает
  bool lampWasOn = false, lampOurs = false;
  uint8_t lampWasLvl = 1, lampSent = 0, volReported = 255;
  uint32_t lampAt = 0;
  float bandPeak[4] = {}, envPeak = 0;
  int phoneVol = -1;                               // 0..127, последняя громкость от телефона
  int volSeen = -1;                                // громкость часов, которую мы уже видели (чужая смена — в журнал)
  uint32_t keysAt = 0;                             // millis() последнего +/− на часах
} spk;

static void spkLoad() {
  Preferences p; p.begin("spk", true);
  spk.bt = p.getBool("mode", false); spk.showEq = p.getBool("eq", true); spk.lamp = p.getBool("lamp", true);
  p.end();
}
// Режим следующей загрузки (−1 — как сейчас). spk.bt до перезагрузки НЕ меняем: это режим ЭТОЙ загрузки, по нему
// основной цикл решает, что запущено. Раньше switchMode менял spk.bt сразу, и за 1,5 с до перезагрузки цикл звал
// netmgr::loop() без netmgr::begin() → «нет сети 90 с» → точка доступа без стека lwip → паника (2026-11-03).
static int8_t spkNext = -1;
// Звучание динамика (core/eq.h): NVS «eq», сброс к заводским стирает (netmgr::factoryReset)
static core::EqParams eqCur;
static void eqApply() { audio::setEq(eqCur.on, eqCur.hp, eqCur.body, eqCur.presence, eqCur.treble); }
static void eqSave() {
  Preferences p; p.begin("eq", false);
  p.putBool("on", eqCur.on); p.putShort("hp", eqCur.hp); p.putChar("body", eqCur.body);
  p.putChar("pres", eqCur.presence); p.putChar("treb", eqCur.treble); p.putString("preset", eqCur.preset);
  p.end();
}
static void eqLoad() {
  Preferences p; p.begin("eq", true);
  eqCur.on = p.getBool("on", eqCur.on); eqCur.hp = p.getShort("hp", eqCur.hp); eqCur.body = p.getChar("body", eqCur.body);
  eqCur.presence = p.getChar("pres", eqCur.presence); eqCur.treble = p.getChar("treb", eqCur.treble);
  const String pr = p.getString("preset", eqCur.preset);
  p.end();
  std::strncpy(eqCur.preset, pr.c_str(), sizeof eqCur.preset - 1); eqCur.preset[sizeof eqCur.preset - 1] = 0;
  core::eqClamp(eqCur);
}
static void spkSave() {
  Preferences p; p.begin("spk", false);
  p.putBool("mode", spkNext >= 0 ? spkNext == 1 : spk.bt); p.putBool("eq", spk.showEq); p.putBool("lamp", spk.lamp);
  p.end();
}
static uint32_t spkConnSec = 0, spkPlaySec = 0;   // итоги сеанса колонки — в NVS, в Wi-Fi видны в /api/events
static uint32_t spkOpens = 0;                     // сколько раз открывали тракт часов (каждый раз — пауза и «подъём» звука)
static std::deque<std::string> volLog;            // журнал громкости сеанса: «секунда источник значение»
static void volNote(const char* who, int v) {
  char b[40]; snprintf(b, sizeof b, "%u %s %d", (unsigned)(millis() / 1000), who, v);
  volLog.push_back(b); if (volLog.size() > 14) volLog.pop_front();
}
static void switchMode(bool speaker, const char* why) {   // сохранить режим и перезагрузиться через 1,5 с
  if (modeSwitchAt || speaker == spk.bt) return;            // уже переключаемся / и так этот режим
  if (spk.bt && !speaker) {
    char r[320];                                   // целиком ~1,5 КБ — частями, иначе хвост (и полбуквы) обрезался
    snprintf(r, sizeof r, "колонка: телефон %s, связь %u с, музыка %u с, мин. память %u, макс. перерыв 58 %u мс на %u с, дольше 1 с: %u; "
             "макс. оборот цикла %u мс на %u с; лампа в такт %s; ", btsink::peer().c_str(),
             (unsigned)spkConnSec, (unsigned)spkPlaySec, (unsigned)ESP.getMinFreeHeap(), (unsigned)audioGapMax, (unsigned)audioGapAt,
             (unsigned)audioGapsLong, (unsigned)loopGapMax, (unsigned)loopGapAt, spk.lamp ? "вкл" : "выкл");
    std::string vl; for (const auto& e : volLog) vl += (vl.empty() ? "" : ", ") + e;
    std::string rl; for (const auto& e : rxOdd) rl += (rl.empty() ? "" : ", ") + e;
    const std::string all = std::string(r) + "тракт открыт " + std::to_string(spkOpens) + " раз; громкость (с, кто, 1..20): " + vl +
                            "; от часов: " + rl + "; " + spp::stats() + "; " + audio::status() + "; " + audio::btDiag();
    Preferences p; p.begin("spk", false); p.putString("last", all.c_str()); p.end();
  }
  spkNext = speaker ? 1 : 0; spkSave();
  char b[80]; snprintf(b, sizeof b, "режим → %s (%s)", speaker ? "колонка" : "Wi-Fi", why);
  events.push_back({millis(), false, b});
  Serial.println(b);
  snprintf(b, sizeof b, "→ %s: %s", speaker ? "колонка" : "Wi-Fi", why); bootlog::why(b);
  modeSwitchAt = millis() + 1500;
}
static uint8_t phoneToClock(int v) { return static_cast<uint8_t>((std::max(0, std::min(127, v)) * 20 + 63) / 127); }

static void lmStart() {
  if (spk.lm || (!spk.showEq && !spk.lamp)) return;
  spk.lm = true;
  audio::setAnalyze(true);
  for (auto& p : spk.bandPeak) p = -40.f;
  spk.envPeak = -40.f;
  if (spk.showEq) { disp.mode = DispMode::Eq; disp.text.clear(); disp.until = 0; disp.periodMs = 66; disp.live = true; }
  spk.lampWasOn = engine.lightOn; spk.lampWasLvl = engine.lightLevel; spk.lampOurs = false; spk.lampSent = 0;
}

static void lmStop() {
  if (!spk.lm) return;
  spk.lm = false;
  audio::setAnalyze(false);
  if (disp.live) dispClock();
  if (spk.lampOurs && engine.mode == core::Mode::Idle) engine.setLight(spk.lampWasOn, spk.lampWasLvl);   // вернуть как было
  spk.lampOurs = false;
}

static void lmTick() {
  float rms[4], env;
  if (!spk.lm || !audio::bands(rms, env)) return;
  if (spk.showEq && disp.live && disp.mode == DispMode::Eq) {
    for (int i = 0; i < 4; ++i) {                  // у каждой полосы свой «потолок»: мгновенно вверх, вниз 3 дБ/с
      const float db = 20.f * log10f(rms[i] + 1e-6f);
      spk.bandPeak[i] = std::max(db, spk.bandPeak[i] - 0.15f);
      const float x = (db - spk.bandPeak[i] + 24.f) / 24.f;   // окно 24 дБ под потолком → 0..5
      disp.lvl[i] = static_cast<uint8_t>(std::max(0.f, std::min(5.f, x * 6.f)));
    }
    if (millis() - disp.last >= disp.periodMs) { disp.last = millis(); disp.pair = wul::eqPairs(disp.lvl); dispPush(false); }
  }
  // лампа сама сглаживает шаги (~5 уровней/с) — чаще 2–3 раз в секунду слать нет смысла
  if (spk.lamp && engine.mode == core::Mode::Idle && state == Link::Online) {
    const float db = 20.f * log10f(env + 1e-6f);
    spk.envPeak = std::max(db, spk.envPeak - 0.075f);           // 1,5 дБ/с
    float x = (db - spk.envPeak + 18.f) / 18.f;
    x = x < 0 ? 0 : x > 1 ? 1 : x;
    const uint8_t lvl = static_cast<uint8_t>(3 + x * 17 + 0.5f);   // 3..20
    if (millis() - spk.lampAt >= 400 && std::abs(lvl - spk.lampSent) >= 2 && txq.size() < 3) {
      spk.lampAt = millis(); spk.lampSent = lvl; spk.lampOurs = true;
      engine.setLight(true, lvl);
    }
  }
}

// Громкость: телефон → часы всегда; часы → телефону — только после +/− на самих часах. Раньше телефону уходила
// любая смена громкости часов, а часы и сами сообщают громкость (свойство 00) — телефон отвечал своей, модуль
// ставил её часам, и громкость гуляла по кругу: «сама то тише, то громче» (2026-11-03).
// Тракт часов закрываем не через 1,5 с без данных, а через 6 с: короткий провал потока — не повод заново открывать
// тракт (58 01, громкость, светомузыка заново) — из такой паузы часы выходили с «подъёмом» звука.
static const uint32_t kSpkIdleCloseMs = 6000;
static void spkTick() {
  if (!btsink::enabled()) return;
  static uint32_t seenBtn = 0;
  static bool started = false;
  if (!started) {                                   // старые нажатия в журнале и стартовая громкость — не события
    started = true;
    if (!engine.buttons.empty()) seenBtn = engine.buttons.back().ms;
    spk.volSeen = engine.volume;
  }
  for (auto it = engine.buttons.rbegin(); it != engine.buttons.rend() && it->ms > seenBtn; ++it)
    if (it->code == 0x05 || it->code == 0x06) spk.keysAt = millis();
  if (!engine.buttons.empty()) seenBtn = engine.buttons.back().ms;
  const bool keys = spk.keysAt && millis() - spk.keysAt < 1500;
  if (engine.volume != spk.volSeen) {               // громкость часов сменилась не нами
    volNote(keys ? "кнопка" : "часы", engine.volume);
    spk.volSeen = engine.volume;
    if (keys && spk.active && engine.mode == core::Mode::Idle && engine.volume != spk.volReported) {   // +/− на часах → телефону
      spk.volReported = engine.volume;
      btsink::reportVolume(engine.volume * 127 / 20);
    }
  }
  int v;
  if (btsink::takeVolume(v)) {                     // громкость с телефона → часы
    spk.phoneVol = v;
    const uint8_t c = phoneToClock(v);
    volNote("тел", c);
    spk.volReported = c;
    if (spk.active && engine.mode == core::Mode::Idle && c != engine.volume) { engine.setVolume(c); spk.volSeen = c; }
  }
  const bool live = audio::btActive();
  if (live && !spk.active && state == Link::Online && engine.mode != core::Mode::Ringing) {
    spk.active = true; ++spkOpens;
    txq.push_front(wul::setProperty(0x58, {0x01}));   // вперёд: в хвосте очереди (лампа) «58» опаздывала до 1,25 с
    const uint8_t c = spk.phoneVol >= 0 ? phoneToClock(spk.phoneVol) : engine.volume;
    engine.setVolume(c); spk.volReported = c; spk.volSeen = c;
    volNote("откр", c);
    lmStart();
    Serial.printf("[bt] играет: %s, громкость %u\n", btsink::peer().c_str(), c);
  } else if (spk.active && (audio::btIdleMs() > kSpkIdleCloseMs || state != Link::Online)) {
    volNote("закр", engine.volume);
    spk.active = false;
    lmStop();
    if (state == Link::Online && !audio::playing()) txq.push_back(wul::audioStop());
  }
  if (spk.active && engine.mode == core::Mode::Ringing) {   // звонок важнее: телефон на паузу
    btsink::pause();
    lmStop();
    spk.active = false;
  }
  if (spk.lm && engine.mode != core::Mode::Idle && spk.lampOurs) spk.lampOurs = false;   // свет ведёт рассвет/закат
  lmTick();
}

static void spkApply() {                           // режим «колонка»: поднять приёмник (до всего остального)
  if (spk.bt && !btsink::enabled()) {
    const uint32_t before = ESP.getFreeHeap();
    char buf[96];
    if (before < 65000 || !audio::btBuffer()) {       // стек Bluetooth без памяти падает (паника) — лучше не включать
      snprintf(buf, sizeof buf, "BT: мало памяти (%u), колонка не включена", (unsigned)before);
      events.push_back({millis(), false, buf});
      return;
    }
    rtcHeap[0] = before; rtcHeap[1] = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT); rtcHeap[2] = rtcHeap[3] = rtcHeap[4] = rtcHeap[5] = 0;
    btsink::memProbe = []() { rtcHeap[2] = ESP.getFreeHeap(); rtcHeap[3] = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT); };
    btsink::begin(netmgr::deviceName().c_str());
    spp::begin(netmgr::deviceName().c_str());        // канал приложения: Wi-Fi в этом режиме нет
    rtcHeap[4] = ESP.getFreeHeap(); rtcHeap[5] = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    snprintf(buf, sizeof buf, "BT: колонка включена, память %u → %u", (unsigned)before, (unsigned)ESP.getFreeHeap());
    events.push_back({millis(), false, buf});
  }
}

static void modeTick() {
  if (modeSwitchAt) {
    if (static_cast<int32_t>(millis() - modeSwitchAt) >= 0) { netmgr::markGood(); ESP.restart(); }
    return;
  }
  // Звук ×3 за 3 с (не во время будильника/заката) — переключить режим
  static uint32_t seen = 0;
  if (!engine.buttons.empty() && engine.buttons.back().ms != seen) {
    seen = engine.buttons.back().ms;
    int n = 0;
    for (auto it = engine.buttons.rbegin(); it != engine.buttons.rend() && seen - it->ms <= 3000; ++it) {
      if (it->code != 0x07) break;
      ++n;
    }
    if (n >= 3 && engine.mode == core::Mode::Idle) {
      engine.buttons.clear();
      if (engine.soundOn) engine.setSound(false);
      switchMode(!spk.bt, "Звук ×3");
      return;
    }
  }
  // колонка без телефона 10 мин — назад в Wi-Fi (иначе модуль недоступен приложению)
  if (spk.bt) {
    static uint32_t phoneAt = 0, secAt = 0;
    if (millis() - secAt >= 1000) { secAt = millis(); spkConnSec += btsink::connected(); spkPlaySec += spk.active; }
    if (btsink::connected() || spk.active || engine.mode != core::Mode::Idle) phoneAt = millis();
    if (millis() - phoneAt > 10UL * 60 * 1000) switchMode(false, "10 мин без телефона");
  }
  // в колонке при первом выходе на связь — 3 с эквалайзера на табло: «я колонка»
  static bool shown = false;
  if (spk.bt && !shown && state == Link::Online && millis() > 3000) {
    shown = true;
    disp.mode = DispMode::Eq; disp.text.clear(); disp.live = false; disp.periodMs = 100; disp.until = millis() + 3000;
  }
}

void setup() {
  // brownout-детектор оставлен включённым (порог 2,43 В): при быстром «выкл-вкл» часов питание
  // проседает не до нуля, и без него ESP32 зависал до полного обесточивания
  detectRelease();
  { Preferences p; if (p.begin("ui", true)) { boardLed = p.getBool("led", false); p.end(); } }
  pinMode(PIN_LED, OUTPUT); digitalWrite(PIN_LED, boardLed ? HIGH : LOW);   // включён — горит с первой строки прошивки
  Serial.begin(115200);
  delay(300);
  {                                                       // счётчик загрузок и просадок — в /api/events
    Preferences d;
    d.begin("diag", false);
    int reason = esp_reset_reason();
    uint32_t boots = d.getUInt("boots", 0) + 1, bo = d.getUInt("brownout", 0) + (reason == ESP_RST_BROWNOUT);
    d.putUInt("boots", boots); d.putUInt("brownout", bo);
    d.end();
    char buf[96];
    // Включение от часов часто приходит как reset=7 (RTC WDT при медленном нарастании питания) — это не падение:
    // после настоящего обесточивания RTC-память пуста (нет rtcMagic), такие загрузки не считаем
    if (rtcMagic != 0xB007C0DEu || reason == ESP_RST_POWERON) {
      rtcMagic = 0xB007C0DEu; rtcCrashes = 0; memset(rtcHeap, 0, sizeof rtcHeap);
    } else if (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT ||
               reason == ESP_RST_BROWNOUT) ++rtcCrashes;
    safeMode = rtcCrashes >= 2;
    snprintf(buf, sizeof buf, "BOOT #%u reset=%d brownout=%u%s%s", (unsigned)boots, reason, (unsigned)bo, safeMode ? " БЕЗОПАСНЫЙ" : "",
             netmgr::otaPending() ? " OTA на проверке" : "");
    if (rtcMagic == 0xB007C0DEu && rtcHeap[0] && rtcHeap[0] < 400000) {
      char h[96];
      snprintf(h, sizeof h, "BT прошлый: %u/%u →контр→ %u/%u →стек→ %u/%u", (unsigned)rtcHeap[0], (unsigned)rtcHeap[1],
               (unsigned)rtcHeap[2], (unsigned)rtcHeap[3], (unsigned)rtcHeap[4], (unsigned)rtcHeap[5]);
      events.push_back({0, false, h});
    }
    events.push_back({0, false, buf});
    events.push_back({0, false, bootlog::begin(boots, reason, reason == ESP_RST_POWERON)});
    Serial.printf("\n=== Wake-up Light ESP32 === %s\n", buf);
  }
  bool passive;                                           // к часам подключён iPad — на линию не выходим даже на миг
  { Preferences pr; pr.begin("link", true); passive = pr.getBool("passive", false); dockAuto = pr.getBool("auto", true); pr.end(); }
  // в доке уже живой iPhone/iPad (линии отпущены с первой строки) — тоже не выходим на линию
  // B12 читаем 5 раз за 200 мс: при включении питания линии detect у часов пляшут (ложная «стыковка» стоила бы ~5–8 с)
  bool dockAtBoot = !passive && dockAuto && detectSaysDevice();
  for (int i = 0; dockAtBoot && i < 4; ++i) { delay(50); dockAtBoot = detectSaysDevice(); }
  ClkSerial.setRxBufferSize(1024);                        // запас на блокирующие операции (скан Wi-Fi, I2C лампы)
  ClkSerial.begin(CLK_BAUD, SERIAL_8N1, PIN_CLK_RX, passive || dockAtBoot ? -1 : PIN_CLK_TX);

  static iap::Link link({
    [](const uint8_t* d, size_t n) { ClkSerial.write(d, n); },
    []() -> int { return ClkSerial.available() ? ClkSerial.read() : -1; }});
  link_ = &link;
  link.timeSource = [](iap::Link::DateTime& t) {          // часы показывают это время на табло
    struct tm lt;
    if (disp.mode != DispMode::Clock) {
      bool ok = localTm(lt);
      t = {ok ? lt.tm_year + 1900 : 2026, ok ? lt.tm_mon + 1 : 1, ok ? lt.tm_mday : 1, disp.pair.h, disp.pair.m};
      return true;
    }
    if (!localTm(lt)) return false;
    t = {lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min};
    return true;
  };

  wul::Transport tr;
  tr.write    = [](const uint8_t* d, size_t n) { link_->sendPhilips(d, n); };
  tr.readByte = []() -> int { return link_->readPhilipsByte(); };
  static wul::Client c(tr);
  client = &c;
  c.onFrame = [](const wul::Frame& f) {
    lastPhilipsAt = millis();
    logFrame(true, f);
    uint8_t code, lvl;
    if (wul::parseRc(f, code, lvl)) engine.onButton(code, lvl, localNow(), millis());
    else if (f.cmd == wul::CMD_RET_VALUE && f.hasProperty()) engine.onClockValue(f.property(), f.value());
  };

  loadAll();
  engine.send = [](const wul::Frame& f) { if (state == Link::Online && txq.size() < 64) txq.push_back(f); };
  engine.persistAlarms = []() { saveBlob("alarms", core::packAlarms(engine.alarms)); };
  engine.persistLog = []() { saveBlob("log", core::packLog(engine.log)); };
  engine.persistSettings = saveSettings;
  engine.onChanged = []() { api::bump(); };
  // мелодий во флеше пока нет — вместо них мягкий прерывистый тон (у каждой мелодии своя высота)
  // мелодии оригинала из внешней флеш; если файла нет — мягкий прерывистый тон своей высоты
  engine.playSound = [](uint8_t idx, uint8_t vol) {
    // громкость задают часы (их аудиопроцессор), модуль отдаёт ровный линейный уровень
    alarmVol = vol;
    // Часами владеет iPhone/iPad из дока: будильники и звук — его (как с завода). Свет модуль сейчас не ведёт, а наш
    // звук лёг бы на общий вход дока поверх звука устройства (и без 58 01 часы его всё равно глушат)
    if (state == Link::Passive) return;
    if (idx >= core::kSoundCount || !audio::play(core::kSoundFiles[idx], kLineGain, true)) audio::tone(440 + idx * 60, kLineGain * 0.5f, 0, audio::Ch::Both, true);
  };
  engine.stopSound = []() { audio::stop(); };
  engine.factoryReset = []() { resetAt = millis() + 2500; Serial.println("[btn] сочетание «сброс» (Снуз ×10)"); };
  // звук будильника/заката раньше играл iPad через аудио дока; у нас — ЦАП PCM5102A (подключим позже)

  spkLoad();
  eqLoad(); eqApply();
  // Колонка падала — назад в Wi-Fi. Решать ДО configTzTime: он заодно поднимает стек lwip (esp_netif_init), и без
  // него веб-сервер в netmgr::begin() падал на socket() — после падения колонки модуль падал ещё раз (2026-11-03)
  if (spk.bt && safeMode) { spk.bt = false; spkSave(); events.push_back({0, false, "режим → Wi-Fi (колонка падала)"}); }
  if (!spk.bt) configTzTime(engine.settings.tz, NTP_SERVER);   // без Wi-Fi SNTP не нужен: время идёт по часам ESP
  applyTz();
  pinMode(PIN_BOOT, INPUT_PULLUP);

  api::Context ac;
  ac.engine = &engine;
  ac.now = localNow;
  ac.deviceName = []() { return client->deviceName(); };
  ac.firmware = []() { return client->fwVersion(); };
  ac.events = []() {
    std::string j = "[";
    for (const auto& e : events) {
      if (j.size() > 1) j += ',';
      j += "{\"ms\":" + std::to_string(e.ms) + ",\"rx\":" + (e.rx ? "true" : "false") + ",\"hex\":\"";
      for (char c : e.hex) {                             // в итогах — имя телефона и заголовок трека: экранируем
        if (c == '"' || c == '\\') j += '\\';
        if (static_cast<unsigned char>(c) >= 0x20) j += c;
      }
      j += "\"}";
    }
    return j + "]";
  };
  ac.netInfo = netmgr::info;
  ac.scan = netmgr::scan;
  ac.saveNetwork = netmgr::save;
  ac.factoryReset = []() { netmgr::factoryReset("из приложения/API"); };
  ac.setTime = setLocalTime;
  ac.settingsChanged = applyTz;
  ac.bus = [](uint32_t since) { return sniff::json(since); };
  // в пассивном режиме UART1 занят прослушкой iPad (PadSerial) — шину не трогаем
  ac.busCmd = [](uint32_t baud) { if (state == Link::Passive) return; if (baud) sniff::setBaud(baud); else sniff::redetect(); };
  ac.busCapture = [](uint32_t ms) { if (state != Link::Passive) sniff::captureEdges(ms); };
  ac.busEdges = sniff::edgesJson;
  ac.tone = [](int hz, float vol, int sec, int ch, bool beep, int hz2) {
    audio::tone(static_cast<uint16_t>(hz), vol, static_cast<uint32_t>(sec < 0 ? 0 : sec), static_cast<audio::Ch>(ch), beep, static_cast<uint16_t>(hz2));
  };
  ac.toneStop = audio::stop;
  ac.toneStatus = []() { return std::string(audio::status()); };
  ac.toneRepin = audio::repin;
  ac.toneBits = [](int bits) { audio::format(bits, false); };
  ac.toneDc = audio::setDc;
  ac.toneBridge = audio::setBridge;
  ac.eqGet = []() { return eqCur; };
  ac.boardLedGet = []() { return boardLed; };
  ac.boardLedSet = [](bool on) {
    if (on == boardLed) return;
    boardLed = on;
    Preferences p; p.begin("ui", false); p.putBool("led", on); p.end();
  };
  ac.eqSet = [](const core::EqParams& q) { eqCur = q; eqApply(); eqSave(); };
  ac.melodies = melodies::listJson;
  ac.melodiesFormat = melodies::format;
  ac.melodyCrc = [](const std::string& n) { return melodies::crc32(n.c_str()); };
  ac.holdAudio = [](bool on) { holdAudio = on; };
  ac.drive = audio::setDrive;
  ac.keepAlive = [](bool on) { keepAlive = on; };
  ac.play = [](const std::string& name, float vol, bool loop) { return audio::play(name.c_str(), vol, loop); };
  ac.flash = [](bool test) { return test ? xflash::selfTest() : xflash::idJson(); };
  ac.coredump = []() {                                 // адреса — в addr2line с firmware.elf той сборки
    esp_core_dump_summary_t s;
    if (esp_core_dump_get_summary(&s) != ESP_OK) return std::string("{\"ok\":false}");
    std::string r = "{\"ok\":true,\"task\":\"" + std::string(s.exc_task) + "\"";
    char b[64];
    snprintf(b, sizeof b, ",\"pc\":\"0x%08x\",\"cause\":%u,\"vaddr\":\"0x%08x\",\"bt\":[", (unsigned)s.exc_pc,
             (unsigned)s.ex_info.exc_cause, (unsigned)s.ex_info.exc_vaddr);
    r += b;
    for (uint32_t i = 0; i < s.exc_bt_info.depth; ++i) { snprintf(b, sizeof b, "%s\"0x%08x\"", i ? "," : "", (unsigned)s.exc_bt_info.bt[i]); r += b; }
    r += "]}";
    return r;
  };
  ac.shortTest = []() {
    // каждый вывод по очереди — 1 и 0, остальные — входы с противоположной подтяжкой; кто повторил оба уровня — замкнут
    static const int pins[] = {4, 5, 13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34};
    ClkSerial.end();
    std::string r = "{\"ok\":true,\"pairs\":[";
    bool first = true;
    for (int d : pins) {
      if (d == 34) continue;                                   // только вход
      bool follows[64] = {};
      for (int level = 1; level >= 0; --level) {
        for (int p : pins) if (p != d) pinMode(p, p == 34 ? INPUT : (level ? INPUT_PULLDOWN : INPUT_PULLUP));
        pinMode(d, OUTPUT); digitalWrite(d, level);
        delayMicroseconds(200);
        for (int p : pins) if (p != d) { bool same = digitalRead(p) == level; follows[p] = level ? same : (follows[p] && same); }
      }
      pinMode(d, INPUT);
      for (int p : pins) if (p != d && p > d && follows[p]) {
        r += std::string(first ? "" : ",") + "[" + std::to_string(d) + "," + std::to_string(p) + "]"; first = false;
      }
    }
    netmgr::restartIn(2000, "тест замыканий");
    return r + "]}";
  };
  ac.relink = []() { if (state == Link::Passive) passiveOff(); else relink("по запросу API"); };   // из пассива — сразу на линию
  ac.passive = [](bool on) { if (on) passiveOn(true); else passiveOff(); };
  ac.dockAuto = [](bool on) {                           // уступать часы iPhone/iPad в доке сам (по умолчанию да)
    dockAuto = on; dockHits = 0;
    Preferences pr; pr.begin("link", false); pr.putBool("auto", on); pr.end();
  };
  ac.detectLines = detectLines;
  ac.dock = []() { return state == Link::Passive ? (dockOn ? "device" : "passive") : ""; };
  ac.tap = [](uint32_t since) {
    std::string r = "{\"n\":" + std::to_string(tapSeq) + ",\"err\":" + std::to_string(tapErr) + ",\"log\":[";
    bool first = true;
    int left = 120;                                     // ответ не больше ~20 КБ — дальше читать с нового since
    for (auto& e : tapLog)
      if (std::strtoul(e.c_str(), nullptr, 10) > since && left-- > 0) { r += std::string(first ? "\"" : ",\"") + e + "\""; first = false; }
    return r + "]}";
  };
  ac.linkStats = []() {
    char b[128];
    snprintf(b, sizeof b, "{\"bytes\":%u,\"good\":%u,\"bad\":%u,\"state\":%d}", (unsigned)link_->rxBytes(),
             (unsigned)link_->rxCount(), (unsigned)link_->rxBad(), static_cast<int>(state) * 10 + failCount);
    std::string s(b); s.pop_back();
    // док: auto — уступать сам; on — сейчас уступили; b12 — пробы (n, удержано, макс./последнее мкс)
    char d[300];
    snprintf(d, sizeof d, ",\"dock\":{\"auto\":%s,\"on\":%s,\"count\":%u,\"pulses\":%u,\"window\":%u,"
             "\"b12\":[%u,%u,%u,%u],\"b12stuck\":%u,\"lines\":\"%d%d%d\",\"last\":\"",
             dockAuto ? "true" : "false", dockOn ? "true" : "false", (unsigned)dockCount, (unsigned)dockPulses, (unsigned)kProbeUs,
             (unsigned)probeB12.n, (unsigned)probeB12.held, (unsigned)probeB12.maxUs, (unsigned)probeB12.lastUs,
             (unsigned)b12Stuck, digitalRead(PIN_DET_B11), digitalRead(PIN_DET_T12), digitalRead(PIN_DET_B12));
    return s + d + dockLast + "\"},\"last\":" + link_->lastRx() + "}";
  };
  ac.lampProbe = lampi2c::probe;
  ac.lampScan = lampi2c::scanJson;
  ac.lampRead = [](uint16_t reg, uint8_t n) {
    std::vector<uint8_t> v;
    std::string err = lampi2c::read(reg, n, v);
    if (!err.empty()) return "{\"ok\":false,\"error\":\"" + err + "\"}";
    std::string hex; char b[3];
    for (uint8_t x : v) { snprintf(b, sizeof b, "%02X", x); hex += b; }
    return "{\"ok\":true,\"hex\":\"" + hex + "\"}";
  };
  ac.lampWrite = [](uint16_t reg, const std::vector<uint8_t>& d) {
    std::string err = lampi2c::write(reg, d);
    return err.empty() ? std::string("{\"ok\":true}") : "{\"ok\":false,\"error\":\"" + err + "\"}";
  };
  ac.displayText = [](const std::string& text, int sec) -> std::string {
    wul::DisplayPair p;
    std::string err;
    if (!wul::textToPairs(text, p, &err)) return err;
    disp.mode = DispMode::Text; disp.pair = p; disp.text = text;
    disp.until = sec > 0 ? millis() + static_cast<uint32_t>(sec) * 1000 : 0;
    dispPush();
    return "";
  };
  ac.displayEq = [](int fps, int sec) {
    disp.mode = DispMode::Eq; disp.text.clear(); disp.live = false;
    disp.periodMs = 1000 / (fps < 1 ? 1 : fps > 20 ? 20 : fps);
    disp.until = sec > 0 ? millis() + static_cast<uint32_t>(sec) * 1000 : 0;
    dispPush();
  };
  ac.displayClock = dispClock;
  ac.speakerInfo = []() {
    api::SpeakerInfo i;
    i.available = true; i.bt = spk.bt; i.on = btsink::enabled(); i.connected = btsink::connected();
    i.playing = spk.active; i.showEq = spk.showEq; i.lamp = spk.lamp;
    i.name = netmgr::deviceName(); i.peer = btsink::peer();
    i.title = btsink::title(); i.artist = btsink::artist();
    return i;
  };
  ac.speakerSet = [](int bt, int showEq, int lamp) {
    if (showEq >= 0) spk.showEq = showEq;
    if (lamp >= 0) spk.lamp = lamp;
    if (spk.lm && ((showEq == 0 && disp.live) || lamp == 0)) { lmStop(); if (spk.active) lmStart(); }
    if (spk.active && !spk.lm) lmStart();
    spkSave();
    if (bt >= 0 && static_cast<bool>(bt) != spk.bt) switchMode(bt, "из приложения");
  };
  ac.displayRaw = [](int h, int m) {                   // диагностика: сырые значения пары
    disp.mode = DispMode::Text; disp.text = "raw"; disp.until = 0;
    disp.pair = {static_cast<uint8_t>(h), static_cast<uint8_t>(m)};
    dispPush();
  };
  ac.displayInfo = []() {
    api::DisplayInfo i;
    i.mode = disp.mode == DispMode::Text ? "text" : disp.mode == DispMode::Eq ? "eq" : "clock";
    i.text = disp.text;
    i.secondsLeft = disp.until ? std::max(0, static_cast<int32_t>(disp.until - millis()) / 1000) : 0;
    return i;
  };

  memMark("до api");
  api::begin(ac);
  memMark("до BT");
  // Режим «колонка»: Bluetooth первым (цельная куча), Wi-Fi не поднимаем вовсе. Падали — уже в Wi-Fi (выше, у spkLoad).
  if (spk.bt) { netmgr::beginOffline(); spkApply(); memMark("колонка"); }
  else {
    Preferences p; p.begin("spk", true); const String last = p.getString("last", ""); p.end();
    if (last.length()) events.push_back({0, false, last.c_str()});
    netmgr::begin(WIFI_SSID, WIFI_PASS);
    memMark("сеть");
  }
  sniff::begin(PIN_SNIFF);
  lampi2c::begin(PIN_LAMP_SCL, PIN_LAMP_SDA);
  audio::begin(PIN_I2S_BCK, PIN_I2S_LRCK, PIN_I2S_DIN);
  audio::setDc(kLineDc);
  audio::setBridge(true);           // вход дока дифференциальный (C506 − C505): R = −моно, +6 дБ, без хрипа (2026-11-02)
  audio::setBtGain(kLineGain);
  if (safeMode) Serial.println("[bt] безопасный режим: колонка не запускается (прошлые загрузки падали)");
  memMark("звук");
  xflash::begin(PIN_FL_SCK, PIN_FL_MISO, PIN_FL_MOSI, PIN_FL_CS);
  melodies::begin();
  memMark("мелодии");

  goState(Link::Release);
  if (passive) passiveOn(true);
  else if (dockAtBoot) { passiveOn(false); dockNote("при загрузке в доке iPhone/iPad — линии не трогаю"); }
}

void loop() {
  {
    static uint32_t prev = 0;
    const uint32_t t = millis();
    if (prev && t - prev > loopGapMax && (spk.active || !spk.bt)) { loopGapMax = t - prev; loopGapAt = t / 1000; }   // в колонке — пока играет
    prev = t;
  }
  if (state == Link::Passive) tapPoll();                 // линии слушаем сами, iAP-разбор и клиент молчат
  else { link_->pump(); client->poll(); }
  if (!spk.bt) net::loop();
  netmgr::loop();                                        // в режиме колонки — только отложенная перезагрузка
  if (spk.bt) spp::loop();
  serviceLink();

  if (resetAt && millis() > resetAt) { resetAt = 0; netmgr::factoryReset("Снуз ×10"); }
  dispTick();
  spkTick();
  if (rtcCrashes && millis() > 60000) rtcCrashes = 0;     // минуту проработали — падения позади
  static bool otaWas = netmgr::otaPending();
  if (otaWas && !netmgr::otaPending()) { otaWas = false; events.push_back({millis(), false, "OTA: новая прошивка подтверждена, откат отменён"}); }
  static bool wifiMarked = false;
  if (!wifiMarked && !spk.bt && netmgr::wifiUp() && millis() > 5000) { wifiMarked = true; memMark("Wi-Fi на связи"); }
  modeTick();
  if (state != Link::Passive) sniff::loop();             // sniff тоже на UART1 — в пассивном режиме он у прослушки
  static uint32_t lastLed = 0;                           // info() собирает строки — не чаще 20 раз в с
  if (millis() - lastLed >= 50) { lastLed = millis(); ledTick(); }
  static uint32_t bootDownAt = 0;                        // кнопка BOOT: 10 с → сброс
  if (digitalRead(PIN_BOOT) == LOW) {
    if (!bootDownAt) bootDownAt = millis();
    else if (millis() - bootDownAt > kBootHoldMs) { bootDownAt = 0; netmgr::factoryReset("кнопка BOOT 10 с"); }
  } else bootDownAt = 0;

  core::LocalTime now = localNow();
  if (state == Link::Online && needOnConnected && now) { needOnConnected = false; engine.onConnected(now); }

  // Пока модуль играет звук, держим усилитель часов: без «58 02» раз в 0,5 с часы через несколько секунд
  // глушат вход дока (так делало и оригинальное приложение на закате). Сами часы в режим звонка не переходят.
  static uint32_t lastHold = 0;
  if (state == Link::Online && millis() - lastHold >= 500) {
    lastHold = millis();
    if (!audio::playing()) holdAudio = false;
    if (keepAlive && (audio::playing() || spk.active)) {   // колонка: и в короткий провал потока (тракт не закрываем)
      // Вне очереди: в режиме колонки очередь занята лампой и громкостью (часы берут 4 кадра/с), а задержка
      // «58 02» больше ~1,5 с — и часы глушат вход дока (пауза в музыке, 2026-11-03). Одна «58 xx» в очереди — хватит.
      // «Уже в очереди» — только если «58» стоит первой: в хвосте за лампой она уйдёт слишком поздно
      const bool queued = !txq.empty() && txq.front().cmd == 0x14 && txq.front().payload.size() == 2 && txq.front().payload[0] == 0x58;
      if (!queued) txq.push_front(wul::setProperty(0x58, {0x02}));
    }
  }
  // будильник: громкость часов нарастает от 1 до заданной за 90 с (ступени ET2314 — по децибелам, на слух плавно)
  static uint8_t rampSent = 255;
  if (engine.mode == core::Mode::Ringing && alarmVol && now && state == Link::Online) {
    const float k = std::min(1.f, static_cast<float>(now - engine.ringStart) / 90.f);
    const uint8_t v = static_cast<uint8_t>(1 + (alarmVol - 1) * k + 0.5f);
    if (v != rampSent && txq.size() < 8) { rampSent = v; txq.push_back(wul::setProperty(0x00, {0x00, v})); }
  } else rampSent = 255;
  if (!txq.empty() && state == Link::Online && millis() - lastTxAt >= 250) {
    // «58 01» при уже открытом тракте часы понимают как «заглушить» — тогда шлём «58 02» (продолжать)
    static uint32_t audioAt = 0;                         // последний 58 01/02; без них > 1,5 с тракт часы закрывают
    static uint8_t audio58 = 0;
    wul::Frame& f = txq.front();
    if (f.cmd == 0x14 && f.payload.size() == 2 && f.payload[0] == 0x58) {
      if (f.payload[1] == 0x01 && audio58 && millis() - audioAt < 1500) f.payload[1] = 0x02;
      if (audio58 && f.payload[1] == 0x02) {
        const uint32_t gap = millis() - audioAt;
        if (gap > 1000) ++audioGapsLong;
        if (gap > audioGapMax) { audioGapMax = gap; audioGapAt = millis() / 1000; }
      }
      audio58 = f.payload[1]; audioAt = millis();
    }
    logFrame(false, f);
    client->send(f); txq.pop_front(); lastTxAt = millis();
  }

  static uint32_t lastTick = 0, lastWifiLog = 0;
  if (millis() - lastTick >= 1000) {
    lastTick = millis();
    bootlog::tick(spk.bt);
    if (now) engine.tick(now);
    if (millis() - lastWifiLog > 30000) {
      lastWifiLog = millis();
      api::NetInfo ni = netmgr::info();
      Serial.printf("[net] link=%s wifi=%s ip=%s ap=%d время=%s\n", ni.link.c_str(), netmgr::wifiUp() ? "ok" : "нет",
                    ni.ip.c_str(), ni.ap, now ? "ok" : "нет");
    }
  }
}
