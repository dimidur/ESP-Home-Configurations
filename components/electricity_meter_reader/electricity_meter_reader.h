#pragma once

#include <string>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "meter_stream.h"

namespace esphome {
namespace electricity_meter_reader {

// Reads an electricity meter's IR head and publishes one OBIS text line
// per state: text lines of IEC 62056-21 meters and the values of SML
// telegrams alike. Dropped telegrams are reported through
// add_on_telegram_dropped_callback with the lines they would have carried.
class ElectricityMeterReader : public text_sensor::TextSensor, public Component, public uart::UARTDevice {
 public:
  void loop() override;
  void dump_config() override;

  void add_on_telegram_dropped_callback(std::function<void(uint32_t)> &&callback) {
    this->telegram_dropped_callbacks_.add(std::move(callback));
  }

 protected:
  void handle_result_(const MeterStreamResult &result);

  MeterStreamReader reader_;
  std::vector<std::string> lines_;
  CallbackManager<void(uint32_t)> telegram_dropped_callbacks_;
};

class TelegramDroppedTrigger : public Trigger<uint32_t> {
 public:
  explicit TelegramDroppedTrigger(ElectricityMeterReader *parent) {
    parent->add_on_telegram_dropped_callback([this](uint32_t lines_lost) { this->trigger(lines_lost); });
  }
};

}  // namespace electricity_meter_reader
}  // namespace esphome
