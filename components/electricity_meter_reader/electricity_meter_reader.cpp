#include "electricity_meter_reader.h"

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace electricity_meter_reader {

static const char *const TAG = "electricity_meter_reader";

// Watchdog feed period, in received bytes.
static constexpr uint8_t BYTES_PER_WDT_FEED = 16;
// Bytes handled per loop() call; the rest waits in the UART buffer, so the
// other components run in between. At ESPHome's loop rate this is still
// many times the 960 bytes per second a 9600 baud meter can send.
static constexpr uint16_t MAX_BYTES_PER_LOOP = 128;

// Lets the platform's system tasks run. On the ESP8266, App.feed_wdt()
// holds off only the software watchdog; without a yield, a long pass ends
// in a hardware watchdog reset.
static void feed_wdt_and_yield() {
  App.feed_wdt();
  yield();
}

void ElectricityMeterReader::loop() {
  uint8_t bytes_since_wdt_feed = 0;
  uint16_t bytes_this_loop = 0;
  uint8_t byte;
  while (bytes_this_loop < MAX_BYTES_PER_LOOP && this->available() && this->read_byte(&byte)) {
    bytes_this_loop++;
    if (++bytes_since_wdt_feed >= BYTES_PER_WDT_FEED) {
      bytes_since_wdt_feed = 0;
      feed_wdt_and_yield();
    }
    this->lines_.clear();
    const MeterStreamResult result = this->reader_.feed(byte, this->lines_);
    this->handle_result_(result);
    for (const auto &line : this->lines_) {
      this->publish_state(line);
      // A telegram publishes many lines, each running the consumer's
      // automations.
      feed_wdt_and_yield();
    }
  }
}

void ElectricityMeterReader::handle_result_(const MeterStreamResult &result) {
  const char *reason = dropped_telegram_reason(result.event);
  if (reason == nullptr) return;
  ESP_LOGD(TAG, "SML telegram dropped after %u bytes: %s", (unsigned) result.telegram_length, reason);
  this->telegram_dropped_callbacks_.call((uint32_t) result.lines_lost);
}

void ElectricityMeterReader::dump_config() {
  LOG_TEXT_SENSOR("", "Electricity Meter Reader", this);
}

}  // namespace electricity_meter_reader
}  // namespace esphome
