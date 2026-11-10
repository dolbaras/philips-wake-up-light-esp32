// Прямой доступ к контроллеру лампы (STM8, I2C-ведомый 0x51, 16-битный адрес регистра) с пятаков
// WUL_SCL / WUL_SDA. Шина 5 В — подключать только через преобразователь уровней (BSS138), подтяжки к 3,3 В
// на его стороне LV. Ведущий на шине — процессор часов (программный I2C ~10 кГц, опрос рег.05 раз в
// секунду, пока лампа горит), поэтому мы: никогда не тянем линию вверх (только «0» или отпустить),
// начинаем обмен лишь после 30 мс тишины и бросаем его, если на линии не то, что мы выставили.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace lampi2c {

void begin(int scl, int sda);           // выводы — вход, шину не трогаем
std::string probe();                    // {"scl":1,"sda":1,"ack":true} — уровни и ответ адреса 0x51
// Чтение n байт с регистра reg; пустая строка ошибки — успех.
std::string read(uint16_t reg, uint8_t n, std::vector<uint8_t>& out);
std::string write(uint16_t reg, const std::vector<uint8_t>& data);
// Чтение по одному байту регистров from..to (не больше 32 за вызов): {"r":{"00":"05",…},"err":{…}}
std::string scanJson(uint16_t from, uint16_t to);

}  // namespace lampi2c
