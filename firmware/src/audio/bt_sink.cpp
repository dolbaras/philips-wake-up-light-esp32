#include "bt_sink.h"
#include <Arduino.h>
#include <BluetoothA2DPSink.h>
#include <esp_bt.h>
#include "audio.h"

namespace btsink {

static BluetoothA2DPSink* sink_ = nullptr;      // создаётся при первом включении — без него стек Bluetooth не поднят
static A2DPNoVolumeControl noVol_;               // данные не трогаем: громкость делают часы
// Пустой вывод для библиотеки. Её штатный вывод (даже с is_output = false) при каждой смене частоты зовёт
// i2s_set_clk(порт 0, частота, 16 бит) — а порт 0 наш и пишется 32-битными отсчётами: шина вдвое медленнее,
// младшие половины отсчётов звучат как «шшш» (2026-11-03). Звук мы берём сами через stream_reader.
class NullOut : public BluetoothA2DPOutput {
 public:
  bool begin() override { return true; }
  size_t write(const uint8_t*, size_t len) override { return len; }
  void end() override {}
  void set_sample_rate(int) override {}
  void set_output_active(bool) override {}
  void write_silence(size_t) override {}
};
static NullOut nullOut_;
static bool on_ = false;
static volatile bool connected_ = false, playing_ = false;
static volatile int vol_ = -1;
static volatile bool volFresh_ = false;
static char name_[32] = "";
void (*memProbe)() = nullptr;

static void onData(const uint8_t* d, uint32_t len) { audio::btWrite(d, len); }   // задача Bluetooth
static void onRate(uint16_t hz) { audio::btRate(hz); }
static void onVolume(int v) { vol_ = v; volFresh_ = true; }
static char title_[64] = "", artist_[48] = "";
static portMUX_TYPE mdMux_ = portMUX_INITIALIZER_UNLOCKED;
static void onMeta(uint8_t id, const uint8_t* text) {   // задача приложения A2DP
  char* dst = id == ESP_AVRC_MD_ATTR_TITLE ? title_ : id == ESP_AVRC_MD_ATTR_ARTIST ? artist_ : nullptr;
  if (!dst) return;
  const size_t cap = dst == title_ ? sizeof title_ : sizeof artist_;
  portENTER_CRITICAL(&mdMux_);
  strncpy(dst, reinterpret_cast<const char*>(text), cap - 1); dst[cap - 1] = 0;
  portEXIT_CRITICAL(&mdMux_);
}
static void onConn(esp_a2d_connection_state_t s, void*) {
  connected_ = s == ESP_A2D_CONNECTION_STATE_CONNECTED;
  if (!connected_) playing_ = false;
  Serial.printf("[bt] связь: %d\n", (int)s);
}
static void onAudio(esp_a2d_audio_state_t s, void*) {
  playing_ = s == ESP_A2D_AUDIO_STATE_STARTED;
  Serial.printf("[bt] звук: %d\n", (int)s);
}

void begin(const char* name) {
  if (on_) return;
  strncpy(name_, name, sizeof name_ - 1);
  if (!sink_) {
    sink_ = new BluetoothA2DPSink(nullOut_);
    sink_->set_stream_reader(onData, false);       // false — библиотека не трогает I2S, звук берёт audio::
    sink_->set_volume_control(&noVol_);
    sink_->set_sample_rate_callback(onRate);
    sink_->set_avrc_rn_volumechange(onVolume);
    sink_->set_avrc_metadata_attribute_mask(ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST);
    sink_->set_avrc_metadata_callback(onMeta);
    sink_->set_on_connection_state_changed(onConn);
    sink_->set_on_audio_state_changed(onAudio);
    sink_->set_auto_reconnect(true);
  }
  // Только классический Bluetooth: память контроллера под BLE отдаём в кучу (иначе Bluedroid не хватает
  // памяти рядом с Wi-Fi — паника в l2c_init, 2026-11-02). Библиотека увидит контроллер уже включённым.
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    cfg.mode = ESP_BT_MODE_CLASSIC_BT;
    if (esp_bt_controller_init(&cfg) == ESP_OK) esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
  }
  if (memProbe) memProbe();
  sink_->start(name_);
  on_ = true;
  Serial.printf("[bt] колонка «%s» включена, свободно %u\n", name_, (unsigned)ESP.getFreeHeap());
}

void end() {
  if (!on_ || !sink_) return;
  sink_->end(false);
  on_ = false; connected_ = false; playing_ = false;
}

bool enabled() { return on_; }
bool connected() { return on_ && connected_; }
bool streaming() { return on_ && playing_; }
std::string peer() { return connected() ? std::string(sink_->get_peer_name()) : std::string(); }

bool takeVolume(int& v) {
  if (!volFresh_) return false;
  volFresh_ = false; v = vol_;
  return true;
}

void reportVolume(int v) {
  if (!connected()) return;
  vol_ = v;
  sink_->set_volume(static_cast<uint8_t>(v < 0 ? 0 : v > 127 ? 127 : v));
}

void pause() { if (streaming()) sink_->pause(); }

static std::string meta(const char* src) {
  char b[64];
  portENTER_CRITICAL(&mdMux_); strncpy(b, src, sizeof b - 1); b[sizeof b - 1] = 0; portEXIT_CRITICAL(&mdMux_);
  return connected() ? std::string(b) : std::string();
}
std::string title() { return meta(title_); }
std::string artist() { return meta(artist_); }

}  // namespace btsink
