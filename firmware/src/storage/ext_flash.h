// Внешняя флеш W25Q64 для мелодий: VSPI — CLK IO18, DO(MISO) IO19, DI(MOSI) IO23, CS IO5, питание 3,3 В.
#pragma once
#include <cstdint>
#include <string>

namespace xflash {

void begin(int sck, int miso, int mosi, int cs);
// JEDEC ID (производитель, тип, ёмкость) и размер; пустой/0xFF — чип не отвечает
std::string idJson();
// Проверка чтения-записи в последнем секторе (4 КБ): стирает его, пишет образец, читает обратно.
std::string selfTest();

uint32_t size();                                             // байт (по JEDEC), 0 — нет чипа
void read(uint32_t addr, uint8_t* buf, uint32_t len);
void eraseSector(uint32_t addr);                             // 4 КБ, addr кратен 4096
void program(uint32_t addr, const uint8_t* data, uint32_t len);   // сам режет по страницам 256 байт

}  // namespace xflash
