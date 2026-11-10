// Звук: I2S → PCM5102A (BCK IO4, LCK IO21, DIN IO22) → вход дока часов (C506/C505, дифференциальный — «мост»).
// Одна задача звука: mp3 из внешней флеш (storage/melodies), тестовый тон или поток Bluetooth (btsink).
// Приоритет: mp3 и тон перебивают Bluetooth. Управление — из любого потока:
// команды только ставятся в очередь, объекты декодера трогает лишь задача звука.
#pragma once
#include <cstdint>

namespace audio {

enum class Ch : uint8_t { Both, Left, Right };

void begin(int bck, int lrck, int din);
// Синус hz Гц, громкость 0..1, на sec секунд (0 — пока не остановят); beep — 0,4 с звук / 0,4 с тишина.
void tone(uint16_t hz, float vol, uint32_t sec, Ch ch = Ch::Both, bool beep = false, uint16_t hz2 = 0);   // hz2 — правый канал отдельно
// mp3 из флеш по имени (без расширения), громкость 0..1; loop — по кругу до stop(). false — файла нет.
bool play(const char* name, float vol, bool loop);
void setVolume(float vol);
void setDc(float volts);                 // постоянка на выходе ЦАП 0..2,9 В (плавно): вход дока часов без неё молчит, рабочее — 1,5 В
float dc();                              // заданная постоянка, В
void setBridge(bool on);                 // R = −моно: вход дока часов дифференциальный (C506 − C505), +6 дБ
bool bridge();
void setDrive(float d);                  // «уплотнение» mp3: 1 — выкл, 2–4 — громче, пики мягко ограничены
void stop();
bool playing();                          // mp3, тон или поток Bluetooth

// Поток Bluetooth: 16 бит стерео. btWrite — из задачи Bluetooth (неблокирующе, лишнее отбрасывается).
void btWrite(const uint8_t* pcm, uint32_t len);
bool btBuffer();                         // выделить кольцо потока (при включении колонки)
void btRate(uint32_t hz);
void setEq(bool on, float hpHz, float body, float presence, float treble);   // звучание динамика (core/eq.h)
void setBtGain(float g);                 // уровень потока 0..1 (как у мелодий)
bool btActive();                         // звук Bluetooth шёл последние 1,5 с
uint32_t btIdleMs();                     // мс без данных от телефона (0xFFFFFFFF — ещё не было)
const char* btDiag();             // диагностика потока Bluetooth: «музыка или мусор»

// Анализ для светомузыки: 4 полосы (≈80, 300, 1200, 4500 Гц) и общий уровень, СКЗ 0..1 от полной шкалы,
// раз в 50 мс. true — есть новые данные с прошлого вызова.
void setAnalyze(bool on);
bool bands(float rms[4], float& env);
const char* status();
void repin(int bck, int lrck, int din);   // диагностика: другие выводы без перепрошивки
void format(int bits, bool msb);          // диагностика: 16/32 бит

}  // namespace audio
