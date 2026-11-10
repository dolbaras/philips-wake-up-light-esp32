#include "audio.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>
#include <cstring>
#include <algorithm>
#include <AudioFileSource.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutput.h>
#include "../storage/ext_flash.h"
#include "../storage/melodies.h"
#include <freertos/stream_buffer.h>

namespace audio {

static const i2s_port_t kPort = I2S_NUM_0;
static const uint32_t kToneRate = 44100;
static TaskHandle_t task_ = nullptr;
static char status_[64] = "не запущен";
static volatile uint32_t frames_ = 0;              // сколько блоков ушло в I2S — видно, что задача крутится

// Тон: параметры меняются из основного цикла, звук считает задача — только volatile-поля.
static volatile uint16_t hz_ = 440, hz2_ = 0;     // hz2_ ≠ 0 — в правом канале свой тон, прерывистый
static volatile float vol_ = 0, target_ = 0;      // огибающая тона (без щелчков)
static volatile uint32_t until_ = 0;              // millis() конца; 0 — без ограничения
static volatile Ch ch_ = Ch::Both;
static volatile bool beep_ = false, on_ = false;
static volatile float gain_ = 0.3f;               // громкость 0..1 (тон и mp3)
static volatile float drive_ = 1.f;               // «уплотнение» mp3: усиление перед мягким ограничителем (1 — выкл)
static volatile int bits_ = 32, want_bits_ = 32;  // разрядность отсчёта на шине; смена — в задаче звука
static volatile uint32_t rate_ = kToneRate;

// mp3: запрос из любого потока → задача звука открывает/закрывает декодер
static char req_name_[28] = "";
static volatile uint32_t req_seq_ = 0, done_seq_ = 0;
static volatile bool req_loop_ = false, req_stop_ = false, mp3_on_ = false;
static char now_[28] = "";

static void anaSetup(uint32_t rate);
static void dspSetup(uint32_t rate);
static void setRate(uint32_t hz) {
  if (hz == rate_ && want_bits_ == bits_) return;
  if (hz != rate_) { anaSetup(hz); dspSetup(hz); }
  rate_ = hz; bits_ = want_bits_;
  i2s_set_clk(kPort, rate_, bits_ == 16 ? I2S_BITS_PER_SAMPLE_16BIT : I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_STEREO);
}

// Буфер стереокадров → I2S в текущей разрядности
static int32_t out_[256 * 2];
static int outN_ = 0;
static void flushOut() {
  if (!outN_) return;
  size_t w;
  esp_err_t e;
  if (bits_ == 16) {
    auto* p16 = reinterpret_cast<int16_t*>(out_);
    for (int i = 0; i < outN_ * 2; ++i) p16[i] = static_cast<int16_t>(out_[i] >> 16);
    e = i2s_write(kPort, out_, outN_ * 4, &w, portMAX_DELAY);
  } else e = i2s_write(kPort, out_, outN_ * 8, &w, portMAX_DELAY);
  outN_ = 0; ++frames_;
  // I2S не встал — i2s_write не блокирует, и задача (приоритет 3 на ядре 0) не отдала бы процессор: сторожевой таймер
  if (e != ESP_OK) vTaskDelay(pdMS_TO_TICKS(20));
}
static volatile int32_t peak_ = 0;               // пиковый отсчёт с последнего status() — видно, что звук не нулевой
// Смещение выхода (PCM5102A пропускает постоянку, полная шкала = ±2,97 В). Вход дока часов (C506/C505) без
// постоянки молчит: нижние полуволны срезаются. С 1,5 В звук громкий и чистый (проверено 2026-11-02).
static volatile int32_t dcTarget_ = 0;
static int32_t dc_ = 0;                          // текущее — плавно идёт к dcTarget_ (≈0,3 с на 1,5 В), без щелчка
static const int32_t kDcStep = 80000;            // на кадр
static int32_t withDc(int32_t v) {
  const int64_t t = static_cast<int64_t>(v) + dc_;
  return t > INT32_MAX ? INT32_MAX : t < INT32_MIN ? INT32_MIN : static_cast<int32_t>(t);
}
// Вход дока часов дифференциальный: на динамик идёт разность C506 − C505. Одинаковый сигнал на обоих гасит сам себя
// (мелодии оригинала — моно), поэтому «мост»: L = моно, R = −моно — разность вдвое больше при том же размахе ЦАП.
static volatile bool bridge_ = true;

// --- анализ для светомузыки: полосовые фильтры (RBJ, Q≈1) по моно, СКЗ за 50 мс ---
struct Biquad {
  float b0 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;   // полосовой: b1 = 0, b2 = −b0
  float run(float x) { const float y = b0 * x + z1; z1 = -a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};
static const float kBandHz[4] = {80, 300, 1200, 4500};
static Biquad bq_[4];
static float acc_[4] = {}, accAll_ = 0;
static uint32_t accN_ = 0, accLen_ = 2205;
static volatile bool analyze_ = false;
static volatile float bandRms_[4] = {}, envRms_ = 0;
static volatile uint32_t anaSeq_ = 0;
static uint32_t anaRead_ = 0;
static void anaSetup(uint32_t rate) {
  for (int i = 0; i < 4; ++i) {
    const float w = 2.f * static_cast<float>(M_PI) * kBandHz[i] / rate, al = sinf(w) / 2.f, a0 = 1.f + al;
    bq_[i] = {al / a0, -al / a0, -2.f * cosf(w) / a0, (1.f - al) / a0, 0, 0};
  }
  accLen_ = rate / 20;
}
static void analyze(float x) {                      // x — моно, −1..1
  for (int i = 0; i < 4; ++i) { const float y = bq_[i].run(x); acc_[i] += y * y; }
  accAll_ += x * x;
  if (++accN_ < accLen_) return;
  for (int i = 0; i < 4; ++i) { bandRms_[i] = sqrtf(acc_[i] / accN_); acc_[i] = 0; }
  envRms_ = sqrtf(accAll_ / accN_); accAll_ = 0; accN_ = 0;
  ++anaSeq_;
}

// --- звучание динамика часов без короба (2026-11-03): эквалайзер, настройки — core/eq.h, POST /api/eq ---
// Без короба динамик бас не отдаёт (передняя и задняя волны гасят друг друга), а ход диффузора на басу велик —
// отсюда хрип. Срез ниже dspHp_ (4-й порядок Баттерворта), «плотность» — колокол 280 Гц, «чёткость» — 3 кГц,
// «верха» — полка от 8 кГц. Без компрессора и лимитера — они сами «качают» громкость; запас по уровню есть
// (поток и мелодии ≈0,5 шкалы, подъём не больше +6 дБ), на крайний случай — жёсткое ограничение на выходе.
struct Bq {                                         // RBJ, транспонированная прямая форма II
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  float run(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};
static Bq hp1_, hp2_, body_, pres_, treb_;
static volatile bool dsp_ = true, dspDirty_ = false;
static volatile float dspHp_ = 150.f, dspBody_ = 3.f, dspPres_ = 0.f, dspTreb_ = 0.f;
static void dspSetup(uint32_t rate) {
  const float fs = static_cast<float>(rate), pi2 = 2.f * static_cast<float>(M_PI);
  auto hp = [&](float f, float q) {
    const float w = pi2 * f / fs, c = cosf(w), al = sinf(w) / (2.f * q), a0 = 1.f + al;
    Bq b; b.b0 = b.b2 = (1.f + c) / 2.f / a0; b.b1 = -(1.f + c) / a0; b.a1 = -2.f * c / a0; b.a2 = (1.f - al) / a0;
    return b;
  };
  auto peak = [&](float f, float db, float q) {
    const float A = powf(10.f, db / 40.f), w = pi2 * f / fs, c = cosf(w), al = sinf(w) / (2.f * q), a0 = 1.f + al / A;
    Bq b; b.b0 = (1.f + al * A) / a0; b.b1 = -2.f * c / a0; b.b2 = (1.f - al * A) / a0; b.a1 = b.b1; b.a2 = (1.f - al / A) / a0;
    return b;
  };
  auto shelf = [&](float f, float db) {                // верхняя полка, крутизна S = 1
    const float A = powf(10.f, db / 40.f), w = pi2 * f / fs, c = cosf(w), al = sinf(w) / sqrtf(2.f), sA = 2.f * sqrtf(A) * al;
    const float a0 = (A + 1.f) - (A - 1.f) * c + sA;
    Bq b; b.b0 = A * ((A + 1.f) + (A - 1.f) * c + sA) / a0; b.b1 = -2.f * A * ((A - 1.f) + (A + 1.f) * c) / a0;
    b.b2 = A * ((A + 1.f) + (A - 1.f) * c - sA) / a0; b.a1 = 2.f * ((A - 1.f) - (A + 1.f) * c) / a0;
    b.a2 = ((A + 1.f) - (A - 1.f) * c - sA) / a0;
    return b;
  };
  hp1_ = hp(dspHp_, 0.5412f); hp2_ = hp(dspHp_, 1.3066f);
  body_ = peak(280.f, dspBody_, 0.9f); pres_ = peak(3000.f, dspPres_, 1.0f);
  treb_ = shelf(fs > 17000.f ? 8000.f : fs * 0.4f, dspTreb_);
}
static int32_t dspRun(int32_t m) {
  float x = static_cast<float>(m) * (1.f / 2147483648.f) + 1e-12f;   // 1e-12 — против денормалов (срез его уберёт)
  if (dspHp_ > 50.f) x = hp2_.run(hp1_.run(x));
  if (dspBody_ != 0.f) x = body_.run(x);
  if (dspPres_ != 0.f) x = pres_.run(x);
  if (dspTreb_ != 0.f) x = treb_.run(x);
  x = x > 0.9999f ? 0.9999f : x < -0.9999f ? -0.9999f : x;
  return static_cast<int32_t>(x * 2147483648.f);
}

static void pushFrame(int32_t l, int32_t r) {
  const int32_t a = l < 0 ? -(l >> 16) : (l >> 16); if (a > peak_) peak_ = a;
  if (outN_ == 256) flushOut();
  if (analyze_) analyze((static_cast<float>(l) + r) * (0.5f / 2147483648.f));
  if (bridge_) {
    int32_t m = static_cast<int32_t>((static_cast<int64_t>(l) + r) / 2);
    if (dsp_) m = dspRun(m);
    l = m; r = m == INT32_MIN ? INT32_MAX : -m;
  }
  const int32_t d = dcTarget_ - dc_;
  if (d) dc_ += d > kDcStep ? kDcStep : d < -kDcStep ? -kDcStep : d;
  out_[2 * outN_] = withDc(l); out_[2 * outN_ + 1] = withDc(r);
  ++outN_;
}

// --- источник: файл во внешней флеш ---
class FlashSource : public AudioFileSource {
 public:
  bool open(const char* name) override { pos_ = 0; open_ = melodies::find(name, off_, size_); return open_; }
  uint32_t read(void* data, uint32_t len) override {
    if (!open_) return 0;
    if (len > size_ - pos_) len = size_ - pos_;
    if (len) xflash::read(off_ + pos_, static_cast<uint8_t*>(data), len);
    pos_ += len; return len;
  }
  bool seek(int32_t pos, int dir) override {
    int64_t p = dir == SEEK_SET ? pos : dir == SEEK_CUR ? pos_ + pos : static_cast<int64_t>(size_) + pos;
    if (p < 0 || p > size_) return false;
    pos_ = static_cast<uint32_t>(p); return true;
  }
  bool close() override { open_ = false; return true; }
  bool isOpen() override { return open_; }
  uint32_t getSize() override { return size_; }
  uint32_t getPos() override { return pos_; }
 private:
  uint32_t off_ = 0, size_ = 0, pos_ = 0;
  bool open_ = false;
};

// --- вывод декодера: в наш I2S с громкостью ---
class I2SOut : public AudioOutput {
 public:
  bool begin() override { return true; }
  bool SetRate(int hz) override { flushOut(); setRate(hz); return true; }
  bool ConsumeSample(int16_t s[2]) override {
    // Буфер полон — отдать его в I2S и вернуть false: декодер выйдет из loop() и повторит этот отсчёт позже,
    // а задача звука успеет обработать «стоп» и смену мелодии (иначе loop() крутится до конца файла).
    if (outN_ == 256) { flushOut(); return false; }
    MakeSampleStereo16(s);
    // Записи оригинала тихие (пики ~−9 дБ, много тишины): усиливаем в drive_ раз и мягко ограничиваем
    // пики (приближение tanh) — средняя громкость растёт без жёсткого клиппинга и хрипа.
    const float d = drive_, g = gain_ * 2147483647.f;
    auto shape = [d](int16_t v) {
      float x = v * (d / 32768.f);
      if (d > 1.01f) { x = x > 3.f ? 3.f : x < -3.f ? -3.f : x; x = x * (27.f + x * x) / (27.f + 9.f * x * x); }
      return x;
    };
    pushFrame(static_cast<int32_t>(shape(s[0]) * g), static_cast<int32_t>(shape(s[1]) * g));
    return true;
  }
  bool stop() override { flushOut(); return true; }
};

static FlashSource src_;
static I2SOut out_dev_;
static AudioGeneratorMP3* mp3_ = nullptr;

static void mp3Close() {
  if (mp3_) { if (mp3_->isRunning()) mp3_->stop(); delete mp3_; mp3_ = nullptr; }
  src_.close(); mp3_on_ = false; now_[0] = 0;
}

static bool mp3Open(const char* name) {
  mp3Close();
  if (!src_.open(name)) return false;
  mp3_ = new AudioGeneratorMP3();
  if (!mp3_->begin(&src_, &out_dev_)) { mp3Close(); return false; }
  strncpy(now_, name, sizeof now_ - 1); mp3_on_ = true;
  return true;
}

// Декодер не открылся (в режиме колонки куча занята Bluetooth — libmad нужно ~30 КБ одним махом): будильник
// не должен молчать — прерывистый сигнал (как запасной тон в engine.playSound: чистый тон громче записей — ½
// уровня). Без повтора (просмотр мелодии) — 2 с.
static volatile uint32_t mp3Fails_ = 0;
static void mp3Fallback() {
  ++mp3Fails_;
  hz_ = 660; hz2_ = 0; ch_ = Ch::Both; beep_ = true; gain_ = gain_ * 0.5f;
  until_ = req_loop_ ? 0 : millis() + 2000;
  on_ = true;
}

static void toneBlock() {
  setRate(kToneRate);
  if (on_ && until_ && static_cast<int32_t>(millis() - until_) >= 0) on_ = false;
  const bool gate = on_ && (!beep_ || (millis() % 800) < 400);
  target_ = gate ? 1.f : 0.f;
  static float phase = 0, phase2 = 0, v2 = 0;
  const float kPi2 = 2.f * static_cast<float>(M_PI);
  const float step = kPi2 * hz_ / kToneRate, step2 = kPi2 * hz2_ / kToneRate;
  const float t2 = (on_ && hz2_ && (millis() % 600) < 300) ? 1.f : 0.f;   // правый: 0,3 с звук / 0,3 с тишина
  float v = vol_;
  for (int i = 0; i < 256; ++i) {
    v += (target_ - v) * 0.002f;                  // ~10 мс нарастание/спад
    v2 += (t2 - v2) * 0.002f;
    const float s = sinf(phase) * v * 0.9f;
    phase += step; if (phase > kPi2) phase -= kPi2;
    const int32_t x = static_cast<int32_t>(s * 32767.f * gain_) << 16;
    if (hz2_) {                                    // два разных тона: левый ровный, правый прерывистый
      const float s2 = sinf(phase2) * v2 * 0.9f;
      phase2 += step2; if (phase2 > kPi2) phase2 -= kPi2;
      pushFrame(x, static_cast<int32_t>(s2 * 32767.f * gain_) << 16);
    } else pushFrame(ch_ == Ch::Right ? 0 : x, ch_ == Ch::Left ? 0 : x);
  }
  vol_ = v;
}

// --- поток Bluetooth: кольцо байт (16 бит стерео) между задачей Bluetooth и задачей звука ---
static const size_t kBtBuf = 16384, kBtPrefill = 8192;    // ≈93 мс и ≈46 мс при 44,1 кГц; создаётся в режиме колонки
static StreamBufferHandle_t bt_ = nullptr;
static volatile uint32_t btRate_ = 44100, btLast_ = 0;
static volatile float btGain_ = 0.48f;
static bool btRun_ = false;
static uint32_t btDry_ = 0;                          // millis(), с которого кольцо пустое
static volatile uint32_t btDrops_ = 0, btUnder_ = 0, btGapMax_ = 0;   // btGapMax_ — самый долгий перерыв данных телефона, мс

// Диагностика «музыка или мусор»: у музыки соседние отсчёты близки — √(Σ(x[n]−x[n−1])²/Σx²) ≈ 0,05…0,5;
// у шума или сдвинутых байтов ≈ 1…1,4. Меряем на входе (от телефона) и на выходе кольца (в ЦАП).
struct Smooth {
  float s2 = 0, d2 = 0; int16_t prev = 0; uint32_t n = 0; int16_t first[4] = {};
  void add(const int16_t* f, size_t frames) {
    for (size_t i = 0; i < frames; ++i) {
      const int16_t x = f[2 * i];
      if (n < 4) first[n] = x;
      const float d = static_cast<float>(x) - prev;
      s2 += static_cast<float>(x) * x; d2 += d * d; prev = x; ++n;
    }
  }
  float ratio() const { return s2 > 0 ? sqrtf(d2 / s2) : -1.f; }
};
static Smooth inS_, outS_;
static volatile uint32_t btCalls_ = 0, btBytes_ = 0, btOddLen_ = 0;

void btWrite(const uint8_t* pcm, uint32_t len) {
  ++btCalls_; btBytes_ += len; if (len & 3) ++btOddLen_;
  if (inS_.n < 100000000) inS_.add(reinterpret_cast<const int16_t*>(pcm), len / 4);
  if (!bt_ || mp3_on_ || on_) return;               // мелодия/тон важнее — поток не копим
  // Как обычная колонка: ждём место в кольце (задача Bluetooth придерживает пакеты у себя), а не выбрасываем —
  // выброшенные куски давали щелчки = «хрип» (5952 потери за 2 мин, 2026-11-03). Пишем целыми кадрами.
  len &= ~3u;
  const uint32_t t0 = millis();
  while (len) {
    const size_t room = xStreamBufferSpacesAvailable(bt_) & ~static_cast<size_t>(3);
    if (!room) {
      if (millis() - t0 > 40) { ++btDrops_; return; }    // дольше держать задачу Bluetooth нельзя: входящие пакеты копятся в куче → нехватка памяти, паника (2026-11-03)
      vTaskDelay(1);
      continue;
    }
    const size_t n = len < room ? len : room;
    xStreamBufferSend(bt_, pcm, n, 0);
    pcm += n; len -= n;
  }
}
bool btBuffer() { if (!bt_) bt_ = xStreamBufferCreate(kBtBuf, 4); return bt_ != nullptr; }
void btRate(uint32_t hz) { if (hz >= 8000 && hz <= 48000) btRate_ = hz; }
void setBtGain(float g) { btGain_ = g < 0 ? 0 : g > 1 ? 1 : g; }
void setEq(bool on, float hpHz, float body, float presence, float treble) {
  dspHp_ = hpHz; dspBody_ = body; dspPres_ = presence; dspTreb_ = treble;
  dsp_ = on; dspDirty_ = true;                      // коэффициенты пересчитает задача звука (она же их и использует)
}
const char* btDiag() {
  static char d[384];
  snprintf(d, sizeof d, "работа %u с, макс. перерыв телефона %u мс, вызовов %u, байт %u, нечётных %u; кадров принято %u выведено %u; гладкость вход %.3f выход %.3f; первые %d %d %d %d",
           (unsigned)(millis() / 1000), (unsigned)btGapMax_, (unsigned)btCalls_, (unsigned)btBytes_, (unsigned)btOddLen_, (unsigned)inS_.n, (unsigned)outS_.n,
           (double)inS_.ratio(), (double)outS_.ratio(), inS_.first[0], inS_.first[1], inS_.first[2], inS_.first[3]);
  return d;
}
// btLast_ пишет задача звука (ядро 1, приоритет 5) — она вытесняет основной цикл на том же ядре. Читать СНАЧАЛА
// btLast_, потом millis(): в обратном порядке задача звука успевала записать более позднее время между ними,
// разность уходила в минус = ~49 дней «без данных» → модуль закрывал тракт часов посреди песни (тракт открыт
// 7 раз за 90 с при перерыве телефона 6 мс — вот они, «прерывается», 2026-11-03).
bool btActive() { const uint32_t last = btLast_; return last && millis() - last < 1500; }
uint32_t btIdleMs() { const uint32_t last = btLast_; return last ? millis() - last : 0xFFFFFFFFu; }

// Тишина со смещением на текущей частоте — держит DMA занятым, пока ждём телефон (частоту I2S не дёргаем)
static void btSilence(int n) { for (int i = 0; i < n; ++i) pushFrame(0, 0); }

// В режиме колонки задача звука живёт здесь: ждёт данные телефона, а не подставляет генератор тона.
// Раньше при каждом миговом опустении кольца сюда вклинивался toneBlock(): 256 кадров тишины и (при 48 кГц)
// перестройка I2S на 44,1 кГц и обратно — звук рвался сотни раз в секунду = «хрип, тупо рандом» (2026-11-03).
static bool btBlock() {
  if (!bt_) return false;
  setRate(btRate_);
  if (!btRun_) {                                     // набираем запас перед стартом
    if (xStreamBufferBytesAvailable(bt_) >= kBtPrefill) { btRun_ = true; btDry_ = 0; }
    else { btSilence(64); return true; }
  }
  static int16_t pcm[256 * 2];
  static uint8_t carry[4]; static size_t carryN = 0;  // неполный кадр с прошлого чтения (на всякий случай)
  auto* raw = reinterpret_cast<uint8_t*>(pcm);
  memcpy(raw, carry, carryN);
  size_t got = carryN + xStreamBufferReceive(bt_, raw + carryN, sizeof pcm - carryN, pdMS_TO_TICKS(10));
  carryN = got & 3; got -= carryN; memcpy(carry, raw + got, carryN);
  const size_t n = got / 4;
  if (!n) {                                          // 10 мс без данных: подкормить DMA тишиной; долго — заново запас
    if (!btDry_) btDry_ = millis();
    if (millis() - btDry_ > 200) { btRun_ = false; btDry_ = 0; ++btUnder_; }
    btSilence(64);
    return true;
  }
  btDry_ = 0;
  if (outS_.n < 100000000) outS_.add(pcm, n);
  const float g = btGain_ * 65536.f;
  for (size_t i = 0; i < n; ++i) pushFrame(static_cast<int32_t>(pcm[2 * i] * g), static_cast<int32_t>(pcm[2 * i + 1] * g));
  if (btLast_ && millis() - btLast_ > btGapMax_) btGapMax_ = millis() - btLast_;
  btLast_ = millis();
  return true;
}

static void run(void*) {
  uint32_t openFrames = 0;                        // frames_ при открытии mp3
  for (;;) {
    if (dspDirty_) { dspDirty_ = false; dspSetup(rate_); }
    if (req_stop_) { req_stop_ = false; mp3Close(); }
    if (req_seq_ != done_seq_) {
      done_seq_ = req_seq_;
      char name[28]; strncpy(name, req_name_, sizeof name);
      on_ = false;
      if (!mp3Open(name)) mp3Fallback();
      openFrames = frames_;
    }
    if (want_bits_ != bits_) setRate(rate_ ? rate_ : kToneRate);
    if (mp3_ && mp3_->isRunning()) {
      if (!mp3_->loop()) {                        // файл кончился
        char name[28]; strncpy(name, now_, sizeof name);
        mp3Close();
        // повтор, только если файл что-то сыграл: битый файл без отсчётов крутился бы без блокировки (сторожевой таймер)
        if (req_loop_ && (frames_ == openFrames || !mp3Open(name))) mp3Fallback();
        openFrames = frames_;
      }
    } else if (on_ || vol_ > 0.0005f || !btBlock()) toneBlock();
  }
}

void begin(int bck, int lrck, int din) {
  i2s_config_t cfg = {};
  cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = kToneRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;   // BCK = 64·fs — надёжнее для PLL PCM5102A без SCK
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count = 4;                            // 4×256 кадров ≈ 23 мс: ОЗУ нужно Bluetooth (было 8 — 16 КБ)
  cfg.dma_buf_len = 256;
  cfg.tx_desc_auto_clear = true;
  esp_err_t e1 = i2s_driver_install(kPort, &cfg, 0, nullptr);
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = bck; pins.ws_io_num = lrck; pins.data_out_num = din; pins.data_in_num = I2S_PIN_NO_CHANGE;
  esp_err_t e2 = i2s_set_pin(kPort, &pins);
  snprintf(status_, sizeof status_, e1 || e2 ? "ошибка I2S: install=%d pin=%d" : "ok", e1, e2);
  anaSetup(kToneRate);
  dspSetup(kToneRate);
  // В режиме колонки ядро 0 занято стеком Bluetooth (контроллер + Bluedroid закреплены за ним): там задача звука
  // успевала лишь вполовину, кольцо переполнялось → «мусор» и нехватка памяти (2026-11-03). Ядро 1 тогда почти
  // свободно (Wi-Fi выключен) — звук туда. В режиме Wi-Fi остаётся на ядре 0, как проверено.
  const BaseType_t core = bt_ ? 1 : 0;
  xTaskCreatePinnedToCore(run, "audio", 12288, nullptr, bt_ ? 5 : 3, &task_, core);   // декодеру mp3 нужен стек
}

void tone(uint16_t hz, float vol, uint32_t sec, Ch ch, bool beep, uint16_t hz2) {
  req_stop_ = true; req_loop_ = false;
  done_seq_ = req_seq_;                            // ещё не начатый play() отменяется — иначе он перебьёт тон
  hz2_ = hz2;
  hz_ = hz < 20 ? 20 : hz > 20000 ? 20000 : hz;
  gain_ = vol < 0 ? 0 : vol > 1 ? 1 : vol;
  ch_ = ch; beep_ = beep;
  until_ = sec ? millis() + sec * 1000 : 0;
  on_ = true;
}

bool play(const char* name, float vol, bool loop) {
  uint32_t off, size;
  if (!melodies::find(name, off, size)) return false;
  gain_ = vol < 0 ? 0 : vol > 1 ? 1 : vol;
  strncpy(req_name_, name, sizeof req_name_ - 1);
  req_loop_ = loop; req_stop_ = false;
  ++req_seq_;
  return true;
}

void setDc(float v) { v = v < 0 ? 0 : v > 2.9f ? 2.9f : v; dcTarget_ = static_cast<int32_t>(v / 2.97f * 2147483647.f); }
float dc() { return dcTarget_ * 2.97f / 2147483647.f; }
void setBridge(bool on) { bridge_ = on; }
bool bridge() { return bridge_; }
void setDrive(float d) { drive_ = d < 1 ? 1 : d > 8 ? 8 : d; }

void setVolume(float vol) { gain_ = vol < 0 ? 0 : vol > 1 ? 1 : vol; }
void stop() { on_ = false; req_loop_ = false; req_stop_ = true; done_seq_ = req_seq_; }   // и ещё не начатый play()
bool playing() { return on_ || mp3_on_ || btActive(); }

void setAnalyze(bool on) { analyze_ = on; }
bool bands(float rms[4], float& env) {
  const uint32_t s = anaSeq_;
  if (s == anaRead_) return false;
  anaRead_ = s;
  for (int i = 0; i < 4; ++i) rms[i] = bandRms_[i];
  env = envRms_;
  return true;
}

const char* status() {
  static char s[384];
  snprintf(s, sizeof s, "%s, блоков %u, тон %d, mp3 %s (сбоев %u), частота %u, бит %d, громкость %.2f, пик %d, смещение %.2f В, мост %d, звучание %s %.0f/%+.0f/%+.0f/%+.0f, BT: %s кольцо %u потерь %u провалов %u, память %u/%u, стек звука своб. %u", status_,
           (unsigned)frames_, (int)on_, mp3_on_ ? now_ : "—", (unsigned)mp3Fails_, (unsigned)rate_, bits_, (double)gain_, (int)peak_, (double)dc(), (int)bridge_,
           dsp_ ? "вкл" : "выкл", (double)dspHp_, (double)dspBody_, (double)dspPres_, (double)dspTreb_, btActive() ? "играет" : "—", bt_ ? (unsigned)xStreamBufferBytesAvailable(bt_) : 0u, (unsigned)btDrops_, (unsigned)btUnder_,
           (unsigned)ESP.getFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
           task_ ? (unsigned)uxTaskGetStackHighWaterMark(task_) : 0u);
  peak_ = 0;
  return s;
}

void repin(int bck, int lrck, int din) {
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = bck; pins.ws_io_num = lrck; pins.data_out_num = din; pins.data_in_num = I2S_PIN_NO_CHANGE;
  i2s_set_pin(kPort, &pins);
}

void format(int bits, bool) { want_bits_ = bits == 16 ? 16 : 32; }

}  // namespace audio
