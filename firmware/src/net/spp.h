// Канал управления в режиме колонки: Bluetooth SPP (RFCOMM) рядом с музыкой A2DP. Wi-Fi в этом режиме выключен,
// и приложение говорит с модулем по тому же телефонному Bluetooth: строка запроса → api::handleLine → строка ответа
// (тот же формат, что у Bluetooth LE, см. API.md). Поднимать после btsink::begin (нужен запущенный Bluedroid).
#pragma once
#include <string>

namespace spp {

void begin(const char* name);   // зарегистрировать сервер SPP (имя службы видно телефону)
void loop();                    // обработать запросы (из основного цикла!)
bool connected();               // приложение подключено по SPP
const char* stats();            // для итогов сеанса: подключений, запросов, неотправленных ответов

}  // namespace spp
