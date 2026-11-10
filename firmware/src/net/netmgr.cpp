#include "netmgr.h"
#include <esp_mac.h>
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <esp_ota_ops.h>
#include <esp_netif.h>
#include <ESPmDNS.h>
#include "web.h"
#include "ble.h"
#include "../diag/bootlog.h"

namespace netmgr {

static constexpr uint32_t kFallbackMs = 90000;     // нет сети 90 с → точка настройки
static constexpr uint32_t kRetryMs = 120000;       // при поднятой точке — пробовать сеть раз в 2 мин

struct Config { std::string ssid, pass, link = "wifi"; bool setupDone = false; };
static Config cfg;
static DNSServer dns;
static bool apOn = false, bleOn = false, offline = false;
static uint32_t lastUpAt = 0, lastRetryAt = 0, restartAt = 0;
static bool otaOn = false;
static int8_t otaPend = -1;                       // -1 — ещё не спрашивали загрузчик
static bool otaTried = false;                     // подтверждаем один раз (не вышло — не долбим каждый цикл)

bool otaPending() {
  if (otaPend < 0) {
    esp_ota_img_states_t st;
    otaPend = esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  }
  return otaPend == 1;
}

void markGood() {
  if (otaTried || !otaPending()) return;
  otaTried = true;
  const esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
  otaPend = e == ESP_OK ? 0 : 1;
  Serial.printf("[ota] прошивка подтверждена (%d)\n", e);
}

// Обновление прошивки по Wi-Fi: pio run -e ota -t upload (модуль внутри часов — без USB)
static void startOta() {
  if (otaOn) return;
  ArduinoOTA.setHostname("wul");
  ArduinoOTA.setPassword(kApPass);
  ArduinoOTA.setMdnsEnabled(false);              // mDNS уже поднят (net::startMdns)
  // до первой записи: пишем в половину с прошлой прошивкой — откатываться станет некуда, значит эта — рабочая
  ArduinoOTA.onStart([]() { markGood(); Serial.println("[ota] приём прошивки…"); });
  ArduinoOTA.onEnd([]() { bootlog::why("OTA"); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[ota] ошибка %u\n", e); });
  ArduinoOTA.begin();
  MDNS.enableArduino(3232, true);
  otaOn = true;
  Serial.println("[ota] готов: wul.local:3232");
}

std::string deviceName() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);             // работает и до запуска Wi-Fi (имя нужно колонке раньше)
  char b[24];
  snprintf(b, sizeof b, "WakeUpLight-%02X%02X", mac[4], mac[5]);
  return b;
}

static void load() {
  Preferences p;
  p.begin("net", true);
  cfg.ssid = p.getString("ssid", "").c_str();
  cfg.pass = p.getString("pass", "").c_str();
  cfg.link = p.getString("link", "wifi").c_str();
  cfg.setupDone = p.getBool("done", false);
  p.end();
}

static void store() {
  Preferences p;
  p.begin("net", false);
  p.putString("ssid", cfg.ssid.c_str());
  p.putString("pass", cfg.pass.c_str());
  p.putString("link", cfg.link.c_str());
  p.putBool("done", cfg.setupDone);
  p.end();
}

static void startAp() {
  if (apOn) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(deviceName().c_str(), kApPass);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  net::setPortal(true);
  apOn = true;
  Serial.printf("[net] точка настройки %s (пароль %s) → http://%s/\n", deviceName().c_str(), kApPass,
                WiFi.softAPIP().toString().c_str());
}

static void stopAp() {
  if (!apOn) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  net::setPortal(false);
  apOn = false;
  Serial.println("[net] точка настройки выключена — модуль в домашней сети");
}

void begin(const char* seedSsid, const char* seedPass) {
  load();
  Preferences p;
  p.begin("net", true);
  bool noSeed = p.getBool("noseed", false);
  p.end();
  if (cfg.ssid.empty() && !noSeed && seedSsid && *seedSsid) {   // сеть из config.h — один раз
    cfg.ssid = seedSsid; cfg.pass = seedPass ? seedPass : ""; cfg.link = "wifi"; cfg.setupDone = true;
    store();
    Serial.println("[net] сеть из config.h сохранена в NVS");
  }

  esp_netif_init();   // стек lwip — до socket() веб-сервера (иначе assert «Invalid mbox»); повторный вызов безвреден
  net::begin();
#ifdef WUL_NO_BLE
  if (cfg.link == "ble") cfg.link = "wifi";   // BLE в этой сборке нет — иначе модуль остался бы вовсе без связи
#endif
  if (cfg.setupDone && cfg.link == "ble") {
    WiFi.mode(WIFI_OFF);
    ble::begin(deviceName());
    bleOn = true;
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("wul");
  if (!cfg.ssid.empty()) WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  if (!cfg.setupDone || cfg.ssid.empty()) startAp();
  lastUpAt = millis();
}

void beginOffline() {
  load();
  offline = true;
  Serial.println("[net] режим колонки: Wi-Fi не поднимаю");
}

bool wifiUp() { return !bleOn && !offline && WiFi.isConnected(); }

void loop() {
  if (restartAt && millis() > restartAt) { markGood(); ESP.restart(); }   // перезагрузка по команде — прошивка жива
  if (!otaTried && otaPend != 0 && millis() > 45000 && (offline || bleOn || apOn || WiFi.isConnected())) markGood();
  if (offline) return;
  if (bleOn) { ble::loop(); return; }
  if (apOn) dns.processNextRequest();

  if (otaOn) ArduinoOTA.handle();
  if (WiFi.isConnected()) {
    lastUpAt = millis();
    net::startMdns();
    startOta();
    if (apOn && cfg.setupDone && WiFi.softAPgetStationNum() == 0) stopAp();   // сеть вернулась
    return;
  }
  if (!apOn && millis() - lastUpAt > kFallbackMs) {
    Serial.println("[net] домашняя сеть недоступна — поднимаю точку настройки");
    startAp();
    lastRetryAt = millis();
  }
  // при точке настройки переподключение STA сбивает канал — пробуем, только когда к точке никто не подключён
  if (apOn && !cfg.ssid.empty() && WiFi.softAPgetStationNum() == 0 && millis() - lastRetryAt > kRetryMs) {
    lastRetryAt = millis();
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  }
}

api::NetInfo info() {
  api::NetInfo n;
  n.link = cfg.link;
  n.ssid = cfg.ssid;
  n.ble = deviceName();
  n.ap = apOn;
  n.setup = !cfg.setupDone;
  if (offline) {}                                // Wi-Fi выключен — ни адреса, ни точки
  else if (!bleOn && WiFi.isConnected()) { n.ip = WiFi.localIP().toString().c_str(); n.rssi = WiFi.RSSI(); }
  else if (apOn) n.ip = WiFi.softAPIP().toString().c_str();
  return n;
}

std::vector<api::WifiNet> scan() {
  std::vector<api::WifiNet> v;
  if (bleOn || offline) return v;               // scanNetworks сам включил бы Wi-Fi — в режиме колонки нет памяти
  int n = WiFi.scanNetworks(false, false);
  for (int i = 0; i < n; ++i) {
    std::string s = WiFi.SSID(i).c_str();
    if (s.empty()) continue;
    bool dup = false;
    for (auto& x : v) if (x.ssid == s) { dup = true; if (WiFi.RSSI(i) > x.rssi) x.rssi = WiFi.RSSI(i); }
    if (!dup) v.push_back({s, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
  }
  WiFi.scanDelete();
  std::sort(v.begin(), v.end(), [](const api::WifiNet& a, const api::WifiNet& b) { return a.rssi > b.rssi; });
  if (v.size() > 20) v.resize(20);
  return v;
}

void restartIn(uint32_t ms, const char* why) { if (why) bootlog::why(why); restartAt = millis() + ms; }

void save(const std::string& ssid, const std::string* pass, const std::string& link, bool setupDone) {
  if (!ssid.empty() && ssid != cfg.ssid && !pass) cfg.pass.clear();    // другая сеть без пароля — открытая
  if (!ssid.empty()) cfg.ssid = ssid;
  if (pass) cfg.pass = *pass;
  cfg.link = link;
  cfg.setupDone = setupDone;
  store();
  Serial.printf("[net] сохранено: link=%s ssid=%s — перезагрузка\n", cfg.link.c_str(), cfg.ssid.c_str());
  restartIn(1500, "сеть сохранена");             // успеть отдать ответ
}

void factoryReset(const char* src) {
  char w[64]; snprintf(w, sizeof w, "СБРОС: %s", src); bootlog::why(w);
  Preferences p;
  p.begin("wul2", false); p.clear(); p.end();
  p.begin("net", false); p.clear(); p.putBool("noseed", true); p.end();
  p.begin("spk", false); p.clear(); p.end();     // режим колонки и его настройки — иначе сброс из колонки вернул бы в колонку
  p.begin("link", false); p.clear(); p.end();    // ручной пассивный режим, автоопределение дока
  p.begin("eq", false); p.clear(); p.end();      // звучание динамика
  p.begin("ui", false); p.clear(); p.end();      // светодиод на плате (по умолчанию выключен)
  Serial.println("[net] СБРОС К ЗАВОДСКИМ — перезагрузка в мастер настройки");
  restartIn(1500);
}

}  // namespace netmgr
