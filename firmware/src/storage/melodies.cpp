#include <memory>
#include "melodies.h"
#include <Arduino.h>
#include <esp_rom_crc.h>
#include <algorithm>
#include <cstring>
#include "ext_flash.h"

namespace melodies {

static const uint32_t kMagic = 0x324C5557;         // «WUL2»
static const uint32_t kSector = 4096;
struct Entry { char name[28]; uint32_t off, size, valid; };   // 40 байт
static const int kMax = (kSector - 8) / sizeof(Entry);
static Entry dir_[kMax];
static int count_ = 0;

static struct { bool on = false; Entry e{}; uint32_t pos = 0, erased = 0; } up_;

static void save() {
  std::unique_ptr<uint8_t[]> mem(new uint8_t[kSector]);   // на время записи: ОЗУ нужно Bluetooth-колонке
  uint8_t* buf = mem.get();
  memset(buf, 0xFF, kSector);
  memcpy(buf, &kMagic, 4); memcpy(buf + 4, &count_, 4);
  memcpy(buf + 8, dir_, count_ * sizeof(Entry));
  xflash::eraseSector(0);
  xflash::program(0, buf, 8 + count_ * sizeof(Entry));
}

void begin() {
  if (!xflash::size()) return;
  uint32_t head[2];
  xflash::read(0, reinterpret_cast<uint8_t*>(head), 8);
  if (head[0] != kMagic || head[1] > static_cast<uint32_t>(kMax)) { count_ = 0; return; }
  count_ = head[1];
  xflash::read(8, reinterpret_cast<uint8_t*>(dir_), count_ * sizeof(Entry));
}

bool find(const char* name, uint32_t& off, uint32_t& size) {
  for (int i = count_ - 1; i >= 0; --i)
    if (dir_[i].valid == 1 && strncmp(dir_[i].name, name, sizeof dir_[i].name) == 0) { off = dir_[i].off; size = dir_[i].size; return true; }
  return false;
}

static uint32_t freeStart() {
  uint32_t end = kSector;
  for (int i = 0; i < count_; ++i) end = std::max(end, (dir_[i].off + dir_[i].size + kSector - 1) / kSector * kSector);
  return end;
}

std::string listJson() {
  std::string j = "{\"size\":" + std::to_string(xflash::size()) + ",\"free\":" +
                  std::to_string(xflash::size() ? xflash::size() - kSector - freeStart() : 0) + ",\"files\":[";
  bool first = true;
  for (int i = 0; i < count_; ++i) {
    if (dir_[i].valid != 1) continue;
    j += std::string(first ? "" : ",") + "{\"name\":\"" + dir_[i].name + "\",\"size\":" + std::to_string(dir_[i].size) + "}";
    first = false;
  }
  return j + "]}";
}

void format() { count_ = 0; save(); }

uint32_t crc32(const char* name) {
  uint32_t off, size;
  if (!find(name, off, size)) return 0;
  std::unique_ptr<uint8_t[]> mem(new uint8_t[1024]);
  uint8_t* buf = mem.get();
  const uint32_t kBuf = 1024;
  uint32_t crc = 0;
  for (uint32_t p = 0; p < size; p += kBuf) {
    const uint32_t n = std::min<uint32_t>(kBuf, size - p);
    xflash::read(off + p, buf, n);
    crc = esp_rom_crc32_le(crc, buf, n);
  }
  return crc;
}

std::string uploadBegin(const char* name) {
  if (!xflash::size()) return "нет флеш";
  if (count_ >= kMax) return "каталог полон";
  if (!name || !*name || strlen(name) >= sizeof(Entry::name)) return "плохое имя";
  up_ = {};
  up_.on = true;
  strncpy(up_.e.name, name, sizeof up_.e.name - 1);
  up_.e.off = freeStart();
  up_.erased = up_.e.off;
  return "";
}

bool uploadWrite(const uint8_t* d, size_t n) {
  if (!up_.on) return false;
  const uint32_t a = up_.e.off + up_.pos;
  if (a + n > xflash::size() - kSector) { up_.on = false; return false; }   // последний сектор — под самопроверку
  while (up_.erased < a + n) { xflash::eraseSector(up_.erased); up_.erased += kSector; }
  xflash::program(a, d, n);
  up_.pos += n;
  return true;
}

std::string uploadEnd() {
  if (!up_.on) return "{\"ok\":false,\"error\":\"не поместилось или не начато\"}";
  up_.on = false;
  for (int i = 0; i < count_; ++i) if (strncmp(dir_[i].name, up_.e.name, sizeof up_.e.name) == 0) dir_[i].valid = 0;  // старая версия
  up_.e.size = up_.pos; up_.e.valid = 1;
  dir_[count_++] = up_.e;
  save();
  return "{\"ok\":true,\"name\":\"" + std::string(up_.e.name) + "\",\"size\":" + std::to_string(up_.pos) +
         ",\"off\":" + std::to_string(up_.e.off) + "}";
}

void uploadAbort() { up_.on = false; }

}  // namespace melodies
