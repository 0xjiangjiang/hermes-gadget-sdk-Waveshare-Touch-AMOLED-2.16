// Wake word detection (ESP-SR WakeNet) on the microphone stream the CodecMic
// task already reads: between conversations every 20 ms block is fed to the
// model, and a detection turns into a hands-free talk press (the board runs
// talk_mode: tap, so the existing VAD then ends the utterance and sends).
//
// The model (wn9_xiaoaitongxue) is selected in the board's sdkconfig.defaults
// and ships inside the app image; nothing else polls the I2S channel.
#include "port.hpp"  // first: pulls in FreeRTOS.h ahead of task.h/queue.h

#include <algorithm>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "freertos/task.h"
#include "model_path.h"

namespace hgp::wake {
namespace {

const char* TAG = "hg.wake";
constexpr const char* kModelName = "wn9_xiaoaitongxue";
// Detections within three seconds of the last one are dropped: the tail of
// the utterance it triggered is still in the air, and the model is fresh
// enough to fire on it again.
constexpr int64_t kRearmUs = 3 * 1000000LL;

bool g_present = false;
std::atomic<bool> g_enabled{false};
const esp_wn_iface_t* g_iface = nullptr;
model_iface_data_t* g_model = nullptr;
int g_chunk = 0;        // samples per detect() call
int16_t* g_frame = nullptr;  // accumulation buffer for the 20 ms reads
size_t g_filled = 0;
int64_t g_armed_at = 0;  // microseconds; detections before this are ignored

}  // namespace

bool begin() {
  srmodel_list_t* models = esp_srmodel_init("model");
  if (!models) {
    ESP_LOGW(TAG, "no ESP-SR model list; wake word unavailable");
    return false;
  }
  if (!esp_srmodel_filter(models, ESP_WN_PREFIX, "xiaoaitongxue")) {
    ESP_LOGW(TAG, "no %s compiled in; wake word unavailable", kModelName);
    return false;
  }
  g_iface = esp_wn_handle_from_name(kModelName);
  if (!g_iface) return false;
  g_model = g_iface->create(kModelName, DET_MODE_90);
  if (!g_model) {
    ESP_LOGW(TAG, "could not create the wake word model");
    return false;
  }
  g_chunk = g_iface->get_samp_chunksize(g_model);
  g_frame = static_cast<int16_t*>(heap_caps_malloc(g_chunk * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!g_frame) g_frame = static_cast<int16_t*>(malloc(g_chunk * sizeof(int16_t)));
  if (!g_frame) {
    ESP_LOGW(TAG, "no memory for the %d sample detection frame", g_chunk);
    return false;
  }
  g_present = true;
  const int rate = g_iface->get_samp_rate(g_model);
  ESP_LOGI(TAG, "%s ready: say '%s' (%d ms frames at %d Hz)", kModelName, g_iface->get_word_name(g_model, 1),
           g_chunk * 1000 / rate, rate);
  return true;
}

void set_enabled(bool on) {
  if (!g_present) on = false;
  const bool was = g_enabled.exchange(on);
  if (was != on) ESP_LOGI(TAG, "wake word %s", on ? "on" : "off");
}

bool enabled() { return g_enabled.load(); }

bool feed(const int16_t* samples, size_t count) {
  if (!g_present || !g_enabled.load()) return false;
  const int64_t now = esp_timer_get_time();
  if (now < g_armed_at) return false;
  size_t off = 0;
  while (off < count) {
    const size_t n = std::min(count - off, static_cast<size_t>(g_chunk) - g_filled);
    std::memcpy(g_frame + g_filled, samples + off, n * sizeof(int16_t));
    g_filled += n;
    off += n;
    if (g_filled < static_cast<size_t>(g_chunk)) continue;
    g_filled = 0;
    if (g_iface->detect(g_model, g_frame) == WAKENET_DETECTED) {
      // No clean() here: the wn9 quantized model's clean dereferences a null
      // convq queue on this build (crashed before the first utterance ever
      // went out). The re-arm window below covers re-triggers instead.
      g_armed_at = esp_timer_get_time() + kRearmUs;
      ESP_LOGI(TAG, "wake word detected");
      return true;
    }
  }
  return false;
}

}  // namespace hgp::wake
