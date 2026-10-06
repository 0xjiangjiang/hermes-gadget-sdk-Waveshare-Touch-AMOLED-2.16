// Display auto-rotation on boards with a QMI8658: the accelerometer decides
// which screen edge is up, a poll task debounces that and posts Rotate
// events, and the `rotation` setting ("auto" or a fixed 0/90/180/270) picks
// between following the IMU and pinning an orientation.
#include "port.hpp"  // first: pulls in FreeRTOS.h ahead of task.h/queue.h

#include <cstdlib>
#include <cstring>

#include "esp_log.h"
#include "freertos/task.h"

namespace hgp {

// The wrapper lives at hgp scope next to the class; SensorLib talks to the
// shared bus through the handle the board already owns.

bool Qmi8658::begin(const ImuConfig& cfg, i2c_master_bus_handle_t bus) {
  // The address pin ties the chip to 0x6a or 0x6b; SensorLib's begin probes
  // the given address, soft-resets the chip and reads WHO_AM_I.
  if (!chip_.begin(bus, cfg.addr) && !chip_.begin(bus, static_cast<uint8_t>(cfg.addr ^ 1))) return false;
  if (!chip_.configAccelerometer(SensorQMI8658::ACC_RANGE_2G, SensorQMI8658::ACC_ODR_31_25Hz,
                                 SensorQMI8658::LPF_MODE_3))
    return false;
  return chip_.enableAccelerometer();
}

bool Qmi8658::read_accel(float out[3]) { return chip_.getAccelerometer(out[0], out[1], out[2]); }

}  // namespace hgp

namespace hgp::imu {
namespace {

const char* TAG = "hg.imu";

// A sample counts as "up" past 0.6 g along the axis; anything shallower
// (roughly the device flat on a table) keeps the current orientation.
constexpr float kUpG = 0.6f;
constexpr int kStableSamples = 6;  // ~600 ms at the 100 ms poll

AmoledDisplay* g_display = nullptr;
TouchInput* g_touch = nullptr;
Qmi8658 g_chip;
bool g_present = false;
std::atomic<uint8_t> g_orientation{kRotationDefault};
std::atomic<bool> g_fixed{false};  // a `rotation` setting overrides the IMU

// Which rotation index matches the measured gravity, calibrated on the
// device: held upright (screen facing the user) the chip reads ax=-1 g and
// that pose wants rotation 2; the left/right poses confirmed the ay axis.
// Each 90° clockwise turn of the board rotates gravity (ax,ay) by -90° and
// needs the next rotation index.
uint8_t orientation_from_accel(const float a[3]) {
  if (a[0] > kUpG) return 0;
  if (a[0] < -kUpG) return 2;
  if (a[1] > kUpG) return 3;
  if (a[1] < -kUpG) return 1;
  return 0xFF;  // flat: keep the current orientation
}

// Applies a rotation right away. App task only: it drives the panel and the
// touch mapping, neither of which is safe from the poll task.
void set_orientation(uint8_t index) {
  g_orientation.store(index);
  if (g_display) g_display->set_orientation(index);
  if (g_touch) g_touch->set_orientation(index);
  ESP_LOGI(TAG, "rotation %u (%u°)", index, index * 90);
}

void poll_task(void*) {
  uint8_t candidate = 0xFF;
  int stable = 0;
  int warmup = 5;  // the first samples after enable are stale register junk
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(100));
    if (g_fixed.load()) {
      candidate = 0xFF;
      stable = 0;
      continue;
    }
    float a[3];
    if (!g_chip.read_accel(a)) {
      stable = 0;  // a busy shared bus or a transient error: try again
      continue;
    }
    if (warmup > 0) {
      if (warmup == 1)
        ESP_LOGI(TAG, "accel ready: x=%+.2fg y=%+.2fg z=%+.2fg (default rotation %u)", a[0], a[1], a[2],
                 g_orientation.load());
      --warmup;
      continue;
    }
    const uint8_t target = orientation_from_accel(a);
    if (target == 0xFF || target == g_orientation.load()) {
      candidate = 0xFF;
      stable = 0;
      continue;
    }
    if (target != candidate) {
      candidate = target;
      stable = 1;
      continue;
    }
    if (++stable < kStableSamples) continue;
    stable = 0;
    ESP_LOGI(TAG, "rotate to %u° (ax=%+.2f ay=%+.2f)", target * 90, a[0], a[1]);
    events::post(EventType::Rotate, &target, 1);
  }
}

void ensure_task() {
  static bool started = false;
  if (started || !g_present) return;
  started = true;
  if (xTaskCreate(&poll_task, "hg-imu", 3072, nullptr, 4, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "could not start the rotation poll task");
    started = false;
  }
}

}  // namespace

bool begin(const ImuConfig& cfg, i2c_master_bus_handle_t bus, AmoledDisplay* display, TouchInput* touch,
           uint8_t initial_madctl) {
  g_display = display;
  g_touch = touch;
  if (!cfg.enabled || !bus) return false;
  bool known = false;
  for (uint8_t i = 0; i < kRotationCount; ++i) {
    if (kRotationMadctl[i] == initial_madctl) {
      g_orientation.store(i);
      known = true;
      break;
    }
  }
  if (!known) {
    ESP_LOGW(TAG, "panel MADCTL 0x%02x is not a rotatable orientation; auto-rotation stays off", initial_madctl);
    return false;
  }
  g_present = g_chip.begin(cfg, bus);
  if (!g_present) {
    ESP_LOGW(TAG, "no QMI8658 at 0x%02x/0x%02x; the display keeps its fixed orientation", cfg.addr,
             cfg.addr ^ 1);
    return false;
  }
  ESP_LOGI(TAG, "QMI8658 ready at 0x%02x (rotation %u°)", cfg.addr, g_orientation.load() * 90);
  return true;
}

void on_rotate_event(uint8_t index) {
  if (index < kRotationCount) set_orientation(index);
}

void apply_setting(std::string_view value) {
  if (!g_present) return;
  const std::string v(value);
  if (v.empty() || v == "auto") {
    g_fixed.store(false);
    ensure_task();
    return;
  }
  const int degrees = std::atoi(v.c_str());
  if (degrees % 90 != 0 || degrees < 0 || degrees > 270) {
    ESP_LOGW(TAG, "rotation setting '%s' is not auto/0/90/180/270; staying automatic", v.c_str());
    g_fixed.store(false);
    ensure_task();
    return;
  }
  g_fixed.store(true);
  set_orientation(static_cast<uint8_t>(degrees / 90));
}

hg::json::Value describe() {
  hg::json::Value r = hg::json::Value::object();
  r.set("present", g_present).set("mode", g_fixed.load() ? "fixed" : "auto");
  r.set("degrees", g_orientation.load() * 90);
  return r;
}

}  // namespace hgp::imu
