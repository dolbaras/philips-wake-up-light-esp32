// HTTP-сервер модуля: веб-панель (gzip, встроена в прошивку) + API (/api/*, см. api.h и API.md).
// В режиме точки настройки работает как captive portal: любой адрес открывает мастер.
#pragma once
#include <WiFi.h>

namespace net {

void begin();               // сервер :80
void startMdns();           // wul.local (когда есть Wi-Fi-сеть)
void setPortal(bool on);    // перенаправлять «чужие» адреса на мастер
void loop();

}  // namespace net
