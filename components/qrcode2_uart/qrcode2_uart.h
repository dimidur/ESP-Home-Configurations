#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/automation.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

#include "esphome/components/uart/uart.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include <vector>
#include <string>
#include <functional>

namespace esphome {
namespace qrcode2_uart {

// Forward declarations
class QRCode2TextSensor;
class QRCode2BinarySensor;

class ScanTrigger : public Trigger<std::string> {
 public:
  explicit ScanTrigger() {}
};

class QRCode2UARTComponent : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  
  void set_scanning_timeout(uint32_t timeout) { scanning_timeout_ = timeout; }
  void update_scanning_timeout(uint32_t timeout_seconds) { scanning_timeout_ = timeout_seconds * 1000; }  // Convert seconds to milliseconds
  void update_long_press_duration(uint32_t duration_ms) { long_press_duration_ = duration_ms; }
  void set_trigger_pin(GPIOPin *pin) { trigger_pin_ = pin; }
  void set_led_pin(GPIOPin *pin) { led_pin_ = pin; }
  void set_scanner_trigger_pin(GPIOPin *pin) { scanner_trigger_pin_ = pin; }
  
  void add_scan_trigger(ScanTrigger *trigger) { scan_triggers_.push_back(trigger); }
  void add_text_sensor(QRCode2TextSensor *sensor) { text_sensors_.push_back(sensor); }
  void add_binary_sensor(QRCode2BinarySensor *sensor) { binary_sensors_.push_back(sensor); }
  void add_long_press_trigger(ScanTrigger *trigger) { long_press_triggers_.push_back(trigger); }
  void add_short_press_trigger(ScanTrigger *trigger) { short_press_triggers_.push_back(trigger); }
  void add_start_scan_trigger(ScanTrigger *trigger) { start_scan_triggers_.push_back(trigger); }
  void add_stop_scan_trigger(ScanTrigger *trigger) { stop_scan_triggers_.push_back(trigger); }
  


  
  void start_scan();
  void stop_scan();
  bool is_scanning() const { return scanning_; }
  
  // LED is controlled by scanner firmware automatically
  
  // Configuration commands for the scanner
  void configure_scanner();
  void set_auto_scan_mode(bool enabled);
  void set_trigger_mode();
  
  // Device information commands - PRESERVED FOR FUTURE REFERENCE
  // These functions can be used to request device info via protocol if needed in the future
  // void get_firmware_version();  // Commented out - preserved for reference
  // void get_hardware_model();   // Commented out - preserved for reference
  // void get_device_info();      // Commented out - preserved for reference
  void reset_scanner();
  

  
 protected:
  void process_uart_data();
  void handle_scan_result(const std::string &result);
  void handle_status_response(uint8_t fid, uint8_t length, const std::vector<uint8_t> &buffer, size_t data_start);

  void check_scan_timeout();
  
  std::vector<ScanTrigger *> scan_triggers_;
  std::vector<QRCode2TextSensor *> text_sensors_;
  std::vector<QRCode2BinarySensor *> binary_sensors_;
  std::vector<ScanTrigger *> long_press_triggers_;
  std::vector<ScanTrigger *> short_press_triggers_;
  std::vector<ScanTrigger *> start_scan_triggers_;
  std::vector<ScanTrigger *> stop_scan_triggers_;
  std::string buffer_;

  bool scanning_{false};
  uint32_t scan_start_time_{0};
  uint32_t scanning_timeout_{20000};  // 20 seconds default
  uint32_t long_press_duration_{5000};  // 5 seconds default
  uint32_t scan_counter_{0}; // Counter for scan uniqueness
  
  GPIOPin *trigger_pin_{nullptr};        // Button pin (GPIO39)
  GPIOPin *led_pin_{nullptr};           // LED pin (GPIO33) 
  GPIOPin *scanner_trigger_pin_{nullptr}; // Scanner trigger pin (GPIO23)
  
  uint32_t button_press_start_time_{0};  // For long press detection
  bool long_press_detected_{false};
  bool button_pressed_{false};  // Current button state - false = not pressed
  
  // QR data timeout tracking for Atomic QRCode2 Base
  bool parsing_qr_code_{false};
  uint32_t last_qr_data_time_{0};
  
  // Scanner command constants - Based on actual protocol documentation
  // Control Commands (TYPE=0x32)
  static constexpr uint8_t CMD_START_SCAN[] = {0x32, 0x75, 0x01};        // Start decode
  static constexpr uint8_t CMD_STOP_SCAN[] = {0x32, 0x75, 0x02};         // Stop decode  
  static constexpr uint8_t CMD_FACTORY_RESET[] = {0x32, 0x76, 0x01};     // Restore factory
  static constexpr uint8_t CMD_ENABLE_ALL_1D[] = {0x32, 0x76, 0x42, 0x01}; // Enable all 1D codes
  static constexpr uint8_t CMD_ENABLE_ALL_2D[] = {0x32, 0x76, 0x42, 0x02}; // Enable all 2D codes
  static constexpr uint8_t CMD_ENABLE_ALL_CODES[] = {0x32, 0x76, 0x42, 0x03}; // Enable all barcodes
  
  // Configuration Commands (TYPE=0x21) - Set read mode to button-trigger  
  static constexpr uint8_t CMD_SET_BUTTON_TRIGGER[] = {0x21, 0x61, 0x41, 0x00};
  
  // Status Commands (TYPE=0x43) - Get device info
  static constexpr uint8_t CMD_GET_SOFTWARE_VER[] = {0x43, 0x02, 0xC2};   // Software version
  static constexpr uint8_t CMD_GET_FIRMWARE_VER[] = {0x43, 0x02, 0xC1};   // Firmware version  
  static constexpr uint8_t CMD_GET_SERIAL_NUM[] = {0x43, 0x02, 0xC5};     // Serial number
  static constexpr uint8_t CMD_GET_HARDWARE_MODEL[] = {0x43, 0x02, 0xC7}; // Hardware model
  static constexpr uint8_t CMD_GET_HARDWARE_VER[] = {0x43, 0x02, 0xC4};   // Hardware version
};

template<typename... Ts> class StartScanAction : public Action<Ts...> {
 public:
  StartScanAction(QRCode2UARTComponent *parent) : parent_(parent) {}
  
  void play(const Ts &...x) override { this->parent_->start_scan(); }
  
 protected:
  QRCode2UARTComponent *parent_;
};

template<typename... Ts> class StopScanAction : public Action<Ts...> {
 public:
  StopScanAction(QRCode2UARTComponent *parent) : parent_(parent) {}
  
  void play(const Ts &...x) override { this->parent_->stop_scan(); }
  
 protected:
  QRCode2UARTComponent *parent_;
};



// DEVICE INFO ACTION - PRESERVED FOR FUTURE REFERENCE
// This action was used to trigger device info requests from YAML automations
// Commented out to simplify code but preserved for future use if needed

// template<typename... Ts> class GetDeviceInfoAction : public Action<Ts...> {
//  public:
//   GetDeviceInfoAction(QRCode2UARTComponent *parent) : parent_(parent) {}
//   
//   void play(const Ts &...x) override {  // ESPHome 2026.x: Action::play takes const-ref args 
//     this->parent_->get_device_info();
//   }
//   
//  protected:
//   QRCode2UARTComponent *parent_;
// };

template<typename... Ts> class ResetScannerAction : public Action<Ts...> {
 public:
  ResetScannerAction(QRCode2UARTComponent *parent) : parent_(parent) {}
  
  void play(const Ts &...x) override {  // ESPHome 2026.x: Action::play takes const-ref args 
    this->parent_->reset_scanner(); 
  }
  
 protected:
  QRCode2UARTComponent *parent_;
};

template<typename... Ts> class UpdateLongPressDurationAction : public Action<Ts...> {
 public:
  UpdateLongPressDurationAction(QRCode2UARTComponent *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(uint32_t, duration_ms)
  
  void play(const Ts &...x) override {  // ESPHome 2026.x: Action::play takes const-ref args 
    uint32_t duration = this->duration_ms_.value(x...);
    this->parent_->update_long_press_duration(duration); 
  }
  
 protected:
  QRCode2UARTComponent *parent_;
};

class QRCode2TextSensor : public text_sensor::TextSensor, public Component {
 public:
  void setup() override {}
  void loop() override {}
  void set_parent(QRCode2UARTComponent *parent) { parent_ = parent; }
  void update_scan_result(const std::string &result) { this->publish_state(result); }
  
 protected:
  QRCode2UARTComponent *parent_{nullptr};
};

class QRCode2BinarySensor : public binary_sensor::BinarySensor, public Component {
 public:
  void setup() override {}
  void loop() override;
  void set_parent(QRCode2UARTComponent *parent) { parent_ = parent; }
  
 protected:
  QRCode2UARTComponent *parent_{nullptr};
  bool last_scanning_state_{false};
};

}  // namespace qrcode2_uart
}  // namespace esphome
