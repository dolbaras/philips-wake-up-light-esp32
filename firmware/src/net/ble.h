// Bluetooth LE-транспорт того же API (api::handle): запрос — JSON-строка в характеристику 0002,
// ответ — notify в 0003 кусками, конец — '\n'. Контракт — API.md.
#pragma once
#include <string>

namespace ble {

void begin(const std::string& name);   // поднять GATT-сервер и рекламу
void loop();                           // обработать накопленные запросы (из основного цикла!)
bool connected();

}  // namespace ble
