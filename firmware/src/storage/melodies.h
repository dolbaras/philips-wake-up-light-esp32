// Мелодии во внешней флеш: сектор 0 — каталог, файлы подряд с выравниванием 4 КБ.
// Загрузка — POST /api/upload?name=… (multipart, curl -F f=@file.mp3), см. tools/upload_melodies.py.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace melodies {

void begin();
bool find(const char* name, uint32_t& off, uint32_t& size);
std::string listJson();
void format();
uint32_t crc32(const char* name);                // CRC32 файла (как zlib.crc32), 0 — нет файла                                   // очистить каталог (файлы заливаются заново)

std::string uploadBegin(const char* name);       // пусто — можно писать
bool uploadWrite(const uint8_t* d, size_t n);
std::string uploadEnd();                         // JSON с итогом
void uploadAbort();

}  // namespace melodies
