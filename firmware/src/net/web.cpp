#include "web.h"
#include <Arduino.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "api.h"
#include "ui_gz.h"          // gzip веб-панели (tools/embed_ui.py)
#include "../storage/melodies.h"
#include "../storage/ext_flash.h"   // мелодии во внешней флеш (загрузка)

namespace net {

static WebServer server(80);
static bool portal = false;

static void cors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.sendHeader("Cache-Control", "no-store");
}

static void sendUi() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", reinterpret_cast<const char*>(kUiGz), kUiGzLen);
}

static void handleAny() {
  String uri = server.uri();
  if (server.method() == HTTP_OPTIONS) { cors(); server.send(204); return; }
  if (uri.startsWith("/api/")) {
    api::Request r;
    r.method = server.method() == HTTP_POST ? "POST" : server.method() == HTTP_GET ? "GET" : "OTHER";
    r.path = uri.c_str();
    if (server.hasArg("rev")) r.rev = server.arg("rev").c_str();
    if (server.hasArg("since")) r.rev = server.arg("since").c_str();      // /api/bus?since=ms
    if (server.hasArg("plain")) r.body = server.arg("plain").c_str();
    api::Response res = api::handle(r);
    cors();
    if (res.code == 304) server.send(304);
    else server.send(res.code, "application/json", res.body.c_str());
    return;
  }
  // Точка настройки: любой адрес (проверки captive portal Android/iOS/Windows) → мастер
  if (portal && server.hostHeader() != WiFi.softAPIP().toString()) {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
    return;
  }
  sendUi();
}

// Загрузка мелодии во внешнюю флеш: POST /api/upload?name=sati, multipart (curl -F f=@sati.mp3)
static std::string uploadResult;
static void onUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    std::string err = melodies::uploadBegin(server.arg("name").c_str());
    uploadResult = err.empty() ? "" : "{\"ok\":false,\"error\":\"" + err + "\"}";
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadResult.empty() && !melodies::uploadWrite(up.buf, up.currentSize))
      uploadResult = "{\"ok\":false,\"error\":\"не поместилось\"}";
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadResult.empty()) uploadResult = melodies::uploadEnd();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    melodies::uploadAbort();
    uploadResult = "{\"ok\":false,\"error\":\"обрыв\"}";
  }
}
static void onUploadDone() { cors(); server.send(200, "application/json", uploadResult.c_str()); }

void begin() {
  server.on("/", HTTP_GET, sendUi);
  server.on("/api/upload", HTTP_POST, onUploadDone, onUpload);
  server.onNotFound(handleAny);
  server.begin();
}

void startMdns() {
  static bool started = false;
  if (started) return;
  if (MDNS.begin("wul")) { MDNS.addService("http", "tcp", 80); started = true; }
}

void setPortal(bool on) { portal = on; }
void loop() { server.handleClient(); }

}  // namespace net
