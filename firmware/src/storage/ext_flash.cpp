#include "ext_flash.h"
#include <Arduino.h>
#include <SPI.h>
#include <algorithm>

namespace xflash {

static SPIClass spi(VSPI);
static int cs_ = -1;
static SemaphoreHandle_t lock_ = nullptr;        // чтение (задача звука) и запись (загрузка по Wi-Fi) — из разных задач
static uint32_t size_ = 0;
static const uint32_t kHz = 20000000;

static void select() { spi.beginTransaction(SPISettings(kHz, MSBFIRST, SPI_MODE0)); digitalWrite(cs_, LOW); }
static void release() { digitalWrite(cs_, HIGH); spi.endTransaction(); }

static void jedec(uint8_t id[3]) {
  select(); spi.transfer(0x9F); for (int i = 0; i < 3; ++i) id[i] = spi.transfer(0); release();
}

static uint8_t status() { select(); spi.transfer(0x05); uint8_t s = spi.transfer(0); release(); return s; }
static bool waitReady(uint32_t ms) { uint32_t t = millis(); while (status() & 1) { if (millis() - t > ms) return false; delay(1); } return true; }
static void writeEnable() { select(); spi.transfer(0x06); release(); }

void begin(int sck, int miso, int mosi, int cs) {
  cs_ = cs;
  pinMode(cs_, OUTPUT); digitalWrite(cs_, HIGH);
  spi.begin(sck, miso, mosi, cs);
  select(); spi.transfer(0xAB); release();          // разбудить (Release Power-down)
  delayMicroseconds(50);
  lock_ = xSemaphoreCreateMutex();
  uint8_t id[3]; jedec(id);
  size_ = (id[2] >= 0x10 && id[2] <= 0x20) ? (1u << id[2]) : 0;
}

uint32_t size() { return size_; }

struct Guard { Guard() { if (lock_) xSemaphoreTake(lock_, portMAX_DELAY); } ~Guard() { if (lock_) xSemaphoreGive(lock_); } };

static void addr24(uint8_t cmd, uint32_t a) { spi.transfer(cmd); spi.transfer(a >> 16); spi.transfer(a >> 8); spi.transfer(a); }

void read(uint32_t addr, uint8_t* buf, uint32_t len) {
  Guard g;
  select(); addr24(0x03, addr); spi.transferBytes(nullptr, buf, len); release();
}

void eraseSector(uint32_t addr) {
  Guard g;
  writeEnable(); select(); addr24(0x20, addr); release();
  waitReady(500);
}

void program(uint32_t addr, const uint8_t* data, uint32_t len) {
  Guard g;
  while (len) {
    const uint32_t n = std::min<uint32_t>(len, 256 - (addr & 255));   // не переходить границу страницы
    writeEnable(); select(); addr24(0x02, addr); spi.writeBytes(data, n); release();
    waitReady(20);
    addr += n; data += n; len -= n;
  }
}

std::string idJson() {
  uint8_t id[3]; jedec(id);
  const uint32_t bytes = (id[2] >= 0x10 && id[2] <= 0x20) ? (1u << id[2]) : 0;
  char b[160];
  snprintf(b, sizeof b, "{\"ok\":%s,\"jedec\":\"%02X %02X %02X\",\"maker\":\"%s\",\"bytes\":%u,\"mb\":%.1f}",
           bytes ? "true" : "false", id[0], id[1], id[2],
           id[0] == 0xEF ? "Winbond" : id[0] == 0xC8 ? "GigaDevice" : id[0] == 0x68 ? "Boya" : id[0] == 0x20 ? "XMC/Micron" : id[0] == 0xFF || id[0] == 0 ? "нет ответа" : "?",
           (unsigned)bytes, bytes / 1048576.0);
  return b;
}

std::string selfTest() {
  uint8_t id[3]; jedec(id);
  if (id[2] < 0x10 || id[2] > 0x20) return "{\"ok\":false,\"error\":\"чип не отвечает\"}";
  const uint32_t addr = (1u << id[2]) - 4096;      // последний сектор — мелодии туда не попадут
  writeEnable(); select(); spi.transfer(0x20); spi.transfer(addr >> 16); spi.transfer(addr >> 8); spi.transfer(addr); release();
  if (!waitReady(500)) return "{\"ok\":false,\"error\":\"стирание не завершилось\"}";
  uint8_t pat[64]; for (int i = 0; i < 64; ++i) pat[i] = static_cast<uint8_t>(i * 7 + 3);
  writeEnable(); select(); spi.transfer(0x02); spi.transfer(addr >> 16); spi.transfer(addr >> 8); spi.transfer(addr);
  for (uint8_t v : pat) spi.transfer(v);
  release();
  if (!waitReady(50)) return "{\"ok\":false,\"error\":\"запись не завершилась\"}";
  uint8_t got[64]; select(); spi.transfer(0x03); spi.transfer(addr >> 16); spi.transfer(addr >> 8); spi.transfer(addr);
  for (auto& v : got) v = spi.transfer(0);
  release();
  int bad = 0; for (int i = 0; i < 64; ++i) bad += got[i] != pat[i];
  char b[96]; snprintf(b, sizeof b, "{\"ok\":%s,\"addr\":%u,\"mismatch\":%d}", bad ? "false" : "true", (unsigned)addr, bad);
  return b;
}

}  // namespace xflash
